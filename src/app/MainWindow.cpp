#include "app/MainWindow.h"
#include "app/LibraryView.h"

#include "Player.h"
#include "Trace.h"
#include "app/Actions.h"
#include "app/I18n.h"
#include "app/App.h"
#include "app/Shell.h"
#include "app/player/ControlBar.h"
#include "app/library/LibraryCommon.h"
#include "app/player/PlayerView.h"
#include "app/player/SeekPreview.h"

#include <shlwapi.h>
#include <windowsx.h>

#include <cmath>
#include <thread>

namespace app {

using namespace ui;

namespace {

constexpr wchar_t kVideoClass[] = L"LumenVideo";
constexpr UINT WM_APP_CLOSED = WM_APP + 71;
constexpr UINT kChromeHideMs = 2200;

// Lays out the library layer, the player and the floating mini player.
class ShellBody : public Widget {
public:
    ShellBody(Widget* library, PlayerView* player, MiniPlayer* mini) : library_(library), player_(player), mini_(mini) {
        dir = Dir::None;
        width = Length::Grow();
        height = Length::Grow();
    }
    void OnLayout() override {
        const bool playerShown = TheApp().PlayerShown();
        // Waiting for the opened file's first frame: the library stays, the player is laid out
        // unseen (mpv's window and the cover get their size), no mini player in between.
        const bool pending = TheApp().PlayerPending();
        library_->visible = !playerShown;
        player_->visible = playerShown;
        const PlayerState& s = TheApp().player().State();
        mini_->visible = !playerShown && !pending && s.fileLoaded;
        // The mini player goes first: the library's scroll views keep their last rows reachable above it.
        Rect card;
        if (mini_->visible) {
            const Size m = mini_->Measure(rect.w, rect.h);
            card = {rect.Right() - m.w - 24, rect.Bottom() - m.h - 24, m.w, m.h};
        }
        if (Host* h = GetHost()) h->floatingCard = card;
        if (playerShown || pending) player_->Arrange(rect);
        if (!playerShown) library_->Arrange(rect);
        if (mini_->visible) mini_->Arrange(card);
    }

private:
    Widget* library_;
    PlayerView* player_;
    MiniPlayer* mini_;
};

class ShellRoot : public Widget {
public:
    explicit ShellRoot(MainWindow* w) : window_(w) {
        dir = Dir::Column;
        width = Length::Grow();
        height = Length::Grow();
    }
    void OnLayout() override {
        // The title bar hides in fullscreen and hide-UI mode.
        Widget* title = Children()[0].get();
        title->visible = !window_->IsFullscreen() && !window_->UiHidden() &&
                         (TheApp().config().GetBool("interface.decorations") || !TheApp().PlayerShown());
        Widget::OnLayout();
    }

private:
    MainWindow* window_;
};

}  // namespace

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    if (chromeTimer_) CancelTimer(chromeTimer_);
    if (subMarginToken_) TheApp().config().Unsubscribe(subMarginToken_);
}

