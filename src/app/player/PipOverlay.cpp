// Picture in Picture: the main window shrinks to a small always-on-top video
// (MainWindow::SetPictureInPicture). This overlay shows on hover: close, back to the full
// window, previous / play-pause / next and a thin progress bar. Dragging anywhere else moves
// the window.
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/ControlBar.h"
#include "app/player/PlayerView.h"

namespace app {

using namespace ui;

namespace {

class PipButton : public IconButton {
public:
    PipButton(const char* icon, const wchar_t* tip, int box, int iconSize) : IconButton(icon, Tr(tip), box, iconSize) {
        round = true;
        tone = Tone::Primary;
    }
    void Paint(Painter& p) override {
        // Dark disc so the icon stays readable over bright video.
        p.Fill(rect, Color::Hex(0x000000, 0.35f), rect.w / 2);
        IconButton::Paint(p);
    }
};

class PipOverlay : public Widget {
public:
    explicit PipOverlay(MainWindow* window) : window_(window) {
        dir = Dir::None;
        ncRole = NcRole::Caption;  // drag the window by the video
        close_ = Add<PipButton>("close", L"Close", 30, 18);
        close_->onClick = [this] {
            window_->Post([] {
                TheApp().main()->SetPictureInPicture(false);
                TheApp().StopPlayback();
            });
        };
        back_ = Add<PipButton>("open", L"Back to full window", 30, 18);
        back_->onClick = [this] { window_->Post([] { TheApp().main()->SetPictureInPicture(false); }); };
        prev_ = Add<PipButton>("previous", L"Previous", 36, 22);
        prev_->onClick = [] { Run(Action::Previous); };
        play_ = Add<PipButton>("play", L"Play", 48, 28);
        play_->onClick = [] { Run(Action::PlayPause); };
        next_ = Add<PipButton>("next", L"Next", 36, 22);
        next_->onClick = [] { Run(Action::Next); };
        seek_ = Add<SeekBar>();
        seek_->compact = true;
        onContextMenu = [this](const MouseEvent& e) { ShowPipContextMenu(GetHost(), {e.x, e.y}); };
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeState | kChangeFile)) {
                play_->icon = TheApp().player().State().paused ? "play" : "pause";
                play_->tooltip = TheApp().player().State().paused ? Tr(L"Play") : Tr(L"Pause");
                Invalidate();
            }
        });
    }
    ~PipOverlay() override {
        if (timer_) window_->CancelTimer(timer_);
    }

    bool ControlsShown() const { return shown_.Target() > 0.5f; }
    void OnLayout() override {
        if (visible && !timer_) {
            // The pointer is over a caption area (no WM_MOUSEMOVE), so poll it while in PiP.
            timer_ = window_->StartTimer(120, [this] { Poll(); }, true);
        }
        const Rect r = rect;
        close_->Arrange({r.x + 8, r.y + 8, 30, 30});
        back_->Arrange({r.Right() - 38, r.y + 8, 30, 30});
        play_->Arrange({r.Cx() - 24, r.Cy() - 24, 48, 48});
        prev_->Arrange({r.Cx() - 24 - 16 - 36, r.Cy() - 18, 36, 36});
        next_->Arrange({r.Cx() + 24 + 16, r.Cy() - 18, 36, 36});
        // A 24 DIP target (WCAG 2.5.8) around the thin track.
        seek_->Arrange({r.x + 10, r.Bottom() - 24, r.w - 20, 24});
        const float k = shown_.Get();
        for (auto& c : Children()) c->visible = k > 0.02f;
    }

    void Paint(Painter& p) override {
        const float k = shown_.Get();
        if (k <= 0.01f) return;
        // Scrim over the video (DirectComposition draws the UI above mpv's window).
        p.Fill(rect, Color::Hex(0x000000, 0.38f * k));
        p.PushOpacity(k);
    }
    void PaintOver(Painter& p) override {
        if (shown_.Get() > 0.01f) p.PopOpacity();
    }
    void PaintChildren(Painter& p) override {
        if (shown_.Get() > 0.01f) Widget::PaintChildren(p);
    }

private:
    void Poll() {
        if (!window_->PictureInPicture()) {
            window_->CancelTimer(timer_);
            timer_ = 0;
            shown_.Set(0);
            return;
        }
        POINT pt;
        RECT wr;
        bool over = GetCursorPos(&pt) && GetWindowRect(window_->Hwnd(), &wr) && PtInRect(&wr, pt);
        // Over a button (client area), or driven by posted messages in automation runs.
        Widget* hot = window_->Hot();
        if (hot && IsAncestorOf(hot)) over = true;
        if (window_->MouseInside() && rect.Contains(window_->MousePos().x, window_->MousePos().y)) over = true;
        if (TheApp().player().State().paused) over = true;
        const float target = over ? 1.0f : 0.0f;
        if (shown_.Target() != target) {
            shown_.To(target, over ? 120.0f : 220.0f);
            window_->RequestLayout();
        }
    }

    MainWindow* window_;
    IconButton *close_, *back_, *prev_, *play_, *next_;
    SeekBar* seek_;
    AnimFloat shown_{1.0f};
    UINT_PTR timer_ = 0;
    ScopedConnection conn_;
};

}  // namespace

std::unique_ptr<Widget> CreatePipOverlay(MainWindow* window) {
    return std::make_unique<PipOverlay>(window);
}

bool PipOverlayShown(const Widget* overlay) {
    const auto* o = dynamic_cast<const PipOverlay*>(overlay);
    return o && o->visible && o->ControlsShown();
}

}  // namespace app
