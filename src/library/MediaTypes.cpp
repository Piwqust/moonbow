#include "MediaTypes.h"

#include <windows.h>

#include <cstring>
#include <cwchar>
#include <algorithm>
#include <cwctype>
#include <initializer_list>
#include <mutex>
#include <vector>

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

std::wstring DiscEntry(const std::wstring& path) {
    auto file = [](const std::wstring& p) {
        const DWORD a = GetFileAttributesW(p.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    };
    if (path.empty() || path.find(L"://") != std::wstring::npos) return {};
    if (ExtOf(path) == L"iso") return path;
    std::wstring dir = path;
    while (dir.size() > 3 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    const std::wstring name = MapCase(FileNameOf(dir), LCMAP_UPPERCASE);
    if (name == L"VIDEO_TS.IFO" || name == L"MOVIEOBJECT.BDMV") return path;
    const DWORD a = GetFileAttributesW(dir.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return {};
    for (const wchar_t* sub : {L"\\VIDEO_TS\\VIDEO_TS.IFO", L"\\VIDEO_TS.IFO", L"\\BDMV\\MovieObject.bdmv",
                               L"\\MovieObject.bdmv"})
        if (file(dir + sub)) return dir + sub;
    return {};
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

std::vector<std::pair<size_t, size_t>> MatchRanges(std::wstring_view text, const std::wstring& foldedQuery) {
    std::vector<std::pair<size_t, size_t>> out;
    const std::wstring folded = SearchFold(text);
    if (folded.size() != text.size()) return out;
    for (size_t i = 0, end = 0; i < foldedQuery.size(); i = end + 1) {
        end = foldedQuery.find(L' ', i);
        if (end == std::wstring::npos) end = foldedQuery.size();
        if (end == i) continue;
        const std::wstring word = foldedQuery.substr(i, end - i);
        for (size_t at = folded.find(word); at != std::wstring::npos; at = folded.find(word, at + word.size()))
            out.emplace_back(at, word.size());
    }
    std::sort(out.begin(), out.end());
    std::vector<std::pair<size_t, size_t>> merged;
    for (const auto& r : out) {
        if (!merged.empty() && r.first <= merged.back().first + merged.back().second)
            merged.back().second = std::max(merged.back().second, r.first + r.second - merged.back().first);
        else
            merged.push_back(r);
    }
    return merged;
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

bool IsReleaseTag(std::wstring_view word) {
    static constexpr std::wstring_view kTags[] = {
        L"2160p",  L"1080p",  L"1080i",  L"720p",   L"576p",   L"480p",     L"4k",     L"uhd",     L"hdr",
        L"hdr10",  L"dv",     L"bluray", L"bdrip",  L"brrip",  L"bdremux",  L"remux",  L"webrip",  L"webdl",
        L"web",    L"hdtv",   L"hdrip",  L"dvdrip", L"dvd",    L"dvdscr",   L"x264",   L"x265",    L"h264",
        L"h265",   L"hevc",   L"avc",    L"xvid",   L"divx",   L"aac",      L"ac3",    L"dts",     L"ddp5",
        L"dd5",    L"atmos",  L"10bit",  L"8bit",   L"proper", L"repack",   L"extended", L"unrated", L"imax",
        L"remastered", L"multi", L"dubbed", L"subbed", L"internal",
    };
    std::wstring w;
    for (wchar_t c : word)
        if (c != L'-') w.push_back(static_cast<wchar_t>(std::towlower(c)));
    for (std::wstring_view t : kTags)
        if (w == t) return true;
    return false;
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

std::wstring EpisodeTitle(std::wstring_view rest) {
    std::wstring words;
    size_t i = 0;
    bool first = true;
    while (i < rest.size()) {
        while (i < rest.size() && (rest[i] == L'.' || rest[i] == L'_' || rest[i] == L' ')) ++i;
        size_t e = i;
        while (e < rest.size() && rest[e] != L'.' && rest[e] != L'_' && rest[e] != L' ') ++e;
        if (e == i) break;
        std::wstring_view w = rest.substr(i, e - i);
        i = e;
        // A double episode's second number ("S01E02E03") and dashes between parts are not words.
        if (first && w.size() >= 2 && (w[0] == L'E' || w[0] == L'e') && IsDigit(w[1])) continue;
        if (w == L"-" || w == L"—" || w == L"–") continue;
        if (IsReleaseTag(w) || (w.front() == L'[' || w.front() == L'(')) break;
        first = false;
        if (!words.empty()) words += L' ';
        words += w;
    }
    return words;
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
            out->title = EpisodeTitle(name.substr(j));
        }
        return true;
    }
    return false;
}

std::wstring EpisodeLabel(std::wstring_view name) {
    EpisodeInfo ep;
    if (!ParseEpisode(name, &ep)) return {};
    wchar_t code[32];
    swprintf_s(code, L"S%02dE%02d", ep.season, ep.episode);
    std::wstring out = code;
    const std::wstring& rest = ep.title.empty() ? ep.show : ep.title;
    if (!rest.empty()) out += L" · " + rest;
    return out;
}

namespace {

// Words of a name between spaces, dots, underscores, dashes and brackets.
std::vector<std::wstring_view> Words(std::wstring_view s) {
    auto sep = [](wchar_t c) {
        return c == L' ' || c == L'.' || c == L'_' || c == L'-' || c == L'(' || c == L')' || c == L'[' ||
               c == L']' || c == L',' || c == L'—' || c == L'–';
    };
    std::vector<std::wstring_view> out;
    for (size_t i = 0; i < s.size();) {
        while (i < s.size() && sep(s[i])) ++i;
        size_t e = i;
        while (e < s.size() && !sep(s[e])) ++e;
        if (e > i) out.push_back(s.substr(i, e - i));
        i = e;
    }
    return out;
}

bool WordIs(std::wstring_view w, std::initializer_list<std::wstring_view> list) {
    for (std::wstring_view x : list)
        if (CompareStringOrdinal(w.data(), static_cast<int>(w.size()), x.data(), static_cast<int>(x.size()), TRUE) ==
            CSTR_EQUAL)
            return true;
    return false;
}

// A word that is only a number of 1..maxDigits digits, else -1.
int NumberWord(std::wstring_view w, size_t maxDigits) {
    size_t i = 0;
    int v = 0;
    return ReadNumber(w, i, maxDigits, &v) && i == w.size() ? v : -1;
}

// Offset of the word `w` (a view into `s`) and of the end of it.
size_t Begin(std::wstring_view s, std::wstring_view w) { return static_cast<size_t>(w.data() - s.data()); }
size_t End(std::wstring_view s, std::wstring_view w) { return Begin(s, w) + w.size(); }

// Letters and digits only, folded: "Frieren: Beyond" and "Frieren - Beyond" compare equal.
std::wstring Squashed(std::wstring_view s) {
    std::wstring out;
    for (wchar_t c : SearchFold(s))
        if (std::iswalnum(c)) out.push_back(c);
    return out;
}

// Episodes numbered without a season, as anime and many shows are released:
//   "[Group] Show Name - 05 [1080p] [ABCD1234]", "Show Name - 12v2 (BD 1080p)" (in the show's folder or
//   followed by release tags), "Show.Name.E05.1080p", "Show Name Ep 05".
// They read as season 1. A plain "Title - 13" elsewhere stays a film ("Apollo - 13").
bool ParseAbsoluteEpisode(std::wstring_view stem, std::wstring_view folderName, EpisodeInfo* out) {
    std::wstring_view s = stem;
    bool group = false;
    while (!s.empty() && s.front() == L'[') {  // fansub groups and tags in front
        const size_t close = s.find(L']');
        if (close == std::wstring_view::npos) break;
        s.remove_prefix(close + 1);
        while (!s.empty() && (s.front() == L' ' || s.front() == L'.' || s.front() == L'_')) s.remove_prefix(1);
        group = true;
    }
    // Release tags in brackets right after the number ("[1080p WEB-DL]", "(BD 1080p)"), not a year
    // ("Apollo - 13 (1995)").
    auto taggedTail = [](std::wstring_view rest) {
        while (!rest.empty() && (rest.front() == L' ' || rest.front() == L'.' || rest.front() == L'_')) rest.remove_prefix(1);
        if (rest.empty() || (rest.front() != L'[' && rest.front() != L'(')) return false;
        const size_t close = rest.find(rest.front() == L'[' ? L']' : L')');
        const std::wstring_view inside = rest.substr(1, close == std::wstring_view::npos ? std::wstring_view::npos : close - 1);
        const int year = NumberWord(inside, 4);
        return !(inside.size() == 4 && year >= 1900 && year <= 2100);
    };
    // "Show - 05": a dash between spaces, then the number (a "v2" revision may follow).
    for (size_t at = s.find(L" - "); at != std::wstring_view::npos; at = s.find(L" - ", at + 1)) {
        size_t i = at + 3, j = i;
        int number = 0;
        if (!ReadNumber(s, j, 4, &number) || number <= 0 || (j - i == 4 && number >= 1900 && number <= 2100)) continue;
        if (j + 1 < s.size() && (s[j] == L'v' || s[j] == L'V') && IsDigit(s[j + 1])) j += 2;
        if (j < s.size() && s[j] != L' ' && s[j] != L'[' && s[j] != L'(' && s[j] != L'.' && s[j] != L'_') continue;
        const std::wstring show = ShowName(s.substr(0, at));
        if (show.empty()) continue;
        const std::wstring_view rest = s.substr(j);
        if (!group && Squashed(show) != Squashed(folderName) && !taggedTail(rest)) continue;
        if (out) {
            out->show = show;
            out->season = 1;
            out->episode = number;
            out->title = EpisodeTitle(rest);
        }
        return true;
    }
    // "E05", "Ep05", "Ep 05" after the show's name.
    const auto words = Words(s);
    for (size_t k = 1; k < words.size(); ++k) {
        const std::wstring_view w = words[k];
        int number = -1;
        size_t last = k;
        if (w.size() >= 3 && (w[0] == L'E' || w[0] == L'e') && IsDigit(w[1])) {  // "E05", not the weaker "E5"
            number = NumberWord(w.substr(1), 3);
        } else if (w.size() >= 3 && (w[0] == L'E' || w[0] == L'e') && (w[1] == L'P' || w[1] == L'p') && IsDigit(w[2])) {
            number = NumberWord(w.substr(2), 3);
        } else if (WordIs(w, {L"ep"}) && k + 1 < words.size()) {
            number = NumberWord(words[k + 1], 3);
            last = k + 1;
        }
        if (number <= 0) continue;
        const std::wstring show = ShowName(s.substr(0, Begin(s, words[k])));
        if (show.empty()) continue;
        if (out) {
            out->show = show;
            out->season = 1;
            out->episode = number;
            out->title = EpisodeTitle(s.substr(End(s, words[last])));
        }
        return true;
    }
    return false;
}

}  // namespace

int ParseSeasonFolder(std::wstring_view name, std::wstring* show) {
    const auto words = Words(name);
    for (size_t k = 0; k < words.size(); ++k) {
        const std::wstring_view w = words[k];
        size_t last = k;  // last word of the marker
        int season = w.size() >= 2 && (w[0] == L'S' || w[0] == L's') ? NumberWord(w.substr(1), 2) : -1;
        if (season < 0 && k + 1 < words.size() && WordIs(w, {L"season", L"сезон"})) {
            season = NumberWord(words[k + 1], 2);
            last = k + 1;
        }
        if (season < 0 && k + 1 < words.size() && WordIs(words[k + 1], {L"season", L"сезон"})) {
            season = NumberWord(w, 2);
            last = k + 1;
        }
        if (season < 0) continue;
        // Only release tags and a year may follow: "Best S1 Clips" is not a season.
        bool tail = true;
        for (size_t j = last + 1; j < words.size() && tail; ++j)
            tail = IsReleaseTag(words[j]) || (words[j].size() == 4 && NumberWord(words[j], 4) >= 1900);
        if (!tail) continue;
        if (show) *show = ShowName(name.substr(0, Begin(name, w)));
        return season;
    }
    return -1;
}

namespace {

std::mutex g_overridesMutex;
std::vector<std::pair<std::wstring, SeriesOverride>> g_overrides;  // folder as given, by PathKey below
std::vector<std::wstring> g_overrideKeys;

}  // namespace

void SetSeriesOverride(const std::wstring& folder, const SeriesOverride* o) {
    const std::wstring key = PathKey(folder);
    std::lock_guard lock(g_overridesMutex);
    for (size_t i = 0; i < g_overrideKeys.size(); ++i)
        if (g_overrideKeys[i] == key) {
            g_overrideKeys.erase(g_overrideKeys.begin() + static_cast<std::ptrdiff_t>(i));
            g_overrides.erase(g_overrides.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    if (!o) return;
    g_overrideKeys.push_back(key);
    g_overrides.push_back({folder, *o});
}

bool SeriesOverrideFor(std::wstring_view folder, SeriesOverride* out) {
    std::lock_guard lock(g_overridesMutex);
    if (g_overrideKeys.empty()) return false;
    const std::wstring key = PathKey(folder);
    for (size_t i = 0; i < g_overrideKeys.size(); ++i)
        if (g_overrideKeys[i] == key) {
            if (out) *out = g_overrides[i].second;
            return true;
        }
    return false;
}

std::vector<std::pair<std::wstring, SeriesOverride>> SeriesOverrides() {
    std::lock_guard lock(g_overridesMutex);
    return g_overrides;
}

bool ParseEpisodePath(std::wstring_view path, EpisodeInfo* out) {
    const std::wstring stem = StemOf(path);
    const std::wstring folder = ParentFolder(path);
    if (SeriesOverride o; SeriesOverrideFor(folder, &o)) {
        if (o.notSeries) return false;
        // The name's own number (S01E05, "- 05", E05, "05 Title"), else its first number.
        EpisodeInfo ep;
        if (!ParseEpisode(stem, &ep) && !ParseAbsoluteEpisode(stem, o.show, &ep)) {
            int number = -1;
            for (const std::wstring_view w : Words(stem))
                if ((number = NumberWord(w, 4)) > 0) break;
            if (number <= 0) return false;
            ep.episode = number;
        }
        ep.show = o.show.empty() ? ShowName(FileNameOf(folder)) : o.show;
        ep.season = o.season;
        if (out) *out = std::move(ep);
        return true;
    }
    std::wstring folderShow;
    const int season = ParseSeasonFolder(FileNameOf(folder), &folderShow);
    EpisodeInfo ep;
    if (!ParseEpisode(stem, &ep)) {
        if (season < 0) {
            if (!ParseAbsoluteEpisode(stem, FileNameOf(folder), &ep)) return false;
            if (out) *out = std::move(ep);
            return true;
        }
        // A numbered file in a season folder: "03 Title", "E03", "Ep03", "Episode 3", "Серия 3", "3 серия".
        const std::wstring_view s = stem;
        const auto words = Words(s);
        int number = -1;
        size_t after = 0;
        for (size_t k = 0; k < words.size() && number < 0; ++k) {
            const std::wstring_view w = words[k];
            const bool next = k + 1 < words.size();
            if (k == 0 && (number = NumberWord(w, 3)) >= 0) {
                after = End(s, w);
            } else if (w.size() >= 2 && (w[0] == L'E' || w[0] == L'e') &&
                       ((number = NumberWord(w.substr(1), 3)) >= 0 ||
                        (w.size() >= 3 && (w[1] == L'P' || w[1] == L'p') && (number = NumberWord(w.substr(2), 3)) >= 0))) {
                after = End(s, w);
            } else if (next && WordIs(w, {L"episode", L"ep", L"серия", L"эпизод"}) &&
                       (number = NumberWord(words[k + 1], 3)) >= 0) {
                after = End(s, words[k + 1]);
            } else if (next && WordIs(words[k + 1], {L"серия"}) && (number = NumberWord(w, 3)) >= 0) {
                after = End(s, words[k + 1]);
            }
        }
        if (number <= 0) return false;
        ep.season = season;
        ep.episode = number;
        ep.title = EpisodeTitle(s.substr(after));
    }
    if (ep.show.empty())
        ep.show = season < 0             ? ShowName(FileNameOf(folder))
                  : !folderShow.empty() ? folderShow
                                        : ShowName(FileNameOf(ParentFolder(folder)));
    if (out) *out = std::move(ep);
    return true;
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

bool HeaderLooksLikeMedia(const unsigned char* h, size_t n) {
    auto at = [&](size_t off, const char* magic, size_t len) { return n >= off + len && memcmp(h + off, magic, len) == 0; };
    if (n < 4) return false;
    return at(4, "ftyp", 4) || at(4, "moov", 4) || at(4, "mdat", 4) ||  // MP4 / MOV
           at(0, "\x1A\x45\xDF\xA3", 4) ||                                  // Matroska / WebM
           at(0, "RIFF", 4) || at(0, "RF64", 4) ||                            // AVI / WAV
           at(0, "fLaC", 4) || at(0, "OggS", 4) || at(0, "ID3", 3) ||          // FLAC, Ogg, tagged MP3
           at(0, "\x30\x26\xB2\x75", 4) ||                                  // ASF (WMV / WMA)
           at(0, "FLV", 3) || at(0, "\x00\x00\x01\xBA", 4) ||                 // FLV, MPEG-PS
           (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0);                            // MPEG audio / ADTS frame
}

static std::wstring SubtitleMatchKey(const std::wstring& path) {
    std::wstring out;
    for (wchar_t c : StemOf(path))
        if (std::iswalnum(c)) out.push_back(static_cast<wchar_t>(std::towlower(c)));
    return out;
}

// "s01e02" / "1x02" -> {1, 2}; {-1, -1} when there is none.
static std::pair<int, int> SubtitleEpisodeOf(const std::wstring& path) {
    std::wstring s = StemOf(path);
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    for (size_t i = 0; i + 3 < s.size(); ++i) {
        size_t j = i;
        if (s[j] == L's' && std::iswdigit(s[j + 1])) {
            ++j;
            int season = 0, ep = 0;
            while (j < s.size() && std::iswdigit(s[j])) season = season * 10 + (s[j++] - L'0');
            if (j < s.size() && s[j] == L'e' && j + 1 < s.size() && std::iswdigit(s[j + 1])) {
                ++j;
                while (j < s.size() && std::iswdigit(s[j])) ep = ep * 10 + (s[j++] - L'0');
                return {season, ep};
            }
        }
    }
    for (size_t i = 1; i + 1 < s.size(); ++i)
        if (s[i] == L'x' && std::iswdigit(s[i - 1]) && std::iswdigit(s[i + 1])) {
            size_t a = i;
            while (a > 0 && std::iswdigit(s[a - 1])) --a;
            size_t b = i + 1;
            int season = 0, ep = 0;
            for (size_t k = a; k < i; ++k) season = season * 10 + (s[k] - L'0');
            while (b < s.size() && std::iswdigit(s[b])) ep = ep * 10 + (s[b++] - L'0');
            if (i - a <= 2) return {season, ep};
        }
    return {-1, -1};
}

int SubtitleMatchScore(const std::wstring& video, const std::wstring& sub) {
    const std::wstring v = SubtitleMatchKey(video), s = SubtitleMatchKey(sub);
    if (v.empty() || s.empty()) return 0;
    if (s == v) return 100;
    // A prefix whose number goes on is another file: "lecture1" is not "lecture10".
    auto numberGoesOn = [](const std::wstring& shorter, const std::wstring& longer) {
        return std::iswdigit(shorter.back()) && std::iswdigit(longer[shorter.size()]);
    };
    if (s.rfind(v, 0) == 0 && !numberGoesOn(v, s)) return 90;  // "Show_E01_English" for "Show_E01"
    if (v.rfind(s, 0) == 0 && s.size() * 2 > v.size() && !numberGoesOn(s, v)) return 70;
    const auto ev = SubtitleEpisodeOf(video), es = SubtitleEpisodeOf(sub);
    if (ev.first >= 0 && ev == es) return 60;
    return 0;
}

}  // namespace library
