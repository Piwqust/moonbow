#include "Paths.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cwchar>
#include <mutex>

namespace {

std::mutex g_mutex;
std::wstring g_override;
std::wstring g_cached;
std::vector<std::wstring> g_corrupt;  // guarded by g_mutex

}  // namespace

std::wstring DataDir() {
    std::lock_guard lock(g_mutex);
    if (!g_override.empty()) return g_override;
    if (!g_cached.empty()) return g_cached;

    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &base))) return {};
    std::wstring path = base;
    CoTaskMemFree(base);
    path += L"\\Lumen";
    SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    g_cached = path;
    return path;
}

std::wstring DataFilePath(std::wstring_view name) {
    std::wstring dir = DataDir();
    if (dir.empty()) return {};
    dir += L'\\';
    dir += name;
    return dir;
}

void SetDataDirForTests(const std::wstring& dir) {
    std::lock_guard lock(g_mutex);
    g_override = dir;
    while (!g_override.empty() && (g_override.back() == L'\\' || g_override.back() == L'/')) g_override.pop_back();
    if (!g_override.empty()) SHCreateDirectoryExW(nullptr, g_override.c_str(), nullptr);
}

bool DataDirOverridden() {
    std::lock_guard lock(g_mutex);
    return !g_override.empty();
}

std::wstring DataFileDisplayPath(std::wstring_view name) {
    if (!DataDirOverridden()) return L"%APPDATA%\\Lumen\\" + std::wstring(name);
    std::wstring full = DataFilePath(name);
    // Short forms keep the file name visible in a narrow field: %TEMP%\..., %USERPROFILE%\...
    wchar_t temp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, temp);
    if (n > 1 && n < MAX_PATH) {
        std::wstring t(temp, n);
        if (t.back() == L'\\') t.pop_back();
        if (full.size() > t.size() && full[t.size()] == L'\\' &&
            CompareStringOrdinal(full.c_str(), static_cast<int>(t.size()), t.c_str(), static_cast<int>(t.size()), TRUE) ==
                CSTR_EQUAL)
            return L"%TEMP%" + full.substr(t.size());
    }
    wchar_t shortPath[MAX_PATH];
    if (full.size() < MAX_PATH && PathUnExpandEnvStringsW(full.c_str(), shortPath, MAX_PATH)) return shortPath;
    return full;
}

std::wstring BackupCorruptFile(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    const bool hasExt = dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
    const std::wstring stem = hasExt ? path.substr(0, dot) : path;
    const std::wstring ext = hasExt ? path.substr(dot) : std::wstring();
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf_s(stamp, L".corrupt-%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond);
    // Two failures within one second (or a clock going back) must not overwrite an earlier copy.
    for (int i = 1; i <= 20; ++i) {
        std::wstring backup = stem + stamp + (i > 1 ? L"-" + std::to_wstring(i) : std::wstring()) + ext;
        if (CopyFileW(path.c_str(), backup.c_str(), TRUE)) {
            std::lock_guard lock(g_mutex);
            g_corrupt.push_back(slash == std::wstring::npos ? path : path.substr(slash + 1));
            return backup;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) break;
    }
    return {};
}

std::vector<std::wstring> TakeCorruptFileNotices() {
    std::lock_guard lock(g_mutex);
    std::vector<std::wstring> out;
    out.swap(g_corrupt);
    return out;
}
