#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <string_view>
#include <vector>

std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(std::wstring_view s);

// "01:02:03" when withHours, "02:03" otherwise.
std::wstring FormatTime(double seconds, bool withHours);

std::wstring FileNameOf(const std::wstring& path);
bool IsMediaFile(const std::wstring& path);

// Media files in the same folder as `path`, sorted like Explorer does.
std::vector<std::wstring> ListSiblingMedia(const std::wstring& path);

// Known folder + subfolder, created if missing. Empty string on failure.
std::wstring KnownFolderPath(REFKNOWNFOLDERID id, const wchar_t* sub);

// Directory that contains Lumen.exe, with trailing backslash.
std::wstring ExeDir();
