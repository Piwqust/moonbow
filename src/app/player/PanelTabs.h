#pragma once
// Tab bodies of the player popovers (implemented in QueuePanel.cpp, SettingsPanel.cpp and its
// track lists in TracksPanel.cpp); Panels.cpp owns the popover itself.
#include "app/player/ControlBar.h"
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

// The track choice at the top of the Video, Audio and Subtitles tabs: "off", the embedded and
// external tracks (add, find online, edit) and, for audio, the output device. type: video, audio, sub.
void AddTrackLists(ui::Widget* page, const wchar_t* type);

// The live settings popover of a bar button (Settings / Speed: playback, Video, Tracks: audio,
// Subtitles) and its footer, which opens the matching page of the Settings window.
std::unique_ptr<ui::Widget> BuildSettingsPanel(PanelKind kind);
std::unique_ptr<ui::Widget> BuildSettingsFooter(PanelKind kind);

// Rows the mini player queue needs (header + list) to size its popover.
float CompactQueueHeight();

}  // namespace app::panel
