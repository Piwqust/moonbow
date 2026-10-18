#include "MediaLibrary.h"

#include "../core/Json.h"
#include "../core/Paths.h"

#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace library {

namespace {

using Clock = std::chrono::steady_clock;

constexpr double kInlineMetadataMs = 3.0;  // per file; above this the metadata pass runs after a first publish
constexpr int kProbeFiles = 8;
constexpr auto kProgressInterval = std::chrono::seconds(2);
constexpr size_t kMetadataChunk = 192;  // files read in parallel between progress checks


double MsSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

bool NaturalLess(const std::wstring& a, const std::wstring& b) {
    return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
}

std::wstring Join(const std::wstring& dir, const wchar_t* name) {
    std::wstring out = dir;
    if (!out.empty() && out.back() != L'\\') out += L'\\';
    out += name;
    return out;
}

// --- Enumeration --------------------------------------------------------------------------

struct TempFolder {
    std::wstring path;
    std::wstring name;
    std::vector<std::unique_ptr<TempFolder>> children;
    std::vector<MediaItem> items;
};

void Enumerate(TempFolder& node, const ScanOptions& opt, const std::atomic<bool>& cancel, ScanStats& stats) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(Join(node.path, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (cancel.load(std::memory_order_relaxed)) break;
        const wchar_t* name = fd.cFileName;
        if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0))) continue;
        DWORD attr = fd.dwFileAttributes;
        if (!opt.includeHidden && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;

        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            if (attr & FILE_ATTRIBUTE_REPARSE_POINT) continue;  // junctions / symlinks: avoid loops
            auto child = std::make_unique<TempFolder>();
            child->path = Join(node.path, name);
            child->name = name;
            Enumerate(*child, opt, cancel, stats);
            if (!child->items.empty() || !child->children.empty()) {
                ++stats.folders;
                node.children.push_back(std::move(child));
            }
            continue;
        }

        MediaKind kind = KindOf(name);
        if (kind == MediaKind::Other) continue;
        MediaItem it;
        it.path = Join(node.path, name);
        it.name = StemOf(name);
        it.folder = node.path;
        it.kind = kind;
        it.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        it.mtime = FileTimeToUnixMs((static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) |
                                    fd.ftLastWriteTime.dwLowDateTime);
        it.offline = (attr & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
                              FILE_ATTRIBUTE_RECALL_ON_OPEN)) != 0;
        node.items.push_back(std::move(it));
        ++stats.files;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

int Flatten(TempFolder& t, int parent, Snapshot& s) {
    int index = static_cast<int>(s.folders.size());
    s.folders.emplace_back();
    s.folders[index].path = t.path;
    s.folders[index].name = t.name;
    s.folders[index].parent = parent;

    std::sort(t.items.begin(), t.items.end(),
              [](const MediaItem& a, const MediaItem& b) { return NaturalLess(a.name, b.name); });
    for (MediaItem& it : t.items) {
        FolderNode& f = s.folders[index];
        (it.kind == MediaKind::Video ? f.videos : f.audios)++;
        f.items.push_back(static_cast<int>(s.items.size()));
        s.items.push_back(std::move(it));
    }

    std::sort(t.children.begin(), t.children.end(),
              [](const auto& a, const auto& b) { return NaturalLess(a->name, b->name); });
    uint32_t totalV = s.folders[index].videos, totalA = s.folders[index].audios;
    for (auto& child : t.children) {
        int c = Flatten(*child, index, s);  // may reallocate s.folders: index, don't hold references
        s.folders[index].children.push_back(c);
        totalV += s.folders[c].totalVideos;
        totalA += s.folders[c].totalAudios;
    }
    s.folders[index].totalVideos = totalV;
    s.folders[index].totalAudios = totalA;
    return index;
}

void FillSearchKey(MediaItem& it) {
    std::wstring text = it.name;
    for (const std::wstring* tag : {&it.title, &it.artist, &it.album}) {
        if (tag->empty()) continue;
        text += L'\n';
        text += *tag;
    }
    it.searchKey = SearchFold(text);
}

