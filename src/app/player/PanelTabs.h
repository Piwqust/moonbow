#pragma once
// Tab bodies of the player popovers (implemented in QueuePanel.cpp, TracksPanel.cpp and
// SettingsPanel.cpp); Panels.cpp owns the popover itself.
#include "ui/Widget.h"

#include <memory>

namespace app::panel {

class Popover;

// Queue / Bookmarks / Chapters. `compact` is the mini player variant (queue only).
std::unique_ptr<ui::Widget> BuildQueueTab(bool compact);
std::unique_ptr<ui::Widget> BuildBookmarksTab();
std::unique_ptr<ui::Widget> BuildChaptersTab();

// Tracks: 0 video, 1 audio, 2 subtitles.
std::unique_ptr<ui::Widget> BuildTracksTab(int tab);
std::unique_ptr<ui::Widget> BuildTracksFooter(Popover* owner);

// Live settings: 0 playback, 1 video, 2 audio, 3 subtitles.
std::unique_ptr<ui::Widget> BuildSettingsTab(int tab);
std::unique_ptr<ui::Widget> BuildSettingsFooter(Popover* owner);

// Rows the mini player queue needs (header + list) to size its popover.
float CompactQueueHeight();

// True for a moment after a popover closed because of a click outside it, so the click
// that closed it does not also pause the video.
bool PanelJustClosed();

}  // namespace app::panel
