#pragma once
// Where Moonbow keeps its own files (settings.json, library.json, history.json, ...).

#include <string>
#include <string_view>
#include <vector>

// %APPDATA%\Moonbow, created on first use. No trailing backslash. Empty if the folder is
// unavailable. Thread-safe. The first time, it starts as a copy of %APPDATA%\Lumen (the old name).
std::wstring DataDir();

// Copies the folder `from` to `to` when `to` doesn't exist yet and `from` does. The copy is made
// next to `to` and renamed into place, so a failed copy leaves no half folder and is retried next
// time. `from` is never changed: a kept Lumen still finds its own data. True when `to` was made.
bool CopyOldDataDir(const std::wstring& from, const std::wstring& to);

// DataDir() + L"\\" + name; empty when DataDir() is.
std::wstring DataFilePath(std::wstring_view name);

// Redirects DataDir() (created if missing). Pass an empty string to go back to %APPDATA%\Moonbow.
// Meant for tests, so they never touch the real profile.
void SetDataDirForTests(const std::wstring& dir);

// True when --data-dir (or a test) moved the data folder away from %APPDATA%\Moonbow.
bool DataDirOverridden();

// One writer per data folder. A second Moonbow on the same folder (--new-instance, or
// library.singleInstance off) reads it but never writes to it, or the last one to save would
// wipe the other's changes. ClaimDataDir() takes a named mutex for the normalized DataDir()
// (call it once, on the UI thread, after --data-dir is applied); when another process holds it,
// json::WriteFileAtomic refuses files inside the folder. RetryClaimDataDir() (UI thread) takes
// over once the other process has exited and returns true then: reload the stores before saving.
void ClaimDataDir();
bool RetryClaimDataDir();
bool DataDirReadOnly();
bool IsInDataDir(const std::wstring& path);

// How to show a data file to the user: "%APPDATA%\Moonbow\<name>" for the default folder (short
// and stable), the real path when the folder was moved.
std::wstring DataFileDisplayPath(std::wstring_view name);

// Copies a file that could not be read (bad JSON) to "<stem>.corrupt-YYYYMMDD-HHMMSS<ext>" next to
// it, so the next save cannot destroy what is still recoverable, and remembers its name for
// TakeCorruptFileNotices(). Returns the backup path, or empty when the copy failed. Thread-safe.
std::wstring BackupCorruptFile(const std::wstring& path);

// File names (e.g. L"history.json") backed up since the last call, so the app can say once that
// they were reset. Thread-safe.
std::vector<std::wstring> TakeCorruptFileNotices();
