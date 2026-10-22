#pragma once
#include <string>
#include <vector>

namespace setup {

// The non-player modes of Lumen.exe; false when this launch is the player.
//   Setup.exe [--silent] [--dir <folder>] [--no-system] [--restore-data]
//                                         install, update, roll back or reinstall (a Lumen.exe with a package)
//   Lumen.exe --uninstall [--silent] [--remove-data]
//   Lumen.exe --switch-version <version> [--silent] [--restore-data]
//   Lumen.exe --make-setup <out.exe> <file>...   builds a Setup.exe from this Lumen.exe and the files
// --no-system (tests): no uninstall entry, shortcut or file types.
// For now it asks with plain message boxes; the setup's own screens replace them.
bool RunSetupMode(const std::vector<std::wstring>& args, int* exitCode);

}  // namespace setup
