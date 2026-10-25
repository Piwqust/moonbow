#pragma once
#include <string>
#include <vector>

namespace setup {

// The non-player modes of Moonbow.exe; false when this launch is the player.
//   Setup.exe [--silent] [--dir <folder>] [--no-system] [--restore-data]
//                                         install, update, roll back or reinstall (a Moonbow.exe with a package)
//   Moonbow.exe --uninstall [--silent] [--remove-data]
//   Moonbow.exe --switch-version <version> [--silent] [--restore-data]
//   Moonbow.exe --make-setup <out.exe> <file>...   builds a Setup.exe from this Moonbow.exe and the files
// --no-system (tests): no uninstall entry, shortcut or file types. Without --silent the setup's
// window (SetupWindow.h) asks and shows the work; --automation --setup-page <name> shows one page.
bool RunSetupMode(const std::vector<std::wstring>& args, int* exitCode);

}  // namespace setup
