#include "Trace.h"
#include "app/App.h"
#include "app/Gallery.h"
#include "app/MainWindow.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"

#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {

std::vector<std::wstring> Arguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (!a.empty() && a[0] != L'-' && a.find(L"://") == std::wstring::npos) {
            wchar_t full[MAX_PATH * 4];
            const DWORD n = GetFullPathNameW(a.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
            if (n > 0 && n < std::size(full)) a.assign(full, n);
        }
        args.push_back(std::move(a));
    }
    LocalFree(argv);
    return args;
}

// Hands the files to an already running Lumen window. Returns true if it did.
bool ForwardToRunningInstance(const std::vector<std::wstring>& args) {
    HWND other = nullptr;
    while ((other = FindWindowExW(nullptr, other, L"LumenHost", app::kMainWindowTitle)) != nullptr) {
        if (!GetWindow(other, GW_OWNER)) break;
    }
    if (!other) return false;
    std::wstring payload;
    for (const auto& a : args)
        if (!a.empty() && a[0] != L'-') payload += a + L"\n";
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);
    COPYDATASTRUCT cds{app::kCopyDataOpen, static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t)),
                       const_cast<wchar_t*>(payload.c_str())};
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 2000,
                               &result) != 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Trace("wWinMain");
    const std::vector<std::wstring> args = Arguments();
    const bool gallery = !args.empty() && args[0] == L"--gallery";

    HANDLE mutex = gallery ? nullptr : CreateMutexW(nullptr, FALSE, L"Lumen.SingleInstance.v2");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS && ForwardToRunningInstance(args)) {
        CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int code = 1;
    if (gfx::Gpu::Get().CreateFactories() && gfx::Text::Get().Init(instance))
        code = gallery ? app::RunGallery() : app::App::Get().Run(instance, args);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
