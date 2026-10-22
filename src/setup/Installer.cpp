#include "Installer.h"  // relative: the unit tests build it without /I src

#include "../core/Paths.h"

#include <windows.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>

namespace setup {
namespace {

namespace fs = std::filesystem;

constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Lumen";
// The Inno Setup beta (tools/installer/lumen.iss) installed into the same folder under this entry.
constexpr wchar_t kInnoKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{0E08DF8D-068E-44DF-96DD-AFDC2CCD259A}_is1";

std::wstring VersionDir(const std::wstring& dir, const std::wstring& version) {
    return dir + L"\\versions\\" + version;
}

bool IsFile(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsDir(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Names of the plain files in a folder.
std::vector<std::wstring> FilesIn(const std::wstring& folder) {
    std::vector<std::wstring> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec))
        if (e.is_regular_file(ec)) names.push_back(e.path().filename().wstring());
    return names;
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

bool EndsWith(const std::wstring& s, std::wstring_view tail) {
    return s.size() >= tail.size() && Lower(s.substr(s.size() - tail.size())) == tail;
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>* out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size);
    out->resize(ok ? static_cast<size_t>(size.QuadPart) : 0);
    for (size_t at = 0; ok && at < out->size();) {
        DWORD got = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(out->size() - at, 1u << 30));
        ok = ReadFile(f, out->data() + at, chunk, &got, nullptr) && got == chunk;
        at += got;
    }
    CloseHandle(f);
    return ok;
}

bool WriteFileBytes(const std::wstring& path, const std::vector<uint8_t>& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    for (size_t at = 0; ok && at < data.size();) {
        DWORD put = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - at, 1u << 30));
        ok = WriteFile(f, data.data() + at, chunk, &put, nullptr) && put == chunk;
        at += put;
    }
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    return ok;
}

// A hard link where the volume allows one, else a copy.
bool LinkOrCopy(const std::wstring& from, const std::wstring& to) {
    DeleteFileW(to.c_str());
    return CreateHardLinkW(to.c_str(), from.c_str(), nullptr) || CopyFileW(from.c_str(), to.c_str(), FALSE);
}

// A folder that is still busy is left for the next Prune.
void RemoveTree(const std::wstring& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

// The Inno beta and older layouts had no versions\ folder: the current files are kept there first,
// so they can be switched back to. Only Lumen's own files: a yt-dlp.exe the user put next to
// Lumen.exe (Config.cpp FindYtDlp) is theirs and must not follow version switches.
void KeepCurrent(const std::wstring& dir, const std::wstring& current) {
    if (current.empty() || IsDir(VersionDir(dir, current))) return;
    const std::wstring tmp = VersionDir(dir, current) + L".tmp";
    RemoveTree(tmp);
    std::error_code ec;
    fs::create_directories(tmp, ec);
    bool ok = !ec;
    for (const std::wstring& name : FilesIn(dir)) {
        const std::wstring lower = Lower(name);
        if (lower == L"lumen.exe" || EndsWith(lower, L".dll") || (lower.rfind(L"readme", 0) == 0 && EndsWith(lower, L".txt")))
            ok = ok && LinkOrCopy(dir + L"\\" + name, tmp + L"\\" + name);
    }
    if (ok && MoveFileW(tmp.c_str(), VersionDir(dir, current).c_str())) return;
    RemoveTree(tmp);
}

// versions\<version> from the package. A file equal to one already in the folder is linked, not
// written again: libmpv (121 MB) usually stays the same across versions.
bool Extract(const Package& package, const std::wstring& dir, const Progress& progress, std::wstring* error) {
    const std::wstring finalDir = VersionDir(dir, package.version);
    const std::wstring tmp = finalDir + L".tmp";
    RemoveTree(tmp);
    std::error_code ec;
    fs::create_directories(tmp, ec);
    if (ec) return *error = L"cannot create " + tmp, false;

    uint64_t total = 0, done = 0;
    for (const PackedFile& f : package.files) total += f.size;
    std::vector<std::wstring> candidates{dir};
    for (const std::wstring& v : StoredVersions(dir)) candidates.push_back(VersionDir(dir, v));
    for (const PackedFile& f : package.files) {
        std::vector<uint8_t> data;
        if (!ReadPackedFile(package, f, &data, error)) return RemoveTree(tmp), false;
        const std::wstring dest = tmp + L"\\" + f.name;
        bool linked = false;
        for (const std::wstring& c : candidates) {
            WIN32_FILE_ATTRIBUTE_DATA a{};
            const std::wstring path = c + L"\\" + f.name;
            if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) ||
                ((uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow) != f.size)
                continue;
            std::vector<uint8_t> existing;
            if (ReadFileBytes(path, &existing) && existing == data && CreateHardLinkW(dest.c_str(), path.c_str(), nullptr)) {
                linked = true;
                break;
            }
        }
        if (!linked && !WriteFileBytes(dest, data)) return *error = L"cannot write " + dest, RemoveTree(tmp), false;
        done += f.size;
        if (progress) progress(total ? double(done) / double(total) : 1.0);
    }
    // A reinstall of the same version replaces its folder; the current files are links of their own.
    if (IsDir(finalDir)) {
        const std::wstring old = finalDir + L".old";
        RemoveTree(old);
        if (!MoveFileW(finalDir.c_str(), old.c_str())) return *error = L"cannot replace " + finalDir, RemoveTree(tmp), false;
        RemoveTree(old);
    }
    if (!MoveFileW(tmp.c_str(), finalDir.c_str())) return *error = L"cannot create " + finalDir, RemoveTree(tmp), false;
    return true;
}

