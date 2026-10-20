#pragma once
#include "app/Signal.h"
#include "core/Config.h"
#include "ui/Controls.h"

#include <memory>
#include <string>
#include <vector>

namespace app {

// Items that can sit in the player control bar ("Customize Player" dialog).
enum class ControlId : uint8_t {
    Previous, PlayPause, Next, Time, Duration, Volume, Audio, Subtitles, Settings, Playlist,
    Spacer, Divider, Speed, Forward10, Backward10, Forward15, Backward15, Stop, Repeat, Shuffle,
    Record, Snapshot, PictureInPicture, Hdr, Zoom, RightSidebar, Quit, Preferences, GoHome,
    Thumbnail, FileName, ArtistName, FolderName, PlaylistName, Properties, Replay, Chapters,
    Favorite, Bookmark, Bookmarks, Playlists,
    Count,
};

struct ControlInfo {
    ControlId id;
    const char* key;       // stable id stored in settings
    const wchar_t* label;  // English, for the customize dialog
    const char* icon;
    bool removable;        // play/pause and the seek bar always stay
};

const std::vector<ControlInfo>& AllControls();
const ControlInfo& ControlInfoFor(ControlId id);

// Which items sit left and right of the seek bar.
struct ControlLayout {
    std::vector<ControlId> left, right;
    static ControlLayout Default();
    std::string Serialize() const;  // "prev,play,next,time|duration,volume,..."
    static ControlLayout Parse(const std::string& s);
};

// The layout in use; the customize dialog edits it through SetControlLayout.
const ControlLayout& CurrentControlLayout();
void SetControlLayout(const ControlLayout& layout);
Signal<>& ControlLayoutChanged();

// Player popovers anchored above the control bar (implemented in Panels.cpp).
enum class PanelKind : uint8_t { Tracks, Subtitles, Settings, Queue, Chapters, Bookmarks, Speed };
void TogglePlayerPanel(PanelKind kind, const ui::Rect& anchor);
void ClosePlayerPanels();
bool PlayerPanelOpen(PanelKind kind);
// The open popover comes back on the same tab, built again (a language switch, App.cpp).
void ReopenPlayerPanels();

// Creates the widget for one control item.
std::unique_ptr<ui::Widget> CreateControl(ControlId id);

// Seek bar: track, buffered range, chapter ticks, hover time tooltip, drag to seek.
class SeekBar : public ui::Widget {
public:
    SeekBar();
    ~SeekBar() override;
    bool compact = false;  // thin variant (mini player)
    bool slim = false;     // minimal control bar: a thin track that thickens under the pointer
    void Paint(ui::Painter& p) override;
    bool OnMouseDown(const ui::MouseEvent& e) override;
    bool OnMouseMove(const ui::MouseEvent& e) override;
    bool OnMouseUp(const ui::MouseEvent& e) override;
    bool OnWheel(const ui::WheelEvent& e) override;
    bool OnKeyDown(const ui::KeyEvent& e) override;
    void OnMouseLeave() override;
    void PaintOver(ui::Painter& p) override;
    void Describe(ui::A11y& a) override;

private:
    double TimeAt(float x) const;
    ui::Rect Track() const;
    bool dragging_ = false;
    double dragTime_ = 0, dragFrom_ = 0;
    float hoverX_ = -1;
    ScopedConnection conn_, liveConn_;
};

// The bar itself: left items, seek bar, right items. Settings > Interface > "Control bar style":
// floating (rounded, inset over the video when it overlays), full width, or minimal (play, time,
// slim seek bar; 40 DIP).
class ControlBar : public ui::Widget {
public:
    ControlBar();
    ~ControlBar() override;
    void Rebuild();
    void OnLayout() override;
    void Paint(ui::Painter& p) override;
    void PaintOver(ui::Painter& p) override;
    float opacity = 1;      // fades in fullscreen, the whole bar with its controls
    bool floating = false;  // set by the player view: drawn as a rounded panel over the video
    float BarHeight() const { return minimal_ ? 40.0f : 56.0f; }
    bool Minimal() const { return minimal_; }
    bool Classic() const { return classic_; }

private:
    void ReadStyle();
    ScopedConnection layoutConn_, langConn_;
    Config::Token configToken_ = 0;
    bool minimal_ = false, classic_ = false;
};

}  // namespace app
