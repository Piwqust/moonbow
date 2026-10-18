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

// One writer per data folder. A second Lumen on the same folder (--new-instance, or
// library.singleInstance off) reads it but never writes to it, or the last one to save would
// wipe the other's changes. ClaimDataDir() takes a named mutex for the normalized DataDir()
// (call it once, on the UI thread, after --data-dir is applied); when another process holds it,
// json::WriteFileAtomic refuses files inside the folder. RetryClaimDataDir() (UI thread) takes
// over once the other process has exited and returns true then: reload the stores before saving.
void ClaimDataDir();
bool RetryClaimDataDir();
bool DataDirReadOnly();
bool IsInDataDir(const std::wstring& path);

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