// --- Metadata -----------------------------------------------------------------------------

std::wstring PropString(IPropertyStore* store, REFPROPERTYKEY key) {
    PROPVARIANT v;
    PropVariantInit(&v);
    std::wstring out;
    if (SUCCEEDED(store->GetValue(key, &v)) && v.vt != VT_EMPTY) {
        PWSTR s = nullptr;
        if (SUCCEEDED(PropVariantToStringAlloc(v, &s)) && s) {  // vectors are joined with "; "
            out = s;
            CoTaskMemFree(s);
        }
    }
    PropVariantClear(&v);
    return out;
}

// The first bytes of the file; false when it can't be opened (locked, gone).
bool ReadHead(const std::wstring& path, unsigned char* buf, size_t cap, size_t* got) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    const BOOL ok = ReadFile(f, buf, static_cast<DWORD>(cap), &n, nullptr);
    CloseHandle(f);
    *got = n;
    return ok != FALSE;
}

// Windows reads the length of every intact MP4/MOV; none means the index (moov) is missing.
bool LengthAlwaysKnown(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    for (const wchar_t* e : {L".mp4", L".m4v", L".mov", L".m4a"})
        if (_wcsicmp(ext, e) == 0) return true;
    return false;
}

void ReadMetadata(MediaItem& it) {
    if (it.offline) return;  // reading would download the file from the cloud
    unsigned char head[16];
    size_t got = 0;
    const bool headRead = ReadHead(it.path, head, sizeof(head), &got);
    it.damaged = headRead && !HeaderMatchesExtension(it.path, head, got);
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreFromParsingName(it.path.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store)))) {
        // The MP4 handler refuses files whose index is cut off (an interrupted download).
        if (headRead && LengthAlwaysKnown(it.path)) it.damaged = true;
        return;
    }
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(store->GetValue(PKEY_Media_Duration, &v)) && v.vt == VT_UI8)
        it.duration = static_cast<double>(v.uhVal.QuadPart) / 1e7;  // 100 ns units
    PropVariantClear(&v);
    if (headRead && !(it.duration > 0) && LengthAlwaysKnown(it.path)) it.damaged = true;
    if (it.kind == MediaKind::Audio) {
        it.title = PropString(store, PKEY_Title);
        it.artist = PropString(store, PKEY_Music_Artist);
        it.album = PropString(store, PKEY_Music_AlbumTitle);
        PROPVARIANT n;
        PropVariantInit(&n);
        if (SUCCEEDED(store->GetValue(PKEY_Music_TrackNumber, &n)) && n.vt == VT_UI4) it.track = static_cast<int>(n.ulVal);
        PropVariantClear(&n);
        if (SUCCEEDED(store->GetValue(PKEY_Media_Year, &n)) && n.vt == VT_UI4) it.year = static_cast<int>(n.ulVal);
        PropVariantClear(&n);
        if (SUCCEEDED(store->GetValue(PKEY_Music_DiscNumber, &n)) && n.vt == VT_UI4) it.disc = static_cast<int>(n.ulVal);
        PropVariantClear(&n);
    }
    store->Release();
}

struct CacheEntry {
    uint64_t size = 0;
    int64_t mtime = 0;
    double duration = 0;
    std::wstring title, artist, album;
    int track = 0, year = 0, disc = 0;
    bool damaged = false;
};

