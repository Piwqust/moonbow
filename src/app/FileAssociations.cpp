#include "app/FileAssociations.h"
#include "app/I18n.h"
#include "core/UserChoice.h"
#include "thumbs/ThumbsClsid.h"
#include "ui/Host.h"

#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <sddl.h>
#include <shlwapi.h>

#include <string>
#include <tuple>
#include <vector>

namespace app {

namespace {

constexpr wchar_t kVideoProgId[] = L"Moonbow.Video";
constexpr wchar_t kAudioProgId[] = L"Moonbow.Audio";
constexpr wchar_t kPlaylistProgId[] = L"Moonbow.Playlist";
constexpr wchar_t kCapabilities[] = L"Software\\Moonbow\\Capabilities";

const wchar_t* const kVideo[] = {L".mkv", L".mp4", L".m4v", L".avi", L".mov", L".webm", L".wmv", L".flv", L".ts",
                                 L".m2ts", L".mts", L".mpg", L".mpeg", L".vob", L".ogv", L".3gp", L".rmvb", L".hevc"};
const wchar_t* const kAudio[] = {L".mp3", L".flac", L".m4a", L".aac", L".ogg", L".opus", L".wav", L".wma", L".ac3",
                                 L".dts", L".mka", L".m4b", L".ape", L".wv",  L".aiff"};
const wchar_t* const kPlaylists[] = {L".m3u", L".m3u8", L".pls"};

std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return std::wstring(buf, n);
}

bool SetValue(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool SetEmpty(const std::wstring& key, const wchar_t* name) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_NONE, nullptr, 0) == ERROR_SUCCESS;
}

bool WriteProgId(const wchar_t* progId, const wchar_t* friendly, const std::wstring& exe) {
    const std::wstring base = std::wstring(L"Software\\Classes\\") + progId;
    const std::wstring command = L"\"" + exe + L"\" \"%1\"";
    // Player: Explorer offers the verb for any number of selected files (Document stops at 15).
    return SetValue(base, nullptr, friendly) && SetValue(base + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0") &&
           SetValue(base + L"\\shell", nullptr, L"open") && SetValue(base + L"\\shell\\open", L"FriendlyAppName", L"Moonbow") &&
           SetValue(base + L"\\shell\\open", L"MultiSelectModel", L"Player") &&
           SetValue(base + L"\\shell\\open\\command", nullptr, command);
}

// "Play in Moonbow" and "Add to the Moonbow queue" under `shell` (a perceived type or Directory):
// shown whichever player is the default. Each selected item starts a Moonbow that hands it to the
// running one (main.cpp), which joins them into one queue (App::OnCopyData).
bool WriteVerbs(const std::wstring& shell, const std::wstring& exe) {
    bool ok = true;
    for (const auto& [verb, label, args] : {std::tuple{L"Moonbow.play", Tr(L"Play in Moonbow"), L""},
                                            std::tuple{L"Moonbow.enqueue", Tr(L"Add to the Moonbow queue"), L"--enqueue "}}) {
        const std::wstring key = shell + L"\\" + verb;
        ok &= SetValue(key, L"MUIVerb", label) && SetValue(key, L"Icon", L"\"" + exe + L"\",0") &&
              SetValue(key, L"MultiSelectModel", L"Player") &&
              SetValue(key + L"\\command", nullptr, L"\"" + exe + L"\" " + args + L"\"%1\"");
    }
    return ok;
}

template <typename F>
void ForEachType(F&& f) {
    for (const wchar_t* ext : kVideo) f(ext, kVideoProgId);
    for (const wchar_t* ext : kAudio) f(ext, kAudioProgId);
    for (const wchar_t* ext : kPlaylists) f(ext, kPlaylistProgId);
}

template <size_t N>
bool WriteTypes(const wchar_t* const (&exts)[N], const wchar_t* progId) {
    bool ok = true;
    for (const wchar_t* ext : exts) {
        ok &= SetEmpty(std::wstring(L"Software\\Classes\\") + ext + L"\\OpenWithProgids", progId);
        ok &= SetValue(std::wstring(kCapabilities) + L"\\FileAssociations", ext, progId);
        ok &= SetEmpty(L"Software\\Classes\\Applications\\Moonbow.exe\\SupportedTypes", ext);
    }
    return ok;
}

constexpr wchar_t kFileExts[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\";
// The apps that were the defaults before Moonbow took over, by extension ("" = Windows' own default).
constexpr wchar_t kPreviousDefaults[] = L"Software\\Moonbow\\PreviousDefaults";
constexpr uint64_t kMinute = 600000000;  // FILETIME ticks

// ProgId of the user's current choice for ext, "" when there is none.
std::wstring CurrentChoice(const wchar_t* ext) {
    wchar_t got[256];
    DWORD n = sizeof(got);
    const std::wstring key = std::wstring(kFileExts) + ext + L"\\UserChoice";
    return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"ProgId", RRF_RT_REG_SZ, nullptr, got, &n) == ERROR_SUCCESS ? got
                                                                                                                    : L"";
}

bool IsMoonbow(const std::wstring& progId) {
    return _wcsnicmp(progId.c_str(), L"Moonbow.", 6) == 0;
}

uint64_t Ticks(FILETIME ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring UserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    std::wstring sid;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buf(size);
    LPWSTR text = nullptr;
    if (size && GetTokenInformation(token, TokenUser, buf.data(), size, &size) &&
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &text)) {
        sid = text;
        LocalFree(text);
    }
    CloseHandle(token);
    return sid;
}

