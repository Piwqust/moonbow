#include "Trace.h"
#include "app/App.h"
#include "app/Gallery.h"
#include "app/MainWindow.h"
#include "core/Paths.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"
#include "ui/Host.h"

#include <objbase.h>
#include <ole2.h>
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
        const bool value = i > 1 && wcscmp(argv[i - 1], L"--scale") == 0;  // a number, not a file
        if (!a.empty() && a[0] != L'-' && a.find(L"://") == std::wstring::npos && !value) {
            wchar_t full[MAX_PATH * 4];
            const DWORD n = GetFullPathNameW(a.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
            if (n > 0 && n < std::size(full)) a.assign(full, n);
        }
        args.push_back(std::move(a));
    }
    LocalFree(argv);
    return args;
}

enum class Forward { NoWindow, Refused, Done };

// The main window: an unowned LumenHost titled "Lumen", or "<file> — Lumen" while something plays
// (an exact-title search missed it then, and a second window opened).
HWND FindMainWindow() {
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"LumenHost", nullptr)) != nullptr) {
        if (GetWindow(w, GW_OWNER)) continue;
        wchar_t title[512];
        const int n = GetWindowTextW(w, title, static_cast<int>(std::size(title)));
        const std::wstring_view t(title, n > 0 ? static_cast<size_t>(n) : 0);
        const std::wstring_view suffix = L" \u2014 Lumen";
        if (t == app::kMainWindowTitle || (t.size() > suffix.size() && t.substr(t.size() - suffix.size()) == suffix))
            return w;
    }
    return nullptr;
}

// Hands the files to an already running Lumen window.
Forward ForwardToRunningInstance(const std::vector<std::wstring>& args) {
    HWND other = FindMainWindow();
    if (!other) return Forward::NoWindow;
    std::wstring payload;
    for (const auto& a : args)
        if (!a.empty() && a[0] != L'-') payload += a + L"\n";
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);
    COPYDATASTRUCT cds{app::kCopyDataOpen, static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t)),
                       const_cast<wchar_t*>(payload.c_str())};
    DWORD_PTR result = 0;
    // A window that is closing answers FALSE: this process then opens the files itself.
    return SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 2000,
                               &result) != 0 &&
                   result != 0
               ? Forward::Done
               : Forward::Refused;
}

// "Only allow one Lumen window" (library.singleInstance). Read straight from the
// settings file: parsing the whole config here would cost startup time.
bool SingleInstanceDisabled() {
    const std::wstring path = DataFilePath(L"settings.json");
    if (path.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::string text(1 << 16, ' ');
    DWORD read = 0;
    ReadFile(f, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(f);
    text.resize(read);
    const size_t at = text.find("\"library.singleInstance\"");
    if (at == std::string::npos) return false;
    const size_t value = text.find_first_not_of(" \t\r\n:", at + 24);
    return value != std::string::npos && text.compare(value, 5, "false") == 0;
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
    // --scale <factor> (--automation only): lay out as if at factor x the monitor's scale;
    // 1.5 or 150 both mean 150 % on a 100 % screen.
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == L"--scale" && ui::Host::Automation()) {
            float f = wcstof(args[i + 1].c_str(), nullptr);
            if (f > 4.0f) f /= 100.0f;
            if (f > 0) ui::Host::SetScaleOverride(f);
        }
    std::vector<std::wstring> files;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--data-dir" || args[i] == L"--scale") { ++i; continue; }
        if (!args[i].empty() && args[i][0] != L'-') files.push_back(args[i]);
    }

    if (!newInstance && SingleInstanceDisabled()) newInstance = true;
    HANDLE mutex = newInstance ? nullptr : CreateMutexW(nullptr, FALSE, L"Lumen.SingleInstance.v2");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // Explorer starts one process per file when several are opened: the first may not have
        // its window yet. Wait for it (up to ~3 s) instead of opening a window of our own.
        Forward f = ForwardToRunningInstance(files);
        for (int i = 0; f == Forward::NoWindow && i < 30; ++i) {
            Sleep(100);
            f = ForwardToRunningInstance(files);
        }
        if (f == Forward::Done) {
            CloseHandle(mutex);
            return 0;
        }
    }
    // Always, also with --new-instance: two writers on one data folder wipe each other's saves.
    if (!gallery) ClaimDataDir();

    // The GPU driver takes ~150 ms to load: start it now, alongside OLE, fonts, settings and the
    // window, instead of after the window is shown.
    if (!gallery) gfx::Gpu::Get().StartDevice();

    // OLE (STA COM + drag and drop targets).
    OleInitialize(nullptr);
    int code = 1;
    if (gfx::Gpu::Get().CreateFactories()) {
        gfx::Text::Get().InitAsync(instance);
        code = gallery ? app::RunGallery() : app::App::Get().Run(instance, files);
    }
    OleUninitialize();
    if (mutex) CloseHandle(mutex);
    Trace("close: wWinMain returns");
    return code;
}
