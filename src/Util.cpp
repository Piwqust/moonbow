#include "Util.h"

#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring FormatTime(double seconds, bool withHours) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    auto total = static_cast<long long>(seconds);
    long long h = total / 3600, m = (total / 60) % 60, s = total % 60;
    wchar_t buf[32];
    if (withHours)
        swprintf_s(buf, L"%02lld:%02lld:%02lld", h, m, s);
    else
        swprintf_s(buf, L"%lld:%02lld", m + h * 60, s);
    return buf;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool IsMediaFile(const std::wstring& path) {
    static const wchar_t* kExts[] = {
        L".mkv", L".mp4", L".m4v", L".avi", L".mov", L".webm", L".wmv", L".flv", L".ts",  L".m2ts",
        L".mts", L".mpg", L".mpeg", L".vob", L".ogv", L".3gp", L".rmvb", L".hevc", L".av1",
        L".mp3", L".flac", L".m4a", L".aac", L".ogg", L".opus", L".wav", L".wma", L".ac3", L".dts"};
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    for (const wchar_t* e : kExts)
        if (_wcsicmp(ext, e) == 0) return true;
    return false;
}

std::vector<std::wstring> ListSiblingMedia(const std::wstring& path) {
    std::vector<std::wstring> files;
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return files;
    std::wstring dir = path.substr(0, slash + 1);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return files;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring full = dir + fd.cFileName;
        if (IsMediaFile(full)) files.push_back(std::move(full));
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(files.begin(), files.end(),
              [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });
    return files;
}

std::wstring KnownFolderPath(REFKNOWNFOLDERID id, const wchar_t* sub) {
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &base))) return {};
    std::wstring path = base;
    CoTaskMemFree(base);
    if (sub && *sub) {
        path += L'\\';
        path += sub;
        SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    }
    return path;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    std::wstring path(buf, n);
    size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}
