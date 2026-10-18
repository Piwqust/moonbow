#include "app/App.h"
#include "ui/Host.h"

#include "Trace.h"
#include "Util.h"
#include "app/Actions.h"
#include "app/Dialogs.h"
#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/SeekPreview.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "ui/Theme.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/settings/ShortcutStore.h"
#include "gfx/Gpu.h"
#include "ui/Controls.h"

#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <sstream>

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
        case Section::Recent: return L"Recent";
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

// --- PlayQueue ------------------------------------------------------------------------

const QueueItem* PlayQueue::CurrentItem() const {
    return current_ >= 0 && current_ < static_cast<int>(items_.size()) ? &items_[static_cast<size_t>(current_)] : nullptr;
}

void PlayQueue::Set(std::vector<QueueItem> items, int current, std::wstring name) {
    items_ = std::move(items);
    current_ = items_.empty() ? -1 : std::clamp(current, 0, static_cast<int>(items_.size()) - 1);
    name_ = std::move(name);
    Reshuffle();
    changed.Emit();
}

void PlayQueue::Add(std::vector<QueueItem> items) {
    for (auto& i : items) items_.push_back(std::move(i));
    if (current_ < 0 && !items_.empty()) current_ = 0;
    Reshuffle();
    changed.Emit();
}

void PlayQueue::InsertNext(std::vector<QueueItem> items) {
    const size_t at = current_ < 0 ? items_.size() : static_cast<size_t>(current_) + 1;
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(at), items.begin(), items.end());
    if (current_ < 0 && !items_.empty()) current_ = 0;
    Reshuffle();
    changed.Emit();
}

void PlayQueue::Remove(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    items_.erase(items_.begin() + index);
    if (index < current_) --current_;
    if (current_ >= static_cast<int>(items_.size())) current_ = static_cast<int>(items_.size()) - 1;
    Reshuffle();
    changed.Emit();
}

