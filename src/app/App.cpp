#include "app/App.h"
#include "ui/Host.h"

#include "Trace.h"
#include "Util.h"
#include "app/Actions.h"
#include "app/CrashReport.h"
#include "app/Dialogs.h"
#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/NowPlaying.h"
#include "app/player/SeekPreview.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "library/MediaTypes.h"
#include "ui/Theme.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/Shell.h"
#include "app/Skins.h"
#include "app/SettingsActions.h"
#include "app/player/PlayerView.h"
#include "app/settings/ShortcutStore.h"
#include "core/SettingsSchema.h"
#include "gfx/Gpu.h"
#include "net/Http.h"
#include "setup/Installer.h"
#include "ui/Controls.h"

#include <exdisp.h>
#include <propkey.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <typeinfo>

namespace app {

using namespace ui;

// --- sections -------------------------------------------------------------------------

const wchar_t* SectionName(Section s) {
    switch (s) {
        case Section::Home: return L"Home";
        case Section::Videos: return L"Videos";
        case Section::Music: return L"Music";
        case Section::Network: return L"Network";
        case Section::Playlists: return L"Playlists";
        case Section::Recent: return L"History";
    }
    return L"";
}

const char* SectionIcon(Section s) {
    switch (s) {
        case Section::Home: return "home";
        case Section::Videos: return "videos";
        case Section::Music: return "songs";
        case Section::Network: return "streaming";
        case Section::Playlists: return "playlist";
        case Section::Recent: return "recent";
    }
    return "home";
}

// --- App -------------------------------------------------------------------------------

namespace {

// The sort column of an open Explorer window that shows `dir` (where the file was most likely
// double-clicked), as library::SortFileEntries takes it; empty when no window shows the folder.
// Explorer's grouping is ignored: Downloads groups by the date it also sorts by.
std::string ExplorerSortOrder(const std::wstring& dir) {
    using gfx::ComPtr;
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows))))
        return {};
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count && i < 64; ++i) {
        VARIANT v{};
        v.vt = VT_I4;
        v.lVal = i;
        ComPtr<IDispatch> disp;
        ComPtr<IServiceProvider> sp;
        ComPtr<IShellBrowser> browser;
        ComPtr<IShellView> view;
        ComPtr<IFolderView2> fv;
        ComPtr<IShellItem> folder;
        if (FAILED(windows->Item(v, &disp)) || !disp || FAILED(disp.As(&sp)) ||
            FAILED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
            FAILED(browser->QueryActiveShellView(&view)) || FAILED(view.As(&fv)) ||
            FAILED(fv->GetFolder(IID_PPV_ARGS(&folder))))
            continue;
        PWSTR shown = nullptr;
        if (FAILED(folder->GetDisplayName(SIGDN_FILESYSPATH, &shown))) continue;
        const bool same = _wcsicmp(shown, dir.c_str()) == 0;
        CoTaskMemFree(shown);
        SORTCOLUMN col{};
        if (!same || FAILED(fv->GetSortColumns(&col, 1))) continue;
        const char* key = IsEqualPropertyKey(col.propkey, PKEY_DateModified) ? "date"
                          : IsEqualPropertyKey(col.propkey, PKEY_DateCreated) ? "created"
                          : IsEqualPropertyKey(col.propkey, PKEY_Size)        ? "size"
                                                                              : "name";
        return std::string(key) + (col.direction == SORT_DESCENDING ? ":desc" : "");
    }
    return {};
}

// playback.folderOrder for the files of `dir` (a folder path without the trailing backslash).
std::string FolderOrder(const Config& config, std::wstring dir) {
    const std::string order = config.GetString("playback.folderOrder");
    if (order != "explorer") return order;
    if (!dir.empty() && dir.back() == L':') dir += L'\\';  // Explorer shows a drive as "C:\"
    const std::string shown = ExplorerSortOrder(dir);
    return shown.empty() ? "name" : shown;
}

std::wstring FolderOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool IsUrl(const std::wstring& s) {
    return s.find(L"://") != std::wstring::npos;
}

