#pragma once
#include <windows.h>
#include <psapi.h>

#include <cstdio>

// Startup timing. Enabled when the MOONBOW_TRACE environment variable is set; lines go to
// %TEMP%\moonbow-trace.txt as "<ms since process start> <private MB> <label>". The private bytes
// (K32GetProcessMemoryInfo, no psapi.lib needed) show what each step of the start costs.
inline void Trace(const char* label) {
    static const bool enabled = GetEnvironmentVariableW(L"MOONBOW_TRACE", nullptr, 0) > 0;
    if (!enabled) return;
    FILETIME create, exit, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
    GetSystemTimePreciseAsFileTime(&now);
    auto ticks = [](FILETIME f) { return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    const double ms = static_cast<double>(ticks(now) - ticks(create)) / 10000.0;
    wchar_t path[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"moonbow-trace.txt");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a") == 0 && f) {
        PROCESS_MEMORY_COUNTERS_EX mem{};
        mem.cb = sizeof(mem);
        const double mb = K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),
                                                  sizeof(mem))
                              ? static_cast<double>(mem.PrivateUsage) / (1024.0 * 1024.0)
                              : 0.0;
        fprintf(f, "%8.1f %6.1f %s\n", ms, mb, label);
        fclose(f);
    }
}
