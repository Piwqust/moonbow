#pragma once
// Tab bodies of the player popovers (implemented in QueuePanel.cpp, TracksPanel.cpp and
// SettingsPanel.cpp); Panels.cpp owns the popover itself.
#include "ui/Widget.h"

#include <memory>

namespace ui {
class IconButton;
}

namespace app::panel {

class Popover;

// Queue / Bookmarks / Chapters. `compact` is the mini player variant (queue only).
// covers: the queue pinned beside the music view (cover art, two-line rows, an unpin button).
std::unique_ptr<ui::Widget> BuildQueueTab(bool compact, bool covers = false);
std::unique_ptr<ui::Widget> BuildBookmarksTab();
std::unique_ptr<ui::Widget> BuildChaptersTab();
// The Repeat button that tells One from All (a small "1"), at the control bar's size.
std::unique_ptr<ui::IconButton> MakeRepeatButton();

// Tracks: 0 video, 1 audio, 2 subtitles.
std::unique_ptr<ui::Widget> BuildTracksTab(int tab);
std::unique_ptr<ui::Widget> BuildTracksFooter(Popover* owner);

// Live settings: 0 playback, 1 video, 2 audio, 3 subtitles.
std::unique_ptr<ui::Widget> BuildSettingsTab(int tab);
std::unique_ptr<ui::Widget> BuildSettingsFooter(Popover* owner);

// Rows the mini player queue needs (header + list) to size its popover.
float CompactQueueHeight();

}  // namespace app::panel
