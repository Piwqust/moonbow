#pragma once
// Tab bodies of the player popovers (implemented in QueuePanel.cpp, SettingsPanel.cpp and its
// track lists in TracksPanel.cpp); Panels.cpp owns the popover itself.
#include "app/player/ControlBar.h"
#include "ui/Widget.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {
class IconButton;
}

namespace app::panel {

class Popover;

// Queue / Bookmarks / Chapters. `compact` is the mini player variant (queue only).
// covers: the queue pinned beside the music view (cover art, two-line rows, an unpin button);
// docked = false in a window of its own, where there is nothing to unpin.
std::unique_ptr<ui::Widget> BuildQueueTab(bool compact, bool covers = false, bool docked = true);
std::unique_ptr<ui::Widget> BuildBookmarksTab();
std::unique_ptr<ui::Widget> BuildChaptersTab();
// The Repeat button that tells One from All (a small "1"), at the control bar's size.
std::unique_ptr<ui::IconButton> MakeRepeatButton();

// A choice in a radio list (TracksPanel.cpp): the name, an optional line of detail under it, and
// for an added track a way to remove it.
struct ChoiceRow {
    std::wstring label;
    std::wstring detail;
    bool checked = false;
    std::function<void()> select;
    std::function<void()> remove;
};

// The track choice at the top of the Video, Audio and Subtitles panels: off, the file's tracks,
// the added ones, and rows to add a file (and find subtitles online). type: video, audio, sub.
void AddTrackLists(ui::Widget* page, const wchar_t* type);
// A radio list over `rows` (read again whenever the player or the live values change).
ui::Widget* AddChoiceList(ui::Widget* page, std::function<std::vector<ChoiceRow>()> rows, std::wstring name = {});
// The output devices as choices ("System default" first).
std::vector<ChoiceRow> OutputDeviceRows();

// The live settings popover of a bar button (Settings / Speed: playback, Video, Tracks: audio,
// Subtitles), page 0 its list and the others the pages its rows open, and its footer, which
// opens the matching page of the Settings window.
std::unique_ptr<ui::Widget> BuildSettingsPanel(PanelKind kind, int page);
std::wstring SettingsPageTitle(PanelKind kind, int page);
// Opens an inner page of the open settings panel (Panels.cpp).
void ShowPanelPage(int page);
// The inner pages of the playback panel (PanelKind::Settings).
enum PlaybackPage { kPlaySpeed = 1, kPlayRepeat, kPlaySleep, kPlayLoop, kPlayRemembered };
// What the playback panel's Speed and Sleep timer rows say ("Normal", "Off"), for the menu.
std::wstring SpeedValue();
std::wstring SleepValue();
std::unique_ptr<ui::Widget> BuildSettingsFooter(PanelKind kind);

// Rows the mini player queue needs (header + list) to size its popover.
float CompactQueueHeight();

}  // namespace app::panel
