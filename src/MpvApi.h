#pragma once
#include <windows.h>

#include <mpv/client.h>

#include <string>

// libmpv is loaded at runtime with LoadLibrary, so the window can appear
// before the (large) DLL is mapped, and no MinGW import library is needed.
#define LUMEN_MPV_FUNCS(X)      \
    X(mpv_client_api_version)   \
    X(mpv_error_string)         \
    X(mpv_free)                 \
    X(mpv_create)               \
    X(mpv_initialize)           \
    X(mpv_terminate_destroy)    \
    X(mpv_set_option)           \
    X(mpv_set_option_string)    \
    X(mpv_command)              \
    X(mpv_command_async)        \
    X(mpv_set_property_async)   \
    X(mpv_observe_property)     \
    X(mpv_wait_event)           \
    X(mpv_set_wakeup_callback)  \
    X(mpv_free_node_contents)

struct MpvApi {
#define LUMEN_MPV_PTR(name) decltype(&::name) name = nullptr;
    LUMEN_MPV_FUNCS(LUMEN_MPV_PTR)
#undef LUMEN_MPV_PTR

    HMODULE module = nullptr;

    bool Load(const std::wstring& dllPath, std::wstring* error) {
        module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            *error = L"Не найден libmpv-2.dll рядом с Lumen.exe";
            return false;
        }
#define LUMEN_MPV_RESOLVE(name)                                                         \
    name = reinterpret_cast<decltype(name)>(GetProcAddress(module, #name));             \
    if (!name) {                                                                        \
        *error = L"В libmpv-2.dll нет функции ";                                        \
        for (const char* c = #name; *c; ++c) error->push_back(static_cast<wchar_t>(*c)); \
        return false;                                                                   \
    }
        LUMEN_MPV_FUNCS(LUMEN_MPV_RESOLVE)
#undef LUMEN_MPV_RESOLVE
        return true;
    }
};
