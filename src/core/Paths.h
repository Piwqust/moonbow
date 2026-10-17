#pragma once
// Where Lumen keeps its own files (settings.json, library.json, history.json, ...).

#include <string>
#include <string_view>

// %APPDATA%\Lumen, created on first use. No trailing backslash. Empty if the folder is
// unavailable. Thread-safe.
std::wstring DataDir();

// DataDir() + L"\\" + name; empty when DataDir() is.
std::wstring DataFilePath(std::wstring_view name);

// Redirects DataDir() (created if missing). Pass an empty string to go back to %APPDATA%\Lumen.
// Meant for tests, so they never touch the real profile.
void SetDataDirForTests(const std::wstring& dir);
