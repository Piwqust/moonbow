#include "Lyrics.h"  // relative: the unit tests build it without /I src

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwctype>

namespace app {

namespace {

std::wstring_view Trim(std::wstring_view s) {
    while (!s.empty() && std::iswspace(s.front())) s.remove_prefix(1);
    while (!s.empty() && std::iswspace(s.back())) s.remove_suffix(1);
    return s;
}

bool Digit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

// Reads digits at `i`; false when there are none.
bool Number(std::wstring_view s, size_t& i, long long* value, size_t* count = nullptr) {
    const size_t start = i;
    long long v = 0;
    while (i < s.size() && Digit(s[i]) && i - start < 12) v = v * 10 + (s[i++] - L'0');
    if (i == start) return false;
    *value = v;
    if (count) *count = i - start;
    return true;
}

// "mm:ss", "mm:ss.xx", "mm:ss:xx" (old tools), "mm:ss.xxx", "h:mm:ss.xx". The whole tag must match.
bool ParseStamp(std::wstring_view s, double* seconds) {
    s = Trim(s);
    size_t i = 0;
    long long a = 0, b = 0;
    if (!Number(s, i, &a) || i >= s.size() || s[i] != L':') return false;
    ++i;
    if (!Number(s, i, &b)) return false;
    double t = static_cast<double>(a * 60 + b);
    if (i < s.size() && s[i] == L':') {
        // A third field: hours when a fraction follows it, otherwise hundredths.
        size_t j = i + 1;
        long long c = 0;
        size_t n = 0;
        if (!Number(s, j, &c, &n)) return false;
        if (j < s.size() && s[j] == L'.') {
            t = static_cast<double>(a * 3600 + b * 60 + c);
            i = j;
        } else {
            t += static_cast<double>(c) / std::pow(10.0, static_cast<double>(n));
            i = j;
        }
    }
    if (i < s.size() && s[i] == L'.') {
        ++i;
        long long f = 0;
        size_t n = 0;
        if (!Number(s, i, &f, &n)) return false;
        t += static_cast<double>(f) / std::pow(10.0, static_cast<double>(n));
    }
    if (i != s.size()) return false;
    *seconds = t;
    return true;
}

// Removes enhanced-LRC word stamps: "<00:12.30>word".
std::wstring StripWordStamps(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'<') {
            const size_t close = s.find(L'>', i);
            double t = 0;
            if (close != std::wstring_view::npos && ParseStamp(s.substr(i + 1, close - i - 1), &t)) {
                i = close;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

bool IsIdTag(std::wstring_view tag) {
    // "[ar:Artist]", "[length: 03:20]": a word, then a colon.
    const size_t colon = tag.find(L':');
    if (colon == std::wstring_view::npos || colon == 0) return false;
    for (size_t i = 0; i < colon; ++i)
        if (!std::iswalpha(tag[i]) && tag[i] != L'#') return false;
    return true;
}

}  // namespace

int Lyrics::LineAt(double t) const {
    if (!synced || lines.empty()) return -1;
    auto it = std::upper_bound(lines.begin(), lines.end(), t,
                               [](double v, const LyricLine& l) { return v < l.time; });
    return static_cast<int>(it - lines.begin()) - 1;
}

Lyrics ParseLyrics(std::wstring_view text) {
    Lyrics out;
    std::vector<LyricLine> timed;
    std::vector<std::wstring> plain;
    double offset = 0;  // seconds; "+" shows lines earlier

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring_view::npos) end = text.size();
        std::wstring_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);

        std::vector<double> stamps;
        bool idTag = false;
        std::wstring_view rest = Trim(line);
        while (!rest.empty() && rest.front() == L'[') {
            const size_t close = rest.find(L']');
            if (close == std::wstring_view::npos) break;
            const std::wstring_view tag = rest.substr(1, close - 1);
            double t = 0;
            if (ParseStamp(tag, &t)) {
                stamps.push_back(t);
            } else if (IsIdTag(tag)) {
                idTag = true;
                const std::wstring_view name = Trim(tag.substr(0, tag.find(L':')));
                if (name.size() == 6 && _wcsnicmp(name.data(), L"offset", 6) == 0) {
                    const std::wstring value(Trim(tag.substr(tag.find(L':') + 1)));
                    offset = _wtof(value.c_str()) / 1000.0;
                }
            } else {
                break;  // "[Chorus]" and the like: part of the text
            }
            rest = Trim(rest.substr(close + 1));
        }
        if (!stamps.empty()) {
            const std::wstring words(Trim(StripWordStamps(rest)));
            for (double t : stamps) timed.push_back({t, words});
        } else if (!idTag || !rest.empty()) {
            plain.emplace_back(Trim(line));
        }
        if (end == text.size()) break;
    }

