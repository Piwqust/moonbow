#pragma once
// Where Lumen keeps its own files (settings.json, library.json, history.json, ...).

#include <string>
#include <string_view>
#include <vector>

// %APPDATA%\Lumen, created on first use. No trailing backslash. Empty if the folder is
// unavailable. Thread-safe.
std::wstring DataDir();

// DataDir() + L"\\" + name; empty when DataDir() is.
std::wstring DataFilePath(std::wstring_view name);

// Redirects DataDir() (created if missing). Pass an empty string to go back to %APPDATA%\Lumen.
// Meant for tests, so they never touch the real profile.
void SetDataDirForTests(const std::wstring& dir);

// True when --data-dir (or a test) moved the data folder away from %APPDATA%\Lumen.
bool DataDirOverridden();

// How to show a data file to the user: "%APPDATA%\Lumen\<name>" for the default folder (short
// and stable), the real path when the folder was moved.
std::wstring DataFileDisplayPath(std::wstring_view name);

// Copies a file that could not be read (bad JSON) to "<stem>.corrupt-YYYYMMDD-HHMMSS<ext>" next to
// it, so the next save cannot destroy what is still recoverable, and remembers its name for
// TakeCorruptFileNotices(). Returns the backup path, or empty when the copy failed. Thread-safe.
std::wstring BackupCorruptFile(const std::wstring& path);

// File names (e.g. L"history.json") backed up since the last call, so the app can say once that
// they were reset. Thread-safe.
std::vector<std::wstring> TakeCorruptFileNotices();
