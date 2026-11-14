#pragma once
#include "app/Signal.h"
#include "ui/Controls.h"

#include <string>
#include <vector>

namespace app {

class MainWindow;
class ControlBar;
class StatsOverlay;
class NextEpisodeCard;
class SkipChapterButton;
class LibraryPane;
class NavSidebar;
class SubtitleWords;

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
    // Clickable subtitle words on pause (SubWords.h).
    SubtitleWords* Words() const { return words_; }
    // Picture in Picture: the hover controls are up, with their seek bar along the bottom edge.
    bool PipControlsShown() const;

    // Rect where the video is shown (window DIPs); empty when not visible.
    ui::Rect VideoRect() const;
    // The picture's area, also the music view's (window DIPs); empty when the player isn't laid out.
    ui::Rect PictureRect() const;
    // Where a toast centres: the video area left of an open popover, clear of it; in the music view
    // the gap under the cover, and then `top` (else -1) is where the toast's top goes.
    ui::Rect ToastBand(float* top = nullptr) const;

    // The player came before its picture (App::ShowPlayer): the picture area stays black until the
    // opening file's first frame is up, then the picture fades in from it.
    void CoverPicture(bool cover);

    void OnLayout() override;
    float SubtitleBand(float h) const;
    ui::Rect SubtitleRect() const;
    void Paint(ui::Painter& p) override;
    void PaintOver(ui::Painter& p) override;

private:
    class VideoArea;
    class ErrorCard;
    void UpdateStats();
    float PaintTopInfo(ui::Painter& p);
    MainWindow* window_;
    // The fullscreen title panel as last painted, and its full text when it cut the title or the
    // chapter (empty otherwise): the video shows it as a tooltip while the pointer is on it.
    ui::Rect topInfo_;
    std::wstring topTip_;
    ui::Widget* side_ = nullptr;
    bool sideWanted_ = false;  // the user's choice; PiP hides the panel without changing it
    ui::Widget* sidePane_ = nullptr;  // the library pane next to the rail in side_
    ErrorCard* error_ = nullptr;
    VideoArea* video_ = nullptr;
    SubtitleWords* words_ = nullptr;
    ui::Widget* music_ = nullptr;
    ui::Widget* dock_ = nullptr;  // the queue pinned beside the music view (QueueDockSide)
    ui::Widget* pip_ = nullptr;
    NextEpisodeCard* nextEpisode_ = nullptr;
    SkipChapterButton* skipChapter_ = nullptr;
    ControlBar* bar_ = nullptr;
    std::unique_ptr<StatsOverlay> stats_;
    // Thin progress line: shown on the last paint, its width and filled part in pixels.
    bool menuPaused_ = false;  // play/pause as last seen (PV-17: the video menu closes on a change)
    int layoutMode_ = -1;      // fullscreen | hide-UI | PiP bits of the last layout
    bool thinShown_ = false;
    bool covered_ = false;          // CoverPicture: waiting for the first frame
    ui::AnimFloat picture_{1.0f};   // 0 = the picture area is black, 1 = the picture shows
    float thinW_ = 0;
    int thinPx_ = -1;
    uint64_t configToken_ = 0;
    ScopedConnection playerConn_, screenConn_, liveConn_, langConn_;
};

// Built by the player module (MusicView.cpp): cover art, blurred backdrop, titles.
std::unique_ptr<ui::Widget> CreateMusicView();
// Hover controls of Picture in Picture mode (PipOverlay.cpp).
std::unique_ptr<ui::Widget> CreatePipOverlay(MainWindow* window);
// Whether the PiP overlay's controls (its seek bar along the bottom) are shown or coming in.
bool PipOverlayShown(const ui::Widget* overlay);
// The compact right-click menu of Picture in Picture: play, audio, subtitles, speed, back.
void ShowPipContextMenu(ui::Host* host, ui::Point at);
// The player's right-click menu; for music it has the lyrics and pinned-queue items instead of
// the video and subtitle ones.
void ShowPlayerContextMenu(ui::Host* host, ui::Point at);
// The queue pinned beside the music view: "left", "right" or "" (ui.json player.queueDock).
std::string QueueDockSide();
void SetQueueDockSide(const std::string& side);
// Shows the lyrics in the music view again if the Lyrics pill hid them (MusicView.cpp).
void ShowMusicLyrics();
// The music view's cover is decoded (or the file has none); asks for it as a paint would.
bool MusicCoverReady();
// Where a toast goes in the music view: the gap between the cover and the title; empty when hidden.
ui::Rect MusicToastSlot();
// audio.lyricsStyle as radio items (MusicView.cpp).
std::vector<ui::MenuItem> LyricsStyleItems();

// Library-mode mini player (bottom-right): small video, seek, title, controls on hover.
class MiniPlayer : public ui::Widget {
public:
    explicit MiniPlayer(MainWindow* window);
    ~MiniPlayer() override;
    ui::Rect VideoRect() const;
    ui::Size MeasureContent(float availW, float availH) override;
    void Paint(ui::Painter& p) override;
    void OnLayout() override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void Describe(ui::A11y& a) override;
    void DebugInfo(std::string& out) const override;  // " overlay" while its buttons are drawn

private:
    ui::Rect VideoBox() const;  // video or cover art
    float OverlayShown() const;  // 0..1: hovered, or the keyboard is on one of the picture's buttons
    ui::Widget* overlay_ = nullptr;
    ui::IconButton* next_ = nullptr;
    bool compact_ = false;  // one row (thumbnail, title, play) in a small window
    ui::AnimFloat hover_;
    ScopedConnection playerConn_, imagesConn_, queueConn_;
};

}  // namespace app
