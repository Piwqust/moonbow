#pragma once
// Installs, switches and removes Lumen versions for one user, no admin rights.
//
// <dir>\Lumen.exe, libmpv-2.dll...   the current version: shortcuts, pins, file types and the jump list
//                                    point here, so they never change across versions
// <dir>\versions\<version>\          every kept version, current included; files equal across versions
//                                    (libmpv) are hard links of one file, as the current files are
// DataDir()\backups\<version>\       the settings files (*.json) as they were when that version was left
//
// Switching first fills versions\<new>, then moves its files over the current ones one by one; a
// failure puts the previous version back. Lumen must be closed (CloseRunningLumen).

#include "Package.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace setup {

struct Target {
    std::wstring dir;     // install folder
    bool system = true;   // uninstall entry, Start menu shortcut, file types; tests turn it off
};

// Where Lumen is installed (the uninstall entry's folder), else %LOCALAPPDATA%\Programs\Lumen.
std::wstring DefaultInstallDir();

// The version of <dir>\Lumen.exe; empty when there is none.
std::wstring InstalledVersion(const std::wstring& dir);
// Versions in <dir>\versions, newest first.
std::vector<std::wstring> StoredVersions(const std::wstring& dir);

// <0, 0, >0 like strcmp, by numbers: "0.10.0" > "0.9.2".
int CompareVersions(std::wstring_view a, std::wstring_view b);

enum class Action { Install, Update, Rollback, Reinstall };
Action Classify(const std::wstring& installed, const std::wstring& incoming);

// 0..1 while files are written.
using Progress = std::function<void(double)>;

// Installs the package's version over whatever is in target.dir (any version, older or newer).
bool Install(const Package& package, const Target& target, const Progress& progress, std::wstring* error);
// Makes a kept version current again (rollback, or forward after one).
bool SwitchVersion(const Target& target, const std::wstring& version, std::wstring* error);
// Removes the program; the data folder too when removeData.
bool Uninstall(const Target& target, bool removeData, std::wstring* error);

// Asks every Lumen running from `dir` to close and waits up to `timeoutMs`; true when none is left.
bool CloseRunningLumen(const std::wstring& dir, unsigned timeoutMs);
bool LumenRunning(const std::wstring& dir);

// Settings files of DataDir() copied to / back from backups\<version>.
bool SnapshotData(const std::wstring& version);
bool RestoreData(const std::wstring& version);
bool HasDataSnapshot(const std::wstring& version);

// Runs `exe args` and waits (up to 30 s); its exit code, -1 when it did not start.
int RunAndWait(const std::wstring& exe, const std::wstring& args);

// Kept besides the current version.
constexpr size_t kKeptOldVersions = 2;

}  // namespace setup
