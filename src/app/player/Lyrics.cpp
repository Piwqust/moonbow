#include "Lyrics.h"  // relative: the unit tests build it without /I src

#include "../../library/TagReader.h"

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

// Takes enhanced-LRC word stamps ("<00:12.30>word") out of a line's text into `words`; the first
// word starts at the line's own stamp `start` when no word stamp comes before it. The text is
// trimmed and the offsets follow it.
std::wstring TakeWordStamps(std::wstring_view s, double start, std::vector<std::pair<uint32_t, double>>* words) {
    std::wstring out;
    out.reserve(s.size());
    words->clear();
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'<') {
            const size_t close = s.find(L'>', i);
            double t = 0;
            if (close != std::wstring_view::npos && ParseStamp(s.substr(i + 1, close - i - 1), &t)) {
                words->push_back({static_cast<uint32_t>(out.size()), t});
                i = close;
                continue;
            }
        }
        out += s[i];
    }
    size_t lead = 0;
    while (lead < out.size() && std::iswspace(out[lead])) ++lead;
    out.erase(0, lead);
    while (!out.empty() && std::iswspace(out.back())) out.pop_back();
    const uint32_t size = static_cast<uint32_t>(out.size());
    for (auto& w : *words) w.first = std::min(size, w.first > lead ? w.first - static_cast<uint32_t>(lead) : 0u);
    if (words->empty() || out.empty()) {
        words->clear();
        return out;
    }
    if (words->front().first > 0) words->insert(words->begin(), {0u, start});
    // Keep the stamps in order of both text and time: a stamp that goes back in time or two at
    // one place (a broken file) would make the fill run backwards.
    std::vector<std::pair<uint32_t, double>> kept;
    for (const auto& w : *words) {
        if (!kept.empty() && w.first == kept.back().first) kept.back().second = std::max(kept.back().second, w.second);
        else if (kept.empty() || w.second >= kept.back().second) kept.push_back(w);
    }
    *words = kept.size() >= 2 || (kept.size() == 1 && kept[0].first < size) ? std::move(kept) : decltype(kept){};
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

double Lyrics::SungFraction(int line, double t) const {
    if (!synced || line < 0 || line >= static_cast<int>(lines.size())) return 0;
    const LyricLine& l = lines[static_cast<size_t>(line)];
    // Even across the line; lines with word stamps use SungChars instead.
    double end = l.time + 0.9 + 0.075 * static_cast<double>(l.text.size());  // ~13 characters a second
    if (static_cast<size_t>(line) + 1 < lines.size()) end = std::min(end, lines[static_cast<size_t>(line) + 1].time);
    if (end <= l.time) return 1;
    return std::clamp((t - l.time) / (end - l.time), 0.0, 1.0);
}

double Lyrics::SungChars(int line, double t) const {
    if (!synced || line < 0 || line >= static_cast<int>(lines.size())) return -1;
    const LyricLine& l = lines[static_cast<size_t>(line)];
    if (l.words.empty()) return -1;
    const auto& w = l.words;
    if (t <= w.front().second) return w.front().first;
    for (size_t i = 0; i + 1 < w.size(); ++i) {
        if (t >= w[i + 1].second) continue;
        const double span = w[i + 1].second - w[i].second;
        const double f = span > 0 ? (t - w[i].second) / span : 1;
        return w[i].first + (w[i + 1].first - w[i].first) * f;
    }
    // The last word, when no stamp says where it ends: about as long as it takes to sing, never
    // past the next line.
    const auto& last = w.back();
    const double chars = static_cast<double>(l.text.size() - last.first);
    if (chars <= 0) return static_cast<double>(l.text.size());
    double end = last.second + 0.3 + 0.075 * chars;
    if (static_cast<size_t>(line) + 1 < lines.size()) end = std::min(end, lines[static_cast<size_t>(line) + 1].time);
    if (end <= last.second) return static_cast<double>(l.text.size());
    return last.first + chars * std::clamp((t - last.second) / (end - last.second), 0.0, 1.0);
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
            std::vector<std::pair<uint32_t, double>> stamped;
            const std::wstring words = TakeWordStamps(rest, stamps.front(), &stamped);
            for (double t : stamps) {
                // A line sung again ("[00:12.00][01:30.00]"): its words move with it.
                LyricLine l{t, words, stamped};
                for (auto& w : l.words) w.second += t - stamps.front();
                timed.push_back(std::move(l));
            }
        } else if (!idTag || !rest.empty()) {
            plain.emplace_back(Trim(line));
        }
        if (end == text.size()) break;
    }

    if (!timed.empty()) {
        out.synced = true;
        std::stable_sort(timed.begin(), timed.end(), [](const LyricLine& a, const LyricLine& b) { return a.time < b.time; });
        for (LyricLine& l : timed) {
            l.time = std::max(0.0, l.time - offset);
            for (auto& w : l.words) w.second = std::max(0.0, w.second - offset);
        }
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
        // Not CP_ACP: the manifest makes that UTF-8 inside Moonbow.exe (json::LegacyCodePage).
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

Lyrics LoadEmbeddedLyrics(const std::wstring& mediaPath) {
    if (mediaPath.empty() || mediaPath.find(L"://") != std::wstring::npos) return {};
    library::TagInfo tags;
    library::TagOptions opt;
    opt.lyrics = true;
    if (!library::ReadTagsFromFile(mediaPath, &tags, opt)) return {};
    Lyrics out;
    for (library::SyncedLyric& l : tags.syncedLyrics) out.lines.push_back({l.time, std::move(l.text)});
    std::stable_sort(out.lines.begin(), out.lines.end(), [](const LyricLine& a, const LyricLine& b) { return a.time < b.time; });
    // Leading blanks only mark silence, as in ParseLyrics.
    while (!out.lines.empty() && out.lines.front().text.empty()) out.lines.erase(out.lines.begin());
    while (!out.lines.empty() && out.lines.back().text.empty()) out.lines.pop_back();
    if (!out.lines.empty()) {
        out.synced = true;
        return out;
    }
    return ParseLyrics(tags.lyrics);
}

Lyrics LoadLyricsFor(const std::wstring& mediaPath) {
    Lyrics l = LoadLyricsFile(LyricsPathFor(mediaPath));
    return l.Empty() ? LoadEmbeddedLyrics(mediaPath) : l;
}

bool IsLyricsTag(std::string_view key) {
    auto iequals = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
    };
    if (iequals(key, "lyrics") || iequals(key, "unsyncedlyrics") || iequals(key, "unsynced lyrics")) return true;
    return key.size() > 7 && iequals(key.substr(0, 7), "lyrics-");
}

}  // namespace app