// Puts the files of versions\<to> in place of those of versions\<from> (empty: nothing installed).
bool Activate(const std::wstring& dir, const std::wstring& from, const std::wstring& to, std::wstring* error) {
    const std::wstring src = VersionDir(dir, to);
    const std::vector<std::wstring> files = FilesIn(src);
    if (std::find(files.begin(), files.end(), L"Lumen.exe") == files.end())
        return *error = L"no Lumen.exe in " + src, false;
    for (const std::wstring& name : files) {
        const std::wstring dest = dir + L"\\" + name, next = dest + L".new";
        if (!LinkOrCopy(src + L"\\" + name, next) ||
            !MoveFileExW(next.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(next.c_str());
            *error = L"cannot replace " + dest + L" (is Lumen still running?)";
            return false;
        }
    }
    if (!from.empty())
        for (const std::wstring& name : FilesIn(VersionDir(dir, from)))
            if (std::find(files.begin(), files.end(), name) == files.end()) DeleteFileW((dir + L"\\" + name).c_str());
    return true;
}

// Activate, and on failure the previous version back.
bool ActivateOrRestore(const std::wstring& dir, const std::wstring& from, const std::wstring& to, std::wstring* error) {
    if (Activate(dir, from, to, error)) return true;
    std::wstring ignored;
    if (!from.empty()) Activate(dir, to, from, &ignored);
    return false;
}

void Prune(const std::wstring& dir, const std::wstring& current) {
    size_t kept = 0;
    for (const std::wstring& v : StoredVersions(dir))
        if (v != current && ++kept > kKeptOldVersions) RemoveTree(VersionDir(dir, v));
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir + L"\\versions", ec)) {
        const std::wstring name = e.path().filename().wstring();
        if (EndsWith(name, L".tmp") || EndsWith(name, L".old")) RemoveTree(e.path().wstring());
    }
    for (const std::wstring& name : FilesIn(dir))
        if (EndsWith(name, L".new")) DeleteFileW((dir + L"\\" + name).c_str());
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

std::wstring ShortcutPath() {
    const std::wstring programs = KnownFolder(FOLDERID_Programs);
    return programs.empty() ? programs : programs + L"\\Lumen.lnk";
}

bool CreateShortcut(const std::wstring& exe) {
    const std::wstring path = ShortcutPath();
    IShellLinkW* link = nullptr;
    if (path.empty() || FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return false;
    link->SetPath(exe.c_str());
    link->SetDescription(L"Lumen");
    IPersistFile* file = nullptr;
    const bool ok = SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))) && SUCCEEDED(file->Save(path.c_str(), TRUE));
    if (file) file->Release();
    link->Release();
    return ok;
}

bool SetString(const wchar_t* name, const std::wstring& value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, kUninstallKey, name, REG_SZ, value.c_str(),
                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool SetDword(const wchar_t* name, DWORD value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, kUninstallKey, name, REG_DWORD, &value, sizeof(value)) == ERROR_SUCCESS;
}

