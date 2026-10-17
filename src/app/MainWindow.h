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
    void PokeChrome();  // mouse moved: show controls, restart the hide timer

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
    void PaintSkeleton(HDC dc) override;

private:
    void BuildUi();
    bool Immersive() const;  // fullscreen or hide-UI

    HWND video_ = nullptr;
    ui::Rect videoRect_{};
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