std::unordered_map<std::wstring, CacheEntry> LoadCache() {
    std::unordered_map<std::wstring, CacheEntry> cache;
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"library-cache.json"), &text)) return cache;
    std::optional<json::Value> doc = json::Parse(text);
    // Version 2 added track numbers and years, 3 disc numbers and the damaged flag: older caches
    // are read again from the files.
    if (!doc || doc->GetInt("version") < 3) return cache;
    const json::Value* items = doc->Find("items");
    if (!items) return cache;
    for (const json::Value& e : items->AsArray()) {
        CacheEntry c;
        c.size = static_cast<uint64_t>(e.GetInt("s"));
        c.mtime = e.GetInt("m");
        c.duration = e.GetNumber("d");
        c.title = e.GetWString("t");
        c.artist = e.GetWString("a");
        c.album = e.GetWString("l");
        c.track = static_cast<int>(e.GetInt("n"));
        c.year = static_cast<int>(e.GetInt("y"));
        c.disc = static_cast<int>(e.GetInt("k"));
        c.damaged = e.GetBool("b");
        cache.emplace(PathKey(e.GetWString("p")), std::move(c));
    }
    return cache;
}

// Property handlers are mostly CPU-bound parsers: a few threads cut a first scan several
// times, and the rest of the machine (playback, the UI) keeps priority over them.
int MetadataThreads() {
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    return static_cast<int>(std::clamp(cores / 2, 1u, 4u));
}

// Reads metadata for items[pending[first..last)] on `threads` worker threads.
void ReadMetadataParallel(Snapshot& snap, const std::vector<size_t>& pending, size_t first, size_t last, int threads,
                          const std::atomic<bool>& cancel) {
    std::atomic<size_t> next{first};
    auto work = [&] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        for (size_t i; !cancel && (i = next++) < last;) {
            MediaItem& it = snap.items[pending[i]];
            ReadMetadata(it);
            FillSearchKey(it);
        }
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        if (SUCCEEDED(com)) CoUninitialize();
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) {
        try {
            pool.emplace_back(work);
        } catch (...) {
            break;
        }
    }
    work();
    for (std::thread& t : pool) t.join();
}

// `unread` (optional, by item index): files whose metadata is not read yet stay out of the
// cache, so an interrupted scan resumes with them next time.
void SaveCache(const Snapshot& s, const std::vector<char>* unread = nullptr) {
    json::Value items = json::Value::MakeArray();
    items.MutArray().reserve(s.items.size());
    for (size_t i = 0; i < s.items.size(); ++i) {
        const MediaItem& it = s.items[i];
        if (it.offline || (unread && (*unread)[i])) continue;
        json::Value e = json::Value::MakeObject();
        e.Set("p", json::ToUtf8(it.path));
        e.Set("s", it.size);
        e.Set("m", it.mtime);
        if (it.duration > 0) e.Set("d", it.duration);
        if (!it.title.empty()) e.Set("t", json::ToUtf8(it.title));
        if (!it.artist.empty()) e.Set("a", json::ToUtf8(it.artist));
        if (!it.album.empty()) e.Set("l", json::ToUtf8(it.album));
        if (it.track > 0) e.Set("n", it.track);
        if (it.year > 0) e.Set("y", it.year);
        if (it.disc > 0) e.Set("k", it.disc);
        if (it.damaged) e.Set("b", true);
        items.Push(std::move(e));
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 3);
    doc.Set("items", std::move(items));
    json::WriteFileAtomic(DataFilePath(L"library-cache.json"), json::Serialize(doc));
}

}  // namespace

// --- Snapshot -----------------------------------------------------------------------------------

void Snapshot::BuildIndex() {
    folderIndex_.clear();
    itemIndex_.clear();
    folderIndex_.reserve(folders.size());
    itemIndex_.reserve(items.size());
    for (size_t i = 0; i < folders.size(); ++i) folderIndex_.emplace(PathKey(folders[i].path), static_cast<int>(i));
    for (size_t i = 0; i < items.size(); ++i) itemIndex_.emplace(PathKey(items[i].path), static_cast<int>(i));
}

int Snapshot::FindFolder(std::wstring_view path) const {
    auto it = folderIndex_.find(PathKey(path));
    return it == folderIndex_.end() ? -1 : it->second;
}

const FolderNode* Snapshot::Folder(std::wstring_view path) const {
    int i = FindFolder(path);
    return i < 0 ? nullptr : &folders[static_cast<size_t>(i)];
}

