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
    bool SidePanel() const { return side_ && side_->visible; }
    ControlBar* Bar() const { return bar_; }

    // Rect where the video is shown (window DIPs); empty when not visible.
    ui::Rect VideoRect() const;

    void OnLayout() override;
    void Paint(ui::Painter& p) override;
    void PaintOver(ui::Painter& p) override;

private:
    class VideoArea;
    class ErrorCard;
    void UpdateStats();
    MainWindow* window_;
    ui::Widget* side_ = nullptr;
    ErrorCard* error_ = nullptr;
    VideoArea* video_ = nullptr;
    ui::Widget* music_ = nullptr;
    ui::Widget* pip_ = nullptr;
    NextEpisodeCard* nextEpisode_ = nullptr;
    SkipChapterButton* skipChapter_ = nullptr;
    ControlBar* bar_ = nullptr;
    std::unique_ptr<StatsOverlay> stats_;
    // Thin progress line: shown on the last paint, its width and filled part in pixels.
    bool thinShown_ = false;
    float thinW_ = 0;
    int thinPx_ = -1;
    uint64_t configToken_ = 0;
    ScopedConnection playerConn_, screenConn_;
};

// Built by the player module (MusicView.cpp): cover art, blurred backdrop, titles.
std::unique_ptr<ui::Widget> CreateMusicView();
// Hover controls of Picture in Picture mode (PipOverlay.cpp).
std::unique_ptr<ui::Widget> CreatePipOverlay(MainWindow* window);

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
