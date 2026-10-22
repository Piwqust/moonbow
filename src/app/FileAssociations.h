#pragma once

namespace app {

// Registers Lumen for the current user (HKCU, no admin rights): ProgIDs for
// video/audio/playlist types, "Open with" entries and a RegisteredApplications
// entry so Lumen appears in Settings > Apps > Default apps. Windows 10/11 don't
// let apps take defaults themselves, so OpenDefaultAppsSettings() opens the
// page where the user confirms.
bool RegisterFileAssociations();
// Removes everything RegisterFileAssociations() and SetDefaultPlayer() wrote (the uninstaller runs it).
bool UnregisterFileAssociations();
// Makes Lumen the default for every registered type by writing Windows' own per-user choice
// (core/UserChoice.h), as SetUserFTA does. The owner chose this over Settings knowing Windows
// may stop accepting it; false when Explorer still opens .mp4/.mp3 elsewhere afterwards.
// Call after RegisterFileAssociations().
bool SetDefaultPlayer();
void OpenDefaultAppsSettings();

}  // namespace app
