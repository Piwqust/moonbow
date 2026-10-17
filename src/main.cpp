#include "Trace.h"
#include "app/App.h"
#include "app/Gallery.h"
#include "app/MainWindow.h"
#include "core/Paths.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"
#include "ui/Host.h"

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
    // Developer flags: --new-instance (skip single-instance hand-off) and
    // --data-dir <dir> (settings, history... somewhere other than %APPDATA%\Lumen).
    bool newInstance = gallery;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--new-instance") newInstance = true;
        if (args[i] == L"--automation") ui::Host::SetAutomation(true);
        if (args[i] == L"--data-dir" && i + 1 < args.size()) SetDataDirForTests(args[i + 1]);
    }
    std::vector<std::wstring> files;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--data-dir") { ++i; continue; }
        if (!args[i].empty() && args[i][0] != L'-') files.push_back(args[i]);
    }

    HANDLE mutex = newInstance ? nullptr : CreateMutexW(nullptr, FALSE, L"Lumen.SingleInstance.v2");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS && ForwardToRunningInstance(files)) {
        CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int code = 1;
    if (gfx::Gpu::Get().CreateFactories() && gfx::Text::Get().Init(instance))
        code = gallery ? app::RunGallery() : app::App::Get().Run(instance, files);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