const MediaItem* Snapshot::FindItem(std::wstring_view path) const {
    auto it = itemIndex_.find(PathKey(path));
    return it == itemIndex_.end() ? nullptr : &items[static_cast<size_t>(it->second)];
}

std::vector<const FolderNode*> Snapshot::Roots() const {
    std::vector<const FolderNode*> out;
    for (const FolderNode& f : folders)
        if (f.parent < 0) out.push_back(&f);
    return out;
}

std::vector<const FolderNode*> Snapshot::Children(std::wstring_view folder) const {
    std::vector<const FolderNode*> out;
    const FolderNode* f = Folder(folder);
    if (!f) return out;
    for (int c : f->children) out.push_back(&folders[static_cast<size_t>(c)]);
    return out;
}

void Snapshot::CollectItems(int folder, bool recursive, std::vector<const MediaItem*>& out) const {
    const FolderNode& f = folders[static_cast<size_t>(folder)];
    for (int i : f.items) out.push_back(&items[static_cast<size_t>(i)]);
    if (recursive)
        for (int c : f.children) CollectItems(c, true, out);
}

std::vector<const MediaItem*> Snapshot::ItemsIn(std::wstring_view folder, bool recursive) const {
    std::vector<const MediaItem*> out;
    int f = FindFolder(folder);
    if (f >= 0) CollectItems(f, recursive, out);
    return out;
}

std::vector<const MediaItem*> Snapshot::Videos() const {
    std::vector<const MediaItem*> out;
    for (const MediaItem& it : items)
        if (it.kind == MediaKind::Video) out.push_back(&it);
    return out;
}

std::vector<const MediaItem*> Snapshot::Music() const {
    std::vector<const MediaItem*> out;
    for (const MediaItem& it : items)
        if (it.kind == MediaKind::Audio) out.push_back(&it);
    return out;
}

std::vector<const MediaItem*> Snapshot::Search(std::wstring_view query, MediaKind kind) const {
    std::vector<const MediaItem*> out;
    std::wstring q = SearchFold(query);
    for (const MediaItem& it : items) {
        if (kind != MediaKind::Other && it.kind != kind) continue;
        if (q.empty() || FoldedContains(it.searchKey, q)) out.push_back(&it);
    }
    return out;
}

void SortItems(std::vector<const MediaItem*>& items, SortKey key, bool descending) {
    auto byName = [](const MediaItem* a, const MediaItem* b) {
        int c = StrCmpLogicalW(a->DisplayTitle().c_str(), b->DisplayTitle().c_str());
        return c != 0 ? c < 0 : NaturalLess(a->path, b->path);
    };
    std::stable_sort(items.begin(), items.end(), [&](const MediaItem* a, const MediaItem* b) {
        switch (key) {
            case SortKey::Date:
                if (a->mtime != b->mtime) return descending ? a->mtime > b->mtime : a->mtime < b->mtime;
                break;
            case SortKey::Size:
                if (a->size != b->size) return descending ? a->size > b->size : a->size < b->size;
                break;
            case SortKey::Duration: {
                bool ua = a->duration <= 0, ub = b->duration <= 0;
                if (ua != ub) return ub;  // unknown durations go last in both directions
                if (a->duration != b->duration)
                    return descending ? a->duration > b->duration : a->duration < b->duration;
                break;
            }
            case SortKey::Name:
                return descending ? byName(b, a) : byName(a, b);
            case SortKey::Track: {
                const MediaItem* x = descending ? b : a;
                const MediaItem* y = descending ? a : b;
                if (x->album.empty() != y->album.empty()) return y->album.empty();  // untagged albums last
                const int c = StrCmpLogicalW(x->album.c_str(), y->album.c_str());
                if (c != 0) return c < 0;
                if (x->disc != y->disc) return x->disc < y->disc;
                if ((x->track > 0) != (y->track > 0)) return x->track > 0;  // numbered tracks first
                if (x->track != y->track) return x->track < y->track;
                return byName(x, y);
            }
        }
        return byName(a, b);
    });
}

