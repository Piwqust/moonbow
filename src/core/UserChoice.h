#pragma once
// The hash Windows keeps next to a per-user default app (HKCU\...\Explorer\FileExts\<ext>\UserChoice,
// value "Hash"): Explorer drops a choice whose hash doesn't match and falls back to its own default.
// Same algorithm as SetUserFTA and Firefox's default agent; checked against 228 real keys.
// Newer Windows 11 builds may move to UserChoiceLatest, a different hash this does not cover.

#include <cstdint>
#include <string>

// minute: the FILETIME (UTC) of the minute the key was written, seconds cleared.
std::wstring UserChoiceHash(const std::wstring& ext, const std::wstring& sid, const std::wstring& progId,
                            uint64_t minute);
