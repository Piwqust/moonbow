#include "Log.h"
#include "Paths.h"

#include <windows.h>

#include <share.h>

#include <cstdio>
#include <mutex>

namespace {

constexpr long long kMaxBytes = 4 << 20;

std::mutex g_mutex;
FILE* g_file = nullptr;
long long g_written = 0;

}  // namespace

std::wstring LogDir() {
    std::wstring dir = DataDir();
    if (dir.empty()) return dir;
    dir += L"\\logs";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void OpenLog(std::wstring_view version) {
    if (DataDirReadOnly()) return;
    const std::wstring dir = LogDir();
    if (dir.empty()) return;
    const std::wstring path = dir + L"\\lumen.log";
    {
        std::lock_guard lock(g_mutex);
        if (g_file) return;
        MoveFileExW(path.c_str(), (dir + L"\\lumen.1.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        g_file = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);  // others can read it while Lumen runs
        g_written = 0;
    }
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof os;
    if (rtlGetVersion) rtlGetVersion(&os);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[160];
    snprintf(line, sizeof line, "Lumen %ls, Windows %lu.%lu.%lu, %04u-%02u-%02u", std::wstring(version).c_str(),
             os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, t.wYear, t.wMonth, t.wDay);
    Log(line);
}

void Log(std::string_view line) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[16];
    const int n = snprintf(stamp, sizeof stamp, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    std::lock_guard lock(g_mutex);
    if (!g_file || g_written > kMaxBytes) return;
    fwrite(stamp, 1, n, g_file);
    fwrite(line.data(), 1, line.size(), g_file);
    fputs("\r\n", g_file);
    g_written += n + static_cast<long long>(line.size()) + 2;
    if (g_written > kMaxBytes) fputs("... 4 MB reached, the rest of this session is not logged\r\n", g_file);
    fflush(g_file);  // a crash right after must not lose the line
}

void CloseLog() {
    std::lock_guard lock(g_mutex);
    if (g_file) fclose(g_file);
    g_file = nullptr;
}
