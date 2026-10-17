#include "App.h"
#include "Trace.h"

#include <objbase.h>
#include <shellapi.h>

#include <string>

namespace {

std::wstring InitialFileFromCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring file;
    if (argv && argc > 1) {
        file = argv[1];
        if (file.find(L"://") == std::wstring::npos) {
            wchar_t full[MAX_PATH * 4];
            DWORD n = GetFullPathNameW(file.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
            if (n > 0 && n < std::size(full)) file.assign(full, n);
        }
    }
    LocalFree(argv);
    return file;
}

// Hands the file to an already running Lumen window. Returns true if it did.
bool ForwardToRunningInstance(const std::wstring& file) {
    HWND other = FindWindowW(kMainWindowClass, nullptr);
    if (!other) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);
    COPYDATASTRUCT cds{kCopyDataOpen, static_cast<DWORD>((file.size() + 1) * sizeof(wchar_t)),
                       const_cast<wchar_t*>(file.c_str())};
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 2000,
                               &result) != 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Trace("wWinMain");
    const std::wstring file = InitialFileFromCommandLine();

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Lumen.SingleInstance.v1");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS && ForwardToRunningInstance(file)) {
        CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int code = 0;
    {
        App app;
        code = app.Run(instance, file);
    }
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