// Windows denies the user SetValue on an existing UserChoice key, so it is made anew. The hash
// names the minute the key was written: a write that crosses a minute is redone.
bool WriteUserChoice(const wchar_t* ext, const wchar_t* progId, const std::wstring& sid) {
    const std::wstring key = std::wstring(kFileExts) + ext + L"\\UserChoice";
    for (int attempt = 0; attempt < 3; ++attempt) {
        RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        const uint64_t minute = Ticks(now) - Ticks(now) % kMinute;
        const std::wstring hash = UserChoiceHash(ext, sid, progId, minute);
        HKEY k = nullptr;
        if (hash.empty() || RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE,
                                            nullptr, &k, nullptr) != ERROR_SUCCESS)
            return false;
        const std::wstring prog = progId;
        bool ok = RegSetValueExW(k, L"ProgId", 0, REG_SZ, reinterpret_cast<const BYTE*>(prog.c_str()),
                                 static_cast<DWORD>((prog.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS &&
                  RegSetValueExW(k, L"Hash", 0, REG_SZ, reinterpret_cast<const BYTE*>(hash.c_str()),
                                 static_cast<DWORD>((hash.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        FILETIME written{};
        ok &= RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, &written) == ERROR_SUCCESS;
        RegCloseKey(k);
        if (!ok) return false;
        if (Ticks(written) - Ticks(written) % kMinute == minute) return true;
    }
    return false;
}

constexpr wchar_t kThumbsClsidKey[] = L"Software\\Classes\\CLSID\\" MOONBOW_THUMBS_CLSID_STR;

std::wstring ThumbHandlerKey(const wchar_t* ext) {
    return std::wstring(L"Software\\Classes\\") + ext + L"\\ShellEx\\" SHELL_THUMBNAIL_HANDLER_STR;
}

// Explorer thumbnails for audio from MoonbowThumbs.dll next to the exe: the cover, else the stand-in
// record. The DLL hands each file to the type's machine-wide handler first (Icaros, say), so this
// HKCU entry only adds what that one could not draw. Without the DLL (a dev build) nothing is written.
bool WriteThumbnailHandler(const std::wstring& exe) {
    const std::wstring dll = exe.substr(0, exe.find_last_of(L'\\') + 1) + MOONBOW_THUMBS_DLL;
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    const std::wstring clsid = kThumbsClsidKey;
    bool ok = SetValue(clsid, nullptr, L"Moonbow thumbnails") && SetValue(clsid + L"\\InprocServer32", nullptr, dll) &&
              SetValue(clsid + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    for (const wchar_t* ext : kAudio) ok &= SetValue(ThumbHandlerKey(ext), nullptr, MOONBOW_THUMBS_CLSID_STR);
    return ok;
}

// What Explorer will really open the type with: it drops a UserChoice whose hash it rejects.
bool OpensWith(const wchar_t* ext, const wchar_t* progId) {
    wchar_t got[128];
    DWORD n = static_cast<DWORD>(std::size(got));
    return AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, ext, nullptr, got, &n) == S_OK && _wcsicmp(got, progId) == 0;
}

}  // namespace

bool RegisterFileAssociations() {
    const std::wstring exe = ExePath();
    bool ok = WriteProgId(kVideoProgId, L"Moonbow video", exe) && WriteProgId(kAudioProgId, L"Moonbow audio", exe) &&
              WriteProgId(kPlaylistProgId, L"Moonbow playlist", exe);
    ok &= SetValue(L"Software\\Classes\\Applications\\Moonbow.exe\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
    ok &= SetValue(L"Software\\Classes\\Applications\\Moonbow.exe", L"FriendlyAppName", L"Moonbow");
    ok &= SetValue(kCapabilities, L"ApplicationName", L"Moonbow");
    ok &= SetValue(kCapabilities, L"ApplicationDescription", L"Fast, minimal video and music player");
    ok &= SetValue(kCapabilities, L"ApplicationIcon", L"\"" + exe + L"\",0");
    ok &= WriteTypes(kVideo, kVideoProgId) && WriteTypes(kAudio, kAudioProgId) && WriteTypes(kPlaylists, kPlaylistProgId);
    for (const wchar_t* shell : {L"SystemFileAssociations\\video\\shell", L"SystemFileAssociations\\audio\\shell", L"Directory\\shell"})
        ok &= WriteVerbs(std::wstring(L"Software\\Classes\\") + shell, exe);
    ok &= SetValue(L"Software\\RegisteredApplications", L"Moonbow", kCapabilities);
    ok &= WriteThumbnailHandler(exe);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool UnregisterFileAssociations() {
    // A key that is already gone counts as removed.
    auto gone = [](LSTATUS s) { return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND; };
    auto removeTree = [&](const std::wstring& key) { return gone(RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str())); };
    bool ok = true;
    // First, while the backup still exists: types that still open in Moonbow go back to the app they
    // had before; a type the user moved elsewhere since is theirs and stays.
    const std::wstring sid = UserSid();
    ForEachType([&](const wchar_t* ext, const wchar_t*) {
        if (!IsMoonbow(CurrentChoice(ext))) return;
        wchar_t prev[256];
        DWORD n = sizeof(prev);
        if (RegGetValueW(HKEY_CURRENT_USER, kPreviousDefaults, ext, RRF_RT_REG_SZ, nullptr, prev, &n) == ERROR_SUCCESS &&
            prev[0] && !sid.empty())
            ok &= WriteUserChoice(ext, prev, sid);
        else  // Windows' own default, or Windows then asks which app to use
            ok &= gone(RegDeleteKeyW(HKEY_CURRENT_USER, (std::wstring(kFileExts) + ext + L"\\UserChoice").c_str()));
    });
    for (const wchar_t* key : {L"Software\\Classes\\Moonbow.Video", L"Software\\Classes\\Moonbow.Audio",
                               L"Software\\Classes\\Moonbow.Playlist", L"Software\\Classes\\Applications\\Moonbow.exe", L"Software\\Moonbow"})
        ok &= removeTree(key);
    for (const wchar_t* shell : {L"SystemFileAssociations\\video\\shell", L"SystemFileAssociations\\audio\\shell", L"Directory\\shell"})
        for (const wchar_t* verb : {L"Moonbow.play", L"Moonbow.enqueue"})
            ok &= removeTree(std::wstring(L"Software\\Classes\\") + shell + L"\\" + verb);
    ForEachType([&](const wchar_t* ext, const wchar_t* progId) {
        ok &= gone(RegDeleteKeyValueW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ext + L"\\OpenWithProgids").c_str(), progId));
    });
    ok &= gone(RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"Moonbow"));
    // Only a handler entry that is still Moonbow's: another app may have taken the type since.
    for (const wchar_t* ext : kAudio) {
        wchar_t value[64];
        DWORD n = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, ThumbHandlerKey(ext).c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &n) == ERROR_SUCCESS &&
            _wcsicmp(value, MOONBOW_THUMBS_CLSID_STR) == 0)
            ok &= gone(RegDeleteKeyW(HKEY_CURRENT_USER, ThumbHandlerKey(ext).c_str()));
    }
    ok &= removeTree(kThumbsClsidKey);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool SetDefaultPlayer() {
    const std::wstring sid = UserSid();
    if (sid.empty()) return false;
    bool ok = true;
    // Video only (the owner's call): music and playlists stay with the user's music app.
    for (const wchar_t* ext : kVideo) {
        // A type already on Moonbow keeps its backup: pressing the button twice must not save Moonbow as "before".
        if (const std::wstring prev = CurrentChoice(ext); !IsMoonbow(prev)) ok &= SetValue(kPreviousDefaults, ext, prev);
        ok &= WriteUserChoice(ext, kVideoProgId, sid);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    // Builds that moved to UserChoiceLatest take the write but keep opening the old app.
    return ok && OpensWith(L".mp4", kVideoProgId) && OpensWith(L".mkv", kVideoProgId);
}

void OpenDefaultAppsSettings() {
    ui::Host::ShellOpen(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=Moonbow", nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace app
