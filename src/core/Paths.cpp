#include "Paths.h"

#include <windows.h>
#include <shlobj.h>

#include <mutex>

namespace {

std::mutex g_mutex;
std::wstring g_override;
std::wstring g_cached;

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
