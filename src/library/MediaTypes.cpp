#include "MediaTypes.h"

#include <windows.h>

#include <cstring>
#include <cwctype>
#include <initializer_list>

namespace library {

namespace {

constexpr std::wstring_view kVideoExts[] = {
    L"mkv", L"mp4",  L"m4v",  L"avi", L"mov", L"webm", L"wmv", L"flv", L"ts",   L"m2ts", L"mts", L"mpg",
    L"mpeg", L"vob", L"ogv",  L"3gp", L"3g2", L"rmvb", L"rm",  L"hevc", L"av1", L"h264", L"h265", L"264",
    L"265", L"m2v",  L"divx", L"xvid", L"asf", L"f4v", L"mxf", L"y4m", L"dv",   L"ivf",  L"qt",  L"mk3d",
};

constexpr std::wstring_view kAudioExts[] = {
    L"mp3", L"flac", L"m4a", L"aac", L"ogg", L"oga", L"opus", L"wav", L"wma", L"ac3", L"dts",
    L"eac3", L"aiff", L"aif", L"aifc", L"alac", L"ape", L"wv", L"mka", L"m4b", L"mpc", L"tta",
    L"amr", L"au", L"caf", L"dsf", L"dff", L"spx", L"mp2", L"mpa", L"tak", L"w64", L"ra",
};

constexpr std::wstring_view kSubtitleExts[] = {L"srt", L"ass", L"ssa", L"sub", L"idx", L"vtt", L"sup", L"smi", L"lrc"};
constexpr std::wstring_view kPlaylistExts[] = {L"m3u", L"m3u8", L"pls", L"cue", L"xspf"};

// Lower-cased extension without the dot, or empty. Extensions are ASCII in practice.
std::wstring ExtOf(std::wstring_view path) {
    size_t dot = path.find_last_of(L'.');
    size_t slash = path.find_last_of(L"\\/");
    if (dot == std::wstring_view::npos || (slash != std::wstring_view::npos && dot < slash)) return {};
    std::wstring ext(path.substr(dot + 1));
    if (ext.size() > 8) return {};
    for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return ext;
}

template <size_t N>
bool InList(const std::wstring& ext, const std::wstring_view (&list)[N]) {
    if (ext.empty()) return false;
    for (std::wstring_view e : list)
        if (ext == e) return true;
    return false;
}

std::wstring MapCase(std::wstring_view s, DWORD flags) {
    if (s.empty()) return {};
    int n = LCMapStringEx(LOCALE_NAME_INVARIANT, flags, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr,
                          nullptr, 0);
    if (n <= 0) return std::wstring(s);
    std::wstring out(static_cast<size_t>(n), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, flags, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                  nullptr, 0);
    return out;
}

}  // namespace

MediaKind KindOf(std::wstring_view path) {
    std::wstring ext = ExtOf(path);
    if (InList(ext, kVideoExts)) return MediaKind::Video;
    if (InList(ext, kAudioExts)) return MediaKind::Audio;
    return MediaKind::Other;
}

bool IsMediaPath(std::wstring_view path) {
    return KindOf(path) != MediaKind::Other;
}

bool IsSubtitlePath(std::wstring_view path) {
    return InList(ExtOf(path), kSubtitleExts);
}

bool IsPlaylistPath(std::wstring_view path) {
    return InList(ExtOf(path), kPlaylistExts);
}

const char* KindName(MediaKind kind) {
    switch (kind) {
        case MediaKind::Video: return "video";
        case MediaKind::Audio: return "audio";
        default: return "other";
    }
}

MediaKind KindFromName(std::string_view name) {
    if (name == "video") return MediaKind::Video;
    if (name == "audio") return MediaKind::Audio;
    return MediaKind::Other;
}

std::wstring NormalizePath(std::wstring_view path) {
    std::wstring out(path);
    for (wchar_t& c : out)
        if (c == L'/') c = L'\\';
    while (out.size() > 1 && out.back() == L'\\') {
        if (out.size() == 3 && out[1] == L':') break;  // "C:\"
        out.pop_back();
    }
    return out;
}

std::wstring PathKey(std::wstring_view path) {
    return MapCase(NormalizePath(path), LCMAP_UPPERCASE);
}

std::wstring ParentFolder(std::wstring_view path) {
    std::wstring p = NormalizePath(path);
    size_t slash = p.find_last_of(L'\\');
    if (slash == std::wstring::npos) return {};
    if (slash == 2 && p[1] == L':') return p.substr(0, 3);  // "C:\a.mkv" -> "C:\"
    return p.substr(0, slash);
}

std::wstring FileNameOf(std::wstring_view path) {
    size_t slash = path.find_last_of(L"\\/");
    return std::wstring(slash == std::wstring_view::npos ? path : path.substr(slash + 1));
}

std::wstring StemOf(std::wstring_view path) {
    std::wstring name = FileNameOf(path);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return name;
}

std::wstring SearchFold(std::wstring_view text) {
    std::wstring out = MapCase(text, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING);
    for (wchar_t& c : out)
        if (c == L'ё') c = L'е';
    return out;
}

int64_t UnixMsNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return FileTimeToUnixMs((static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
}

int64_t FileTimeToUnixMs(uint64_t t) {
    constexpr uint64_t kEpochDiff = 116444736000000000ULL;  // 1601 -> 1970 in 100 ns units
    if (t < kEpochDiff) return 0;
    return static_cast<int64_t>((t - kEpochDiff) / 10000);
}

// --- episodes --------------------------------------------------------------------------------

namespace {

bool IsDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}
bool IsAsciiLetter(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// Reads 1..maxDigits digits at `i`; fails when more digits follow.
bool ReadNumber(std::wstring_view s, size_t& i, size_t maxDigits, int* value) {
    const size_t start = i;
    int v = 0;
    while (i < s.size() && IsDigit(s[i]) && i - start < maxDigits) v = v * 10 + (s[i++] - L'0');
    if (i == start || (i < s.size() && IsDigit(s[i]))) return false;
    *value = v;
    return true;
}

std::wstring ShowName(std::wstring_view s) {
    std::wstring out(s);
    for (wchar_t& c : out)
        if (c == L'.' || c == L'_') c = L' ';
    auto junk = [](wchar_t c) {
        return c == L' ' || c == L'-' || c == L'[' || c == L'(' || c == L'—' || c == L'–';
    };
    while (!out.empty() && junk(out.back())) out.pop_back();
    size_t b = 0;
    while (b < out.size() && junk(out[b])) ++b;
    return out.substr(b);
}

}  // namespace

bool ParseEpisode(std::wstring_view name, EpisodeInfo* out) {
    for (size_t i = 0; i < name.size(); ++i) {
        const bool boundary = i == 0 || !(IsAsciiLetter(name[i - 1]) || IsDigit(name[i - 1]));
        if (!boundary) continue;
        size_t j = i;
        int season = 0, episode = 0;
        if (name[i] == L'S' || name[i] == L's') {
            // S01E02, s1e2, S01.E02
            ++j;
            if (!ReadNumber(name, j, 2, &season)) continue;
            if (j < name.size() && (name[j] == L'.' || name[j] == L' ')) ++j;
            if (j >= name.size() || (name[j] != L'E' && name[j] != L'e')) continue;
            ++j;
            if (!ReadNumber(name, j, 3, &episode)) continue;
        } else if (IsDigit(name[i])) {
            // 1x02, but not 1920x1080: the season has at most two digits.
            if (!ReadNumber(name, j, 2, &season)) continue;
            if (j >= name.size() || (name[j] != L'x' && name[j] != L'X')) continue;
            ++j;
            if (j >= name.size() || !IsDigit(name[j]) || !ReadNumber(name, j, 3, &episode)) continue;
        } else {
            continue;
        }
        // "2x03abc" is not a marker; "S01E02E03" (a double episode) is.
        if (j < name.size() && IsAsciiLetter(name[j]) && name[j] != L'E' && name[j] != L'e') continue;
        if (episode <= 0) continue;
        if (out) {
            out->show = ShowName(name.substr(0, i));
            out->season = season;
            out->episode = episode;
        }
        return true;
    }
    return false;
}

int ParseDiscNumber(std::wstring_view s) {
    size_t i = 0;
    while (i < s.size() && std::iswspace(s[i])) ++i;
    int v = 0;
    if (!ReadNumber(s, i, 3, &v)) return 0;
    return v;
}

// --- header check ----------------------------------------------------------------------------

bool HeaderMatchesExtension(std::wstring_view path, const unsigned char* h, size_t n) {
    const std::wstring ext = ExtOf(path);
    auto is = [&](std::initializer_list<const wchar_t*> list) {
        for (const wchar_t* e : list)
            if (ext == e) return true;
        return false;
    };
    auto at = [&](size_t off, const char* magic, size_t len) { return n >= off + len && memcmp(h + off, magic, len) == 0; };
    // No real container fits in 12 bytes: an empty or tiny "movie" is a placeholder or text.
    if (n < 12) return KindOf(path) == MediaKind::Other;
    if (is({L"mp4", L"m4v", L"m4a", L"m4b", L"mov", L"qt", L"3gp", L"3g2", L"f4v"}))
        return at(4, "ftyp", 4) || at(4, "moov", 4) || at(4, "mdat", 4) || at(4, "free", 4) || at(4, "wide", 4) ||
               at(4, "skip", 4);
    if (is({L"mkv", L"webm", L"mka", L"mk3d"})) return at(0, "\x1A\x45\xDF\xA3", 4);
    if (is({L"avi", L"wav"})) return at(0, "RIFF", 4) || at(0, "RF64", 4);
    if (is({L"flac"})) return at(0, "fLaC", 4) || at(0, "ID3", 3);
    if (is({L"ogg", L"oga", L"ogv", L"opus", L"spx"})) return at(0, "OggS", 4);
    if (is({L"wmv", L"wma", L"asf"})) return at(0, "\x30\x26\xB2\x75", 4);
    if (is({L"mp3"})) return at(0, "ID3", 3) || (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0);
    return true;
}

}  // namespace library