bool MainWindow::Create() {
    HostOptions o;
    o.title = kMainWindowTitle;
    o.width = 1280;
    o.height = 780;
    o.minWidth = 720;
    o.minHeight = 440;
    o.mainWindow = true;
    if (!Host::Create(o)) return false;
    if (g_instanceGroup) SetPropW(Hwnd(), kInstanceGroupProp, reinterpret_cast<HANDLE>(g_instanceGroup));

    static bool registered = [] {
        WNDCLASSEXW vc{sizeof(vc)};
        vc.lpfnWndProc = DefWindowProcW;
        vc.hInstance = GetModuleHandleW(nullptr);
        vc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        vc.lpszClassName = kVideoClass;
        return RegisterClassExW(&vc) != 0;
    }();
    if (!registered) return false;
    // Disabled, so mouse input over the video reaches this window's widgets.
    video_ = CreateWindowExW(WS_EX_NOPARENTNOTIFY, kVideoClass, L"",
                             WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_DISABLED, 0, 0, 1, 1, Hwnd(), nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!video_) return false;

    BuildUi();
    // Over video toasts sit at the top. In the library they sit at the bottom, like a snackbar:
    // at the top they covered the page's header rows (tabs, "Shuffle", an album's title).
    // In fullscreen they go below the top info panel while it shows (PlayerView::PaintTopInfo).
    toastTop = [this](float h) {
        if (!TheApp().PlayerShown()) {
            float bottom = ClientSize().h - 24;
            // Above the mini player when a centred toast (at most 600 DIP wide) could reach it.
            if (mini_ && mini_->visible && mini_->rect.x < (ClientSize().w + 600) / 2) bottom = mini_->rect.y - 12;
            return bottom - h;
        }
        return IsFullscreen() && ChromeOpacity() > 0.5f ? 72.0f : 56.0f;
    };
    toastBand = [this] { return TheApp().PlayerShown() ? player_->ToastBand() : Rect{0, 0, ClientSize().w, 0}; };
    // A new base margin from Settings is what mpv has now; recompute the lift on top of it.
    subMarginToken_ = TheApp().config().Subscribe("subtitles.marginBottom", [this](const std::string&) {
        appliedSubMargin_ = -1;
        RequestLayout();
    });

    onKey = [this](const KeyEvent& e) {
        const bool arrow = e.key == VK_UP || e.key == VK_DOWN || e.key == VK_LEFT || e.key == VK_RIGHT;
        if (arrow && !TheApp().PlayerShown() && !Focus() && !e.mods.ctrl && !e.mods.alt && library_ && library_->View())
            if (Widget* first = library_->View()->InitialFocus()) {
                SetFocus(first, true);
                if (first->OnKeyDown(e)) {
                    RequestRender();
                    return true;
                }
            }
        const Action a = ActionForKey(e);
        if (a == Action::None) return false;
        // Esc and F leave Picture in Picture first.
        if (pip_ && (a == Action::ExitFullscreen || a == Action::Fullscreen)) {
            SetPictureInPicture(false);
            if (a == Action::Fullscreen) Run(a);
            return true;
        }
        // Esc leaves fullscreen / hide-UI first; in the windowed player it returns to the library.
        if (a == Action::ExitFullscreen && !IsFullscreen() && !UiHidden()) {
            if (!TheApp().PlayerShown()) return false;
            TheApp().ShowPlayer(false);
            return true;
        }
        Run(a);
        PokeChrome();
        return true;
    };
    // Dropped while the queue is open (the drop lands on or beside it): added to the queue, as in
    // PotPlayer and VLC. One folder opens as a library page to sort and pick from; anything else
    // plays and replaces the queue.
    onDropFiles = [](std::vector<std::wstring> files) {
        if (PlayerPanelOpen(PanelKind::Queue)) {
            TheApp().Enqueue(files);
        } else if (files.size() == 1 && PathIsDirectoryW(files[0].c_str())) {
            lib::ShowFolderInLibrary(files[0]);
        } else {
            TheApp().Open(files);
        }
    };
    dropHint = Tr(L"Drop to play");
    langConn_ = Connect(TheApp().languageChanged, [this] { dropHint = Tr(L"Drop to play"); });
    onCloseRequest = [this] {
        // A second close (Quit from the tray, "when finished: close") would start a second
        // shutdown of the same mpv.
        if (TheApp().Quitting()) return false;
        TheApp().BeginQuit();
        Trace("close: requested");
        // mpv's video window is a child of ours, owned by an mpv thread, so
        // shut mpv down off the UI thread while this thread keeps pumping.
        ShowWindow(Hwnd(), SW_HIDE);
        Trace("close: window hidden");
        HWND hwnd = Hwnd();
        std::thread([hwnd] {
            TheApp().player().Shutdown();
            PostMessageW(hwnd, WM_APP_CLOSED, 0, 0);
        }).detach();
        return false;
    };
    onDestroyed = [this] {
        RemovePropW(Hwnd(), kInstanceGroupProp);
        TheApp().OnMainDestroyed();
    };
    return true;
}

