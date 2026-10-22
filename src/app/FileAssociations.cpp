#include "app/FileAssociations.h"
#include "app/I18n.h"
#include "core/UserChoice.h"
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

constexpr wchar_t kVideoProgId[] = L"Lumen.Video";
constexpr wchar_t kAudioProgId[] = L"Lumen.Audio";
constexpr wchar_t kPlaylistProgId[] = L"Lumen.Playlist";
constexpr wchar_t kCapabilities[] = L"Software\\Lumen\\Capabilities";

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
           SetValue(base + L"\\shell", nullptr, L"open") && SetValue(base + L"\\shell\\open", L"FriendlyAppName", L"Lumen") &&
           SetValue(base + L"\\shell\\open", L"MultiSelectModel", L"Player") &&
           SetValue(base + L"\\shell\\open\\command", nullptr, command);
}

// "Play in Lumen" and "Add to the Lumen queue" under `shell` (a perceived type or Directory):
// shown whichever player is the default. Each selected item starts a Lumen that hands it to the
// running one (main.cpp), which joins them into one queue (App::OnCopyData).
bool WriteVerbs(const std::wstring& shell, const std::wstring& exe) {
    bool ok = true;
    for (const auto& [verb, label, args] : {std::tuple{L"Lumen.play", Tr(L"Play in Lumen"), L""},
                                            std::tuple{L"Lumen.enqueue", Tr(L"Add to the Lumen queue"), L"--enqueue "}}) {
        const std::wstring key = shell + L"\\" + verb;
        ok &= SetValue(key, L"MUIVerb", label) && SetValue(key, L"Icon", L"\"" + exe + L"\",0") &&
              SetValue(key, L"MultiSelectModel", L"Player") &&
              SetValue(key + L"\\command", nullptr, L"\"" + exe + L"\" " + args + L"\"%1\"");
    }
    return ok;
}

template <size_t N>
bool WriteTypes(const wchar_t* const (&exts)[N], const wchar_t* progId) {
    bool ok = true;
    for (const wchar_t* ext : exts) {
        ok &= SetEmpty(std::wstring(L"Software\\Classes\\") + ext + L"\\OpenWithProgids", progId);
        ok &= SetValue(std::wstring(kCapabilities) + L"\\FileAssociations", ext, progId);
        ok &= SetEmpty(L"Software\\Classes\\Applications\\Lumen.exe\\SupportedTypes", ext);
    }
    return ok;
}

constexpr wchar_t kFileExts[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\";
constexpr uint64_t kMinute = 600000000;  // FILETIME ticks

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

// What Explorer will really open the type with: it drops a UserChoice whose hash it rejects.
bool OpensWith(const wchar_t* ext, const wchar_t* progId) {
    wchar_t got[128];
    DWORD n = static_cast<DWORD>(std::size(got));
    return AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, ext, nullptr, got, &n) == S_OK && _wcsicmp(got, progId) == 0;
}

}  // namespace

bool RegisterFileAssociations() {
    const std::wstring exe = ExePath();
    bool ok = WriteProgId(kVideoProgId, L"Lumen video", exe) && WriteProgId(kAudioProgId, L"Lumen audio", exe) &&
              WriteProgId(kPlaylistProgId, L"Lumen playlist", exe);
    ok &= SetValue(L"Software\\Classes\\Applications\\Lumen.exe\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
    ok &= SetValue(L"Software\\Classes\\Applications\\Lumen.exe", L"FriendlyAppName", L"Lumen");
    ok &= SetValue(kCapabilities, L"ApplicationName", L"Lumen");
    ok &= SetValue(kCapabilities, L"ApplicationDescription", L"Fast, minimal video and music player");
    ok &= SetValue(kCapabilities, L"ApplicationIcon", L"\"" + exe + L"\",0");
    ok &= WriteTypes(kVideo, kVideoProgId) && WriteTypes(kAudio, kAudioProgId) && WriteTypes(kPlaylists, kPlaylistProgId);
    for (const wchar_t* shell : {L"SystemFileAssociations\\video\\shell", L"SystemFileAssociations\\audio\\shell", L"Directory\\shell"})
        ok &= WriteVerbs(std::wstring(L"Software\\Classes\\") + shell, exe);
    ok &= SetValue(L"Software\\RegisteredApplications", L"Lumen", kCapabilities);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool UnregisterFileAssociations() {
    // A key that is already gone counts as removed.
    auto gone = [](LSTATUS s) { return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND; };
    auto removeTree = [&](const std::wstring& key) { return gone(RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str())); };
    bool ok = true;
    for (const wchar_t* key : {L"Software\\Classes\\Lumen.Video", L"Software\\Classes\\Lumen.Audio",
                               L"Software\\Classes\\Lumen.Playlist", L"Software\\Classes\\Applications\\Lumen.exe", L"Software\\Lumen"})
        ok &= removeTree(key);
    for (const wchar_t* shell : {L"SystemFileAssociations\\video\\shell", L"SystemFileAssociations\\audio\\shell", L"Directory\\shell"})
        for (const wchar_t* verb : {L"Lumen.play", L"Lumen.enqueue"})
            ok &= removeTree(std::wstring(L"Software\\Classes\\") + shell + L"\\" + verb);
    auto removeProgIds = [&](const auto& exts, const wchar_t* progId) {
        for (const wchar_t* ext : exts)
            ok &= gone(RegDeleteKeyValueW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ext + L"\\OpenWithProgids").c_str(), progId));
    };
    removeProgIds(kVideo, kVideoProgId);
    removeProgIds(kAudio, kAudioProgId);
    removeProgIds(kPlaylists, kPlaylistProgId);
    // Our own default choices go too: Windows then asks which app to use instead of failing to start Lumen.
    auto removeChoices = [&](const auto& exts, const wchar_t* progId) {
        for (const wchar_t* ext : exts) {
            const std::wstring key = std::wstring(kFileExts) + ext + L"\\UserChoice";
            wchar_t got[128];
            DWORD n = sizeof(got);
            if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"ProgId", RRF_RT_REG_SZ, nullptr, got, &n) == ERROR_SUCCESS &&
                _wcsicmp(got, progId) == 0)
                ok &= gone(RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str()));
        }
    };
    removeChoices(kVideo, kVideoProgId);
    removeChoices(kAudio, kAudioProgId);
    removeChoices(kPlaylists, kPlaylistProgId);
    ok &= gone(RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"Lumen"));
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool SetDefaultPlayer() {
    const std::wstring sid = UserSid();
    if (sid.empty()) return false;
    bool ok = true;
    auto set = [&](const auto& exts, const wchar_t* progId) {
        for (const wchar_t* ext : exts) ok &= WriteUserChoice(ext, progId, sid);
    };
    set(kVideo, kVideoProgId);
    set(kAudio, kAudioProgId);
    set(kPlaylists, kPlaylistProgId);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    // Builds that moved to UserChoiceLatest take the write but keep opening the old app.
    return ok && OpensWith(L".mp4", kVideoProgId) && OpensWith(L".mp3", kAudioProgId);
}

void OpenDefaultAppsSettings() {
    ui::Host::ShellOpen(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=Lumen", nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace app