bool IsDirectory(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<std::wstring> MediaInFolder(const std::wstring& dir, bool recursive, const std::string& order) {
    std::wstring probe = dir;
    if (!probe.empty() && probe.back() != L'\\') probe += L'\\';
    std::vector<std::wstring> out = ListSiblingMedia(probe + L"x", order);  // lists the folder of a path
    if (!recursive) return out;
    std::vector<std::wstring> subdirs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((probe + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            subdirs.push_back(probe + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(subdirs.begin(), subdirs.end(),
              [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });
    for (const auto& sub : subdirs) {
        if (out.size() > 5000) break;
        auto more = MediaInFolder(sub, true, order);
        out.insert(out.end(), more.begin(), more.end());
    }
    return out;
}

bool IsPlaylistFile(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    return _wcsicmp(ext, L".m3u") == 0 || _wcsicmp(ext, L".m3u8") == 0;
}

QueueItem ItemFor(const std::wstring& path) {
    return QueueItem{path, IsUrl(path) ? path : FileNameOf(path), 0};
}

std::vector<std::wstring> FileDialog(HWND owner, bool folders, bool multi, bool subtitles) {
    std::vector<std::wstring> out;
    gfx::ComPtr<IFileOpenDialog> dlg;
    if (FAILED(ui::Host::CoCreateDialog(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (folders) opts |= FOS_PICKFOLDERS;
    if (multi) opts |= FOS_ALLOWMULTISELECT;
    dlg->SetOptions(opts);
    const COMDLG_FILTERSPEC media[] = {
        {Tr(L"Video and audio"),
         L"*.mkv;*.mp4;*.m4v;*.avi;*.mov;*.webm;*.wmv;*.flv;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.vob;*.ogv;*.3gp;*.rmvb;"
         L"*.mp3;*.flac;*.m4a;*.aac;*.ogg;*.opus;*.wav;*.wma;*.ac3;*.dts;*.m3u;*.m3u8;*.pls"},
        {Tr(L"All files"), L"*.*"},
    };
    const COMDLG_FILTERSPEC subs[] = {
        {Tr(L"Subtitles"), L"*.srt;*.ass;*.ssa;*.vtt;*.sub;*.sup;*.idx;*.smi"},
        {Tr(L"All files"), L"*.*"},
    };
    if (!folders) {
        if (subtitles)
            dlg->SetFileTypes(2, subs);
        else
            dlg->SetFileTypes(2, media);
    }
    if (FAILED(dlg->Show(owner))) return out;
    gfx::ComPtr<IShellItemArray> items;
    if (FAILED(dlg->GetResults(&items))) return out;
    DWORD n = 0;
    items->GetCount(&n);
    for (DWORD i = 0; i < n; ++i) {
        gfx::ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            out.emplace_back(path);
            CoTaskMemFree(path);
        }
    }
    return out;
}

}  // namespace

App::App() : thumbnails_(2) {}

App& App::Get() {
    static App app;
    return app;
}

// --- settings & state ------------------------------------------------------------------

void App::ApplyTheme() {
    const std::string mode = config_.GetString("general.theme");
    bool dark = mode != "light";
    if (mode == "system") {
        DWORD light = 0, size = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS)
            dark = light == 0;
    }
    static const char* const kAccents[] = {"white", "pearl", "silver", "steel", "graphite"};
    int accent = 0;
    const std::string a = config_.GetString("general.accent");
    for (int i = 0; i < 5; ++i)
        if (a == kAccents[i]) accent = i;
    ui::Theme::Get().Set(dark ? ui::ThemeMode::Dark : ui::ThemeMode::Light, accent);
    for (ui::Host* h : ui::Host::All()) h->OnThemeChanged();
}

void App::OnConfigChanged(const std::string& key) {
    if (key == "general.theme" || key == "general.accent") {
        ApplyTheme();
        return;
    }
    if (key == "general.language") {
        SetLanguage(config_.GetString("general.language") == "en" ? Language::English : Language::Russian);
        system_.LanguageChanged();
        ReopenPlayerPanels();  // before the rebuilt library pane closes every popup
        languageChanged.Emit();
        // The preferred audio and subtitle languages default to the interface's ("app").
        for (const char* k : {"audio.language", "subtitles.language"})
            for (const auto& [name, value] : MpvOptionFor(config_, k)) player_.ApplySetting(name, value);
        return;
    }
    if (key == "playback.repeat") {
        const std::string r = config_.GetString("playback.repeat");
        queue_.SetRepeat(r == "one" ? Repeat::One : r == "all" ? Repeat::All : Repeat::Off);
    }
    if (key == "playback.shuffle") queue_.SetShuffle(config_.GetBool("playback.shuffle"));
    if (key == "playback.stopAfterEach") SyncNextInPlayer();
    if (key == "interface.alwaysOnTop") ApplyWindowOptions();
    if (key == "privacy.rememberPosition") player_.rememberPosition = config_.GetBool("privacy.rememberPosition");
    if (key == "privacy.excludePatterns") history_.SetExcludePatterns(config_.GetWString("privacy.excludePatterns"));
    if (key.rfind("interface.", 0) == 0) {
        system_.SettingsChanged();
        if (main_) main_->RequestLayout();
    }
    if (key == "library.showHidden" || key == "library.readTags") {
        library_.SetOptions({config_.GetBool("library.showHidden"), config_.GetBool("library.readTags")});
        library_.Rescan();
    }
    // Settings mapped to mpv apply live as properties. vf and hwdec are LiveState's: CPU filters
    // need copy-back decoding first, in that order.
    for (const auto& [name, value] : MpvOptionFor(config_, key))
        if (name != "vf" && name != "hwdec") player_.ApplySetting(name, value);
}

std::string App::UiValue(const std::string& key, const std::string& fallback) const {
    auto it = ui_.find(key);
    return it == ui_.end() ? fallback : it->second;
}

void App::SetUiValue(const std::string& key, const std::string& value) {
    if (UiValue(key) == value) return;
    ui_[key] = value;
    uiDirty_ = true;
}

void App::LoadUiState() {
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"ui.json"), &text)) return;
    std::string error;
    auto doc = json::Parse(text, &error);
    if (!doc || !doc->IsObject()) {
        BackupCorruptFile(DataFilePath(L"ui.json"));
        // Only the layout is lost; someone with a settings file has been through onboarding.
        if (GetFileAttributesW(DataFilePath(L"settings.json").c_str()) != INVALID_FILE_ATTRIBUTES)
            ui_["onboarded"] = "1";
        return;
    }
    for (const auto& [k, v] : doc->AsObject())
        if (v.IsString()) ui_[k] = v.AsString();
}

void App::SaveUiState() {
    if (!uiDirty_) return;
    json::Value doc = json::Value::MakeObject();
    for (const auto& [k, v] : ui_) doc.Set(k, json::Value(v));
    if (json::WriteFileAtomic(DataFilePath(L"ui.json"), json::Serialize(doc, true))) uiDirty_ = false;
}

void App::SaveWindowPlacement() {
    if (!main_ || !main_->Hwnd() || main_->IsFullscreen() || !IsWindowVisible(main_->Hwnd())) return;
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (!GetWindowPlacement(main_->Hwnd(), &wp)) return;
    const RECT& r = wp.rcNormalPosition;
    // Minimized from maximized comes back maximized.
    const bool max = wp.showCmd == SW_SHOWMAXIMIZED || (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED));
    char buf[96];
    snprintf(buf, sizeof(buf), "%ld,%ld,%ld,%ld,%d", r.left, r.top, r.right, r.bottom, max ? 1 : 0);
    SetUiValue("window.placement", buf);
}

void App::RestoreWindowPlacement() {
    if (!config_.GetBool("interface.rememberWindow")) return;
    long l = 0, t = 0, r = 0, b = 0;
    int max = 0;
    if (sscanf_s(UiValue("window.placement").c_str(), "%ld,%ld,%ld,%ld,%d", &l, &t, &r, &b, &max) != 5) return;
    if (r - l < 300 || b - t < 200) return;
    // Only restore onto a monitor that still exists.
    RECT rc{l, t, r, b};
    if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) return;
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(main_->Hwnd(), &wp);
    wp.rcNormalPosition = rc;
    wp.showCmd = ui::Host::Automation() ? SW_SHOWNOACTIVATE : max ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    wp.flags = 0;
    SetWindowPlacement(main_->Hwnd(), &wp);
    // Onto a monitor scaled unlike the one it was made on, WM_DPICHANGED resized it by the scale
    // ratio (Host applies the suggested rect). It is on that monitor now: once more, at the saved size.
    WINDOWPLACEMENT now{sizeof(now)};
    if (GetWindowPlacement(main_->Hwnd(), &now) && !EqualRect(&now.rcNormalPosition, &rc))
        SetWindowPlacement(main_->Hwnd(), &wp);
}

void App::FlushStores() {
    if (RetryClaimDataDir()) {
        // The first instance has exited: start saving, from what it left on disk. Settings,
        // folders and ui.json (presets, layout) too, or this one's stale copies would replace them.
        config_.Load();
        ui_.clear();
        uiDirty_ = false;
        LoadUiState();
        const std::vector<std::wstring> roots = library_.Roots();
        library_.Load();
        if (library_.Roots() != roots) library_.Rescan();
        history_.Load();
        playlists_.Load();
        bookmarks_.Load();
        favorites_.Load();
        Live().ReloadFileSettings();
        libraryChanged.Emit();
    }
    SaveWindowPlacement();
    SaveHistoryPosition();
    config_.Flush();
    history_.Flush();
    playlists_.Flush();
    bookmarks_.Flush();
    favorites_.Flush();
    SaveUiState();
}

void App::SaveHistoryPosition() {
    const PlayerState& s = player_.State();
    const QueueItem* item = queue_.CurrentItem();
    if (!s.fileLoaded || !item || s.duration <= 0 || player_.HoldsResumePoint()) return;
    if (!config_.GetBool("privacy.saveHistory") || !config_.GetBool("privacy.rememberPosition")) return;
    history_.UpdatePosition(item->path, s.position, s.duration);
}

void App::StopAtStart() {
    SaveHistoryPosition();
    player_.StopAtStart();
}

void App::OnLibraryScanned() {
    libraryChanged.Emit();
    libraryShown_ = true;
}

void App::OnThumbnails() {
    images_.OnCompleted();
}

