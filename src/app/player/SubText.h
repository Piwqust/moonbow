#pragma once
// Subtitle text for the word picker (SubWords.cpp): mpv's sub-text/ass split into events, ASS
// override tags stripped, words found without ICU, and the dictionary / translator links built.
// Header-only and window-free, so tests/core_tests.cpp checks it directly.
#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::subtext {

// One subtitle event as shown: plain text ('\n' between its lines) and its numpad alignment
// (1-3 bottom, 4-6 middle, 7-9 top; 2 when the event doesn't say). `marks` holds kItalic and
// kBold per character of `text` (empty when the event has neither).
struct Event {
    std::wstring text;
    int align = 2;
    std::vector<uint8_t> marks;
};

inline constexpr uint8_t kItalic = 1, kBold = 2;

// A word: [start, start + length) in the event's text, UTF-16 units.
struct Span {
    uint32_t start = 0, length = 0;
    bool operator==(const Span& o) const { return start == o.start && length == o.length; }
};

namespace detail {

inline bool IsDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

// Legacy \a values (SSA): 1-3 bottom, 5-7 top, 9-11 middle, in left/center/right order.
inline int LegacyAlign(int a) {
    if (a >= 1 && a <= 3) return a;
    if (a >= 5 && a <= 7) return a + 2;
    if (a >= 9 && a <= 11) return a - 5;
    return 0;
}

// Reads the tags of one override block {…}: the first alignment, the drawing mode and the
// italic / bold state (\i1, \b1 or a weight of 600 and more; \r goes back to plain).
inline void ParseBlock(std::wstring_view block, int& align, bool& drawing, uint8_t& mark) {
    for (size_t i = 0; i < block.size(); ++i) {
        if (block[i] != L'\\') continue;
        std::wstring_view rest = block.substr(i + 1);
        auto number = [](std::wstring_view s, size_t from) {
            int v = 0;
            size_t k = from;
            while (k < s.size() && IsDigit(s[k]) && v < 100000) v = v * 10 + (s[k++] - L'0');
            return k > from ? v : -1;
        };
        auto flag = [&](uint8_t bit, int v, int on) {
            if (v < 0) return;
            mark = v >= on ? static_cast<uint8_t>(mark | bit) : static_cast<uint8_t>(mark & ~bit);
        };
        if (rest.size() >= 3 && rest[0] == L'a' && rest[1] == L'n' && IsDigit(rest[2])) {
            const int v = number(rest, 2);
            if (align == 0 && v >= 1 && v <= 9) align = v;
        } else if (rest.size() >= 2 && rest[0] == L'a' && IsDigit(rest[1])) {
            const int v = LegacyAlign(number(rest, 1));
            if (align == 0 && v) align = v;
        } else if (rest.size() >= 2 && rest[0] == L'p' && IsDigit(rest[1])) {
            drawing = number(rest, 1) > 0;  // \p1 starts vector drawing commands, \p0 ends them
        } else if (rest.size() >= 2 && rest[0] == L'i' && IsDigit(rest[1])) {
            flag(kItalic, number(rest, 1), 1);
        } else if (rest.size() >= 2 && rest[0] == L'b' && IsDigit(rest[1])) {
            const int v = number(rest, 1);
            flag(kBold, v, v > 1 ? 600 : 1);  // \b1, or a weight such as \b700
        } else if (!rest.empty() && rest[0] == L'r' && (rest.size() == 1 || rest[1] == L'\\' || rest[1] == L'}')) {
            mark = 0;
        }
    }
}

inline bool IsSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == 0x00A0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200B);
}

// Trims each line, drops empty lines, keeps single '\n' between the rest. `marks`, when given,
// runs alongside `s` and is tidied the same way.
inline std::wstring TidyLines(const std::wstring& s, std::vector<uint8_t>* marks = nullptr) {
    std::wstring out;
    std::vector<uint8_t> kept;
    size_t from = 0;
    while (from <= s.size()) {
        size_t nl = s.find(L'\n', from);
        if (nl == std::wstring::npos) nl = s.size();
        size_t a = from, b = nl;
        while (a < b && IsSpace(s[a])) ++a;
        while (b > a && IsSpace(s[b - 1])) --b;
        if (b > a) {
            if (!out.empty()) {
                out += L'\n';
                if (marks) kept.push_back(0);
            }
            out.append(s, a, b - a);
            if (marks) kept.insert(kept.end(), marks->begin() + static_cast<ptrdiff_t>(a), marks->begin() + static_cast<ptrdiff_t>(b));
        }
        from = nl + 1;
    }
    if (marks) *marks = std::move(kept);
    return out;
}

}  // namespace detail

