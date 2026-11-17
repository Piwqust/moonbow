#pragma once

namespace app {

// Settings > Interface > Separate windows (advanced): the player's controls and the audio meters
// in windows of their own, opened and closed by interface.controlsWindow / interface.metersWindow.
// Called once after the main window exists; it opens what the settings say and follows them.
void InitDetachedWindows();

}  // namespace app
