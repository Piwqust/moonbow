#pragma once
#include "ui/Controls.h"
#include "ui/Host.h"

namespace app {

class TitleBar;
class NavSidebar;
class LibraryPane;
class PlayerView;
class MiniPlayer;

inline constexpr wchar_t kMainWindowTitle[] = L"Lumen";
inline constexpr ULONG_PTR kCopyDataOpen = 0x4C554D31;  // 'LUM1'

// The main window: title bar, library (sidebar + content) or player, and the
// mini player. Owns the child HWND mpv renders into; widgets tell it where the
// video should appear (PlayerView, MiniPlayer) via SetVideoRect.
class MainWindow : public ui::Host {
public:
    MainWindow();
    ~MainWindow() override;

    bool Create();
    HWND VideoHwnd() const { return video_; }

    // Window-DIP rect for the video this frame; call during layout.
    void SetVideoRect(const ui::Rect& r);
    void HideVideo();

    // Hide UI mode (Ctrl+\): only the video and an auto-hiding control bar.
    void SetUiHidden(bool hidden);
    bool UiHidden() const { return uiHidden_ || pip_; }
    // True while the controls overlay is visible in fullscreen/hide-UI mode.
    float ChromeOpacity() const { return chrome_.Get(); }
    // The control bar floats over the video and auto-hides: fullscreen / hide-UI / PiP,
    // and in a window when Auto-hide the controls = Always and a video is shown.
    bool BarOverlays() const;
    void PokeChrome();  // mouse moved: show controls, restart the hide timer
    // Pointer moved to (x, y): like PokeChrome, but while the controls are hidden a move of a few
    // pixels (a nudged desk, a jittery sensor) doesn't bring them back.
    void PointerMoved(float x, float y);
    void UpdateSubtitleLift();

    // Picture in Picture: compact always-on-top video; off restores the previous placement.
    void SetPictureInPicture(bool on);
    bool PictureInPicture() const { return pip_; }

    TitleBar* titleBar() const { return titleBar_; }
    PlayerView* playerView() const { return player_; }
    LibraryPane* library() const { return library_; }
    NavSidebar* sidebar() const { return sidebar_; }

    void UpdateScreen();  // library/player switch after App::ShowPlayer

protected:
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) override;
    void OnLayoutDone() override;
    void OnFramePainted() override;
    void PaintSkeleton(HDC dc) override;
    void PaintBackground(ui::Painter& p) override;

private:
    void BuildUi();
    bool Immersive() const;  // fullscreen or hide-UI

    HWND video_ = nullptr;
    ui::Rect videoRect_{};
    ui::Point lastPointer_{-1000, -1000};
    int appliedSubMargin_ = -1;  // sub-margin-y last sent to mpv (UpdateSubtitleLift)
    uint64_t subMarginToken_ = 0;
    bool videoWanted_ = false;
    RECT appliedVideo_{-1, -1, -1, -1};
    bool uiHidden_ = false;
    bool pip_ = false;
    WINDOWPLACEMENT pipRestore_{sizeof(WINDOWPLACEMENT)};
    ui::AnimFloat chrome_{1.0f};
    UINT_PTR chromeTimer_ = 0;

    TitleBar* titleBar_ = nullptr;
    NavSidebar* sidebar_ = nullptr;
    LibraryPane* library_ = nullptr;
    ui::Widget* libraryLayer_ = nullptr;
    PlayerView* player_ = nullptr;
    MiniPlayer* mini_ = nullptr;
};

}  // namespace app