int App::Run(HINSTANCE instance, std::vector<std::wstring> args) {
    instance_ = instance;
    for (auto& a : args)
        if (!a.empty() && a[0] != L'-') pendingOpen_.push_back(a);
    if (!pendingOpen_.empty()) launchAt_ = ui::AnimFloat::Now();

    config_.Load();
    LoadUiState();
    // First run: the language of Windows (Russian or English) before any window is built, so the
    // onboarding never switches it under a built UI (it can still be switched there).
    if (UiValue("onboarded") != "1" && config_.IsDefault("general.language"))
        config_.SetString("general.language", PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? "ru" : "en");
    LoadKeyBindings();
    SetLanguage(config_.GetString("general.language") == "en" ? Language::English : Language::Russian);
    ApplyTheme();
    Skins::Get().Start(config_);
    SetControlLayout(ControlLayout::Parse(UiValue("controls.layout", ControlLayout::Default().Serialize())));
    // The library opens on the section it was left in (only the section: a folder may be gone).
    {
        const std::string section = UiValue("library.section");
        if (section.size() == 1 && section[0] >= '0' && section[0] <= static_cast<char>('0' + static_cast<int>(Section::Recent)))
            nav_[0].route.section = static_cast<Section>(section[0] - '0');
        routeChanged.Connect([this] { SetUiValue("library.section", std::to_string(static_cast<int>(route().section))); });
    }
    config_.Subscribe("", [this](const std::string& key) { OnConfigChanged(key); });
    {
        const std::string r = config_.GetString("playback.repeat");
        queue_.SetRepeat(r == "one" ? Repeat::One : r == "all" ? Repeat::All : Repeat::Off);
        queue_.SetShuffle(config_.GetBool("playback.shuffle"));
    }
    queue_.changed.Connect([this] {
        if (!settingCurrent_) SyncNextInPlayer();
    });
    Trace("config loaded");

    main_ = std::make_unique<MainWindow>();
    if (!main_->Create()) return 1;
    // From now on other launches find this window: theirs join these files (OnCopyData).
    forwarded_ = pendingOpen_;
    forwardedAt_ = ui::AnimFloat::Now();
    RestoreWindowPlacement();
    if (!IsWindowVisible(main_->Hwnd())) main_->Show();
    if (!pendingOpen_.empty()) {
        // The queue and the player's frame, named after the file, come with the window's first
        // frame instead of after mpv starts (posted: the reveal waits for it). PlayIndex keeps
        // the item for OnPlayerReady.
        main_->Post([this] {
            if (engineReady_ || pendingOpen_.empty()) return;
            const std::vector<std::wstring> files = pendingOpen_;
            Open(files);
        });
    }
    ApplyWindowOptions();
    system_.Attach(main_->Hwnd());
    Trace("window shown");
    // The GPU device started in wWinMain; libmpv starts after the window is up.
    gfx::Gpu::Get().NotifyWhenReady(main_->Hwnd(), ui::Host::kDeviceReadyMessage);
    // MpvOptions already expands general.extraOptions (quotes, --no-x) and puts it last.
    player_.SetStartOptions(MpvOptions(config_));
    player_.rememberPosition = config_.GetBool("privacy.rememberPosition");
    player_.onTrackChoice = OnTrackChoice;
    player_.StartAsync(main_->Hwnd(), main_->VideoHwnd());

    // Stores and the library scan load after the window is up.
    history_.Load();
    history_.SetExcludePatterns(config_.GetWString("privacy.excludePatterns"));
    playlists_.Load();
    bookmarks_.Load();
    favorites_.Load();
    library_.Load();
    library_.SetOptions({config_.GetBool("library.showHidden"), config_.GetBool("library.readTags")});
    library_.SetNotifyWindow(main_->Hwnd(), kLibraryMessage);
    thumbnails_.SetNotifyWindow(main_->Hwnd(), kThumbnailsMessage);
    if (config_.GetBool("library.scanOnStartup") || !library_.Current()) library_.Rescan();
    started_ = true;
    ControlLayoutChanged().Connect([this] { SetUiValue("controls.layout", CurrentControlLayout().Serialize()); });
    main_->StartTimer(3000, [this] { FlushStores(); }, true);
    InitDialogs();
    // Data files that could not be parsed were kept as *.corrupt-<time>.json (core/Paths.h).
    if (std::vector<std::wstring> bad = TakeCorruptFileNotices(); !bad.empty()) {
        static const std::pair<const wchar_t*, const wchar_t*> kWhat[] = {
            {L"history.json", L"watch history"},  {L"playlists.json", L"playlists"},
            {L"bookmarks.json", L"bookmarks"},    {L"favorites.json", L"favorites"},
            {L"library.json", L"library folders"}, {L"ui.json", L"window layout"},
            {L"settings.json", L"settings"},      {L"file-settings.json", L"per-file settings"},
        };
        std::wstring names;
        for (const std::wstring& n : bad) {
            std::wstring what = n;
            for (const auto& [file, label] : kWhat)
                if (_wcsicmp(n.c_str(), file) == 0) what = Tr(label);
            names += (names.empty() ? L"" : L", ") + what;
        }
        ShowToast(main_.get(),
                  Fmt(Tr(L"Damaged file replaced: {0}. A backup is in the data folder."), {names}),
                  "info", 8000);
    }
    if (const std::wstring crash = TakeCrashNotice(); !crash.empty()) {
        Log("the previous session crashed: " + json::ToUtf8(crash));
        ShowToast(main_.get(), Tr(L"Moonbow crashed last time."), "info", 10000, Tr(L"Show report"),
                  [] { RunSettingsAction("open-logs-folder"); });
    }
    if (UiValue("onboarded") != "1") {
        app::ShowOnboarding([this] {
            SetUiValue("onboarded", "1");
            FlushStores();
        });
    }
    // Once after an update: what the new version brings. Not on a first start (onboarding) or after
    // going back to an older version.
    if (const std::string last = UiValue("lastVersion"), now = json::ToUtf8(net::AppVersion()); last != now) {
        if (!last.empty() && UiValue("onboarded") == "1" && setup::CompareVersions(Utf8ToWide(last), net::AppVersion()) < 0)
            ShowWhatsNew(Utf8ToWide(last));
        SetUiValue("lastVersion", now);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Trace("close: message loop ended");
    FlushStores();
    Trace("close: stores saved");
    ShutdownSeekPreview();
    Trace("close: seek preview stopped");
    Skins::Get().Stop();
    main_.reset();
    Trace("close: window objects freed");
    return static_cast<int>(msg.wParam);
}

void App::OnPlayerReady(bool ok) {
    // Closed during the cold start: the shutdown thread joins the init thread and destroys mpv;
    // joining it here too, or setting mpv up, would race with that.
    if (quitting_) return;
    if (!player_.FinishStart(ok)) {
        ShowToast(main_.get(), player_.Error(), "info", 6000);
        return;
    }
    Trace("mpv ready");
    engineReady_ = true;
    Live().Start();  // owns live vf and hwdec changes (OnConfigChanged skips them)
    if (pendingIndex_ >= 0) {
        const int i = pendingIndex_;
        pendingIndex_ = -1;
        PlayIndex(i, pendingShowPlayer_);
    } else if (!pendingOpen_.empty()) {
        auto files = std::move(pendingOpen_);
        pendingOpen_.clear();
        Open(files);
    }
    launchAt_ = -1;  // a later open waits from its own start
    playerChanged.Emit(kChangeState | kChangeVolume);
    system_.Update(kChangeState);  // the media keys can resume the last item from the start
}

void App::OnPlayerEvents() {
    if (quitting_) return;  // mpv is being destroyed on the shutdown thread
    unsigned changes = player_.ProcessEvents();
    if (!changes) return;
    const PlayerState& s = player_.State();
    // First, so history and the overlays below see the new item as the current one.
    if (changes & kChangeAdvance) OnAdvanced();

    // time-pos changes every frame; repaint the UI a few times per second only.
    if (changes == kChangeTime) {
        static double lastEmit = 0;
        static long long lastSecond = -1;
        const double now = ui::AnimFloat::Now();
        const long long sec = static_cast<long long>(s.position);
        if (sec == lastSecond && now - lastEmit < 250) return;
        lastEmit = now;
        lastSecond = sec;
    }
    if (changes & kChangeError) {
        failedPath_ = queue_.CurrentItem() ? queue_.CurrentItem()->path : std::wstring();
        failedError_ = s.lastError;
        if (failedPath_.empty()) ShowToast(main_.get(), Fmt(Tr(L"Can't play: {0}"), {s.lastError}), "info", 3000);
    }
    if ((changes & kChangeFile) && s.fileLoaded) failedPath_.clear();
    // Before CheckPlayerHold: the player it shows has its picture.
    if (openAt_ >= 0 && ((changes & kChangeError) || (s.loadSerial > openSerial_ && s.shownSerial == s.loadSerial))) {
        openAt_ = -1;
        if (main_) main_->RequestRender();  // the loading line goes
    }
    if ((changes & kChangeFile) && queue_.CurrentItem() && s.fileLoaded && s.loadSerial != recordedSerial_ &&
        config_.GetBool("privacy.saveHistory")) {
        recordedSerial_ = s.loadSerial;
        if (history_.RecordPlay(queue_.CurrentItem()->path, s.title, s.duration)) system_.HistoryChanged();
    }
    if ((changes & kChangeTime) && s.fileLoaded) {
        const double now = ui::AnimFloat::Now();
        if (now - lastHistorySave_ > 5000) {
            lastHistorySave_ = now;
            SaveHistoryPosition();
        }
    }
    // The queue keeps what the playing file says about itself, so its row stays the same after it.
    // Once a second in: right after a gapless move on, media-title can still be the previous file's.
    if ((changes & kChangeTime) && s.fileLoaded && s.position >= 1 && s.loadSerial != infoSerial_)
        if (const QueueItem* item = queue_.CurrentItem(); item && item->path == s.path) {
            infoSerial_ = s.loadSerial;
            // A folder queue's title is the file name; tags win (the library's reader first, as the
            // player screens show them), a playlist's own title stays.
            const std::wstring file = FileNameOf(item->path);
            const std::wstring& title = CurrentTags().title;
            const bool learnTitle = !title.empty() && title != file && !IsUrl(item->path) &&
                                    (item->title.empty() || item->title == file);
            queue_.SetInfo(queue_.Current(), learnTitle ? title : std::wstring(), s.duration);
        }
    if ((changes & kChangeFile) && s.fileLoaded) fitPending_ = true;
    if (changes & (kChangeVideoSize | kChangeFile)) FitIfPending();
    if (changes & kChangeVolume) config_.SetDouble("audio.lastVolume", std::round(s.volume));
    if (playerPending_ && main_) {
        main_->RequestLayout();  // the unseen player places the video window for the new file
        CheckPlayerHold();
    }
    playerChanged.Emit(changes);
    OnPlayerChangeForOsd(changes);
    system_.Update(changes);
    if (changes & kChangeEnd) OnPlaybackEnded();
}

void App::OnPlaybackEnded() {
    // "Stop after each item" leaves the last frame up (keep-open pauses there).
    if (config_.GetBool("playback.stopAfterEach") && queue_.RepeatMode() != Repeat::One) return;
    // mpv goes on to the queued item by itself (OnAdvanced).
    if (player_.HasQueuedNext()) return;
    if (queue_.NextIndex(false) >= 0) {
        Next(false);
        return;
    }
    // End of the queue.
    const std::string finished = config_.GetString("playback.whenFinished");
    const QueueItem* item = queue_.CurrentItem();
    if (finished == "next" && item && item->path.find(L"://") == std::wstring::npos) {
        // Continue with the file after this one in its folder.
        const auto siblings = ListSiblingMedia(item->path, FolderOrder(config_, FolderOf(item->path)));
        for (size_t i = 0; i + 1 < siblings.size(); ++i)
            if (_wcsicmp(siblings[i].c_str(), item->path.c_str()) == 0) {
                BuildFolderQueue(siblings[i + 1]);
                return;
            }
    } else if (finished == "close") {
        Quit();
    }
}

void App::ApplyWindowOptions() {
    if (!main_ || !main_->Hwnd()) return;
    const bool top = config_.GetBool("interface.alwaysOnTop");
    SetWindowPos(main_->Hwnd(), top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void App::FitWindowToVideo() {
    const std::string mode = config_.GetString("interface.autoResize");
    if (mode == "never" || (mode == "first" && fittedOnce_)) return;
    if (!main_ || !playerShown_ || main_->IsFullscreen() || main_->IsWindowMaximized() || main_->UiHidden()) return;
    const PlayerState& s = player_.State();
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromWindow(main_->Hwnd(), MONITOR_DEFAULTTONEAREST), &mi)) return;
    const RECT& work = mi.rcWork;
    const double scale = main_->Scale();
    // Chrome around the video: title bar, control bar and the 10 DIP side frame.
    const double chromeW = 20 * scale, chromeH = (40 + 56) * scale;
    const double maxW = (work.right - work.left) * 0.85 - chromeW, maxH = (work.bottom - work.top) * 0.85 - chromeH;
    const double f = std::min({1.0, maxW / static_cast<double>(s.videoW), maxH / static_cast<double>(s.videoH)});
    int w = static_cast<int>(std::lround(static_cast<double>(s.videoW) * f + chromeW));
    int h = static_cast<int>(std::lround(static_cast<double>(s.videoH) * f + chromeH));
    w = std::max(w, static_cast<int>(760 * scale));
    h = std::max(h, static_cast<int>(440 * scale));
    const SIZE band = main_->FrameBand();  // the window's invisible resize band around the client area
    w += 2 * band.cx;
    h += band.cy;
    RECT cur;
    GetWindowRect(main_->Hwnd(), &cur);
    int x = (cur.left + cur.right) / 2 - w / 2, y = (cur.top + cur.bottom) / 2 - h / 2;
    x = std::clamp(x, static_cast<int>(work.left), std::max<int>(work.left, work.right - w));
    y = std::clamp(y, static_cast<int>(work.top), std::max<int>(work.top, work.bottom - h));
    if (IsRectEmpty(&preFitRect_)) preFitRect_ = cur;
    main_->ChangeSizeAhead(main_->ClientSizeFor(RECT{x, y, x + w, y + h}),
                           [&] { SetWindowPos(main_->Hwnd(), nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE); });
    fittedOnce_ = true;
}

void App::FitIfPending() {
    const PlayerState& s = player_.State();
    // Held back while the player waits for its first frame: ShowPlayer fits in the same step.
    if (!fitPending_ || playerPending_ || s.videoW <= 0 || s.videoH <= 0) return;
    fitPending_ = false;
    FitWindowToVideo();
    if (config_.GetBool("video.startFullscreen") && s.hasVideo && main_ && !main_->IsFullscreen() &&
        !main_->PictureInPicture() && playerShown_)
        main_->SetFullscreen(true, config_.GetString("interface.fullscreenScreen") == "primary");
}

namespace {
// The longest the screen waits for a file's first frame before it shows the player anyway.
constexpr UINT kPlayerHoldMs = 700;
// A wait shorter than this reads as instant; a longer one shows the clicked item pressed.
constexpr UINT kSlowOpenMs = 150;
}  // namespace

void App::HoldPlayer() {
    holdSerial_ = player_.State().loadSerial;  // Open was just sent: this is the previous file's
    if (playerPending_ || !main_) return;
    playerPending_ = true;
    holdStart_ = ui::AnimFloat::Now();
    holdTimer_ = main_->StartTimer(kPlayerHoldMs, [this] {
        holdTimer_ = 0;
        ShowPlayer(true);
    });
    main_->StartTimer(kSlowOpenMs, [this] { main_->RequestRender(); });
    main_->RequestLayout();
}

std::wstring App::SlowOpeningPath() const {
    const QueueItem* item = queue_.CurrentItem();
    if (!playerPending_ || !item || ui::AnimFloat::Now() - holdStart_ < kSlowOpenMs - 1) return {};
    return item->path;
}

void App::CheckPlayerHold() {
    if (!playerPending_ || !main_) return;
    const PlayerState& s = player_.State();
    const QueueItem* item = queue_.CurrentItem();
    const bool failed = item && !failedPath_.empty() && failedPath_ == item->path;  // its error card shows
    const bool playing = s.fileLoaded && s.loadSerial > holdSerial_ && s.shownSerial == s.loadSerial;
    if (failed || (playing && (s.hasVideo || MusicCoverReady())))
        main_->Post([this] {
            if (playerPending_) ShowPlayer(true);
        });
}

bool App::FirstScreenReady() const {
    if (!started_) return false;
    // A file given at launch: the player's frame at once; its picture and cover fade in themselves.
    if (playerShown_) return true;
    if (OpensFileAtStart() || images_.Busy()) return false;
    // The library is scanned at every start: Home waits for its first results instead of
    // showing "Scanning your library" (or nothing) and then the cards.
    return libraryShown_ || library_.Roots().empty();
}

bool App::HasMedia() const {
    return player_.State().fileLoaded;
}

namespace {

// Subtitle files dropped onto a playing video are added to it.
bool AddedAsSubtitles(const std::vector<std::wstring>& paths) {
    App& app = TheApp();
    if (!app.player().State().fileLoaded) return false;
    for (const auto& p : paths) {
        const wchar_t* ext = PathFindExtensionW(p.c_str());
        bool sub = false;
        for (const wchar_t* e : {L".srt", L".ass", L".ssa", L".vtt", L".sub", L".sup", L".idx", L".smi"})
            if (_wcsicmp(ext, e) == 0) sub = true;
        if (!sub) return false;
    }
    for (const auto& p : paths) app.player().AddSubtitle(p);
    ShowToast(app.main(), Tr(L"Subtitles added"), "subtitles");
    return true;
}

// Missing files: say so instead of playing something else from that folder.
std::vector<std::wstring> ExistingOnly(const std::vector<std::wstring>& paths) {
    std::vector<std::wstring> existing;
    std::wstring missing;
    for (const auto& p : paths) {
        if (IsUrl(p) || GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            existing.push_back(p);
        else if (missing.empty())
            missing = p;
    }
    if (!missing.empty())
        ShowToast(TheApp().main(), Fmt(Tr(L"File not found: {0}"), {FileNameOf(missing)}), "info", 4000);
    return existing;
}

// The entries of an .m3u as queue items; false (with a toast) when it can't be read.
bool ReadPlaylistItems(const std::wstring& path, std::vector<QueueItem>* out) {
    std::vector<library::PlaylistItem> entries;
    if (!library::ReadM3u(path, &entries) || entries.empty()) {
        ShowToast(TheApp().main(), Fmt(Tr(L"Couldn't read the playlist {0}"), {FileNameOf(path)}), "info", 4000);
        return false;
    }
    for (auto& e : entries) out->push_back({std::move(e.path), std::move(e.title), e.duration});
    return true;
}

}  // namespace

void App::Enqueue(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    if (AddedAsSubtitles(paths)) return;
    std::vector<QueueItem> items;
    for (const auto& p : ExistingOnly(paths)) {
        if (!IsUrl(p) && IsDirectory(p)) {
            for (const auto& f : MediaInFolder(p, config_.GetString("library.openFolder") != "folder",
                                               FolderOrder(config_, p)))
                items.push_back(ItemFor(f));
        } else if (!IsUrl(p) && IsPlaylistFile(p)) {
            ReadPlaylistItems(p, &items);  // its tracks, not the .m3u as one line
        } else {
            items.push_back(ItemFor(p));
        }
    }
    if (items.empty()) return;
    const long long added = static_cast<long long>(items.size());
    const int first = static_cast<int>(queue_.Items().size());
    queue_.Add(std::move(items));
    // Nothing plays yet: the first added item starts, as with the queue's own "+" button.
    if (!player_.State().fileLoaded && pendingOpen_.empty() && pendingIndex_ < 0) {
        PlayIndex(first, false);
        return;
    }
    ShowToast(main_.get(), Fmt(Tr(L"Added to the queue: {0}"), {std::to_wstring(added)}), "list");
}

void App::BuildFolderQueue(const std::wstring& file) {
    if (!config_.GetBool("playback.folderAsPlaylist")) {
        PlayQueueItems({ItemFor(file)}, 0);
        return;
    }
    std::vector<std::wstring> siblings = ListSiblingMedia(file, FolderOrder(config_, FolderOf(file)));
    // Same kind only: a song doesn't pull the folder's videos into the queue (nor a film its .ac3).
    const bool audio = library::KindOf(file) == library::MediaKind::Audio;
    std::vector<QueueItem> items;
    int index = -1;
    for (const auto& s : siblings) {
        if ((library::KindOf(s) == library::MediaKind::Audio) != audio) continue;
        if (_wcsicmp(s.c_str(), file.c_str()) == 0) index = static_cast<int>(items.size());
        items.push_back(ItemFor(s));
    }
    // A file the sibling scan doesn't list (an extension IsMediaFile doesn't know: .mka, .ape,
    // .aiff, .pls) plays alone; its folder's first file must not play in its place.
    if (index < 0) {
        items = {ItemFor(file)};
        index = 0;
    }
    const size_t slash = file.find_last_of(L"\\/");
    PlayQueueItems(std::move(items), index, slash == std::wstring::npos ? L"" : FileNameOf(file.substr(0, slash)));
}

void App::Open(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    if (!engineReady_ && !main_) {
        pendingOpen_ = paths;
        pendingIndex_ = -1;  // the newer request wins
        return;
    }
    if (AddedAsSubtitles(paths)) return;
    const std::vector<std::wstring> existing = ExistingOnly(paths);
    if (existing.empty()) return;
    if (existing.size() != paths.size()) {
        Open(existing);
        return;
    }
    // Playlist files become the queue.
    if (paths.size() == 1 && IsPlaylistFile(paths[0])) {
        std::vector<QueueItem> items;
        if (!ReadPlaylistItems(paths[0], &items)) return;
        std::wstring name = FileNameOf(paths[0]);
        if (const size_t dot = name.find_last_of(L'.'); dot != std::wstring::npos) name.resize(dot);
        PlayQueueItems(std::move(items), 0, std::move(name));
        return;
    }
    if (paths.size() == 1 && !IsUrl(paths[0])) {
        if (IsDirectory(paths[0])) {
            std::vector<QueueItem> items;
            for (const auto& f : MediaInFolder(paths[0], config_.GetString("library.openFolder") != "folder",
                                               FolderOrder(config_, paths[0])))
                items.push_back(ItemFor(f));
            if (items.empty()) {
                ShowToast(main_.get(), Tr(L"No media files in this folder"), "folder");
                return;
            }
            PlayQueueItems(std::move(items), 0, FileNameOf(paths[0]));
            return;
        }
        BuildFolderQueue(paths[0]);
        return;
    }
    std::vector<QueueItem> items;
    for (const auto& p : paths) {
        if (!IsUrl(p) && IsDirectory(p))
            for (const auto& f : MediaInFolder(p, config_.GetString("library.openFolder") != "folder",
                                               FolderOrder(config_, p)))
                items.push_back(ItemFor(f));
        else
            items.push_back(ItemFor(p));
    }
    PlayQueueItems(std::move(items), 0);
}

void App::PlayQueueItems(std::vector<QueueItem> items, int index, std::wstring name, bool showPlayer) {
    if (items.empty()) return;
    queue_.Set(std::move(items), index, std::move(name));
    PlayIndex(queue_.Current(), showPlayer);
}

void App::PlayIndex(int index, bool showPlayer) {
    settingCurrent_ = true;
    queue_.SetCurrent(index);
    settingCurrent_ = false;
    const QueueItem* item = queue_.CurrentItem();
    if (!item) return;
    // The wait runs from the launch for a file given at launch; an open still in flight keeps its
    // loading line.
    if (openAt_ < 0) openAt_ = launchAt_ >= 0 ? launchAt_ : ui::AnimFloat::Now();
    openSerial_ = player_.State().loadSerial;
    if (main_) {
        const double left = kLoadingLineMs - (ui::AnimFloat::Now() - openAt_);
        main_->StartTimer(static_cast<UINT>(std::max(1.0, left)), [this] { main_->RequestRender(); });
    }
    if (!engineReady_) {
        // Keep the queue (a playlist, a folder) as built: reopening the path would rebuild it
        // from the file's folder.
        pendingOpen_.clear();
        pendingIndex_ = queue_.Current();
        pendingShowPlayer_ = showPlayer;
        // At launch the player's frame comes at once, while mpv starts.
        if (showPlayer && !playerShown_ && main_ && !main_->ContentShown()) ShowPlayer(true);
        return;
    }
    // A jump the user asked for: "replace" (it also drops the queued next file), then queue anew.
    player_.Open(item->path);
    SyncNextInPlayer(true);
    // "Start playing as soon as a file opens"; the next item of a running queue always plays.
    player_.SetPause(!config_.GetBool("playback.autoPlay") && !player_.State().fileLoaded);
    if (showPlayer && !playerShown_) {
        // At launch there is no screen to keep: the player's frame comes at once (its picture
        // fades in when it is up). Later the current screen stays until the first frame.
        if (main_ && main_->ContentShown())
            HoldPlayer();
        else
            ShowPlayer(true);
    }
}

void App::SyncNextInPlayer(bool force) {
    if (!engineReady_) return;
    // Nothing plays: an entry left in mpv's playlist would wait there for no reason.
    if (!force && player_.State().idle) return;
    // Repeat One: no entry, OnPlaybackEnded seeks back instead.
    const int i = config_.GetBool("playback.stopAfterEach") || Live().stopAfter ? -1 : queue_.GaplessNextIndex();
    queuedIndex_ = i;
    player_.QueueNext(i >= 0 ? queue_.Items()[static_cast<size_t>(i)].path : std::wstring());
}

void App::RemoveFromQueue(int index) {
    if (index < 0 || index >= static_cast<int>(queue_.Items().size())) return;
    if (index != queue_.Current() || player_.State().idle) {
        queue_.Remove(index);
        return;
    }
    const bool hasNext = queue_.Items().size() > 1 && queue_.NextIndex(true) >= 0 && queue_.NextIndex(true) != index;
    settingCurrent_ = true;
    queue_.Remove(index);
    settingCurrent_ = false;
    if (hasNext)
        PlayIndex(queue_.Current(), false);
    else
        StopPlayback();
}

void App::OnAdvanced() {
    // The item that just ended gets what Next() saves for it: a finished position, no resume point.
    if (const QueueItem* prev = queue_.CurrentItem()) {
        player_.ForgetPosition(prev->path);
        const double d = player_.AdvancedFromDuration();
        if (d > 0 && config_.GetBool("privacy.saveHistory") && config_.GetBool("privacy.rememberPosition"))
            history_.UpdatePosition(prev->path, d, d);
    }
    lastRestart_ = 0;
    // By path: the queue may have been edited between mpv taking the entry and this event.
    if (const int i = queue_.IndexOfStarted(player_.AdvancedPath(), queuedIndex_); i >= 0) {
        settingCurrent_ = true;
        queue_.SetCurrent(i);
        settingCurrent_ = false;
    }
    queuedIndex_ = -1;
    SyncNextInPlayer(true);
}

bool App::Next(bool userAction) {
    SaveHistoryPosition();
    const int i = queue_.NextIndex(userAction);
    if (i < 0) return false;
    if (i == queue_.Current() && !userAction) {
        player_.SeekAbsolute(0, true);
        player_.SetPause(false);
        return true;
    }
    PlayIndex(i, false);
    return true;
}

void App::Previous() {
    const double now = ui::AnimFloat::Now();
    const PlayerState& ps = player_.State();
    if (ps.fileLoaded && ps.position > 3 && !ps.eof && now - lastRestart_ > 3000) {
        player_.SeekAbsolute(0, true);
        lastRestart_ = now;
        return;
    }
    lastRestart_ = 0;
    const int i = queue_.PrevIndex();
    if (i >= 0) PlayIndex(i, false);
}

void App::StopPlayback() {
    failedPath_.clear();
    openAt_ = -1;
    SaveHistoryPosition();
    player_.Stop();
    if (main_ && main_->IsFullscreen()) main_->SetFullscreen(false);
    ShowPlayer(false);
}

void App::OpenFileDialog() {
    auto files = FileDialog(main_->Hwnd(), false, true, false);
    if (!files.empty()) Open(files);
}

void App::OpenFolderDialog() {
    auto folders = FileDialog(main_->Hwnd(), true, false, false);
    if (!folders.empty()) Open(folders);
}

void App::OpenSubtitleDialog() {
    if (!player_.State().fileLoaded) return;
    for (const auto& f : FileDialog(main_->Hwnd(), false, true, true)) player_.AddSubtitle(f);
}

void App::OpenUrl(const std::wstring& url) {
    if (url.empty())
        ShowNetworkStreamDialog();
    else
        Open({url});
}

void App::Navigate(Route r) {
    if (r == route()) {
        routeChanged.Emit();
        return;
    }
    routeLeaving.Emit();
    nav_.resize(static_cast<size_t>(historyPos_) + 1);
    nav_.push_back(NavEntry{std::move(r), {}});
    if (nav_.size() > 100) nav_.erase(nav_.begin());
    historyPos_ = static_cast<int>(nav_.size()) - 1;
    routeChanged.Emit();
}

// Back and Forward step over the pages of playlists deleted since: they would only say
// "Playlist not found" (UX-9). The last page of the history is kept even then.
bool App::DeadRoute(const Route& r) const {
    return r.section == Section::Playlists && !r.path.empty() && !playlists_.Find(WideToUtf8(r.path));
}

void App::GoBack() {
    if (!CanGoBack()) return;
    routeLeaving.Emit();
    --historyPos_;
    while (historyPos_ > 0 && DeadRoute(route())) --historyPos_;
    routeChanged.Emit();
}

void App::GoForward() {
    if (!CanGoForward()) return;
    routeLeaving.Emit();
    ++historyPos_;
    while (CanGoForward() && DeadRoute(route())) ++historyPos_;
    routeChanged.Emit();
}

void App::ShowPlayer(bool show) {
    const bool held = playerPending_;
    if (held) {
        playerPending_ = false;
        if (holdTimer_ && main_) main_->CancelTimer(holdTimer_);
        holdTimer_ = 0;
    }
    if (show == playerShown_) {
        if (held) {
            if (main_) main_->RequestLayout();  // the video window the hold placed goes
            screenChanged.Emit();               // pages and the title kept still for the hold catch up
            ReleaseHeldOsd(show);
        }
        return;
    }
    playerShown_ = show;
    if (!show && main_ && main_->IsFullscreen()) main_->SetFullscreen(false);
    // Space and the arrows belong to the player once it is shown.
    if (show && main_) main_->SetFocus(nullptr);
    // Back in the library: undo the video-sized window.
    if (!show && main_ && !IsRectEmpty(&preFitRect_)) {
        if (!main_->IsWindowMaximized() && !IsIconic(main_->Hwnd())) {
            const RECT r = preFitRect_;
            main_->ChangeSizeAhead(main_->ClientSizeFor(r), [&] {
                SetWindowPos(main_->Hwnd(), nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            });
        }
        preFitRect_ = {};
    }
    if (main_) main_->UpdateScreen();
    if (show && main_ && main_->playerView()) main_->playerView()->CoverPicture(openAt_ >= 0);
    screenChanged.Emit();
    if (held && show) FitIfPending();
    if (held) ReleaseHeldOsd(show);
}

void App::OpenSettings(const std::string& category) {
    ShowSettingsWindow(category);
}

void App::ShowOnboarding() {
    app::ShowOnboarding({});
}

void App::BeginQuit() {
    // The close hides the window first; the final FlushStores skips a hidden one.
    SaveWindowPlacement();
    quitting_ = true;
    player_.Detach();
}

void App::EndSession() {
    if (!main_) return;
    // The usual close (hide, mpv shut down on another thread), unless it already runs.
    if (!quitting_) SendMessageW(main_->Hwnd(), WM_CLOSE, 0, 0);
    FlushStores();
    if (engineReady_) Live().FlushFileSettings();
    Trace("close: session end, stores saved");
    // mpv writes watch-later on the shutdown thread. Its video window is a child of ours, so this
    // thread keeps answering sent messages meanwhile; posted ones wait.
    const ULONGLONG until = GetTickCount64() + 3000;
    while (!player_.Stopped() && GetTickCount64() < until)
        if (MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_SENDMESSAGE) == WAIT_OBJECT_0) {
            MSG m;
            PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
        }
    Trace(player_.Stopped() ? "close: session end, player stopped" : "close: session end, player still stopping");
}

void App::Quit() {
    if (main_) main_->Close();
}

void App::OnCopyData(const std::wstring& payload) {
    if (quitting_) return;
    std::vector<std::wstring> files;
    size_t start = 0;
    while (start < payload.size()) {
        size_t end = payload.find(L'\n', start);
        if (end == std::wstring::npos) end = payload.size();
        if (end > start) files.push_back(payload.substr(start, end - start));
        start = end + 1;
    }
    // Explorer's "Add to the Moonbow queue". With nothing playing yet the files open as below.
    const bool enqueue = std::erase(files, L"--enqueue") > 0;
    if (files.empty()) return;
    if ((enqueue || config_.GetBool("library.enqueue")) && player_.State().fileLoaded) {
        Enqueue(files);
        return;
    }
    // Explorer starts one Moonbow per file when several are opened together, and each hands its
    // file over moments after the others: together they are one queue, as if opened at once.
    const double now = ui::AnimFloat::Now();
    const bool together = now - forwardedAt_ < 1000 && !forwarded_.empty();
    forwardedAt_ = now;
    if (!together) {
        forwarded_ = files;
        Open(files);
        return;
    }
    forwarded_.insert(forwarded_.end(), files.begin(), files.end());
    // The first one plays already: the queue grows around it instead of opening it again.
    const QueueItem* current = queue_.CurrentItem();
    const bool plain = std::all_of(forwarded_.begin(), forwarded_.end(), [](const std::wstring& p) {
        return !IsUrl(p) && !IsPlaylistFile(p) && library::IsMediaPath(p) && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    });
    if (engineReady_ && plain && current && current->path == forwarded_[0]) {
        std::vector<QueueItem> items;
        for (const auto& p : forwarded_) items.push_back(ItemFor(p));
        queue_.Set(std::move(items), 0);
        return;
    }
    Open(forwarded_);
}

void App::OnMainDestroyed() {
    PostQuitMessage(0);
}

// --- automation commands ---------------------------------------------------------------------

namespace {

// First visible widget under `w` (depth first) that matches; hidden subtrees are skipped.
Widget* FindVisible(Widget* w, const std::function<bool(Widget*)>& match) {
    if (!w || !w->visible) return nullptr;
    if (match(w)) return w;
    for (auto& c : w->Children())
        if (Widget* hit = FindVisible(c.get(), match)) return hit;
    return nullptr;
}

bool TypeContains(const Widget* w, const char* part) {
    return strstr(typeid(*w).name(), part) != nullptr;
}

std::string IconOf(Widget* w) {
    if (auto* b = dynamic_cast<IconButton*>(w)) return b->icon;
    if (auto* v = dynamic_cast<IconView*>(w)) return v->icon;
    if (auto* b = dynamic_cast<Button*>(w)) return b->icon;
    return {};
}

bool HasIcon(Widget* w, const char* icon) {
    return dynamic_cast<IconButton*>(w) && IconOf(w) == icon;
}

// "#id", "icon=<name>", "type=<part of the C++ type name>" or "tip=<tooltip>".
std::function<bool(Widget*)> Selector(const std::string& s) {
    if (s.size() > 1 && s[0] == '#') return [id = s.substr(1)](Widget* w) { return w->id == id; };
    if (s.rfind("icon=", 0) == 0) return [icon = s.substr(5)](Widget* w) { return IconOf(w) == icon; };
    if (s.rfind("type=", 0) == 0) return [part = s.substr(5)](Widget* w) { return TypeContains(w, part.c_str()); };
    if (s.rfind("tip=", 0) == 0) return [tip = Utf8ToWide(s.substr(4))](Widget* w) { return w->tooltip == tip; };
    return nullptr;
}

Widget* FindInHost(Host* host, const std::function<bool(Widget*)>& match) {
    // Topmost popup first: that is what a click would reach.
    for (size_t i = host->PopupCount(); i-- > 0;)
        if (Widget* hit = FindVisible(host->PopupAt(i), match)) return hit;
    return FindVisible(host->Root(), match);
}

bool ParseSection(const std::string& name, Section* out) {
    static const std::pair<const char*, Section> kSections[] = {
        {"home", Section::Home},           {"videos", Section::Videos},     {"music", Section::Music},
        {"network", Section::Network},     {"playlists", Section::Playlists}, {"recent", Section::Recent},
    };
    for (const auto& [n, s] : kSections)
        if (name == n) {
            *out = s;
            return true;
        }
    return false;
}

bool ParsePanel(const std::string& name, PanelKind* out) {
    static const std::pair<const char*, PanelKind> kPanels[] = {
        {"tracks", PanelKind::Tracks},     {"audio", PanelKind::Tracks},       {"subtitles", PanelKind::Subtitles},
        {"settings", PanelKind::Settings}, {"queue", PanelKind::Queue},        {"chapters", PanelKind::Chapters},
        {"bookmarks", PanelKind::Bookmarks}, {"speed", PanelKind::Speed},
    };
    for (const auto& [n, k] : kPanels)
        if (name == n) {
            *out = k;
            return true;
        }
    return false;
}

}  // namespace

bool App::AutomationCommand(const std::wstring& command, std::string* reply) {
    std::string& r = *reply;
    MainWindow* w = main_.get();
    if (!w || !Host::Automation()) {
        r = "automation off";
        return false;
    }
    const std::string text = WideToUtf8(command);
    const size_t sp = text.find(' ');
    const std::string verb = text.substr(0, sp);
    std::string arg = sp == std::string::npos ? std::string() : text.substr(sp + 1);
    while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\r' || arg.back() == '\n')) arg.pop_back();
    while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
    const std::wstring warg = Utf8ToWide(arg);

    if (verb == "action") {
        for (const ActionInfo& info : AllActions()) {
            if (arg != info.id || info.action == Action::None) continue;
            if (!Enabled(info.action)) {
                r = "disabled: " + arg;
                return false;
            }
            app::Run(info.action);
            r = "ran " + arg;
            return true;
        }
        r = "unknown action: " + arg;
        return false;
    }

    if (verb == "nav") {
        const size_t s2 = arg.find(' ');
        const std::string where = arg.substr(0, s2);
        const std::wstring path = s2 == std::string::npos ? std::wstring() : Utf8ToWide(arg.substr(s2 + 1));
        if (where == "settings") {
            const std::string page = WideToUtf8(path);
            if (!page.empty() && !settings::FindCategory(page)) {
                r = "unknown settings page: " + page;
                return false;
            }
            OpenSettings(page);
            r = "settings " + (page.empty() ? std::string("(last page)") : page);
            return true;
        }
        if (where == "player" || where == "library") {
            ShowPlayer(where == "player");
            r = where;
            return true;
        }
        if (where == "back" || where == "forward") {
            if (where == "back" ? !CanGoBack() : !CanGoForward()) {
                r = "no " + where + " history";
                return false;
            }
            ShowPlayer(false);
            where == "back" ? GoBack() : GoForward();
            r = where;
            return true;
        }
        Section section;
        if (!ParseSection(where, &section)) {
            r = "unknown route: " + where;
            return false;
        }
        ShowPlayer(false);
        Navigate(Route{section, path});
        r = "route " + arg;
        return true;
    }

    if (verb == "panel") {
        if (arg == "close") {
            ClosePlayerPanels();
            r = "panels closed";
            return true;
        }
        PanelKind kind;
        if (!ParsePanel(arg, &kind)) {
            r = "unknown panel: " + arg;
            return false;
        }
        if (!playerShown_) ShowPlayer(true);
        // The second toggle covers the "this click just closed it" guard (Panels.cpp).
        if (!PlayerPanelOpen(kind)) TogglePlayerPanel(kind, {});
        if (!PlayerPanelOpen(kind)) TogglePlayerPanel(kind, {});
        if (!PlayerPanelOpen(kind)) {
            r = "panel did not open: " + arg;
            return false;
        }
        r = "panel " + arg;
        return true;
    }

    if (verb == "drop" && main_ && main_->onDropFiles) {  // as if dropped on the window
        main_->onDropFiles({warg});
        r = "drop " + arg;
        return true;
    }

    if (verb == "open" || verb == "queue") {
        if (warg.empty() || (!IsUrl(warg) && GetFileAttributesW(warg.c_str()) == INVALID_FILE_ATTRIBUTES)) {
            r = "not found: " + arg;
            return false;
        }
        if (verb == "open") {
            Open({warg});
        } else {
            queue_.Add({ItemFor(warg)});
        }
        r = verb + " " + arg;
        return true;
    }

    if (verb == "set") {
        const size_t s2 = arg.find(' ');
        const std::string key = arg.substr(0, s2);
        const std::string value = s2 == std::string::npos ? std::string() : arg.substr(s2 + 1);
        if (!settings::FindItem(key)) {
            r = "unknown setting: " + key;
            return false;
        }
        // JSON first (true, 42, "text"); anything that doesn't parse or fit is tried as a string.
        bool ok = false;
        if (auto parsed = json::Parse(value)) ok = config_.Set(key, *parsed);
        if (!ok) ok = config_.Set(key, json::Value(value));
        r = ok ? "set " + key : "rejected value for " + key + ": " + value;
        return ok;
    }

    if (verb == "menu") {
        Widget* target = nullptr;
        bool context = false;  // open with onContextMenu instead of a click
        if (arg == "close") {
            w->CloseAllPopups();
            r = "menus closed";
            return true;
        } else if (arg == "app") {
            target = FindVisible(w->titleBar(), [](Widget* x) { return TypeContains(x, "AppMenuButton"); });
        } else if (arg == "more" || arg == "sort") {
            target = FindVisible(w->library(), [&](Widget* x) { return HasIcon(x, arg.c_str()); });
        } else if (arg == "add") {
            target = FindVisible(w->sidebar(), [](Widget* x) { return TypeContains(x, "AddItem"); });
        } else if (arg == "player") {
            target = FindVisible(w->Root(), [](Widget* x) { return TypeContains(x, "VideoArea") && x->onContextMenu; });
            context = true;
        } else if (arg == "pip") {
            if (!w->PictureInPicture()) {
                r = "not in picture in picture";
                return false;
            }
            const Size c = w->ClientSize();
            ShowPipContextMenu(w, {c.w / 2, c.h / 2});
            r = "menu pip";
            return true;
        } else {
            r = "unknown menu: " + arg;
            return false;
        }
        if (!target || !target->IsEnabled()) {
            r = "menu not available here: " + arg;
            return false;
        }
        w->CloseAllPopups();
        if (context) {
            MouseEvent e{target->rect.Cx(), target->rect.Cy(), MouseButton::Right, {}};
            target->onContextMenu(e);
        } else {
            target->OnClick();
        }
        r = w->HasPopup() ? "menu " + arg : "menu did not open: " + arg;
        return w->HasPopup();
    }

    if (verb == "click") {
        auto match = Selector(arg);
        if (!match) {
            r = "bad selector (#id, icon=, type=, tip=): " + arg;
            return false;
        }
        Widget* target = FindInHost(w, match);
        while (target && !target->interactive) target = target->Parent();
        if (!target || !target->IsEnabled()) {
            r = "no enabled interactive widget for " + arg;
            return false;
        }
        // The name first: a click that closes its own dialog destroys the widget.
        r = std::string("clicked ") + typeid(*target).name();
        target->OnClick();
        return true;
    }

    if (verb == "wait-idle") {
        std::string busy;
        for (Host* h : Host::All())
            if (h->Hwnd() && IsWindowVisible(h->Hwnd()) && !h->Idle()) busy += h->IsMainWindow() ? " main-window" : " window";
        if (!engineReady_ || !pendingOpen_.empty()) busy += " player-starting";
        const PlayerState& s = player_.State();
        if (engineReady_ && !s.idle && !s.fileLoaded) busy += " file-loading";
        if (library_.Scanning()) busy += " library-scan";
        if (const size_t n = thumbnails_.PendingCount()) busy += " thumbnails=" + std::to_string(n);
        r = busy.empty() ? "idle" : "busy:" + busy;
        return busy.empty();
    }

    if (verb == "dump") {
        if (arg == "all") {
            for (Host* h : Host::All())
                if (h->Hwnd() && IsWindowVisible(h->Hwnd())) r += WideToUtf8(h->WriteTreeDump()) + "\n";
        } else {
            r = WideToUtf8(w->WriteTreeDump());
        }
        return true;
    }

    if (verb == "state") {
        static const char* const kSectionNames[] = {"home", "videos", "music", "network", "playlists", "recent"};
        const Route& rt = route();
        const PlayerState& s = player_.State();
        r = std::string("route=") + kSectionNames[static_cast<int>(rt.section)] +
            (rt.path.empty() ? "" : " " + WideToUtf8(rt.path)) + "\nplayer=" + (playerShown_ ? "1" : "0") +
            "\nfile=" + (s.fileLoaded ? "1" : "0") + "\npaused=" + (s.paused ? "1" : "0") +
            "\nqueue=" + std::to_string(queue_.Items().size()) + "\ncurrent=" + std::to_string(queue_.Current()) +
            "\nfullscreen=" + (w->IsFullscreen() ? "1" : "0") + "\npip=" + (w->PictureInPicture() ? "1" : "0") +
            "\npopups=" + std::to_string(w->PopupCount()) + "\n";
        return true;
    }

    if (verb == "prop") {
        // Read-only: tests check mpv's own view (hwdec-current, vf, frame-drop-count...).
        if (!engineReady_ || arg.empty()) {
            r = engineReady_ ? "usage: prop <mpv property>" : "player not ready";
            return false;
        }
        r = arg + "=" + player_.GetPropertyString(arg.c_str());
        return true;
    }

    r = "unknown command: " + verb;
    return false;
}

}  // namespace app