// --- MediaLibrary -------------------------------------------------------------------------------

MediaLibrary::MediaLibrary() : current_(std::make_shared<Snapshot>()) {}

MediaLibrary::~MediaLibrary() {
    Cancel();
}

bool MediaLibrary::Load() {
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"library.json"), &text)) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    std::optional<json::Value> doc = json::Parse(text);
    if (!doc) return false;
    std::vector<std::wstring> roots;
    if (const json::Value* list = doc->Find("roots"))
        for (const json::Value& r : list->AsArray())
            if (r.IsString() && !r.AsString().empty()) roots.push_back(NormalizePath(r.AsWString()));
    roots_ = std::move(roots);
    return true;
}

bool MediaLibrary::Save() const {
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    json::Value& roots = doc["roots"];
    roots = json::Value::MakeArray();
    for (const std::wstring& r : roots_) roots.Push(json::ToUtf8(r));
    return json::WriteFileAtomic(DataFilePath(L"library.json"), json::Serialize(doc, true));
}

bool MediaLibrary::AddRoot(const std::wstring& folder) {
    std::wstring path = NormalizePath(folder);
    DWORD attr = GetFileAttributesW(path.c_str());
    if (path.empty() || attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
    std::wstring key = PathKey(path);
    auto inside = [](const std::wstring& child, const std::wstring& parent) {
        if (child.size() <= parent.size() || child.compare(0, parent.size(), parent) != 0) return false;
        return parent.back() == L'\\' || child[parent.size()] == L'\\';
    };
    for (const std::wstring& r : roots_) {
        std::wstring rk = PathKey(r);
        if (rk == key || inside(key, rk)) return false;  // already covered
    }
    // The new root may contain existing ones: they become redundant.
    std::erase_if(roots_, [&](const std::wstring& r) { return inside(PathKey(r), key); });
    roots_.push_back(path);
    Save();
    return true;
}

bool MediaLibrary::RemoveRoot(const std::wstring& folder) {
    std::wstring key = PathKey(folder);
    size_t before = roots_.size();
    std::erase_if(roots_, [&](const std::wstring& r) { return PathKey(r) == key; });
    if (roots_.size() == before) return false;
    Save();
    return true;
}

void MediaLibrary::SetRoots(std::vector<std::wstring> folders) {
    roots_.clear();
    for (const std::wstring& f : folders) AddRoot(f);
    Save();
}

void MediaLibrary::SetNotifyWindow(HWND hwnd, UINT msg) {
    notifyMsg_ = msg;
    notifyWnd_ = hwnd;
}

std::shared_ptr<const Snapshot> MediaLibrary::Current() const {
    std::lock_guard lock(mutex_);
    return current_;
}

void MediaLibrary::Publish(std::shared_ptr<const Snapshot> snapshot) {
    uint64_t generation = snapshot->generation;
    bool complete = snapshot->complete;
    {
        std::lock_guard lock(mutex_);
        current_ = std::move(snapshot);
    }
    if (complete) scanning_ = false;  // before the message, so the UI sees a finished scan
    HWND hwnd = notifyWnd_.load();
    if (hwnd) PostMessageW(hwnd, notifyMsg_.load(), complete ? 1 : 0, static_cast<LPARAM>(generation));
}

void MediaLibrary::Cancel() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    scanning_ = false;
}

uint64_t MediaLibrary::Rescan() {
    Cancel();
    uint64_t generation = ++generation_;
    scanning_ = true;
    try {
        thread_ = std::thread(&MediaLibrary::ScanThread, this, generation, roots_, options_);
    } catch (...) {
        scanning_ = false;
        return 0;
    }
    return generation;
}

