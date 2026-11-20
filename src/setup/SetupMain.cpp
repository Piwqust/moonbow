#include "setup/SetupMain.h"

#include "app/I18n.h"
#include "setup/Installer.h"
#include "setup/Package.h"
#include "setup/SetupWindow.h"
#include "ui/Host.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cstdio>

namespace setup {
namespace {

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

// A GUI process prints to the console it was started from (tools\package-beta.ps1).
void Print(const std::wstring& text) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* out = nullptr;
        if (_wfreopen_s(&out, L"CONOUT$", L"w", stderr) == 0) fwprintf(stderr, L"%s\n", text.c_str());
    }
}

std::wstring TempDir() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    return temp;
}

// The copies RelaunchFromTemp left: a running exe can't delete itself, so the next run does.
void DeleteOldTempCopies() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((TempDir() + L"Moonbow-setup-*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do DeleteFileW((TempDir() + fd.cFileName).c_str());  // fails on the running one: fine
    while (FindNextFileW(h, &fd));
    FindClose(h);
}

// The exe inside the install folder cannot replace or delete its own files: a copy in %TEMP% does
// the work and this one exits.
int RelaunchFromTemp(const std::vector<std::wstring>& args, const std::wstring& dir) {
    const std::wstring copy = TempDir() + L"Moonbow-setup-" + std::to_wstring(GetCurrentProcessId()) + L".exe";
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

// This exe is inside `dir` (the current Moonbow.exe or a kept version's).
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

// Silent runs close a running Moonbow themselves; the window says it will and does it on the click.
int Fail(const wchar_t* what, const std::wstring& error) {
    Print(std::wstring(what) + L": " + error);
    return 1;
}

Target TargetOf(const std::vector<std::wstring>& args, std::wstring dir) {
    return {std::move(dir), !Has(args, L"--no-system")};
}

int RunInstall(const Package& package, const std::vector<std::wstring>& args) {
    std::wstring dir = Value(args, L"--dir");
    if (dir.empty()) dir = DefaultInstallDir();
    const Target target = TargetOf(args, dir);
    // Moonbow's updater starts the setup and then quits: wait for it before closing it from here.
    WaitForParent(args);
    if (!Has(args, L"--silent"))
        return RunWindow({Op::Install, &package, {}, target, Value(args, L"--setup-page"), Has(args, L"--update")});
    const Action action = Classify(InstalledVersion(dir), package.version);
    std::wstring error;
    if (!CloseRunningMoonbow(dir, 15000)) return Fail(L"Moonbow setup", L"Moonbow did not close");
    if (!Install(package, target, nullptr, &error)) return Fail(L"Moonbow setup", error);
    // A rollback keeps the newer version's settings unless asked: older Moonbow ignores what it does not know.
    if (action == Action::Rollback && Has(args, L"--restore-data")) RestoreData(package.version);
    return 0;
}

int RunUninstall(const std::vector<std::wstring>& args) {
    std::wstring dir = Value(args, L"--dir");
    if (dir.empty()) dir = FolderOf(SelfPath());
    if (RunsFrom(dir) && !Has(args, L"--wait-pid")) return RelaunchFromTemp(args, dir);
    WaitForParent(args);
    const Target target = TargetOf(args, dir);
    if (!Has(args, L"--silent")) return RunWindow({Op::Uninstall, nullptr, {}, target, Value(args, L"--setup-page")});
    std::wstring error;
    if (!CloseRunningMoonbow(dir, 15000)) return Fail(L"Moonbow uninstall", L"Moonbow did not close");
    // A silent uninstall keeps the data: only a person who said yes loses their library.
    if (!Uninstall(target, Has(args, L"--remove-data"), &error)) return Fail(L"Moonbow uninstall", error);
    return 0;
}

int RunSwitch(const std::vector<std::wstring>& args) {
    const std::wstring version = Value(args, L"--switch-version");
    std::wstring dir = Value(args, L"--dir");
    if (dir.empty()) dir = DefaultInstallDir();
    if (RunsFrom(dir) && !Has(args, L"--wait-pid")) return RelaunchFromTemp(args, dir);
    WaitForParent(args);
    const Target target = TargetOf(args, dir);
    if (!Has(args, L"--silent")) return RunWindow({Op::Switch, nullptr, version, target, Value(args, L"--setup-page")});
    const std::wstring installed = InstalledVersion(dir);
    std::wstring error;
    if (!CloseRunningMoonbow(dir, 15000)) return Fail(L"Moonbow switch", L"Moonbow did not close");
    if (!SwitchVersion(target, version, &error)) return Fail(L"Moonbow switch", error);
    if (CompareVersions(version, installed) < 0 && Has(args, L"--restore-data")) RestoreData(version);
    return 0;
}

int RunMakeSetup(const std::vector<std::wstring>& args) {
    if (args.size() < 3) return Fail(L"make-setup", L"usage: Moonbow.exe --make-setup <out.exe> <file>...");
    std::wstring error;
    if (WritePackage(SelfPath(), {args.begin() + 2, args.end()}, args[1], &error)) return 0;
    return Fail(L"make-setup", error);
}

}  // namespace

bool RunSetupMode(const std::vector<std::wstring>& args, int* exitCode) {
    const std::wstring first = args.empty() ? std::wstring() : args[0];
    Package package;
    const bool isSetup = first != L"--make-setup" && OpenPackage(SelfPath(), &package);
    if (!isSetup && first != L"--uninstall" && first != L"--switch-version" && first != L"--make-setup") return false;
    if (first == L"--make-setup") return *exitCode = RunMakeSetup(args), true;
    DeleteOldTempCopies();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // the Start menu shortcut
    *exitCode = isSetup ? RunInstall(package, args) : first == L"--uninstall" ? RunUninstall(args) : RunSwitch(args);
    CoUninitialize();
    return true;
}

}  // namespace setup