// One ASS event's Text field as plain text: override blocks {…} removed, vector drawings
// (\p1 … \p0) dropped, \N a line break, \n a space (libass breaks there only in wrap style 2),
// \h a no-break space. `align` gets the event's first \an (or legacy \a), 0 if none; `marks`
// gets kItalic / kBold per character, or stays empty when no character has either.
inline std::wstring StripAssTags(std::wstring_view s, int* align = nullptr, std::vector<uint8_t>* marks = nullptr) {
    std::wstring out;
    std::vector<uint8_t> m;
    out.reserve(s.size());
    int a = 0;
    bool drawing = false;
    uint8_t mark = 0;
    bool any = false;
    auto put = [&](wchar_t c) {
        out += c;
        m.push_back(mark);
        any |= mark != 0;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c == L'{') {
            const size_t close = s.find(L'}', i + 1);
            if (close != std::wstring_view::npos) {
                detail::ParseBlock(s.substr(i + 1, close - i - 1), a, drawing, mark);
                i = close;
                continue;
            }
            // An unclosed brace is shown as written, as libass does.
        }
        if (drawing) continue;
        if (c == L'\\' && i + 1 < s.size()) {
            const wchar_t n = s[i + 1];
            if (n == L'N') { put(L'\n'); ++i; continue; }
            if (n == L'n') { put(L' '); ++i; continue; }
            if (n == L'h') { put(static_cast<wchar_t>(0x00A0)); ++i; continue; }
        }
        put(c);
    }
    if (align) *align = a;
    std::wstring text = detail::TidyLines(out, &m);
    if (marks) {
        any = false;
        for (uint8_t k : m) any |= k != 0;
        if (any) *marks = std::move(m);
        else marks->clear();
    }
    return text;
}

// mpv's sub-text/ass: the Text fields of every event on screen, one per line. Events with no
// text left (drawings, empty karaoke lines) are dropped.
inline std::vector<Event> ParseEvents(std::wstring_view ass) {
    std::vector<Event> events;
    size_t from = 0;
    while (from <= ass.size()) {
        size_t nl = ass.find(L'\n', from);
        if (nl == std::wstring_view::npos) nl = ass.size();
        int align = 0;
        std::vector<uint8_t> marks;
        std::wstring text = StripAssTags(ass.substr(from, nl - from), &align, &marks);
        if (!text.empty()) events.push_back({std::move(text), align ? align : 2, std::move(marks)});
        from = nl + 1;
    }
    return events;
}

// Plain subtitle text (secondary-sub-text): only tidied, its lines already split by mpv.
inline std::wstring PlainText(std::wstring_view s) {
    std::wstring t(s);
    for (wchar_t& c : t)
        if (c == L'\r') c = L' ';
    return detail::TidyLines(t);
}

namespace detail {

enum class Kind : uint8_t { Other, Letter, Mark, Joiner, Ideograph, Hiragana, Katakana };

inline bool IsJoiner(wchar_t c) {
    // Apostrophes and hyphens inside a word ("don't", "кое-как", "l'homme"), the Catalan middle dot.
    return c == L'\'' || c == 0x2019 || c == 0x02BC || c == L'-' || c == 0x2010 || c == 0x2011 || c == 0x00B7;
}

inline std::vector<Kind> Classify(std::wstring_view s) {
    std::vector<Kind> kinds(s.size(), Kind::Other);
    if (s.empty()) return kinds;
    std::vector<WORD> t1(s.size()), t3(s.size());
    const int n = static_cast<int>(s.size());
    if (!GetStringTypeW(CT_CTYPE1, s.data(), n, t1.data()) || !GetStringTypeW(CT_CTYPE3, s.data(), n, t3.data()))
        return kinds;
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            // Outside the BMP only the CJK extension planes hold words; the rest is mostly emoji.
            const uint32_t cp = 0x10000 + ((static_cast<uint32_t>(c) - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            const Kind k = cp >= 0x20000 && cp <= 0x3FFFF ? Kind::Ideograph : Kind::Other;
            kinds[i] = k;
            kinds[i + 1] = k == Kind::Ideograph ? Kind::Mark : Kind::Other;  // the low half stays with it
            ++i;
            continue;
        }
        if (IsJoiner(c)) kinds[i] = Kind::Joiner;
        else if (c == 0x30FC || (t3[i] & C3_KATAKANA)) kinds[i] = Kind::Katakana;  // ー lengthens katakana
        else if (t3[i] & C3_HIRAGANA) kinds[i] = Kind::Hiragana;
        else if (((t3[i] & C3_IDEOGRAPH) && !(t1[i] & C1_PUNCT)) || c == 0x3005) kinds[i] = Kind::Ideograph;
        else if (t3[i] & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK)) kinds[i] = (t1[i] & C1_ALPHA) ? Kind::Letter : Kind::Mark;
        else if (t1[i] & (C1_ALPHA | C1_DIGIT)) kinds[i] = Kind::Letter;
    }
    return kinds;
}

}  // namespace detail

