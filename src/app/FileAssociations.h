#pragma once

namespace app {

// Registers Lumen for the current user (HKCU, no admin rights): ProgIDs for
// video/audio/playlist types, "Open with" entries and a RegisteredApplications
// entry so Lumen appears in Settings > Apps > Default apps. Windows 10/11 don't
// let apps take defaults themselves, so OpenDefaultAppsSettings() opens the
// page where the user confirms.
bool RegisterFileAssociations();
// Removes everything RegisterFileAssociations() wrote (the uninstaller runs it).
bool UnregisterFileAssociations();
void OpenDefaultAppsSettings();

}  // namespace app
