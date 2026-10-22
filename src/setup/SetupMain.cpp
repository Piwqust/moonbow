#include "setup/SetupMain.h"

#include "app/I18n.h"
#include "setup/Installer.h"
#include "setup/Package.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>

namespace setup {
namespace {

using app::Fmt;
using app::Tr;

bool Has(const std::vector<std::wstring>& args, const wchar_t* flag) {
    return std::find(args.begin(), args.end(), flag) != args.end();
}

std::wstring Value(const std::vector<std::wstring>& args, const wchar_t* flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    return it != args.end() && it + 1 != args.end() ? *(it + 1) : std::wstring();
}

std::wstring SelfPath() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return std::wstring(buf, n);
}

std::wstring FolderOf(const std::wstring& path) {
    return path.substr(0, path.find_last_of(L"\\/"));
}

bool Ask(const std::wstring& text, UINT buttons = MB_OKCANCEL | MB_ICONQUESTION) {
    const int r = MessageBoxW(nullptr, text.c_str(), L"Lumen", buttons | MB_SETFOREGROUND);
    return r == IDOK || r == IDYES;
}

void Tell(const std::wstring& text, UINT icon = MB_ICONINFORMATION) {
    MessageBoxW(nullptr, text.c_str(), L"Lumen", MB_OK | icon | MB_SETFOREGROUND);
}

// A GUI process prints to the console it was started from (tools\package-beta.ps1).
void Print(const std::wstring& text) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* out = nullptr;
        if (_wfreopen_s(&out, L"CONOUT$", L"w", stderr) == 0) fwprintf(stderr, L"%s\n", text.c_str());
    }
}

// Lumen running from the install folder is closed first; the person is asked unless silent.
bool MakeLumenClose(const std::wstring& dir, bool silent) {
    if (!LumenRunning(dir)) return true;
    if (!silent && !Ask(Tr(L"Lumen is open. Close it and continue? Where you stopped is saved."))) return false;
    if (CloseRunningLumen(dir, 15000)) return true;
    if (!silent) Tell(Tr(L"Lumen did not close. Close it and try again."), MB_ICONWARNING);
    return false;
}

std::wstring TempDir() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    return temp;
}