// The words of a text, in order. Letters and digits form words, with apostrophes, hyphens and
// combining marks inside them; spaces, punctuation and emoji separate them. Chinese characters
// are words of one character each (no dictionary to segment them), and runs of hiragana or of
// katakana are one word each. Thai, written without spaces, comes out one word per phrase.
inline std::vector<Span> SegmentWords(std::wstring_view s) {
    using detail::Kind;
    const std::vector<Kind> k = detail::Classify(s);
    std::vector<Span> words;
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const Kind kind = k[i];
        if (kind == Kind::Ideograph) {
            size_t e = i + 1;
            while (e < n && k[e] == Kind::Mark) ++e;
            words.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(e - i)});
            i = e;
            continue;
        }
        if (kind == Kind::Hiragana || kind == Kind::Katakana) {
            size_t e = i + 1;
            while (e < n && (k[e] == kind || k[e] == Kind::Mark)) ++e;
            words.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(e - i)});
            i = e;
            continue;
        }
        if (kind != Kind::Letter) {
            ++i;
            continue;
        }
        size_t e = i + 1;
        while (e < n) {
            if (k[e] == Kind::Letter || k[e] == Kind::Mark) {
                ++e;
            } else if (k[e] == Kind::Joiner && e + 1 < n && k[e + 1] == Kind::Letter) {
                e += 2;
            } else {
                break;
            }
        }
        words.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(e - i)});
        i = e;
    }
    return words;
}

// Text from the start of word `first` to the end of word `last` (inclusive), line breaks as spaces.
inline std::wstring WordRange(std::wstring_view text, const std::vector<Span>& words, size_t first, size_t last) {
    if (words.empty() || first >= words.size()) return {};
    if (last >= words.size()) last = words.size() - 1;
    if (last < first) std::swap(first, last);
    const size_t a = words[first].start, b = words[last].start + words[last].length;
    std::wstring out(text.substr(a, b - a));
    for (wchar_t& c : out)
        if (c == L'\n' || c == 0x00A0) c = L' ';
    return out;
}

// Every event joined into one line of text ("Copy phrase").
inline std::wstring Phrase(const std::vector<std::wstring>& parts) {
    std::wstring out;
    for (const std::wstring& p : parts) {
        for (wchar_t c : p) {
            const bool space = c == L'\n' || detail::IsSpace(c);
            if (space) {
                if (!out.empty() && out.back() != L' ') out += L' ';
            } else {
                out += c;
            }
        }
        if (!out.empty() && out.back() != L' ') out += L' ';
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

// Whether the first strong character is right-to-left (Hebrew, Arabic, Syriac, Thaana, N'Ko).
inline bool IsRtl(std::wstring_view s) {
    for (wchar_t c : s) {
        if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF)) return true;
        WORD t = 0;
        if (GetStringTypeW(CT_CTYPE1, &c, 1, &t) && (t & C1_ALPHA)) return false;
    }
    return false;
}

// Picture subtitles (Blu-ray PGS, DVD, DVB, XSUB) carry no text: no words to pick.
inline bool IsBitmapCodec(std::wstring_view codec) {
    static constexpr std::wstring_view kBitmap[] = {L"hdmv_pgs_subtitle", L"pgssub", L"dvd_subtitle", L"dvdsub",
                                                    L"dvb_subtitle", L"dvbsub", L"xsub", L"dvb_teletext"};
    for (std::wstring_view b : kBitmap)
        if (codec.size() == b.size() && _wcsnicmp(codec.data(), b.data(), b.size()) == 0) return true;
    return false;
}

// Percent-encodes UTF-8 for a URL query or path: everything but A-Z a-z 0-9 - _ . ~.
inline std::wstring UrlEncode(std::wstring_view s) {
    std::string utf8;
    if (!s.empty()) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        utf8.resize(static_cast<size_t>(n > 0 ? n : 0));
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), utf8.data(), n, nullptr, nullptr);
    }
    static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : utf8) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<wchar_t>(c);
        } else {
            out += L'%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

// A dictionary or translator link from a template of Settings > Subtitles: {text} becomes the
// encoded words, {lang} the interface language ("ru"). Only http and https links are opened, so
// a template can never start a program; anything else gives an empty string.
inline std::wstring LookupUrl(std::wstring_view tmpl, std::wstring_view text, std::wstring_view lang) {
    size_t a = 0, b = tmpl.size();
    while (a < b && detail::IsSpace(tmpl[a])) ++a;
    while (b > a && detail::IsSpace(tmpl[b - 1])) --b;
    tmpl = tmpl.substr(a, b - a);
    auto starts = [&](std::wstring_view p) { return tmpl.size() > p.size() && _wcsnicmp(tmpl.data(), p.data(), p.size()) == 0; };
    if (!starts(L"https://") && !starts(L"http://")) return {};
    for (wchar_t c : tmpl)
        if (c < 0x20 || c == L' ' || c == L'"') return {};
    const std::wstring encoded = UrlEncode(text);
    std::wstring out;
    bool used = false;
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl.compare(i, 6, L"{text}") == 0) {
            out += encoded;
            used = true;
            i += 5;
        } else if (tmpl.compare(i, 6, L"{lang}") == 0) {
            out += UrlEncode(lang);
            i += 5;
        } else {
            out += tmpl[i];
        }
    }
    // A template without {text} still gets the words, at the end (a plain search URL).
    if (!used) out += encoded;
    return out;
}

}  // namespace app::subtext
