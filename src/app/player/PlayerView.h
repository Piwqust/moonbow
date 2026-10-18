#pragma once
#include "app/Signal.h"
#include "ui/Controls.h"

namespace app {

class MainWindow;
class ControlBar;
class StatsOverlay;
class NextEpisodeCard;
class SkipChapterButton;
class LibraryPane;
class NavSidebar;

// The player screen: optional side panel (icon rail + compact library list),
// the video (or the music view for audio files) and the control bar.
// In fullscreen / hide-UI mode the bar floats over the video and auto-hides.
class PlayerView : public ui::Widget {
public:
    explicit PlayerView(MainWindow* window);
    ~PlayerView() override;

    void SetSidePanel(bool shown);
    bool SidePanel() const { return sideWanted_; }
    ControlBar* Bar() const { return bar_; }
    // Picture in Picture: the hover controls are up, with their seek bar along the bottom edge.
    bool PipControlsShown() const;

    // Rect where the video is shown (window DIPs); empty when not visible.
    ui::Rect VideoRect() const;

    void OnLayout() override;
    float SubtitleBand(float h) const;
    void Paint(ui::Painter& p) override;
    void PaintOver(ui::Painter& p) override;

private:
    class VideoArea;
    class ErrorCard;
    void UpdateStats();
    float PaintTopInfo(ui::Painter& p);
    MainWindow* window_;
    ui::Widget* side_ = nullptr;
    bool sideWanted_ = false;  // the user's choice; PiP hides the panel without changing it
    ui::Widget* sidePane_ = nullptr;  // the library pane next to the rail in side_
    ErrorCard* error_ = nullptr;
    VideoArea* video_ = nullptr;
    ui::Widget* music_ = nullptr;
    ui::Widget* pip_ = nullptr;
    NextEpisodeCard* nextEpisode_ = nullptr;
    SkipChapterButton* skipChapter_ = nullptr;
    ControlBar* bar_ = nullptr;
    std::unique_ptr<StatsOverlay> stats_;
    // Thin progress line: shown on the last paint, its width and filled part in pixels.
    bool menuPaused_ = false;  // play/pause as last seen (PV-17: the video menu closes on a change)
    int layoutMode_ = -1;      // fullscreen | hide-UI | PiP bits of the last layout
    bool thinShown_ = false;
    float thinW_ = 0;
    int thinPx_ = -1;
    uint64_t configToken_ = 0;
    ScopedConnection playerConn_, screenConn_, liveConn_;
};

// Built by the player module (MusicView.cpp): cover art, blurred backdrop, titles.
std::unique_ptr<ui::Widget> CreateMusicView();
// Hover controls of Picture in Picture mode (PipOverlay.cpp).
std::unique_ptr<ui::Widget> CreatePipOverlay(MainWindow* window);
// Whether the PiP overlay's controls (its seek bar along the bottom) are shown or coming in.
bool PipOverlayShown(const ui::Widget* overlay);
// The compact right-click menu of Picture in Picture: play, audio, subtitles, speed, back.
void ShowPipContextMenu(ui::Host* host, ui::Point at);

// Library-mode mini player (bottom-right): small video, seek, title, controls on hover.
class MiniPlayer : public ui::Widget {
public:
    explicit MiniPlayer(MainWindow* window);
    ~MiniPlayer() override;
    ui::Rect VideoRect() const;
    ui::Size MeasureContent(float availW, float availH) override;
    void Paint(ui::Painter& p) override;
    void PaintOver(ui::Painter& p) override;
    void OnLayout() override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;

private:
    ui::Rect VideoBox() const;  // video or cover art
    MainWindow* window_;
    ui::Widget* overlay_ = nullptr;
    bool compact_ = false;  // one row (thumbnail, title, play) in a small window
    ui::AnimFloat hover_;
    ScopedConnection playerConn_, imagesConn_;
};

}  // namespace app
