#pragma once
// The player's parts in windows of their own: any of them (the controls, the seek bar, the cover,
// what plays, the lyrics, the queue, the visualization, the audio meters together or each meter
// alone) opens in a separate window, a copy: the main window keeps its own. The windows are tool
// windows on their own, so they stay on screen when the main one is minimized, and Moonbow puts
// them back where they were at the next start (windows.json in the data folder).
//
// The controls and the meters also follow their Settings rows (Settings > Interface > Separate
// windows: interface.controlsWindow, interface.metersWindow); closing one with X turns its row off.

#include <string>
#include <vector>

namespace ui {
class Host;
struct MenuItem;
}  // namespace ui

namespace app {

// Called once after the main window exists: opens what was open last time and follows the settings.
void InitDetachedWindows();
// The app is quitting: remembers which windows are open and where, then closes them.
void CloseDetachedWindows();

// Panel ids: "controls", "seekbar", "cover", "nowplaying", "lyrics", "queue", "viz", "meters",
// "vu", "vu.left", "vu.right", "loudness", "levels", "stereo", "spectrum".
bool PanelWindowOpen(const std::string& id);
void TogglePanelWindow(const std::string& id);
// Opens or closes a panel's window; false for an unknown id or one this Windows can't show (the
// meters need process loopback, Windows 10 2004).
bool SetPanelWindow(const std::string& id, bool open);
// "id open|closed" per panel, for test runs.
std::string PanelWindowsState();

// The "Separate windows" submenu: every panel with a tick when its window is open.
std::vector<ui::MenuItem> PanelWindowItems();
// A right-click menu inside a panel window ends with the window's entries (keep above other
// windows, title bar, close); other windows get nothing.
void AppendPanelWindowMenu(ui::Host* host, std::vector<ui::MenuItem>& items);

}  // namespace app
