#include "MediaLibrary.h"

#include "TagReader.h"
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
#include <functional>
#include <cwctype>
#include <thread>

namespace library {

namespace {

using Clock = std::chrono::steady_clock;

constexpr double kInlineMetadataMs = 3.0;  // per file; above this the metadata pass runs after a first publish
constexpr int kProbeFiles = 8;
constexpr double kInlineMetadataTotalMs = 300;  // estimated rest of the pass; above it, publish first
constexpr auto kProgressInterval = std::chrono::seconds(2);
constexpr size_t kMetadataChunk = 192;  // files read in parallel between progress checks
constexpr int kCacheVersion = 5;        // library-cache.json format, see LoadCache
// A scan still running after this tells the window once (pages then follow its progress); quicker
// scans post nothing but their result.
constexpr double kAnnounceMs = 400;
// An inline metadata pass (one publish at the end) that runs longer than this publishes what it
// has and continues as a second pass: the files show while slow ones are read.
constexpr double kInlineLimitMs = 1000;


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

void Enumerate(TempFolder& node, const ScanOptions& opt, const std::atomic<bool>& cancel, ScanStats& stats,
               std::atomic<size_t>& found, const std::function<void()>& tick) {
    tick();
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
            Enumerate(*child, opt, cancel, stats, found, tick);
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
        found.fetch_add(1, std::memory_order_relaxed);
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

// Windows reads the length of every intact MP4/MOV; none means the index (moov) is missing.
bool LengthAlwaysKnown(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    for (const wchar_t* e : {L".mp4", L".m4v", L".mov", L".m4a"})
        if (_wcsicmp(ext, e) == 0) return true;
    return false;
}

void FirstGenre(std::wstring& genre) {
    if (const size_t semi = genre.find(L';'); semi != std::wstring::npos) genre.resize(semi);
    while (!genre.empty() && iswspace(genre.back())) genre.pop_back();
}

// The Windows property system: formats TagReader doesn't cover, and durations it couldn't get.
// Fills only what is still empty.
void ReadShellMetadata(MediaItem& it, bool headRead) {
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreFromParsingName(it.path.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store)))) {
        // The MP4 handler refuses files whose index is cut off (an interrupted download).
        if (headRead && LengthAlwaysKnown(it.path)) it.damaged = true;
        return;
    }
    PROPVARIANT v;
    PropVariantInit(&v);
    if (!(it.duration > 0) && SUCCEEDED(store->GetValue(PKEY_Media_Duration, &v)) && v.vt == VT_UI8)
        it.duration = static_cast<double>(v.uhVal.QuadPart) / 1e7;  // 100 ns units
    PropVariantClear(&v);
    if (headRead && !(it.duration > 0) && LengthAlwaysKnown(it.path)) it.damaged = true;
    if (it.kind == MediaKind::Audio) {
        auto text = [&](std::wstring& field, REFPROPERTYKEY key) {
            if (field.empty()) field = PropString(store, key);
        };
        text(it.title, PKEY_Title);
        text(it.artist, PKEY_Music_Artist);
        text(it.album, PKEY_Music_AlbumTitle);
        text(it.albumArtist, PKEY_Music_AlbumArtist);
        PROPVARIANT n;
        PropVariantInit(&n);
        if (it.track <= 0 && SUCCEEDED(store->GetValue(PKEY_Music_TrackNumber, &n)) && n.vt == VT_UI4)
            it.track = static_cast<int>(n.ulVal);
        PropVariantClear(&n);
        if (it.year <= 0 && SUCCEEDED(store->GetValue(PKEY_Media_Year, &n)) && n.vt == VT_UI4) it.year = static_cast<int>(n.ulVal);
        PropVariantClear(&n);
        if (it.disc <= 0 && SUCCEEDED(store->GetValue(PKEY_Music_DiscNumber, &n)) && n.vt == VT_UI4)
            it.disc = static_cast<int>(n.ulVal);
        PropVariantClear(&n);
        // The MP3 handler leaves DiscNumber empty and exposes ID3 TPOS only as PartOfSet ("2/2").
        if (it.disc <= 0) it.disc = ParseDiscNumber(PropString(store, PKEY_Music_PartOfSet));
        // Genres are read here, with the other tags, so the Genres page never reads files itself.
        if (it.genre.empty()) {
            it.genre = PropString(store, PKEY_Music_Genre);
            FirstGenre(it.genre);
        }
    }
    store->Release();
}