    if (!timed.empty()) {
        out.synced = true;
        std::stable_sort(timed.begin(), timed.end(), [](const LyricLine& a, const LyricLine& b) { return a.time < b.time; });
        for (LyricLine& l : timed) l.time = std::max(0.0, l.time - offset);
        // Blank stamps before the first words and repeated blanks only mark silence.
        for (LyricLine& l : timed) {
            if (l.text.empty() && (out.lines.empty() || out.lines.back().text.empty())) continue;
            out.lines.push_back(std::move(l));
        }
        while (!out.lines.empty() && out.lines.back().text.empty()) out.lines.pop_back();
        if (out.lines.empty()) out.synced = false;
        return out;
    }

    // Unsynced: keep stanza breaks (single blank lines), drop blank edges.
    for (std::wstring& l : plain) {
        if (l.empty() && (out.lines.empty() || out.lines.back().text.empty())) continue;
        out.lines.push_back({-1, std::move(l)});
    }
    while (!out.lines.empty() && out.lines.back().text.empty()) out.lines.pop_back();
    return out;
}

std::wstring DecodeLyricsFile(std::string_view bytes, unsigned codePage) {
    auto widen = [](std::string_view s, UINT cp, DWORD flags) {
        std::wstring w;
        if (s.empty()) return w;
        const int n = MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (n <= 0) return w;
        w.resize(static_cast<size_t>(n));
        MultiByteToWideChar(cp, flags, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    };
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring w((bytes.size() - 2) / 2, L'\0');
        memcpy(w.data(), bytes.data() + 2, w.size() * 2);
        return w;
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE && static_cast<unsigned char>(bytes[1]) == 0xFF) {
        std::wstring w((bytes.size() - 2) / 2, L'\0');
        for (size_t i = 0; i < w.size(); ++i)
            w[i] = static_cast<wchar_t>((static_cast<unsigned char>(bytes[2 + 2 * i]) << 8) |
                                        static_cast<unsigned char>(bytes[3 + 2 * i]));
        return w;
    }
    if (bytes.size() >= 3 && bytes.substr(0, 3) == "\xEF\xBB\xBF") bytes.remove_prefix(3);
    // Strict UTF-8 first; invalid sequences mean a legacy code page.
    std::wstring w = widen(bytes, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!w.empty() || bytes.empty()) return w;
    if (!codePage) {
        // Not CP_ACP: the manifest makes that UTF-8 inside Lumen.exe (json::LegacyCodePage).
        DWORD cp = 0;
        GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                        reinterpret_cast<LPWSTR>(&cp), sizeof(cp) / sizeof(wchar_t));
        codePage = cp && cp != CP_UTF8 ? cp : 1252;
    }
    return widen(bytes, codePage, 0);
}

std::wstring LyricsPathFor(const std::wstring& mediaPath) {
    if (mediaPath.empty() || mediaPath.find(L"://") != std::wstring::npos) return {};
    const size_t slash = mediaPath.find_last_of(L"\\/");
    const size_t name = slash == std::wstring::npos ? 0 : slash + 1;
    const size_t dot = mediaPath.find_last_of(L'.');
    if (name >= mediaPath.size()) return {};
    const size_t end = dot != std::wstring::npos && dot > name ? dot : mediaPath.size();
    return mediaPath.substr(0, end) + L".lrc";
}

Lyrics LoadLyricsFile(const std::wstring& lrcPath) {
    if (lrcPath.empty()) return {};
    HANDLE f = CreateFileW(lrcPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    std::string bytes;
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= (1 << 20)) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) read = 0;
        bytes.resize(read);
    }
    CloseHandle(f);
    return ParseLyrics(DecodeLyricsFile(bytes));
}

bool IsLyricsTag(std::string_view key) {
    auto iequals = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
    };
    if (iequals(key, "lyrics") || iequals(key, "unsyncedlyrics") || iequals(key, "unsynced lyrics")) return true;
    return key.size() > 7 && iequals(key.substr(0, 7), "lyrics-");
}

}  // namespace app
