#include "app/CrashReport.h"

#include "core/Log.h"

#include <windows.h>

#include <crtdbg.h>
#include <dbghelp.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

namespace app {

namespace {

constexpr DWORD kAbort = 0xE04C0001;  // ours: std::terminate, abort, pure call, invalid parameter
constexpr size_t kKeep = 5;

// Filled at install: the handler must not allocate (the heap may be what broke).
wchar_t g_dir[MAX_PATH * 2];
wchar_t g_version[32];
char g_note[512];  // the failed check's message (debug builds), empty otherwise
EXCEPTION_POINTERS* g_ep = nullptr;
DWORD g_thread = 0;

void WriteText(const wchar_t* path, const char* text) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD done = 0;
    WriteFile(f, text, static_cast<DWORD>(strlen(text)), &done, nullptr);
    CloseHandle(f);
}

DWORD WINAPI WriteReport(void*) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[40];
    swprintf_s(name, L"crash-%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    wchar_t path[MAX_PATH * 2 + 64];

    swprintf_s(path, L"%s\\%s.dmp", g_dir, name);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{g_thread, g_ep, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                          g_ep ? &info : nullptr, nullptr, nullptr);
        CloseHandle(f);
    }

    const EXCEPTION_RECORD* r = g_ep ? g_ep->ExceptionRecord : nullptr;
    const DWORD code = r ? r->ExceptionCode : 0;
    void* at = r ? r->ExceptionAddress : nullptr;
    HMODULE module = nullptr;
    wchar_t modulePath[MAX_PATH] = L"?";
    if (at && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 static_cast<LPCWSTR>(at), &module))
        GetModuleFileNameW(module, modulePath, MAX_PATH);
    const wchar_t* slash = wcsrchr(modulePath, L'\\');
    char text[512];
    snprintf(text, sizeof text,
             "Lumen %ls crashed.\r\n"
             "Exception 0x%08lX%s at %ls+0x%llX, thread %lu.\r\n%s%s"
             "The .dmp file next to this one has the stacks; open it in WinDbg with Lumen.pdb of this build.\r\n",
             g_version, code, code == kAbort ? " (std::terminate, abort or a CRT check)" : "",
             slash ? slash + 1 : modulePath,
             static_cast<unsigned long long>(static_cast<BYTE*>(at) - reinterpret_cast<BYTE*>(module)), g_thread,
             g_note[0] ? "Check: " : "", g_note);
    swprintf_s(path, L"%s\\%s.txt", g_dir, name);
    WriteText(path, text);

    // For TakeCrashNotice() at the next start.
    char marker[40];
    snprintf(marker, sizeof marker, "%ls", name);
    swprintf_s(path, L"%s\\crashed", g_dir);
    WriteText(path, marker);
    return 0;
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) Sleep(INFINITE);  // another thread is writing; it ends the process
    g_ep = ep;
    g_thread = GetCurrentThreadId();
    // On a thread of its own: after a stack overflow this one has almost no stack left.
    if (HANDLE t = CreateThread(nullptr, 0, WriteReport, nullptr, 0, nullptr)) {
        WaitForSingleObject(t, 60000);
        CloseHandle(t);
    }
    TerminateProcess(GetCurrentProcess(), ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : kAbort);
    return EXCEPTION_EXECUTE_HANDLER;
}

// No C++ objects here: __try cannot share a function with destructors.
void Abort() {
    __try {
        RaiseException(kAbort, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    } __except (OnCrash(GetExceptionInformation())) {
    }
    TerminateProcess(GetCurrentProcess(), kAbort);
}

}  // namespace

void InstallCrashHandler(const std::wstring& version) {
#ifdef __SANITIZE_ADDRESS__
    return;  // AddressSanitizer reports crashes itself (ASAN_OPTIONS=log_path=...)
#endif
    const std::wstring dir = LogDir();
    if (dir.empty()) return;
    wcsncpy_s(g_dir, dir.c_str(), _TRUNCATE);
    // Test runs that wipe their data folders (tools\dev\sweep.ps1) collect reports elsewhere.
    if (wchar_t custom[MAX_PATH]; GetEnvironmentVariableW(L"LUMEN_CRASH_DIR", custom, MAX_PATH) - 1u < MAX_PATH - 1u)
        wcsncpy_s(g_dir, custom, _TRUNCATE);
    wcsncpy_s(g_version, version.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(OnCrash);
    std::set_terminate([] { Abort(); });
    _set_purecall_handler([] { Abort(); });
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { Abort(); });
    signal(SIGABRT, [](int) { Abort(); });
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    // A failed CRT or STL check (index out of range, heap corruption) becomes a crash report with its
    // message instead of a dialog that nobody sees in a test run.
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, [](int type, char* message, int*) -> int {
        if (type == _CRT_WARN) return FALSE;
        strncpy_s(g_note, message ? message : "", _TRUNCATE);
        Abort();
        return TRUE;
    });
#endif
}

std::wstring TakeCrashNotice() {
    const std::wstring dir = LogDir();
    if (dir.empty()) return {};
    std::vector<std::wstring> reports;  // "crash-YYYYMMDD-HHMMSS": sorting by name is sorting by time
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\crash-*.txt").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do reports.emplace_back(fd.cFileName, wcslen(fd.cFileName) - 4);
        while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(reports.begin(), reports.end());
    for (size_t i = 0; i + kKeep < reports.size(); ++i) {
        DeleteFileW((dir + L"\\" + reports[i] + L".txt").c_str());
        DeleteFileW((dir + L"\\" + reports[i] + L".dmp").c_str());
    }

    const std::wstring marker = dir + L"\\crashed";
    FILE* f = nullptr;
    if (_wfopen_s(&f, marker.c_str(), L"rb") != 0 || !f) return {};
    char name[40] = {};
    fread(name, 1, sizeof name - 1, f);
    fclose(f);
    DeleteFileW(marker.c_str());
    std::wstring wide(name, name + strlen(name));  // ASCII by construction
    return wide.empty() ? std::wstring() : dir + L"\\" + wide + L".txt";
}

void CrashForTest(bool terminate) {
    if (terminate) std::terminate();
    *static_cast<volatile int*>(nullptr) = 0;
}

}  // namespace app