// The copies RelaunchFromTemp left: a running exe can't delete itself, so the next run does.
void DeleteOldTempCopies() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((TempDir() + L"Lumen-setup-*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do DeleteFileW((TempDir() + fd.cFileName).c_str());  // fails on the running one: fine
    while (FindNextFileW(h, &fd));
    FindClose(h);
}

// The exe inside the install folder cannot replace or delete its own files: a copy in %TEMP% does
// the work and this one exits.
int RelaunchFromTemp(const std::vector<std::wstring>& args, const std::wstring& dir) {
    const std::wstring copy = TempDir() + L"Lumen-setup-" + std::to_wstring(GetCurrentProcessId()) + L".exe";
    if (!CopyFileW(SelfPath().c_str(), copy.c_str(), FALSE)) return 1;
    std::wstring cmd = L"\"" + copy + L"\"";
    for (const std::wstring& a : args) cmd += L" \"" + a + L"\"";
    cmd += L" --dir \"" + dir + L"\" --wait-pid " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(copy.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 1;
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

// This exe is inside `dir` (the current Lumen.exe or a kept version's).
bool RunsFrom(const std::wstring& dir) {
    const std::wstring self = SelfPath();
    return self.size() > dir.size() && _wcsnicmp(self.c_str(), (dir + L"\\").c_str(), dir.size() + 1) == 0;
}

void WaitForParent(const std::vector<std::wstring>& args) {
    const DWORD pid = static_cast<DWORD>(_wtoi(Value(args, L"--wait-pid").c_str()));
    if (!pid) return;
    if (HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
        WaitForSingleObject(p, 10000);
        CloseHandle(p);
    }
}

void Launch(const std::wstring& dir) {
    ShellExecuteW(nullptr, L"open", (dir + L"\\Lumen.exe").c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
}

int RunInstall(const Package& package, const std::vector<std::wstring>& args) {
    const bool silent = Has(args, L"--silent");
    Target target{Value(args, L"--dir"), !Has(args, L"--no-system")};
    if (target.dir.empty()) target.dir = DefaultInstallDir();
    const std::wstring installed = InstalledVersion(target.dir), incoming = package.version;
    const Action action = Classify(installed, incoming);
    if (!silent) {
        const std::wstring question =
            action == Action::Install  ? Fmt(Tr(L"Install Lumen {0}?"), {incoming})
            : action == Action::Update ? Fmt(Tr(L"Update Lumen {0} to {1}?"), {installed, incoming})
            : action == Action::Rollback
                ? Fmt(Tr(L"Go back from Lumen {0} to {1}? The current version stays on this computer, so you can return to it."),
                      {installed, incoming})
                : Fmt(Tr(L"Lumen {0} is already installed. Install it again?"), {incoming});
        if (!Ask(question)) return 1;
    }
    if (!MakeLumenClose(target.dir, silent)) return 1;
    HCURSOR cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::wstring error;
    const bool ok = Install(package, target, nullptr, &error);
    SetCursor(cursor);
    if (!ok) {
        if (silent) Print(L"Lumen setup: " + error);
        else Tell(Tr(L"Lumen could not be installed.") + std::wstring(L"\n\n") + error, MB_ICONERROR);
        return 1;
    }
    // A rollback keeps the newer version's settings unless asked: older Lumen ignores what it does not know.
    if (action == Action::Rollback && HasDataSnapshot(incoming) &&
        (silent ? Has(args, L"--restore-data")
                : Ask(Fmt(Tr(L"Restore the settings and library Lumen {0} had when you left it? Choose No to keep the current ones."),
                          {incoming}),
                      MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION)))
        RestoreData(incoming);
    if (silent) return 0;
    // As the Inno setup's ticked box did: video opens in Lumen from the first install on.
    if (action == Action::Install && target.system) RunAndWait(target.dir + L"\\Lumen.exe", L"--set-default-player");
    Launch(target.dir);
    return 0;
}

int RunUninstall(const std::vector<std::wstring>& args) {
    const bool silent = Has(args, L"--silent");
    std::wstring dir = Value(args, L"--dir");
    if (dir.empty()) dir = FolderOf(SelfPath());
    if (RunsFrom(dir) && !Has(args, L"--wait-pid")) return RelaunchFromTemp(args, dir);
    WaitForParent(args);
    if (!silent && !Ask(Tr(L"Remove Lumen from this computer?"), MB_YESNO | MB_ICONQUESTION)) return 1;
    if (!MakeLumenClose(dir, silent)) return 1;
    // A silent uninstall keeps the data: only a person who said yes loses their library.
    const bool removeData =
        silent ? Has(args, L"--remove-data")
               : Ask(Tr(L"Also delete Lumen's settings, library, logs and crash reports (the %APPDATA%\\Lumen folder)?\n\n"
                        L"Choose No if you plan to install Lumen again."),
                     MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION);
    std::wstring error;
    if (!Uninstall({dir, !Has(args, L"--no-system")}, removeData, &error)) {
        if (silent) Print(L"Lumen uninstall: " + error);
        else Tell(Tr(L"Lumen could not be removed.") + std::wstring(L"\n\n") + error, MB_ICONERROR);
        return 1;
    }
    if (!silent) Tell(Tr(L"Lumen was removed."));
    return 0;
}

int RunSwitch(const std::vector<std::wstring>& args) {
    const bool silent = Has(args, L"--silent");
    const std::wstring version = Value(args, L"--switch-version");
    std::wstring dir = Value(args, L"--dir");
    if (dir.empty()) dir = DefaultInstallDir();
    if (RunsFrom(dir) && !Has(args, L"--wait-pid")) return RelaunchFromTemp(args, dir);
    WaitForParent(args);
    const std::wstring installed = InstalledVersion(dir);
    if (!silent && !Ask(Fmt(Tr(L"Switch from Lumen {0} to {1}?"), {installed, version}))) return 1;
    if (!MakeLumenClose(dir, silent)) return 1;
    std::wstring error;
    if (!SwitchVersion({dir, !Has(args, L"--no-system")}, version, &error)) {
        if (silent) Print(L"Lumen switch: " + error);
        else Tell(Tr(L"Lumen could not be installed.") + std::wstring(L"\n\n") + error, MB_ICONERROR);
        return 1;
    }
    if (CompareVersions(version, installed) < 0 && HasDataSnapshot(version) &&
        (silent ? Has(args, L"--restore-data")
                : Ask(Fmt(Tr(L"Restore the settings and library Lumen {0} had when you left it? Choose No to keep the current ones."),
                          {version}),
                      MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION)))
        RestoreData(version);
    if (!silent) Launch(dir);
    return 0;
}

int RunMakeSetup(const std::vector<std::wstring>& args) {
    if (args.size() < 3) return Print(L"usage: Lumen.exe --make-setup <out.exe> <file>..."), 2;
    std::wstring error;
    if (WritePackage(SelfPath(), {args.begin() + 2, args.end()}, args[1], &error)) return 0;
    Print(L"make-setup: " + error);
    return 1;
}

}  // namespace

bool RunSetupMode(const std::vector<std::wstring>& args, int* exitCode) {
    const std::wstring first = args.empty() ? std::wstring() : args[0];
    Package package;
    const bool isSetup = first != L"--make-setup" && OpenPackage(SelfPath(), &package);
    if (!isSetup && first != L"--uninstall" && first != L"--switch-version" && first != L"--make-setup") return false;
    if (first == L"--make-setup") return *exitCode = RunMakeSetup(args), true;
    app::SetLanguage(PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? app::Language::Russian
                                                                                : app::Language::English);
    DeleteOldTempCopies();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // the Start menu shortcut
    *exitCode = isSetup ? RunInstall(package, args) : first == L"--uninstall" ? RunUninstall(args) : RunSwitch(args);
    CoUninitialize();
    return true;
}

}  // namespace setup
