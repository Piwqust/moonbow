#include "Paths.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <atomic>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <mutex>

namespace {

std::mutex g_mutex;
std::wstring g_override;
std::wstring g_cached;
std::vector<std::wstring> g_corrupt;  // guarded by g_mutex
HANDLE g_dataMutex = nullptr;          // UI thread only (a mutex belongs to the thread owning it)
std::atomic<bool> g_dataReadOnly{false};

std::wstring UpperPath(std::wstring s) {
    if (!s.empty()) CharUpperBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

}  // namespace

std::wstring DataDir() {
    std::lock_guard lock(g_mutex);
    if (!g_override.empty()) return g_override;
    if (!g_cached.empty()) return g_cached;

    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &base))) return {};
    std::wstring path = base;
    CoTaskMemFree(base);
    CopyOldDataDir(path + L"\\Lumen", path + L"\\Moonbow");
    path += L"\\Moonbow";
    SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    g_cached = path;
    return path;
}

bool CopyOldDataDir(const std::wstring& from, const std::wstring& to) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(to, ec) || !fs::is_directory(from, ec)) return false;
    const std::wstring tmp = to + L".copy";
    fs::remove_all(tmp, ec);
    fs::copy(from, tmp, fs::copy_options::recursive, ec);
    // Files just written are often held open for a moment (Defender, the indexer), and renaming
    // their folder fails with access denied. A failed move here is never retried (the app then
    // makes an empty data folder), so wait for them: at most ~1 s, only when it fails.
    for (int i = 0; !ec && i < 20; ++i) {
        if (MoveFileW(tmp.c_str(), to.c_str())) return true;
        const DWORD err = GetLastError();
        if (err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) break;
        Sleep(50);
    }
    fs::remove_all(tmp, ec);
    return false;
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

void ClaimDataDir() {
    if (g_dataMutex) return;
    // Per folder, so test instances with their own --data-dir never block each other.
    const std::wstring dir = UpperPath(DataDir());
    if (dir.empty()) return;
    wchar_t name[64];
    swprintf_s(name, L"Moonbow.Data.%016llx", static_cast<unsigned long long>(std::hash<std::wstring>{}(dir)));
    g_dataMutex = CreateMutexW(nullptr, TRUE, name);
    g_dataReadOnly = g_dataMutex && GetLastError() == ERROR_ALREADY_EXISTS;  // not owned then
}

bool RetryClaimDataDir() {
    if (!g_dataReadOnly || !g_dataMutex) return false;
    const DWORD r = WaitForSingleObject(g_dataMutex, 0);
    if (r != WAIT_OBJECT_0 && r != WAIT_ABANDONED) return false;
    g_dataReadOnly = false;
    return true;
}

bool DataDirReadOnly() {
    return g_dataReadOnly;
}

bool IsInDataDir(const std::wstring& path) {
    const std::wstring dir = UpperPath(DataDir());
    return !dir.empty() && path.size() > dir.size() && (path[dir.size()] == L'\\' || path[dir.size()] == L'/') &&
           UpperPath(path.substr(0, dir.size())) == dir;
}

std::wstring DataFileDisplayPath(std::wstring_view name) {
    if (!DataDirOverridden()) return L"%APPDATA%\\Moonbow\\" + std::wstring(name);
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
