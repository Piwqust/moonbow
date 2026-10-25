#pragma once
#include <windows.h>

#include <cstdio>

// Startup timing. Enabled when the MOONBOW_TRACE environment variable is set;
// lines go to %TEMP%\moonbow-trace.txt as "<ms since process start> <label>".
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
        fprintf(f, "%8.1f %s\n", ms, label);
        fclose(f);
    }
}
