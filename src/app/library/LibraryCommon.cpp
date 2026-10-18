#include "app/library/LibraryCommon.h"
#include "ui/Host.h"

#include "Util.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "library/MediaTypes.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cwchar>
#include <cwctype>
#include <unordered_set>

namespace app::lib {

using namespace ui;

// --- formatting ----------------------------------------------------------------------

std::wstring FormatDuration(double seconds) {
    if (!(seconds > 0)) return {};
    const long long s = static_cast<long long>(seconds + 0.5);
    wchar_t buf[32];
    if (s >= 3600)
        swprintf_s(buf, L"%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else
        swprintf_s(buf, L"%lld:%02lld", s / 60, s % 60);
    return buf;
}

std::wstring FormatSize(uint64_t bytes) {
    const bool ru = CurrentLanguage() == Language::Russian;
    const wchar_t* units[] = {Tr(L"B"), Tr(L"KB"), Tr(L"MB"), Tr(L"GB"), Tr(L"TB")};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024 && u < 4) {
        v /= 1024;
        ++u;
    }
    wchar_t buf[48];
    if (u == 0 || v >= 100)
        swprintf_s(buf, L"%.0f %s", v, units[u]);
    else
        swprintf_s(buf, L"%.1f %s", v, units[u]);
    std::wstring s = buf;
    if (ru)
        for (auto& c : s)
            if (c == L'.') c = L',';
    return s;
}

namespace {

SYSTEMTIME LocalTimeOf(int64_t unixMs) {
    const uint64_t ft = static_cast<uint64_t>(unixMs) * 10000ull + 116444736000000000ull;
    FILETIME f{static_cast<DWORD>(ft & 0xFFFFFFFF), static_cast<DWORD>(ft >> 32)};
    FILETIME local{};
    FileTimeToLocalFileTime(&f, &local);
    SYSTEMTIME st{};
    FileTimeToSystemTime(&local, &st);
    return st;
}

const wchar_t* LocaleName() {
    return CurrentLanguage() == Language::Russian ? L"ru-RU" : L"en-US";
}

long long DayNumber(const SYSTEMTIME& st) {
    SYSTEMTIME d = st;
    d.wHour = d.wMinute = d.wSecond = d.wMilliseconds = 0;
    FILETIME f{};
    SystemTimeToFileTime(&d, &f);
    const uint64_t v = (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    return static_cast<long long>(v / 864000000000ull);
}

}  // namespace

std::wstring FormatDate(int64_t unixMs) {
    if (unixMs <= 0) return {};
    const SYSTEMTIME st = LocalTimeOf(unixMs);
    wchar_t buf[64];
    // Fixed patterns: the user's own short-date override ("17-Oct-26") shouldn't leak into one language only.
    const wchar_t* pattern = CurrentLanguage() == Language::Russian ? L"dd.MM.yyyy" : L"MMM d, yyyy";
    if (!GetDateFormatEx(LocaleName(), 0, &st, pattern, buf, 64, nullptr)) return {};
    return buf;
}

std::wstring FormatClock(int64_t unixMs) {
    if (unixMs <= 0) return {};
    const SYSTEMTIME st = LocalTimeOf(unixMs);
    wchar_t buf[16];
    swprintf_s(buf, L"%02u:%02u", st.wHour, st.wMinute);
    return buf;
}

int DayBucket(int64_t unixMs) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const long long d = DayNumber(now) - DayNumber(LocalTimeOf(unixMs));
    return d <= 0 ? 0 : d == 1 ? 1 : 2;
}

namespace {

std::wstring CountWith(long long n, const wchar_t* en1, const wchar_t* enN, const wchar_t* ru1, const wchar_t* ru2,
                       const wchar_t* ru5) {
    const bool ru = CurrentLanguage() == Language::Russian;
    return std::to_wstring(n) + L" " + (ru ? Plural(n, ru1, ru2, ru5) : (n == 1 ? en1 : enN));
}

}  // namespace

std::wstring CountSongs(long long n) {
    return CountWith(n, L"song", L"songs", L"песня", L"песни", L"песен");
}

std::wstring CountPlays(long long n) {
    return CountWith(n, L"play", L"plays", L"прослушивание", L"прослушивания", L"прослушиваний");
}

std::wstring CountAlbums(long long n) {
    return CountWith(n, L"album", L"albums", L"альбом", L"альбома", L"альбомов");
}

std::wstring CountArtists(long long n) {
    return CountWith(n, L"artist", L"artists", L"исполнитель", L"исполнителя", L"исполнителей");
}

std::wstring CountGenres(long long n) {
    return CountWith(n, L"genre", L"genres", L"жанр", L"жанра", L"жанров");
}

std::wstring CountStations(long long n) {
    return CountWith(n, L"station", L"stations", L"станция", L"станции", L"станций");
}

std::wstring CountEpisodes(long long n) {
    return CountWith(n, L"episode", L"episodes", L"выпуск", L"выпуска", L"выпусков");
}

std::wstring ArtistLabel(const std::wstring& artist) {
    return artist.empty() ? std::wstring(Tr(L"Unknown artist")) : artist;
}

std::wstring AlbumLabel(const std::wstring& album) {
    return album.empty() ? std::wstring(Tr(L"Unknown album")) : album;
}

std::wstring AlbumFolderKey(const library::MediaItem& m) {
    std::wstring folder = m.folder;
    std::wstring name = library::FileNameOf(folder);
    for (auto& c : name) c = static_cast<wchar_t>(towlower(c));
    for (const wchar_t* prefix : {L"cd", L"disc", L"disk", L"диск"}) {
        const size_t n = wcslen(prefix);
        if (name.compare(0, n, prefix) != 0) continue;
        size_t i = n;
        while (i < name.size() && (name[i] == L' ' || name[i] == L'_' || name[i] == L'-' || name[i] == L'.')) ++i;
        const size_t digits = i;
        while (i < name.size() && iswdigit(name[i])) ++i;
        if (i > digits && i == name.size()) folder = library::ParentFolder(folder);
        break;
    }
    return library::PathKey(folder);
}

std::wstring AlbumRouteArg(const library::MediaItem& m) {
    return m.album.empty() ? std::wstring() : m.album + L"\n" + AlbumFolderKey(m);
}

std::wstring MediaTitle(const std::wstring& path, const library::MediaItem* item, const std::wstring& storedTitle) {
    if (IsUrl(path)) return storedTitle.empty() ? path : storedTitle;
    if (item) {
        if (item->kind == library::MediaKind::Audio) return item->DisplayTitle();
        // Episodes read "S01E02 · Pilot" instead of the raw "Show.Name.S01E02.Pilot.1080p".
        if (std::wstring ep = library::EpisodeLabel(item->name); !ep.empty()) return ep;
        return item->name;
    }
    // Outside the library: a stored title unless it is just the file name (mpv's default title).
    if (!storedTitle.empty() && !SamePath(storedTitle, library::FileNameOf(path))) return storedTitle;
    return library::StemOf(path);
}

bool IsUrl(const std::wstring& path) {
    return path.find(L"://") != std::wstring::npos;
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring NowPlayingPath() {
    App& app = TheApp();
    if (!app.player().State().fileLoaded) return {};
    const QueueItem* item = app.queue().CurrentItem();
    return item ? item->path : std::wstring();
}

// --- queue / playback -------------------------------------------------------------------

QueueItem MakeQueueItem(const std::wstring& path, const std::wstring& title, double duration) {
    QueueItem q;
    q.path = path;
    q.title = title;
    q.duration = duration;
    return q;
}

void PlayQueue(std::vector<QueueItem> items, int index, const std::wstring& name) {
    if (items.empty()) return;
    index = std::clamp(index, 0, static_cast<int>(items.size()) - 1);
    TheApp().PlayQueueItems(std::move(items), index, name);
}

void PlayNext(std::vector<QueueItem> items) {
    if (items.empty()) return;
    App& app = TheApp();
    if (app.queue().Items().empty() || !app.player().State().fileLoaded) {
        PlayQueue(std::move(items), 0, {});
        return;
    }
    const size_t n = items.size();
    app.queue().InsertNext(std::move(items));
    Toast(n == 1 ? std::wstring(Tr(L"Will play next")) : Fmt(Tr(L"{0} items will play next"), static_cast<long long>(n)),
          "next");
}

void AddToQueue(std::vector<QueueItem> items) {
    if (items.empty()) return;
    App& app = TheApp();
    if (app.queue().Items().empty() || !app.player().State().fileLoaded) {
        PlayQueue(std::move(items), 0, {});
        return;
    }
    const size_t n = items.size();
    app.queue().Add(std::move(items));
    Toast(n == 1 ? std::wstring(Tr(L"Added to queue")) : Fmt(Tr(L"{0} items added to queue"), static_cast<long long>(n)),
          "queue");
}

// --- file dialogs ---------------------------------------------------------------------

namespace {

HWND MainHwnd() {
    return TheApp().main() ? TheApp().main()->Hwnd() : nullptr;
}

std::wstring PickFolder() {
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(ui::Host::CoCreateDialog(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(Tr(L"Add folder to library"));
    if (SUCCEEDED(dlg->Show(MainHwnd()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

const COMDLG_FILTERSPEC kPlaylistFilter[] = {{L"M3U", L"*.m3u8;*.m3u"}};

}  // namespace

std::wstring PickPlaylistFile() {
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(ui::Host::CoCreateDialog(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    dlg->SetFileTypes(1, kPlaylistFilter);
    dlg->SetTitle(Tr(L"Import playlist"));
    if (SUCCEEDED(dlg->Show(MainHwnd()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

std::wstring PickPlaylistSaveFile(const std::wstring& suggestedName) {
    std::wstring result;
    IFileSaveDialog* dlg = nullptr;
    if (FAILED(ui::Host::CoCreateDialog(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    dlg->SetFileTypes(1, kPlaylistFilter);
    dlg->SetDefaultExtension(L"m3u8");
    std::wstring name = suggestedName;
    for (auto& c : name)
        if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    dlg->SetFileName(name.c_str());
    dlg->SetTitle(Tr(L"Export playlist"));
    if (SUCCEEDED(dlg->Show(MainHwnd()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

// --- library roots ----------------------------------------------------------------------

void RescanLibrary() {
    TheApp().library().Rescan();
    TheApp().libraryChanged.Emit();  // pages show the scanning indicator right away
}

void AddFolderToLibrary() {
    const std::wstring folder = PickFolder();
    if (folder.empty()) return;
    library::MediaLibrary& lib = TheApp().library();
    if (!lib.AddRoot(folder)) {
        Toast(Tr(L"This folder is already in the library"), "folder");
        return;
    }
    lib.Save();
    RescanLibrary();
    Toast(Fmt(Tr(L"Added “{0}” — scanning…"), {FileNameOf(folder).empty() ? folder : FileNameOf(folder)}), "folder");
}

void RemoveLibraryRoot(const std::wstring& folder) {
    library::MediaLibrary& lib = TheApp().library();
    if (!lib.RemoveRoot(folder)) return;
    lib.Save();
    RescanLibrary();
    Toast(Tr(L"Folder removed from the library. Files stay on disk."), "folder");
}

// --- shell ------------------------------------------------------------------------------

void ShowInExplorer(const std::wstring& path) {
    if (path.empty() || IsUrl(path)) return;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
    if (pidl) {
        if (ui::Host::NativeUiAllowed()) SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        return;
    }
    ui::Host::ShellOpen(nullptr, L"open", library::ParentFolder(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// --- menus ------------------------------------------------------------------------------

MenuItem Item(const wchar_t* label, const char* icon, std::function<void()> action, bool enabled) {
    MenuItem m;
    m.label = Tr(label);
    m.icon = icon ? icon : "";
    m.action = std::move(action);
    m.enabled = enabled;
    return m;
}

std::vector<MenuItem> AddToPlaylistMenu(std::vector<QueueItem> items) {
    std::vector<MenuItem> out;
    auto toPlaylist = [](const std::vector<QueueItem>& src) {
        std::vector<library::PlaylistItem> v;
        v.reserve(src.size());
        for (const auto& q : src) v.push_back({q.path, q.title, q.duration});
        return v;
    };
    for (const library::Playlist& pl : TheApp().playlists().All()) {
        MenuItem m;
        m.label = pl.name;
        m.icon = "playlist";
        const std::string id = pl.id;
        const std::wstring name = pl.name;
        m.action = [id, name, items, toPlaylist] {
            const library::Playlist* p = TheApp().playlists().Find(id);
            if (!p) return;
            // Items already in the playlist are skipped, not duplicated.
            std::unordered_set<std::wstring> have;
            for (const auto& it : p->items) have.insert(library::PathKey(it.path));
            std::vector<QueueItem> fresh;
            for (const QueueItem& q : items)
                if (have.insert(library::PathKey(q.path)).second) fresh.push_back(q);
            if (fresh.empty()) {
                Toast(Fmt(Tr(L"Already in “{0}”"), {name}), "playlist");
                return;
            }
            if (TheApp().playlists().Add(id, toPlaylist(fresh))) {
                const size_t skipped = items.size() - fresh.size();
                Toast(skipped ? Fmt(Tr(L"Added to “{0}”, {1} already there"), {name, std::to_wstring(skipped)})
                              : Fmt(Tr(L"Added to “{0}”"), {name}),
                      "playlist");
                TheApp().libraryChanged.Emit();
            }
        };
        out.push_back(std::move(m));
    }
    if (!out.empty()) out.push_back(MenuItem::Separator());
    out.push_back(Item(L"New Playlist…", "add", [items, toPlaylist] {
        ShowNewPlaylistDialog([items, toPlaylist](const std::string& id) {
            if (id.empty()) return;
            if (TheApp().playlists().Add(id, toPlaylist(items))) {
                if (const library::Playlist* p = TheApp().playlists().Find(id))
                    Toast(Fmt(Tr(L"Added to “{0}”"), {p->name}), "playlist");
                TheApp().libraryChanged.Emit();
            }
        });
    }));
    return out;
}

std::vector<MenuItem> MediaMenu(const std::wstring& path, std::vector<QueueItem> queue, int index,
                                const std::wstring& queueName, bool goToFolder) {
    std::vector<MenuItem> m;
    QueueItem self = index >= 0 && index < static_cast<int>(queue.size()) ? queue[static_cast<size_t>(index)]
                                                                          : MakeQueueItem(path);
    m.push_back(Item(L"Play", "playback", [queue, index, queueName] { PlayQueue(queue, index, queueName); }));
    m.push_back(Item(L"Play Next", "next_twotone", [self] { PlayNext({self}); }));
    m.push_back(Item(L"Add to Queue", "queue", [self] { AddToQueue({self}); }));
    MenuItem pl = Item(L"Add to Playlist", "playlist", nullptr);
    pl.submenu = AddToPlaylistMenu({self});
    m.push_back(std::move(pl));
    m.push_back(MenuItem::Separator());

    const library::MediaKind kind = library::KindOf(path);
    if (kind == library::MediaKind::Video) {
        const library::HistoryEntry* h = TheApp().history().Find(path);
        const bool watched = h && h->duration > 0 && h->Progress() >= 0.95;
        if (watched) {
            m.push_back(Item(L"Mark as Unwatched", "replay", [path] {
                TheApp().history().Remove(path);
                TheApp().libraryChanged.Emit();
            }));
        } else {
            const double dur = self.duration > 0 ? self.duration : (h ? h->duration : 0);
            m.push_back(Item(L"Mark as Watched", "check_active", [path, dur] {
                // Position == duration reads as 100% watched everywhere (grids, Continue Watching).
                const double d = dur > 0 ? dur : 1;
                TheApp().history().UpdatePosition(path, d, d);
                TheApp().libraryChanged.Emit();
            }));
        }
    }
    const bool fav = TheApp().favorites().Contains(path);
    MenuItem f = Item(fav ? L"Remove from Favorites" : L"Favorite", fav ? "favorite_active" : "favorite", [path] {
        const bool on = TheApp().favorites().Toggle(path);
        Toast(on ? Tr(L"Added to favorites") : Tr(L"Removed from favorites"), on ? "favorite_active" : "favorite");
        TheApp().libraryChanged.Emit();  // Home's Favorites row and the heart marks
    });
    m.push_back(std::move(f));
    if (!IsUrl(path)) {
        m.push_back(MenuItem::Separator());
        m.push_back(Item(L"Show in File Explorer", "open", [path] { ShowInExplorer(path); }));
        if (goToFolder) {
            const std::wstring folder = library::ParentFolder(path);
            const Section sec = kind == library::MediaKind::Audio ? Section::Music : Section::Videos;
            m.push_back(Item(L"Go to Folder", "folder", [folder, sec] { TheApp().Navigate(Route{sec, folder}); }));
        }
    }
    return m;
}

// --- prompts ----------------------------------------------------------------------------

void ShowTextPrompt(const std::wstring& title, const std::wstring& label, const std::wstring& initial,
                    const std::wstring& okText, std::function<void(const std::wstring&)> done,
                    const std::wstring& placeholder) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    auto d = std::make_unique<Dialog>(title, 480);
    if (!label.empty()) d->Body()->Add<Label>(label, TextStyle::Body, Tone::Tertiary);
    auto* box = d->Body()->Add<TextBox>(placeholder);
    box->width = Length::Grow();
    box->SetText(initial);
    d->Footer()->Add<Spacer>();
    Dialog* raw = d.get();
    d->Footer()->Add<Button>(Tr(L"Cancel"), ButtonKind::Ghost)->onClick = [raw] { raw->Close(); };
    auto ok = [raw, box, done] {
        std::wstring text = box->Text();
        while (!text.empty() && iswspace(text.back())) text.pop_back();
        while (!text.empty() && iswspace(text.front())) text.erase(text.begin());
        if (text.empty()) return;
        auto run = done;  // the dialog (and this lambda) is gone after Close()
        raw->Close();
        if (run) run(text);
    };
    d->Footer()->Add<Button>(okText, ButtonKind::Primary)->onClick = ok;
    box->onSubmit = [ok](const std::wstring&) { ok(); };
    ShowDialog(w, std::move(d));
    w->SetFocus(box, true);
    box->SelectAll();
}

void ShowConfirm(const std::wstring& title, const std::wstring& text, const std::wstring& okText,
                 std::function<void()> done) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    auto d = std::make_unique<Dialog>(title, 440);
    auto* l = d->Body()->Add<Label>(text, TextStyle::Body, Tone::Secondary);
    l->wrap = true;
    l->width = Length::Grow();
    d->Footer()->Add<Spacer>();
    Dialog* raw = d.get();
    d->Footer()->Add<Button>(Tr(L"Cancel"), ButtonKind::Ghost)->onClick = [raw] { raw->Close(); };
    auto* okBtn = d->Footer()->Add<Button>(okText, ButtonKind::Primary);
    okBtn->onClick = [raw, done] {
        auto run = done;  // the dialog (and this lambda) is gone after Close()
        raw->Close();
        if (run) run();
    };
    ShowDialog(w, std::move(d));
    w->SetFocus(okBtn, true);
}

void Toast(const std::wstring& text, const char* icon) {
    if (TheApp().main()) ShowToast(TheApp().main(), text, icon ? icon : "");
}

// --- small stores ------------------------------------------------------------------------

namespace {

// Entries are "<url>\t<title>" joined by "\n" (URLs and titles never contain either).
std::string Encode(const std::vector<SavedStream>& list) {
    std::wstring s;
    for (const auto& e : list) {
        if (!s.empty()) s += L'\n';
        s += e.url;
        s += L'\t';
        s += e.title;
    }
    return WideToUtf8(s);
}

std::vector<SavedStream> Decode(const std::string& text) {
    std::vector<SavedStream> out;
    const std::wstring s = Utf8ToWide(text);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(L'\n', pos);
        if (end == std::wstring::npos) end = s.size();
        const std::wstring line = s.substr(pos, end - pos);
        const size_t tab = line.find(L'\t');
        SavedStream e;
        e.url = line.substr(0, tab);
        if (tab != std::wstring::npos) e.title = line.substr(tab + 1);
        if (!e.url.empty()) out.push_back(std::move(e));
        pos = end + 1;
    }
    return out;
}

}  // namespace

std::vector<SavedStream> SavedStreams(const std::string& group) {
    const std::string key = "network." + group;
    const std::string v = TheApp().UiValue(key, "\x01");
    if (v == "\x01" && group == "radio") {
        // Curated public stations; the user can remove them.
        return {
            {L"https://ice1.somafm.com/groovesalad-128-mp3", L"SomaFM — Groove Salad"},
            {L"https://ice1.somafm.com/dronezone-128-mp3", L"SomaFM — Drone Zone"},
            {L"https://ice1.somafm.com/secretagent-128-mp3", L"SomaFM — Secret Agent"},
            {L"https://stream.radioparadise.com/mp3-128", L"Radio Paradise — Main Mix"},
            {L"https://stream.radioparadise.com/mellow-128", L"Radio Paradise — Mellow Mix"},
        };
    }
    if (v == "\x01") return {};
    return Decode(v);
}

void SetSavedStreams(const std::string& group, const std::vector<SavedStream>& list) {
    TheApp().SetUiValue("network." + group, Encode(list));
}

std::wstring PlaylistImage(const library::Playlist& p) {
    const std::string cover = TheApp().UiValue("playlist.cover." + p.id);
    if (!cover.empty()) {
        std::wstring path = Utf8ToWide(cover);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
    }
    const auto snap = TheApp().library().Current();
    for (const auto& it : p.items)
        if (!IsUrl(it.path)) {
            const library::MediaItem* m = snap ? snap->FindItem(it.path) : nullptr;
            return m && m->offline ? std::wstring() : it.path;
        }
    return {};
}

void CountPlaylistPlay(const std::string& id) {
    TheApp().SetUiValue("stats.playlist." + id, std::to_string(PlaylistPlays(id) + 1));
}

int PlaylistPlays(const std::string& id) {
    return atoi(TheApp().UiValue("stats.playlist." + id, "0").c_str());
}

bool ListModeFor(const std::string& key, bool fallbackList) {
    const std::string v = TheApp().UiValue("view." + key);
    if (v == "list") return true;
    if (v == "grid") return false;
    return fallbackList;
}

void SetListModeFor(const std::string& key, bool list) {
    TheApp().SetUiValue("view." + key, list ? "list" : "grid");
}

}  // namespace app::lib