bool WriteUninstallEntry(const std::wstring& dir, const std::wstring& version) {
    const std::wstring exe = dir + L"\\Lumen.exe";
    uint64_t bytes = 0;
    for (const std::wstring& name : FilesIn(dir)) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (GetFileAttributesExW((dir + L"\\" + name).c_str(), GetFileExInfoStandard, &a))
            bytes += (uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t date[16];
    swprintf_s(date, L"%04u%02u%02u", now.wYear, now.wMonth, now.wDay);
    return SetString(L"DisplayName", L"Lumen") && SetString(L"DisplayVersion", version) &&
           SetString(L"Publisher", L"Lumen") && SetString(L"DisplayIcon", L"\"" + exe + L"\",0") &&
           SetString(L"InstallLocation", dir) && SetString(L"InstallDate", date) &&
           SetString(L"UninstallString", L"\"" + exe + L"\" --uninstall") &&
           SetString(L"QuietUninstallString", L"\"" + exe + L"\" --uninstall --silent") && SetDword(L"NoModify", 1) &&
           SetDword(L"NoRepair", 1) && SetDword(L"EstimatedSize", static_cast<DWORD>(bytes / 1024));
}

// The Inno beta's entry and uninstaller: its uninstall would now delete our files.
void RemoveInno(const std::wstring& dir) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kInnoKey);
    for (const std::wstring& name : FilesIn(dir)) {
        const std::wstring lower = Lower(name);
        if (lower.rfind(L"unins", 0) == 0 && (EndsWith(lower, L".exe") || EndsWith(lower, L".dat")))
            DeleteFileW((dir + L"\\" + name).c_str());
    }
}

// File types point at <dir>\Lumen.exe, which registers them itself (its path, its language).
bool Integrate(const Target& target, const std::wstring& version, std::wstring* error) {
    if (!target.system) return true;
    const std::wstring exe = target.dir + L"\\Lumen.exe";
    RemoveInno(target.dir);
    if (RunAndWait(exe, L"--register-file-types") != 0) return *error = L"cannot register the file types", false;
    if (!WriteUninstallEntry(target.dir, version)) return *error = L"cannot write the uninstall entry", false;
    if (!CreateShortcut(exe)) return *error = L"cannot create the Start menu shortcut", false;
    return true;
}

std::wstring BackupDir(const std::wstring& version) {
    const std::wstring data = DataDir();
    return data.empty() ? data : data + L"\\backups\\" + version;
}

bool CopyJson(const std::wstring& from, const std::wstring& to) {
    std::error_code ec;
    fs::create_directories(to, ec);
    bool ok = !ec;
    for (const std::wstring& name : FilesIn(from))
        if (EndsWith(name, L".json")) ok &= CopyFileW((from + L"\\" + name).c_str(), (to + L"\\" + name).c_str(), FALSE) != 0;
    return ok;
}

// Lumen.exe processes started from `dir` (any version inside it), not this one.
std::vector<DWORD> LumenProcesses(const std::wstring& dir) {
    std::vector<DWORD> pids;
    const std::wstring prefix = Lower(dir) + L"\\";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W e{sizeof(e)};
    for (BOOL more = Process32FirstW(snap, &e); more; more = Process32NextW(snap, &e)) {
        if (e.th32ProcessID == GetCurrentProcessId() || _wcsicmp(e.szExeFile, L"Lumen.exe") != 0) continue;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
        if (!p) continue;
        wchar_t path[MAX_PATH * 4];
        DWORD n = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(p, 0, path, &n) && Lower(std::wstring(path, n)).rfind(prefix, 0) == 0)
            pids.push_back(e.th32ProcessID);
        CloseHandle(p);
    }
    CloseHandle(snap);
    return pids;
}

}  // namespace

std::wstring DefaultInstallDir() {
    wchar_t path[MAX_PATH * 2];
    for (const wchar_t* key : {kUninstallKey, kInnoKey}) {
        DWORD n = sizeof(path);
        if (RegGetValueW(HKEY_CURRENT_USER, key, L"InstallLocation", RRF_RT_REG_SZ, nullptr, path, &n) == ERROR_SUCCESS &&
            path[0]) {
            std::wstring dir = path;
            while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
            return dir;
        }
    }
    const std::wstring programs = KnownFolder(FOLDERID_UserProgramFiles);
    return programs.empty() ? programs : programs + L"\\Lumen";
}

std::wstring InstalledVersion(const std::wstring& dir) {
    const std::wstring exe = dir + L"\\Lumen.exe";
    return IsFile(exe) ? FileVersion(exe) : std::wstring();
}

std::vector<std::wstring> StoredVersions(const std::wstring& dir) {
    std::vector<std::wstring> versions;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir + L"\\versions", ec)) {
        const std::wstring name = e.path().filename().wstring();
        if (e.is_directory(ec) && !EndsWith(name, L".tmp") && !EndsWith(name, L".old") &&
            IsFile(e.path().wstring() + L"\\Lumen.exe"))
            versions.push_back(name);
    }
    std::sort(versions.begin(), versions.end(),
              [](const std::wstring& a, const std::wstring& b) { return CompareVersions(a, b) > 0; });
    return versions;
}