void MediaLibrary::ScanThread(uint64_t generation, std::vector<std::wstring> roots, ScanOptions options) {
    HRESULT com = options.readMetadata ? CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE) : E_FAIL;

    auto snap = std::make_shared<Snapshot>();
    snap->generation = generation;
    snap->roots = roots;

    // 1. Enumerate.
    auto t0 = Clock::now();
    for (const std::wstring& root : roots) {
        if (cancel_) break;
        DWORD attr = GetFileAttributesW(root.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        TempFolder t;
        t.path = root;
        t.name = FileNameOf(root);
        if (t.name.empty() || (root.size() == 3 && root[1] == L':')) t.name = root;  // "D:\"
        Enumerate(t, options, cancel_, snap->stats);
        ++snap->stats.folders;
        Flatten(t, -1, *snap);
    }
    snap->stats.enumerateMs = MsSince(t0);

    // 2. Metadata: cached values first, then probe the cost of reading the rest.
    std::vector<size_t> pending;
    if (options.readMetadata && !cancel_) {
        auto cache = LoadCache();
        for (size_t i = 0; i < snap->items.size(); ++i) {
            MediaItem& it = snap->items[i];
            auto c = cache.find(PathKey(it.path));
            if (c != cache.end() && c->second.size == it.size && c->second.mtime == it.mtime) {
                it.duration = c->second.duration;
                it.title = std::move(c->second.title);
                it.artist = std::move(c->second.artist);
                it.album = std::move(c->second.album);
                it.track = c->second.track;
                it.year = c->second.year;
                it.disc = c->second.disc;
                it.damaged = c->second.damaged;
            } else if (!it.offline) {
                pending.push_back(i);
            }
        }
    }
    for (MediaItem& it : snap->items) FillSearchKey(it);
    snap->BuildIndex();

    size_t done = 0;
    auto tm = Clock::now();
    while (done < pending.size() && done < kProbeFiles && !cancel_) {
        ReadMetadata(snap->items[pending[done]]);
        FillSearchKey(snap->items[pending[done]]);
        ++done;
    }
    double probeMs = MsSince(tm);
    snap->stats.metadataSecondPass = done > 0 && probeMs / static_cast<double>(done) > kInlineMetadataMs &&
                                     done < pending.size();

    if (cancel_) {
        if (SUCCEEDED(com)) CoUninitialize();
        return;
    }

    if (snap->stats.metadataSecondPass || pending.empty() || !options.readMetadata) {
        snap->complete = pending.size() == done;
        Publish(std::make_shared<Snapshot>(*snap));
    }

    // 3. Remaining metadata in parallel chunks, publishing progress (and saving what is read)
    // every few seconds.
    auto lastPublish = Clock::now();
    const int threads = MetadataThreads();
    std::vector<char> unread(snap->items.size(), 0);
    for (size_t i = done; i < pending.size(); ++i) unread[pending[i]] = 1;
    while (done < pending.size() && !cancel_) {
        const size_t last = std::min(pending.size(), done + kMetadataChunk);
        ReadMetadataParallel(*snap, pending, done, last, threads, cancel_);
        if (cancel_) break;
        for (size_t i = done; i < last; ++i) unread[pending[i]] = 0;
        done = last;
        if (snap->stats.metadataSecondPass && Clock::now() - lastPublish > kProgressInterval && done < pending.size()) {
            snap->stats.metadataFiles = done;
            if (options.readMetadata) SaveCache(*snap, &unread);
            Publish(std::make_shared<Snapshot>(*snap));
            lastPublish = Clock::now();
        }
    }
    snap->stats.metadataMs = MsSince(tm);
    snap->stats.metadataFiles = done;
    snap->stats.metadataMsPerFile = done ? snap->stats.metadataMs / static_cast<double>(done) : 0;

    if (!cancel_ && (!snap->complete || done > 0)) {
        snap->complete = true;
        if (options.readMetadata && done > 0) SaveCache(*snap);
        Publish(std::make_shared<const Snapshot>(std::move(*snap)));
    }
    if (SUCCEEDED(com)) CoUninitialize();
    scanning_ = false;  // also covers a scan that ended without a complete publish
}

}  // namespace library
