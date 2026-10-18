#pragma once
// WASAPI process loopback (AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK), which the music view's
// visualizer listens through, exists from Windows 10 version 2004 (build 19041). Header-only so
// the settings schema can ask without linking the capture code.

#include <windows.h>

namespace audio {

inline bool ProcessLoopbackSupported() {
    static const bool supported = [] {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto fn = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof v;
        if (!fn || fn(&v) != 0) return false;
        return v.dwMajorVersion > 10 || (v.dwMajorVersion == 10 && v.dwBuildNumber >= 19041);
    }();
    return supported;
}

}  // namespace audio
