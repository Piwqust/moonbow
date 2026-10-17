#include "app/MainWindow.h"

#include "Player.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/Shell.h"
#include "app/player/ControlBar.h"
#include "app/player/PlayerView.h"

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
    ShellBody(MainWindow* w, Widget* library, PlayerView* player, MiniPlayer* mini)
        : window_(w), library_(library), player_(player), mini_(mini) {
        dir = Dir::None;
        width = Length::Grow();
        height = Length::Grow();
    }
    void OnLayout() override {
        const bool playerShown = TheApp().PlayerShown();
        library_->visible = !playerShown;
        player_->visible = playerShown;
        if (playerShown)
            player_->Arrange(rect);
        else
            library_->Arrange(rect);
        const PlayerState& s = TheApp().player().State();
        mini_->visible = !playerShown && s.fileLoaded;
        if (mini_->visible) {
            const Size m = mini_->Measure(rect.w, rect.h);
            mini_->Arrange({rect.Right() - m.w - 24, rect.Bottom() - m.h - 24, m.w, m.h});
        }
    }

private:
    MainWindow* window_;
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
        title->visible = !window_->IsFullscreen() && !window_->UiHidden();
        Widget::OnLayout();
    }
    bool OnMouseMove(const MouseEvent&) override {
        window_->PokeChrome();
        return false;
    }

private:
    MainWindow* window_;
};

}  // namespace

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() {
    if (chromeTimer_) CancelTimer(chromeTimer_);
}

bool MainWindow::Create() {
    HostOptions o;
    o.title = kMainWindowTitle;
    o.width = 1280;
    o.height = 780;
    o.minWidth = 720;
    o.minHeight = 440;
    if (!Host::Create(o)) return false;

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

    onKey = [this](const KeyEvent& e) {
        const Action a = ActionForKey(e);
        if (a == Action::None) return false;
        // Esc only leaves fullscreen / hide-UI; elsewhere it does nothing.
        if (a == Action::ExitFullscreen && !IsFullscreen() && !UiHidden()) return false;
        Run(a);
        PokeChrome();
        return true;
    };
    onDropFiles = [](std::vector<std::wstring> files) { TheApp().Open(files); };
    onCloseRequest = [this] {
        // mpv's video window is a child of ours, owned by an mpv thread, so
        // shut mpv down off the UI thread while this thread keeps pumping.
        ShowWindow(Hwnd(), SW_HIDE);
        HWND hwnd = Hwnd();
        std::thread([hwnd] {
            TheApp().player().Shutdown();
            PostMessageW(hwnd, WM_APP_CLOSED, 0, 0);
        }).detach();
        return false;
    };
    onDestroyed = [] { TheApp().OnMainDestroyed(); };
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

    auto* body = root->Add<ShellBody>(this, libraryLayer_, player_, mini_);
    body->AddChild(std::move(library));
    body->AddChild(std::move(player));
    body->AddChild(std::move(mini));
    SetRoot(std::move(root));
}

bool MainWindow::Immersive() const {
    return IsFullscreen() || uiHidden_;
}

void MainWindow::SetVideoRect(const Rect& r) {
    videoRect_ = r;
    videoWanted_ = !r.Empty();
}

void MainWindow::HideVideo() {
    videoWanted_ = false;
}

void MainWindow::UpdateScreen() {
    CloseAllPopups();
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

void MainWindow::PokeChrome() {
    SetCursorHidden(false);
    if (!Immersive()) {
        if (chrome_.Target() != 1) chrome_.Set(1);
        return;
    }
    if (chrome_.Target() < 1) {
        chrome_.To(1, 140);
        RequestLayout();
    }
    if (chromeTimer_) CancelTimer(chromeTimer_);
    chromeTimer_ = StartTimer(kChromeHideMs, [this] {
        chromeTimer_ = 0;
        if (!Immersive()) return;
        // Keep the controls while the pointer is on them or a popup is open.
        Widget* hot = Hot();
        const bool overBar = hot && player_ && player_->Bar() && player_->Bar()->IsAncestorOf(hot);
        if (HasPopup() || overBar || TheApp().player().State().paused) {
            PokeChrome();
            return;
        }
        chrome_.To(0, 260);
        SetCursorHidden(MouseInside());
        RequestLayout();
    });
}

void MainWindow::OnLayoutDone() {
    // Where should the video be this frame?
    Rect target{};
    if (TheApp().PlayerShown() && player_)
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
            Destroy();
            *result = 0;
            return true;
        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (!cds || cds->dwData != kCopyDataOpen) return false;
            std::wstring payload(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            while (!payload.empty() && payload.back() == L'\0') payload.pop_back();
            if (IsIconic(Hwnd())) ShowWindow(Hwnd(), SW_RESTORE);
            SetForegroundWindow(Hwnd());
            // Handle after this SendMessage returns, so the sender isn't blocked.
            Post([payload] { TheApp().OnCopyData(payload); });
            *result = TRUE;
            return true;
        }
        case WM_SIZE:
        case WM_WINDOWPOSCHANGED:
            if (titleBar_) titleBar_->Refresh();
            return false;
        default:
            return false;
    }
}

}  // namespace app