void ReadMetadata(MediaItem& it) {
    if (it.offline) return;  // reading would download the file from the cloud
    bool headRead = false;
    {
        FileSource file(it.path);
        headRead = file.Ok();  // false when it can't be opened (locked, gone)
        if (headRead) {
            unsigned char head[16];
            const size_t got = file.Read(0, head, sizeof(head));
            // A header that doesn't fit the extension is damage only when it is no media at all:
            // mpv sniffs the content, so an MP3 named .mkv plays. Such a file has nothing for the
            // property system to read either.
            it.damaged = !HeaderMatchesExtension(it.path, head, got) && !HeaderLooksLikeMedia(head, got);
            if (it.damaged) return;
        }
        TagInfo t;
        TagOptions o;
        o.tags = it.kind == MediaKind::Audio;
        if (headRead && ReadTags(file, &t, o)) {
            if (t.damaged && LengthAlwaysKnown(it.path)) {  // an MP4 without its index
                it.damaged = true;
                return;
            }
            it.duration = t.duration;
            if (o.tags) {
                it.title = std::move(t.title);
                it.artist = std::move(t.artist);
                it.album = std::move(t.album);
                it.albumArtist = std::move(t.albumArtist);
                it.genre = std::move(t.genre);
                FirstGenre(it.genre);
                it.track = t.track;
                it.year = t.year;
                it.disc = t.disc;
            }
            // Matroska gives only the length: a .mka song still asks the property system for its tags.
            if (it.duration > 0 && !(t.format == TagFormat::Matroska && o.tags)) return;
        }
    }
    ReadShellMetadata(it, headRead);
}

struct CacheEntry {
    uint64_t size = 0;
    int64_t mtime = 0;
    double duration = 0;
    std::wstring title, artist, album, albumArtist, genre;
    int track = 0, year = 0, disc = 0;
    bool damaged = false;
    int64_t firstSeen = 0;
    bool stale = false;  // an older format: only firstSeen is kept, the tags are read again
};