void MainWindow::BuildUi() {
    auto root = std::make_unique<ShellRoot>(this);
    titleBar_ = root->Add<TitleBar>(this);

    auto library = std::make_unique<Widget>();
    library->dir = Dir::Row;
    library->width = Length::Grow();
    library->height = Length::Grow();
    sidebar_ = library->Add<NavSidebar>();
    library_ = library->Add<LibraryPane>(false);
    libraryLayer_ = library.get();

    auto player = std::make_unique<PlayerView>(this);
    player_ = player.get();
    auto mini = std::make_unique<MiniPlayer>(this);
    mini_ = mini.get();

    auto* body = root->Add<ShellBody>(libraryLayer_, player_, mini_);
    body->AddChild(std::move(library));
    body->AddChild(std::move(player));
    body->AddChild(std::move(mini));
    SetRoot(std::move(root));
}

bool MainWindow::Immersive() const {
    return IsFullscreen() || uiHidden_ || pip_;
}

bool MainWindow::BarOverlays() const {
    if (Immersive()) return true;
    App& app = TheApp();
    return app.PlayerShown() && app.player().State().fileLoaded && app.player().State().hasVideo &&
           app.config().GetString("interface.autoHide") == "always";
}

void MainWindow::SetPictureInPicture(bool on) {
    if (on == pip_) return;
    HWND hwnd = Hwnd();
    if (on) {
        if (!TheApp().player().State().fileLoaded) return;
        if (IsFullscreen()) SetFullscreen(false);
        if (!TheApp().PlayerShown()) TheApp().ShowPlayer(true);
        CloseAllPopups();
        GetWindowPlacement(hwnd, &pipRestore_);
        if (IsZoomed(hwnd) || IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        // Last PiP rect (physical pixels) if it is still on a monitor, else bottom-right.
        const float s = Scale();
        RECT r{};
        int x = 0, y = 0, w = 0, h = 0;
        const std::string saved = TheApp().UiValue("pip.rect");
        if (sscanf_s(saved.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 100 && h > 60) r = {x, y, x + w, y + h};
        MONITORINFO mi{sizeof(mi)};
        HMONITOR mon = r.right > r.left ? MonitorFromRect(&r, MONITOR_DEFAULTTONULL) : nullptr;
        if (!mon) {
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            const LONG pw = static_cast<LONG>(480 * s), ph = static_cast<LONG>(270 * s), m = static_cast<LONG>(24 * s);
            r = {mi.rcWork.right - pw - m, mi.rcWork.bottom - ph - m, mi.rcWork.right - m, mi.rcWork.bottom - m};
        }
        pip_ = true;
        SetWindowPos(hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    } else {
        RECT r;
        if (GetWindowRect(hwnd, &r)) {
            char buf[64];
            sprintf_s(buf, "%ld,%ld,%ld,%ld", r.left, r.top, r.right - r.left, r.bottom - r.top);
            TheApp().SetUiValue("pip.rect", buf);
        }
        pip_ = false;
        // Back to "Keep window above other windows" as it is set, not always off (App::ApplyWindowOptions).
        SetWindowPos(hwnd, TheApp().config().GetBool("interface.alwaysOnTop") ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0,
                     0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPlacement(hwnd, &pipRestore_);
    }
    chrome_.Set(1);
    PokeChrome();
    RequestLayout();
}

namespace {

UINT ChromeHideDelay() {
    const double s = TheApp().config().GetDouble("interface.hideDelay");
    return static_cast<UINT>(std::clamp(s, 0.5, 10.0) * 1000);
}

bool ChromeAutoHides() {
    return TheApp().config().GetString("interface.autoHide") != "never";
}

// "Show controls in fullscreen" off: fullscreen is keyboard-only, the bar never comes up.
bool FullscreenWithoutControls(const MainWindow* w) {
    return w->IsFullscreen() && !TheApp().config().GetBool("interface.controlsInFullscreen");
}

}  // namespace

void MainWindow::SetVideoRect(const Rect& r) {
    videoRect_ = r;
    videoWanted_ = !r.Empty();
}

void MainWindow::HideVideo() {
    videoWanted_ = false;
}

void MainWindow::UpdateScreen() {
    CloseAllPopups();
    if (pip_ && !TheApp().PlayerShown()) SetPictureInPicture(false);
    if (!TheApp().PlayerShown() && uiHidden_) uiHidden_ = false;
    RequestLayout();
}

void MainWindow::SetUiHidden(bool hidden) {
    if (hidden == uiHidden_) return;
    if (hidden && !TheApp().PlayerShown()) return;
    uiHidden_ = hidden;
    chrome_.Set(1);
    PokeChrome();
    RequestLayout();
}

void MainWindow::PointerMoved(float x, float y) {
    const float dx = x - lastPointer_.x, dy = y - lastPointer_.y;
    const bool hidden = BarOverlays() && chrome_.Target() < 0.5f;
    if (hidden && dx * dx + dy * dy < 6 * 6) return;  // keep the anchor until a real move
    lastPointer_ = {x, y};
    PokeChrome();
}

void MainWindow::PokeChrome() {
    SetCursorHidden(false);
    if (!BarOverlays()) {
        if (chrome_.Target() != 1) chrome_.Set(1);
        return;
    }
    if (FullscreenWithoutControls(this)) {
        if (chrome_.Target() != 0) {
            chrome_.To(0, 140);
            RequestLayout();
        }
    } else if (chrome_.Target() < 1) {
        chrome_.To(1, 140);
        RequestLayout();
    }
    if (chromeTimer_) CancelTimer(chromeTimer_);
    if (!ChromeAutoHides()) return;
    chromeTimer_ = StartTimer(ChromeHideDelay(), [this] {
        chromeTimer_ = 0;
        if (!BarOverlays()) return;
        // Keep the controls while the pointer is on them or drags one (the seek bar held still above
        // the bar), the keyboard is on one of them, or a menu or panel is open (toasts don't count).
        Widget* hot = Pressed() ? Pressed() : Hot();
        const Widget* bar = player_ ? player_->Bar() : nullptr;
        const bool overBar = hot && bar && bar->IsAncestorOf(hot);
        const bool keyboardOnBar = FocusVisible() && bar && bar->IsAncestorOf(Focus());
        if (HasInteractivePopup() || overBar || keyboardOnBar || TheApp().player().State().paused) {
            PokeChrome();
            return;
        }
        chrome_.To(0, 320, ui::Ease::InOut);
        SetCursorHidden(MouseInside());
        RequestLayout();
    });
}

// Thumbnails that were queued for tiles no longer painted (scrolled away, page left) are
// dropped, so the visible ones don't wait behind them.
void MainWindow::OnFramePainted() {
    TheApp().CheckPlayerHold();  // before EndFrame: a song's cover is asked for while the player waits
    TheApp().images().EndFrame();
}

bool MainWindow::ContentReady() {
    return TheApp().FirstScreenReady();
}

UINT MainWindow::ContentCapMs() {
    return TheApp().OpensFileAtStart() ? 700 : 400;
}

// While the control bar floats over the picture, subtitles move up above it (as in uosc and
// YouTube) and go back down when it hides. mpv's sub-margin-y is in pixels of a 720-line OSD.
void MainWindow::UpdateSubtitleLift() {
    Player& mpv = TheApp().player();
    const PlayerState& s = mpv.State();
    if (!mpv.Ready() || !s.fileLoaded) return;
    const int base = static_cast<int>(std::lround(TheApp().config().GetDouble("subtitles.marginBottom")));
    int margin = base;
    const float h = ClientSize().h;
    if (player_ && player_->visible && player_->Bar() && player_->Bar()->visible && s.hasVideo && !pip_ &&
        BarOverlays() && chrome_.Target() > 0.5f && h > 0) {
        const float lift = h - player_->Bar()->rect.y + 8;
        margin = base + static_cast<int>(std::lround(std::max(0.0f, lift) / h * 720));
    } else if (pip_ && player_ && player_->visible && s.hasVideo && player_->PipControlsShown() && h > 0) {
        // PiP: above the overlay's seek bar (16 DIP from the bottom), so it doesn't strike the line through.
        margin = base + static_cast<int>(std::lround(26.0f / h * 720));
    }
    // Above the seek bar's frame preview while it shows, not under it.
    if (const Rect v = player_ && !pip_ ? player_->VideoRect() : Rect{}; SeekPreviewTop() > 0 && v.h > 0)
        margin = std::max(margin, base + static_cast<int>(std::lround(std::max(0.0f, v.Bottom() - SeekPreviewTop() + 8) / v.h * 720)));
    if (margin == appliedSubMargin_) return;
    appliedSubMargin_ = margin;
    mpv.SetProperty("sub-margin-y", std::to_string(margin));
}

void MainWindow::OnLayoutDone() {
    UpdateSubtitleLift();
    // Where should the video be this frame?
    Rect target{};
    if ((TheApp().PlayerShown() || TheApp().PlayerPending()) && player_)
        target = player_->VideoRect();
    else if (mini_ && mini_->visible)
        target = mini_->VideoRect();
    SetVideoRect(target);
    const bool show = videoWanted_ && TheApp().player().State().fileLoaded && TheApp().player().State().hasVideo;

    const float s = Scale();
    RECT r{static_cast<LONG>(std::lround(videoRect_.x * s)), static_cast<LONG>(std::lround(videoRect_.y * s)),
           static_cast<LONG>(std::lround(videoRect_.Right() * s)), static_cast<LONG>(std::lround(videoRect_.Bottom() * s))};
    if (!show) r = {0, 0, 0, 0};
    if (memcmp(&r, &appliedVideo_, sizeof(RECT)) == 0) return;
    appliedVideo_ = r;
    if (show) {
        SetWindowPos(video_, nullptr, r.left, r.top, std::max(1L, r.right - r.left), std::max(1L, r.bottom - r.top),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
    } else {
        ShowWindow(video_, SW_HIDE);
    }
}

// Opaque everywhere except the hole where mpv's video window shows through; no hole while the
// player waits for the first frame (the window is placed already, under the library).
void MainWindow::PaintBackground(Painter& p) {
    const Size client = ClientSize();
    const Color bg = P().bgBase;
    const float s = Scale();
    if (appliedVideo_.right <= appliedVideo_.left || appliedVideo_.bottom <= appliedVideo_.top ||
        TheApp().PlayerPending()) {
        p.Fill({0, 0, client.w, client.h}, bg);
        return;
    }
    const float l = appliedVideo_.left / s, t = appliedVideo_.top / s, r = appliedVideo_.right / s,
                b = appliedVideo_.bottom / s;
    p.Fill({0, 0, client.w, t}, bg);
    p.Fill({0, b, client.w, client.h - b}, bg);
    p.Fill({0, t, l, b - t}, bg);
    p.Fill({r, t, client.w - r, b - t}, bg);
    // The picture fades in with the rest of the window's first content.
    if (const float k = ContentAppear(); k < 1) p.Fill({l, t, r - l, b - t}, bg.Fade(1 - k));
}

void MainWindow::PaintSkeleton(HDC dc) {
    RECT rc;
    GetClientRect(Hwnd(), &rc);
    auto fill = [dc](RECT r, Color c) {
        const uint32_t v = c.Rgb();
        HBRUSH b = CreateSolidBrush(RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF));
        FillRect(dc, &r, b);
        DeleteObject(b);
    };
    fill(rc, P().bgBase);
    if (!IsFullscreen()) {
        RECT title = rc;
        title.bottom = static_cast<LONG>(40 * Scale());
        fill(title, P().bgChrome);
    }
}

bool MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    if (TheApp().system().HandleMessage(msg, wp, lp, result)) return true;
    switch (msg) {
        case WM_APP_MPV_READY:
            TheApp().OnPlayerReady(wp != 0);
            *result = 0;
            return true;
        case WM_APP_MPV_WAKEUP:
            TheApp().OnPlayerEvents();
            *result = 0;
            return true;
        case kLibraryMessage:
            TheApp().OnLibraryScanned();
            *result = 0;
            return true;
        case kThumbnailsMessage:
            TheApp().OnThumbnails();
            *result = 0;
            return true;
        case WM_APP_CLOSED:
            Trace("close: destroying the window");
            Destroy();
            *result = 0;
            return true;
        case WM_ENDSESSION:
            if (wp) TheApp().EndSession();
            *result = 0;
            return true;
        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (cds && cds->dwData == kCopyDataCommand && Host::Automation()) {
                std::wstring command(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
                while (!command.empty() && command.back() == L'\0') command.pop_back();
                std::string reply;
                const bool ok = TheApp().AutomationCommand(command, &reply);
                reply = (ok ? "ok\n" : "failed\n") + reply;
                wchar_t tmp[MAX_PATH];
                GetTempPathW(MAX_PATH, tmp);
                const std::wstring path =
                    std::wstring(tmp) + L"lumen-cmd-" + std::to_wstring(GetCurrentProcessId()) + L".txt";
                if (HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);
                    f != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(f, reply.data(), static_cast<DWORD>(reply.size()), &written, nullptr);
                    CloseHandle(f);
                }
                *result = ok ? TRUE : FALSE;
                return true;
            }
            if (!cds || cds->dwData != kCopyDataOpen) return false;
            // Closing: refuse, so the sending instance opens the files itself (main.cpp). Also when
            // the close is queued already: the files would come after it and be dropped.
            MSG queued;
            if (TheApp().Quitting() ||
                PeekMessageW(&queued, Hwnd(), WM_CLOSE, WM_CLOSE, PM_NOREMOVE | PM_QS_POSTMESSAGE)) {
                *result = FALSE;
                return true;
            }
            std::wstring payload(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            while (!payload.empty() && payload.back() == L'\0') payload.pop_back();
            if (!Host::Automation() && (IsIconic(Hwnd()) || !IsWindowVisible(Hwnd()))) ShowWindow(Hwnd(), SW_RESTORE);
            Host::Activate(Hwnd());
            // Handle after this SendMessage returns, so the sender isn't blocked.
            Post([payload] { TheApp().OnCopyData(payload); });
            *result = TRUE;
            return true;
        }
        case WM_GETMINMAXINFO:
            if (!pip_) return false;
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {static_cast<LONG>(256 * Scale()),
                                                                  static_cast<LONG>(144 * Scale())};
            *result = 0;
            return true;
        case WM_NCRBUTTONUP:
            // In PiP the video is the caption (drag to move): a right click there would open the
            // Windows system menu; show the player's PiP menu instead.
            if (!pip_ || wp != HTCAPTION) return false;
            {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(Hwnd(), &pt);
                ShowPipContextMenu(this, {pt.x / Scale(), pt.y / Scale()});
            }
            *result = 0;
            return true;
        case WM_NCRBUTTONDOWN:
            if (!pip_ || wp != HTCAPTION) return false;
            *result = 0;
            return true;
        case WM_NCLBUTTONDBLCLK:
            // Double click on the PiP video returns to the full window instead of maximizing.
            if (!pip_ || wp != HTCAPTION) return false;
            Post([this] { SetPictureInPicture(false); });
            *result = 0;
            return true;
        case WM_SETTINGCHANGE:
            // Windows switched apps between light and dark: the System theme follows it.
            if (lp && lstrcmpW(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0 &&
                TheApp().config().GetString("general.theme") == "system")
                TheApp().ApplyTheme();
            return false;
        case WM_KEYDOWN:
            // Tab over the hidden floating bar brings it back first, so the focus can move into it
            // (Host moves it right after this; the next layout keeps the bar, PlayerView::OnLayout).
            if (wp == VK_TAB && BarOverlays() && !FullscreenWithoutControls(this) && !HasInteractivePopup() && player_ &&
                player_->Bar() && !pip_) {
                PokeChrome();
                player_->Bar()->visible = true;
            }
            return false;
        case WM_SIZE:
            if (titleBar_) titleBar_->Refresh();
            return false;
        case WM_WINDOWPOSCHANGED:
            // Dragging the window only moves it (SWP_NOSIZE): nothing to lay out or repaint.
            if (titleBar_ && !(reinterpret_cast<const WINDOWPOS*>(lp)->flags & SWP_NOSIZE)) titleBar_->Refresh();
            return false;
        default:
            return false;
    }
}

}  // namespace app