int CompareVersions(std::wstring_view a, std::wstring_view b) {
    auto next = [](std::wstring_view& s) {
        unsigned long long n = 0;
        size_t i = 0;
        for (; i < s.size() && s[i] >= L'0' && s[i] <= L'9'; ++i) n = n * 10 + (s[i] - L'0');
        while (i < s.size() && (s[i] < L'0' || s[i] > L'9')) ++i;  // the dot
        s.remove_prefix(i);
        return n;
    };
    while (!a.empty() || !b.empty()) {
        const unsigned long long x = next(a), y = next(b);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

Action Classify(const std::wstring& installed, const std::wstring& incoming) {
    if (installed.empty()) return Action::Install;
    const int c = CompareVersions(installed, incoming);
    return c < 0 ? Action::Update : c > 0 ? Action::Rollback : Action::Reinstall;
}

bool Install(const Package& package, const Target& target, const Progress& progress, std::wstring* error) {
    const std::wstring& dir = target.dir;
    if (package.version.empty()) return *error = L"the setup has no version", false;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!IsDir(dir)) return *error = L"cannot create " + dir, false;
    if (LumenRunning(dir)) return *error = L"Lumen is running", false;
    const std::wstring current = InstalledVersion(dir);
    KeepCurrent(dir, current);
    if (!Extract(package, dir, progress, error)) return false;
    if (!current.empty() && current != package.version) SnapshotData(current);
    if (!ActivateOrRestore(dir, IsDir(VersionDir(dir, current)) ? current : L"", package.version, error)) return false;
    Prune(dir, package.version);
    return Integrate(target, package.version, error);
}

bool SwitchVersion(const Target& target, const std::wstring& version, std::wstring* error) {
    const std::wstring& dir = target.dir;
    if (!IsDir(VersionDir(dir, version))) return *error = L"Lumen " + version + L" is not kept", false;
    if (LumenRunning(dir)) return *error = L"Lumen is running", false;
    const std::wstring current = InstalledVersion(dir);
    KeepCurrent(dir, current);
    if (!current.empty() && current != version) SnapshotData(current);
    if (!ActivateOrRestore(dir, IsDir(VersionDir(dir, current)) ? current : L"", version, error)) return false;
    Prune(dir, version);
    return Integrate(target, version, error);
}

bool Uninstall(const Target& target, bool removeData, std::wstring* error) {
    const std::wstring& dir = target.dir;
    if (LumenRunning(dir)) return *error = L"Lumen is running", false;
    if (target.system) {
        if (IsFile(dir + L"\\Lumen.exe")) RunAndWait(dir + L"\\Lumen.exe", L"--unregister-file-types");
        DeleteFileW(ShortcutPath().c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
        RemoveInno(dir);
    }
    // Lumen's files only: anything else the user put in the folder stays, and so does the folder then.
    std::vector<std::wstring> ours{L"Lumen.exe"};
    for (const std::wstring& v : StoredVersions(dir))
        for (const std::wstring& name : FilesIn(VersionDir(dir, v))) ours.push_back(name);
    for (const std::wstring& name : FilesIn(dir))
        if (std::find(ours.begin(), ours.end(), name) != ours.end() || EndsWith(name, L".new"))
            DeleteFileW((dir + L"\\" + name).c_str());
    RemoveTree(dir + L"\\versions");
    RemoveDirectoryW(dir.c_str());
    if (removeData && !DataDir().empty()) RemoveTree(DataDir());
    if (IsFile(dir + L"\\Lumen.exe")) return *error = L"cannot delete " + dir + L"\\Lumen.exe", false;
    return true;
}

bool LumenRunning(const std::wstring& dir) {
    return !LumenProcesses(dir).empty();
}

bool CloseRunningLumen(const std::wstring& dir, unsigned timeoutMs) {
    const std::vector<DWORD> pids = LumenProcesses(dir);
    if (pids.empty()) return true;
    std::vector<HANDLE> handles;
    for (DWORD pid : pids)
        if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid)) handles.push_back(h);
    // Its main window's close is a full quit (MainWindow onCloseRequest): settings and watch-later saved.
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"LumenHost", nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (!GetWindow(w, GW_OWNER) && std::find(pids.begin(), pids.end(), pid) != pids.end())
            PostMessageW(w, WM_CLOSE, 0, 0);
    }
    if (!handles.empty())
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, timeoutMs);
    for (HANDLE h : handles) CloseHandle(h);
    return !LumenRunning(dir);
}

bool SnapshotData(const std::wstring& version) {
    const std::wstring backup = BackupDir(version);
    return !backup.empty() && CopyJson(DataDir(), backup);
}

bool HasDataSnapshot(const std::wstring& version) {
    const std::wstring backup = BackupDir(version);
    return !backup.empty() && IsFile(backup + L"\\settings.json");
}

bool RestoreData(const std::wstring& version) {
    return HasDataSnapshot(version) && CopyJson(BackupDir(version), DataDir());
}

int RunAndWait(const std::wstring& exe, const std::wstring& args) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

}  // namespace setup
