#include "app/FileAssociations.h"
#include "ui/Host.h"

#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <string>

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
    return SetValue(base, nullptr, friendly) && SetValue(base + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0") &&
           SetValue(base + L"\\shell", nullptr, L"open") && SetValue(base + L"\\shell\\open", L"FriendlyAppName", L"Lumen") &&
           SetValue(base + L"\\shell\\open\\command", nullptr, command);
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
    ok &= SetValue(L"Software\\RegisteredApplications", L"Lumen", kCapabilities);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

void OpenDefaultAppsSettings() {
    ui::Host::ShellOpen(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=Lumen", nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace app