std::unordered_map<std::wstring, CacheEntry> LoadCache() {
    std::unordered_map<std::wstring, CacheEntry> cache;
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"library-cache.json"), &text)) return cache;
    std::optional<json::Value> doc = json::Parse(text);
    // Version 2 added track numbers and years, 3 disc numbers and the damaged flag, 4 genres and
    // the MP3 disc number (PartOfSet), 5 album artists, first-seen times and Lumen's own tag
    // reader. Older caches are read again from the files; a version 4 cache still says which
    // files were already known, so an upgrade doesn't make every file "new".
    const int64_t version = doc ? doc->GetInt("version") : 0;
    if (version < 4) return cache;
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
        c.genre = e.GetWString("g");
        c.track = static_cast<int>(e.GetInt("n"));
        c.year = static_cast<int>(e.GetInt("y"));
        c.disc = static_cast<int>(e.GetInt("k"));
        c.damaged = e.GetBool("b");
        c.albumArtist = e.GetWString("aa");
        c.firstSeen = e.GetInt("f");
        c.stale = version < kCacheVersion || e.GetBool("o");
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
                          const std::atomic<bool>& cancel, std::atomic<size_t>& progress) {
    std::atomic<size_t> next{first};
    auto work = [&] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        for (size_t i; !cancel && (i = next++) < last;) {
            MediaItem& it = snap.items[pending[i]];
            ReadMetadata(it);
            FillSearchKey(it);
            progress.fetch_add(1, std::memory_order_relaxed);
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

using KeptCache = std::vector<std::pair<std::wstring, CacheEntry>>;  // PathKey -> entry

// `unread` (by item index): files whose metadata is not read yet (still queued, or cloud-only)
// are written as stale entries: only their first-seen time is kept, so they don't turn "new"
// again, and the tags are read on a later scan. `kept`: entries of library roots that were not
// reachable (a USB disk unplugged), written back unchanged.
void SaveCache(const Snapshot& s, const std::vector<char>& unread, const KeptCache& kept) {
    json::Value items = json::Value::MakeArray();
    items.MutArray().reserve(s.items.size() + kept.size());
    auto add = [&](const std::wstring& path, const CacheEntry& c) {
        json::Value e = json::Value::MakeObject();
        e.Set("p", json::ToUtf8(path));
        e.Set("s", c.size);
        e.Set("m", c.mtime);
        if (c.duration > 0) e.Set("d", c.duration);
        if (!c.title.empty()) e.Set("t", json::ToUtf8(c.title));
        if (!c.artist.empty()) e.Set("a", json::ToUtf8(c.artist));
        if (!c.album.empty()) e.Set("l", json::ToUtf8(c.album));
        if (!c.albumArtist.empty()) e.Set("aa", json::ToUtf8(c.albumArtist));
        if (!c.genre.empty()) e.Set("g", json::ToUtf8(c.genre));
        if (c.track > 0) e.Set("n", c.track);
        if (c.year > 0) e.Set("y", c.year);
        if (c.disc > 0) e.Set("k", c.disc);
        if (c.damaged) e.Set("b", true);
        if (c.firstSeen > 0) e.Set("f", c.firstSeen);
        if (c.stale) e.Set("o", true);  // tags not read yet (or from an older format)
        items.Push(std::move(e));
    };
    for (size_t i = 0; i < s.items.size(); ++i) {
        const MediaItem& it = s.items[i];
        if (unread[i]) {
            CacheEntry stale;
            stale.size = it.size;
            stale.mtime = it.mtime;
            stale.firstSeen = it.firstSeen;
            stale.stale = true;
            add(it.path, stale);
            continue;
        }
        add(it.path, {it.size, it.mtime, it.duration, it.title, it.artist, it.album, it.albumArtist, it.genre, it.track,
                      it.year, it.disc, it.damaged, it.firstSeen});
    }
    for (const auto& [key, c] : kept) add(key, c);
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", kCacheVersion);
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
    newSince_ = UnixMsNow();  // no record yet: only files found from now on are new
    readOnly_ = false;
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"library.json"), &text)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true;
        readOnly_ = true;  // locked by a backup or sync tool: saving now would drop the folder list
        return false;
    }
    std::optional<json::Value> doc = json::Parse(text);
    // Valid JSON of the wrong shape ("roots": "x") is as unreadable as broken JSON.
    const json::Value* list = doc && doc->IsObject() ? doc->Find("roots") : nullptr;
    if (!doc || !doc->IsObject() || (list && !list->IsArray())) {
        BackupCorruptFile(DataFilePath(L"library.json"));  // the next save would overwrite it
        return false;
    }
    std::vector<std::wstring> roots;
    if (list)
        for (const json::Value& r : list->AsArray())
            if (r.IsString() && !r.AsString().empty()) roots.push_back(NormalizePath(r.AsWString()));
    roots_ = std::move(roots);
    if (const int64_t seen = doc->GetInt("seen"); seen > 0) newSince_ = seenSaved_ = seen;
    return true;
}