void PlayQueue::Move(int from, int to) {
    const int n = static_cast<int>(items_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    QueueItem item = std::move(items_[static_cast<size_t>(from)]);
    items_.erase(items_.begin() + from);
    items_.insert(items_.begin() + to, std::move(item));
    if (current_ == from)
        current_ = to;
    else if (from < current_ && to >= current_)
        --current_;
    else if (from > current_ && to <= current_)
        ++current_;
    Reshuffle();
    changed.Emit();
}

void PlayQueue::Clear() {
    items_.clear();
    order_.clear();
    current_ = -1;
    changed.Emit();
}

void PlayQueue::SetCurrent(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    current_ = index;
    changed.Emit();
}

void PlayQueue::SetShuffle(bool on) {
    shuffle_ = on;
    Reshuffle();
    changed.Emit();
}

void PlayQueue::SetRepeat(Repeat r) {
    repeat_ = r;
    changed.Emit();
}

void PlayQueue::Reshuffle() {
    order_.resize(items_.size());
    std::iota(order_.begin(), order_.end(), 0);
    if (!shuffle_ || order_.size() < 2) return;
    static std::mt19937 rng{std::random_device{}()};
    std::shuffle(order_.begin(), order_.end(), rng);
    // The current item stays first so "next" walks the rest.
    auto it = std::find(order_.begin(), order_.end(), current_);
    if (it != order_.end()) std::iter_swap(order_.begin(), it);
}

int PlayQueue::NextIndex(bool userAction) const {
    if (items_.empty()) return -1;
    if (repeat_ == Repeat::One && !userAction) return current_;
    if (!shuffle_) {
        if (current_ + 1 < static_cast<int>(items_.size())) return current_ + 1;
        return repeat_ == Repeat::All ? 0 : -1;
    }
    auto it = std::find(order_.begin(), order_.end(), current_);
    if (it == order_.end() || it + 1 == order_.end()) return repeat_ == Repeat::All ? order_.front() : -1;
    return *(it + 1);
}

int PlayQueue::PrevIndex() const {
    if (items_.empty()) return -1;
    if (!shuffle_) return current_ > 0 ? current_ - 1 : (repeat_ == Repeat::All ? static_cast<int>(items_.size()) - 1 : -1);
    auto it = std::find(order_.begin(), order_.end(), current_);
    if (it == order_.end() || it == order_.begin()) return -1;
    return *(it - 1);
}

double PlayQueue::TotalDuration() const {
    double sum = 0;
    for (const QueueItem& i : items_) sum += i.duration;
    return sum;
}

// --- App -------------------------------------------------------------------------------

namespace {

constexpr UINT WM_APP_SHUTDOWN_DONE = WM_APP + 70;

bool IsUrl(const std::wstring& s) {
    return s.find(L"://") != std::wstring::npos;
}

bool IsDirectory(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<std::wstring> MediaInFolder(const std::wstring& dir, bool recursive) {
    std::wstring probe = dir;
    if (!probe.empty() && probe.back() != L'\\') probe += L'\\';
    std::vector<std::wstring> out = ListSiblingMedia(probe + L"x");  // lists the folder of a path
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
        auto more = MediaInFolder(sub, true);
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
        languageChanged.Emit();
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
    // Settings mapped to mpv apply live as properties.
    for (const auto& [name, value] : MpvOptionFor(config_, key)) player_.SetProperty(name.c_str(), value);
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
    char buf[96];
    snprintf(buf, sizeof(buf), "%ld,%ld,%ld,%ld,%d", r.left, r.top, r.right, r.bottom, wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
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
}

void App::FlushStores() {
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
    if (!s.fileLoaded || !item || s.duration <= 0) return;
    if (!config_.GetBool("privacy.saveHistory") || !config_.GetBool("privacy.rememberPosition")) return;
    history_.UpdatePosition(item->path, s.position, s.duration);
}

void App::OnLibraryScanned() {
    libraryChanged.Emit();
}

void App::OnThumbnails() {
    images_.OnCompleted();
}

int App::Run(HINSTANCE instance, std::vector<std::wstring> args) {
    instance_ = instance;
    for (auto& a : args)
        if (!a.empty() && a[0] != L'-') pendingOpen_.push_back(a);

    config_.Load();
    LoadKeyBindings();
    SetLanguage(config_.GetString("general.language") == "en" ? Language::English : Language::Russian);
    ApplyTheme();
    LoadUiState();
    SetControlLayout(ControlLayout::Parse(UiValue("controls.layout", ControlLayout::Default().Serialize())));
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
    RestoreWindowPlacement();
    if (!IsWindowVisible(main_->Hwnd())) main_->Show();
    ApplyWindowOptions();
    system_.Attach(main_->Hwnd());
    Trace("window shown");
    // The GPU device started in wWinMain; libmpv starts after the window is up.
    gfx::Gpu::Get().NotifyWhenReady(main_->Hwnd(), ui::Host::kDeviceReadyMessage);
    {
        auto options = MpvOptions(config_);
        // general.extraOptions: "--deband=yes --sub-ass-override=force" (applied at start).
        std::istringstream extra(config_.GetString("general.extraOptions"));
        for (std::string tok; extra >> tok;) {
            while (!tok.empty() && tok[0] == '-') tok.erase(0, 1);
            const size_t eq = tok.find('=');
            if (tok.empty()) continue;
            options.emplace_back(tok.substr(0, eq), eq == std::string::npos ? "yes" : tok.substr(eq + 1));
        }
        player_.SetStartOptions(std::move(options));
    }
    player_.rememberPosition = config_.GetBool("privacy.rememberPosition");
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
    ControlLayoutChanged().Connect([this] { SetUiValue("controls.layout", CurrentControlLayout().Serialize()); });
    main_->StartTimer(3000, [this] { FlushStores(); }, true);
    InitDialogs();
    // Data files that could not be parsed were kept as *.corrupt-<time>.json (core/Paths.h).
    if (std::vector<std::wstring> bad = TakeCorruptFileNotices(); !bad.empty()) {
        static const std::pair<const wchar_t*, const wchar_t*> kWhat[] = {
            {L"history.json", L"watch history"},  {L"playlists.json", L"playlists"},
            {L"bookmarks.json", L"bookmarks"},    {L"favorites.json", L"favorites"},
            {L"library.json", L"library folders"}, {L"ui.json", L"window layout"},
            {L"settings.json", L"settings"},
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
    if (UiValue("onboarded") != "1") {
        app::ShowOnboarding([this] {
            SetUiValue("onboarded", "1");
            FlushStores();
        });
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    FlushStores();
    ShutdownSeekPreview();
    main_.reset();
    return static_cast<int>(msg.wParam);
}

void App::OnPlayerReady(bool ok) {
    if (!player_.FinishStart(ok)) {
        ShowToast(main_.get(), player_.Error(), "info", 6000);
        return;
    }
    Trace("mpv ready");
    engineReady_ = true;
    if (!pendingOpen_.empty()) {
        auto files = std::move(pendingOpen_);
        pendingOpen_.clear();
        Open(files);
    }
    playerChanged.Emit(kChangeState | kChangeVolume);
}

void App::OnPlayerEvents() {
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
    if ((changes & kChangeFile) && queue_.CurrentItem() && s.fileLoaded && s.loadSerial != recordedSerial_ &&
        config_.GetBool("privacy.saveHistory")) {
        recordedSerial_ = s.loadSerial;
        history_.RecordPlay(queue_.CurrentItem()->path, s.title, s.duration);
    }
    if ((changes & kChangeTime) && s.fileLoaded) {
        const double now = ui::AnimFloat::Now();
        if (now - lastHistorySave_ > 5000) {
            lastHistorySave_ = now;
            SaveHistoryPosition();
        }
    }
    if ((changes & kChangeFile) && s.fileLoaded) fitPending_ = true;
    if ((changes & (kChangeVideoSize | kChangeFile)) && fitPending_ && s.videoW > 0 && s.videoH > 0) {
        fitPending_ = false;
        FitWindowToVideo();
        if (config_.GetBool("video.startFullscreen") && s.hasVideo && main_ && !main_->IsFullscreen() &&
            !main_->PictureInPicture() && playerShown_)
            main_->SetFullscreen(true, config_.GetString("interface.fullscreenScreen") == "primary");
    }
    if (changes & kChangeVolume) config_.SetDouble("audio.lastVolume", std::round(s.volume));
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
        const auto siblings = ListSiblingMedia(item->path);
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
    RECT cur;
    GetWindowRect(main_->Hwnd(), &cur);
    int x = (cur.left + cur.right) / 2 - w / 2, y = (cur.top + cur.bottom) / 2 - h / 2;
    x = std::clamp(x, static_cast<int>(work.left), std::max<int>(work.left, work.right - w));
    y = std::clamp(y, static_cast<int>(work.top), std::max<int>(work.top, work.bottom - h));
    if (IsRectEmpty(&preFitRect_)) preFitRect_ = cur;
    SetWindowPos(main_->Hwnd(), nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    fittedOnce_ = true;
}

bool App::HasMedia() const {
    return player_.State().fileLoaded;
}

void App::BuildFolderQueue(const std::wstring& file) {
    if (!config_.GetBool("playback.folderAsPlaylist")) {
        PlayQueueItems({ItemFor(file)}, 0);
        return;
    }
    std::vector<std::wstring> siblings = ListSiblingMedia(file);
    const bool audio = [&] {
        const wchar_t* ext = PathFindExtensionW(file.c_str());
        for (const wchar_t* e : {L".mp3", L".flac", L".m4a", L".aac", L".ogg", L".opus", L".wav", L".wma"})
            if (_wcsicmp(ext, e) == 0) return true;
        return false;
    }();
    // Same kind only: a song doesn't pull the folder's videos into the queue.
    std::vector<QueueItem> items;
    int index = 0;
    for (const auto& s : siblings) {
        const wchar_t* ext = PathFindExtensionW(s.c_str());
        bool isAudio = false;
        for (const wchar_t* e : {L".mp3", L".flac", L".m4a", L".aac", L".ogg", L".opus", L".wav", L".wma"})
            if (_wcsicmp(ext, e) == 0) isAudio = true;
        if (isAudio != audio) continue;
        if (_wcsicmp(s.c_str(), file.c_str()) == 0) index = static_cast<int>(items.size());
        items.push_back(ItemFor(s));
    }
    if (items.empty()) items.push_back(ItemFor(file));
    const size_t slash = file.find_last_of(L"\\/");
    PlayQueueItems(std::move(items), index, slash == std::wstring::npos ? L"" : FileNameOf(file.substr(0, slash)));
}

void App::Open(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    if (!engineReady_) {
        pendingOpen_ = paths;
        return;
    }
    // Subtitle files dropped onto a playing video are added to it.
    if (player_.State().fileLoaded) {
        bool allSubs = true;
        for (const auto& p : paths) {
            const wchar_t* ext = PathFindExtensionW(p.c_str());
            bool sub = false;
            for (const wchar_t* e : {L".srt", L".ass", L".ssa", L".vtt", L".sub", L".sup", L".idx", L".smi"})
                if (_wcsicmp(ext, e) == 0) sub = true;
            allSubs &= sub;
        }
        if (allSubs) {
            for (const auto& p : paths) player_.AddSubtitle(p);
            ShowToast(main_.get(), Tr(L"Subtitles added"), "subtitles");
            return;
        }
    }
    // Missing files: say so instead of playing something else from that folder.
    std::vector<std::wstring> existing;
    std::wstring missing;
    for (const auto& p : paths) {
        if (IsUrl(p) || GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            existing.push_back(p);
        else if (missing.empty())
            missing = p;
    }
    if (!missing.empty())
        ShowToast(main_.get(), Fmt(Tr(L"File not found: {0}"), {FileNameOf(missing)}), "info", 4000);
    if (existing.empty()) return;
    if (existing.size() != paths.size()) {
        Open(existing);
        return;
    }
    // Playlist files become the queue.
    if (paths.size() == 1 && IsPlaylistFile(paths[0])) {
        std::vector<library::PlaylistItem> entries;
        if (!library::ReadM3u(paths[0], &entries) || entries.empty()) {
            ShowToast(main_.get(), Fmt(Tr(L"Couldn't read the playlist {0}"), {FileNameOf(paths[0])}), "info", 4000);
            return;
        }
        std::vector<QueueItem> items;
        for (auto& e : entries) items.push_back({std::move(e.path), std::move(e.title), e.duration});
        std::wstring name = FileNameOf(paths[0]);
        if (const size_t dot = name.find_last_of(L'.'); dot != std::wstring::npos) name.resize(dot);
        PlayQueueItems(std::move(items), 0, std::move(name));
        return;
    }
    if (paths.size() == 1 && !IsUrl(paths[0])) {
        if (IsDirectory(paths[0])) {
            std::vector<QueueItem> items;
            for (const auto& f : MediaInFolder(paths[0], config_.GetString("library.openFolder") != "folder"))
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
            for (const auto& f : MediaInFolder(p, config_.GetString("library.openFolder") != "folder"))
                items.push_back(ItemFor(f));
        else
            items.push_back(ItemFor(p));
    }
    PlayQueueItems(std::move(items), 0);
}

void App::PlayQueueItems(std::vector<QueueItem> items, int index, std::wstring name) {
    if (items.empty()) return;
    queue_.Set(std::move(items), index, std::move(name));
    PlayIndex(queue_.Current());
}

void App::PlayIndex(int index, bool showPlayer) {
    settingCurrent_ = true;
    queue_.SetCurrent(index);
    settingCurrent_ = false;
    const QueueItem* item = queue_.CurrentItem();
    if (!item) return;
    if (!engineReady_) {
        pendingOpen_ = {item->path};
        return;
    }
    // A jump the user asked for: "replace" (it also drops the queued next file), then queue anew.
    player_.Open(item->path);
    SyncNextInPlayer(true);
    // "Start playing as soon as a file opens"; the next item of a running queue always plays.
    player_.SetPause(!config_.GetBool("playback.autoPlay") && !player_.State().fileLoaded);
    if (showPlayer || playerShown_) ShowPlayer(true);
}

void App::SyncNextInPlayer(bool force) {
    if (!engineReady_) return;
    // Nothing plays: an entry left in mpv's playlist would wait there for no reason.
    if (!force && player_.State().idle) return;
    int i = -1;
    if (queue_.CurrentItem() && !config_.GetBool("playback.stopAfterEach") && !Live().stopAfter) {
        i = queue_.NextIndex(false);
        if (i == queue_.Current()) i = -1;  // Repeat One: OnPlaybackEnded seeks back instead
    }
    queuedIndex_ = i;
    player_.QueueNext(i >= 0 ? queue_.Items()[static_cast<size_t>(i)].path : std::wstring());
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
    if (queuedIndex_ >= 0 && queuedIndex_ < static_cast<int>(queue_.Items().size())) {
        settingCurrent_ = true;
        queue_.SetCurrent(queuedIndex_);
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
    nav_.resize(static_cast<size_t>(historyPos_) + 1);
    nav_.push_back(std::move(r));
    if (nav_.size() > 100) nav_.erase(nav_.begin());
    historyPos_ = static_cast<int>(nav_.size()) - 1;
    routeChanged.Emit();
}

void App::GoBack() {
    if (!CanGoBack()) return;
    --historyPos_;
    routeChanged.Emit();
}

void App::GoForward() {
    if (!CanGoForward()) return;
    ++historyPos_;
    routeChanged.Emit();
}

void App::ShowPlayer(bool show) {
    if (show == playerShown_) return;
    playerShown_ = show;
    if (!show && main_ && main_->IsFullscreen()) main_->SetFullscreen(false);
    // Space and the arrows belong to the player once it is shown.
    if (show && main_) main_->SetFocus(nullptr);
    // Back in the library: undo the video-sized window.
    if (!show && main_ && !IsRectEmpty(&preFitRect_)) {
        if (!main_->IsWindowMaximized() && !IsIconic(main_->Hwnd()))
            SetWindowPos(main_->Hwnd(), nullptr, preFitRect_.left, preFitRect_.top, preFitRect_.right - preFitRect_.left,
                         preFitRect_.bottom - preFitRect_.top, SWP_NOZORDER | SWP_NOACTIVATE);
        preFitRect_ = {};
    }
    if (main_) main_->UpdateScreen();
    screenChanged.Emit();
}

void App::OpenSettings(const std::string& category) {
    ShowSettingsWindow(category);
}

void App::ShowOnboarding() {
    app::ShowOnboarding({});
}

void App::Quit() {
    if (main_) main_->Close();
}

void App::OnCopyData(const std::wstring& payload) {
    std::vector<std::wstring> files;
    size_t start = 0;
    while (start < payload.size()) {
        size_t end = payload.find(L'\n', start);
        if (end == std::wstring::npos) end = payload.size();
        if (end > start) files.push_back(payload.substr(start, end - start));
        start = end + 1;
    }
    if (files.empty()) return;
    if (config_.GetBool("library.enqueue") && player_.State().fileLoaded) {
        std::vector<QueueItem> items;
        for (const auto& f : files) items.push_back(ItemFor(f));
        queue_.Add(std::move(items));
        ShowToast(main_.get(), Fmt(Tr(L"Added to the queue: {0}"), {std::to_wstring(files.size())}), "list");
        return;
    }
    Open(files);
}

void App::OnMainDestroyed() {
    PostQuitMessage(0);
}

}  // namespace app