bool MediaLibrary::Save() const {
    if (readOnly_) return false;
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    json::Value& roots = doc["roots"];
    roots = json::Value::MakeArray();
    for (const std::wstring& r : roots_) roots.Push(json::ToUtf8(r));
    if (seenSaved_ > 0) doc.Set("seen", seenSaved_);
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

void MediaLibrary::MarkSeen() {
    // Writes only when the library holds files newer than the saved time: most views write nothing.
    if (Current()->newestFirstSeen <= seenSaved_) return;
    seenSaved_ = UnixMsNow();
    Save();
}

ScanProgress MediaLibrary::Progress() const {
    ScanProgress p;
    p.scanning = scanning_.load();
    if (!p.scanning) return p;
    p.counting = counting_.load();
    p.done = progressDone_.load(std::memory_order_relaxed);
    p.total = progressTotal_.load(std::memory_order_relaxed);
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    p.elapsedMs = static_cast<double>(now - scanStartMs_.load());
    return p;
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
    // Before the message, so the UI sees a finished scan (unless a newer one has started).
    if (complete && generation == generation_) scanning_ = false;
    HWND hwnd = notifyWnd_.load();
    if (hwnd) PostMessageW(hwnd, notifyMsg_.load(), complete ? 1 : 0, static_cast<LPARAM>(generation));
}

void MediaLibrary::Cancel() {
    if (cancel_) *cancel_ = true;
    if (thread_.joinable()) thread_.join();
    scanning_ = false;
}

uint64_t MediaLibrary::Rescan() {
    // No join here: a scan of a sleeping NAS or USB disk can take seconds to notice the cancel.
    // The new thread waits for the old one (so publishes and cache writes stay in order).
    if (cancel_) *cancel_ = true;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    cancel_ = cancel;
    auto previous = std::make_shared<std::thread>(std::move(thread_));
    const uint64_t generation = ++generation_;
    scanStartMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    counting_ = true;
    progressDone_ = progressTotal_ = 0;
    scanning_ = true;
    try {
        thread_ = std::thread([this, generation, roots = roots_, options = options_, cancel, previous]() mutable {
            if (previous->joinable()) previous->join();
            ScanThread(generation, std::move(roots), options, *cancel);
        });
    } catch (...) {
        if (previous->joinable()) previous->join();
        scanning_ = false;
        return 0;
    }
    return generation;
}

void MediaLibrary::ScanThread(uint64_t generation, std::vector<std::wstring> roots, ScanOptions options,
                              const std::atomic<bool>& cancel) {
    HRESULT com = options.readMetadata ? CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE) : E_FAIL;

    auto snap = std::make_shared<Snapshot>();
    snap->generation = generation;
    snap->roots = roots;
    counting_ = true;  // again: the scan before this one may have touched the counters
    progressDone_ = progressTotal_ = 0;
    const int64_t scanTime = UnixMsNow();
    const auto started = Clock::now();
    bool announced = false;
    auto announce = [&] {
        if (announced || MsSince(started) < kAnnounceMs) return;
        announced = true;
        if (HWND hwnd = notifyWnd_.load()) PostMessageW(hwnd, notifyMsg_.load(), 0, static_cast<LPARAM>(generation));
    };

    // 1. Enumerate.
    auto t0 = Clock::now();
    std::vector<std::wstring> missingRoots;  // PathKey with a trailing backslash
    for (const std::wstring& root : roots) {
        if (cancel) break;
        DWORD attr = GetFileAttributesW(root.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring key = PathKey(root);
            if (key.empty() || key.back() != L'\\') key += L'\\';
            missingRoots.push_back(std::move(key));
            continue;
        }
        TempFolder t;
        t.path = root;
        t.name = FileNameOf(root);
        if (t.name.empty() || (root.size() == 3 && root[1] == L':')) t.name = root;  // "D:\"
        Enumerate(t, options, cancel, snap->stats, progressTotal_, announce);
        ++snap->stats.folders;
        Flatten(t, -1, *snap);
    }
    snap->stats.enumerateMs = MsSince(t0);

    // 2. Metadata: cached values first, then probe the cost of reading the rest.
    std::vector<size_t> pending;
    std::vector<char> unread(snap->items.size(), 0);  // see SaveCache
    bool uncached = false;                             // a file the cache doesn't know yet
    KeptCache kept;  // a drive that is off keeps its tags for when it comes back
    if (options.readMetadata && !cancel) {
        auto cache = LoadCache();
        for (const auto& [key, entry] : cache)
            for (const std::wstring& r : missingRoots)
                if (key.compare(0, r.size(), r) == 0) {
                    kept.emplace_back(key, entry);
                    break;
                }
        // A root the cache knows nothing about (a new library, a folder just added) is scanned for
        // the first time: its files aren't "new", they are the library.
        std::vector<std::wstring> rootKeys;
        std::vector<char> knownRoot(roots.size(), 0);
        for (size_t r = 0; r < roots.size(); ++r) {
            std::wstring key = PathKey(roots[r]);
            if (key.empty() || key.back() != L'\\') key += L'\\';
            for (const auto& entry : cache)
                if (entry.first.compare(0, key.size(), key) == 0) {
                    knownRoot[r] = 1;
                    break;
                }
            rootKeys.push_back(std::move(key));
        }
        int lastRoot = -1;
        std::wstring lastFolder;
        for (size_t i = 0; i < snap->items.size(); ++i) {
            MediaItem& it = snap->items[i];
            const std::wstring pathKey = PathKey(it.path);
            auto c = cache.find(pathKey);
            if (c != cache.end()) {
                it.firstSeen = c->second.firstSeen;
            } else {
                uncached = true;
                if (it.folder != lastFolder) {  // items come folder by folder
                    lastFolder = it.folder;
                    lastRoot = -1;
                    for (size_t r = 0; r < roots.size() && lastRoot < 0; ++r)
                        if (pathKey.compare(0, rootKeys[r].size(), rootKeys[r]) == 0) lastRoot = static_cast<int>(r);
                }
                it.firstSeen = lastRoot >= 0 && knownRoot[static_cast<size_t>(lastRoot)] ? scanTime : 0;
            }
            // Files marked damaged are few and are checked again (the rule may have changed since
            // the cache was written); everything else comes from the cache.
            if (c != cache.end() && c->second.size == it.size && c->second.mtime == it.mtime && !c->second.damaged &&
                !c->second.stale) {
                it.duration = c->second.duration;
                it.title = std::move(c->second.title);
                it.artist = std::move(c->second.artist);
                it.album = std::move(c->second.album);
                it.albumArtist = std::move(c->second.albumArtist);
                it.genre = std::move(c->second.genre);
                it.track = c->second.track;
                it.year = c->second.year;
                it.disc = c->second.disc;
                it.damaged = c->second.damaged;
            } else if (!it.offline) {
                pending.push_back(i);
            } else {
                unread[i] = 1;  // reading would download it from the cloud
            }
        }
    }
    for (MediaItem& it : snap->items) {
        FillSearchKey(it);
        snap->newestFirstSeen = std::max(snap->newestFirstSeen, it.firstSeen);
    }
    snap->BuildIndex();
    progressTotal_ = snap->items.size();
    progressDone_ = snap->items.size() - pending.size();
    counting_ = false;

    size_t done = 0;
    auto tm = Clock::now();
    while (done < pending.size() && done < kProbeFiles && !cancel) {
        ReadMetadata(snap->items[pending[done]]);
        FillSearchKey(snap->items[pending[done]]);
        ++done;
        ++progressDone_;
        announce();
    }
    double probeMs = MsSince(tm);
    // Slow files, or fast ones in such numbers (20 000 songs) that the list would stay empty for
    // seconds: show the files first, then fill in their tags.
    const double perFile = done > 0 ? probeMs / static_cast<double>(done) : 0;
    const double restMs = perFile * static_cast<double>(pending.size() - done) / MetadataThreads();
    snap->stats.metadataSecondPass =
        done > 0 && done < pending.size() && (perFile > kInlineMetadataMs || restMs > kInlineMetadataTotalMs);

    if (cancel) {
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
    for (size_t i = done; i < pending.size(); ++i) unread[pending[i]] = 1;
    while (done < pending.size() && !cancel) {
        const size_t last = std::min(pending.size(), done + kMetadataChunk);
        ReadMetadataParallel(*snap, pending, done, last, threads, cancel, progressDone_);
        if (cancel) break;
        for (size_t i = done; i < last; ++i) unread[pending[i]] = 0;
        done = last;
        announce();
        // The probe guessed fast files, but slow ones followed (MKVs for the property system):
        // show the files now and fill in the rest as a second pass.
        if (!snap->stats.metadataSecondPass && done < pending.size() && MsSince(tm) > kInlineLimitMs) {
            snap->stats.metadataSecondPass = true;
            snap->complete = false;
            snap->stats.metadataFiles = done;
            if (options.readMetadata) SaveCache(*snap, unread, kept);
            Publish(std::make_shared<Snapshot>(*snap));
            lastPublish = Clock::now();
        }
        if (snap->stats.metadataSecondPass && Clock::now() - lastPublish > kProgressInterval && done < pending.size()) {
            snap->stats.metadataFiles = done;
            if (options.readMetadata) SaveCache(*snap, unread, kept);
            Publish(std::make_shared<Snapshot>(*snap));
            lastPublish = Clock::now();
        }
    }
    snap->stats.metadataMs = MsSince(tm);
    snap->stats.metadataFiles = done;
    snap->stats.metadataMsPerFile = done ? snap->stats.metadataMs / static_cast<double>(done) : 0;

    // Files only found (cloud-only ones are never read) are saved too, or they'd be new every scan.
    if (!cancel && options.readMetadata && (done > 0 || uncached)) SaveCache(*snap, unread, kept);
    if (!cancel && (!snap->complete || done > 0)) {
        snap->complete = true;
        Publish(std::make_shared<const Snapshot>(std::move(*snap)));
    }
    if (SUCCEEDED(com)) CoUninitialize();
    // Also covers a scan that ended without a complete publish; a newer scan keeps the flag.
    if (generation == generation_) scanning_ = false;
}

}  // namespace library
