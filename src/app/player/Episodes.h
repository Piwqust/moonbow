#pragma once
// Series helpers for the player (string logic, unit-tested in tests/core_tests.cpp): which
// file is the next episode of "Show.S01E02.mkv" / "Show 1x02.mkv", and chapter
// names that mark an opening or the end credits.
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <initializer_list>
#include <string>
#include <vector>

#include "../../library/MediaTypes.h"  // relative: tests/core_tests.cpp includes this header too

namespace app::episodes {

struct Episode {
    int season = -1, number = -1;
    std::wstring show;  // lowercase letters and digits before the marker ("show"), may be empty
    bool Valid() const { return season >= 0 && number >= 0; }
};

// The library's reading of the path (S01E02, 1x02, a numbered file in a season folder, an anime
// release "Show - 05"); the show as lowercase letters and digits.
inline Episode Parse(const std::wstring& path) {
    Episode e;
    library::EpisodeInfo info;
    if (!library::ParseEpisodePath(path, &info)) return e;
    e.season = info.season;
    e.number = info.episode;
    for (wchar_t c : info.show)
        if (std::iswalnum(c)) e.show.push_back(static_cast<wchar_t>(std::towlower(c)));
    return e;
}

// `next` continues `cur`: the following episode of the season, or the first of the next season.
inline bool IsNext(const std::wstring& cur, const std::wstring& next) {
    const Episode a = Parse(cur), b = Parse(next);
    if (!a.Valid() || !b.Valid()) return false;
    // Same show: the same name before the marker, or (when a name is missing) the same folder.
    const bool sameShow = !a.show.empty() && !b.show.empty()
                              ? a.show == b.show
                              : _wcsicmp(library::ParentFolder(cur).c_str(), library::ParentFolder(next).c_str()) == 0;
    if (!sameShow) return false;
    return (b.season == a.season && b.number == a.number + 1) || (b.season == a.season + 1 && b.number == 1);
}

// The next episode among `candidates` (the season's next number wins over the next season's first).
inline int FindNext(const std::wstring& cur, const std::vector<std::wstring>& candidates) {
    const Episode a = Parse(cur);
    if (!a.Valid()) return -1;
    int best = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!IsNext(cur, candidates[i])) continue;
        if (Parse(candidates[i]).season == a.season) return static_cast<int>(i);
        if (best < 0) best = static_cast<int>(i);
    }
    return best;
}

// "S01E03"
inline std::wstring Label(const Episode& e) {
    wchar_t buf[32];
    swprintf_s(buf, L"S%02dE%02d", e.season, e.number);
    return buf;
}

// The file name for a line under Label(): without the extension and without the S01E02 / 1x02
// marker the label already shows. "Show.Name.S01E02.The.Title.mkv" -> "Show Name · The Title";
// empty when nothing else is left ("S01E02.mkv").
inline std::wstring DisplayName(const std::wstring& path) {
    const std::wstring stem = library::StemOf(path);
    const Episode e = Parse(path);
    size_t from = std::wstring::npos, to = 0;
    auto number = [&stem](size_t i, size_t* end) {
        int v = -1;
        while (i < stem.size() && std::iswdigit(stem[i])) v = (v < 0 ? 0 : v * 10) + (stem[i++] - L'0');
        *end = i;
        return v;
    };
    if (e.Valid())
        for (size_t i = 0; i < stem.size() && from == std::wstring::npos; ++i) {
            if (i > 0 && std::iswalnum(stem[i - 1])) continue;  // a marker starts a word
            size_t j = 0, k = 0;
            const bool s = std::towlower(stem[i]) == L's';
            const int season = number(s ? i + 1 : i, &j);
            if (season != e.season || j >= stem.size()) continue;
            const wchar_t sep = static_cast<wchar_t>(std::towlower(stem[j]));
            if ((s && sep != L'e') || (!s && sep != L'x')) continue;
            if (number(j + 1, &k) != e.number) continue;
            from = i, to = k;
        }
    auto clean = [](std::wstring t) {
        for (wchar_t& c : t)
            if (c == L'.' || c == L'_') c = L' ';
        std::wstring out;
        for (wchar_t c : t)
            if (c != L' ' || (!out.empty() && out.back() != L' ')) out.push_back(c);
        auto trim = [](wchar_t c) { return c == L' ' || c == L'-' || c == L'–' || c == L'·' || c == L'['; };
        while (!out.empty() && (trim(out.back()) || out.back() == L'(')) out.pop_back();
        size_t lead = 0;
        while (lead < out.size() && (trim(out[lead]) || out[lead] == L']' || out[lead] == L')')) ++lead;
        return out.substr(lead);
    };
    if (from == std::wstring::npos) return clean(stem);
    const std::wstring show = clean(stem.substr(0, from)), title = clean(stem.substr(to));
    if (show.empty() || title.empty()) return show + title;
    return show + L" · " + title;
}

// "Show — S01E02": an episode as the window title and the messages name it (a container's title
// tag rarely says which episode it is); empty when `path` is not an episode.
inline std::wstring Title(const std::wstring& path) {
    const Episode e = Parse(path);
    if (!e.Valid()) return {};
    const std::wstring name = DisplayName(path);
    return name.empty() ? Label(e) : name + L" — " + Label(e);
}

enum class ChapterKind { Other, Intro, Credits };

// Chapter titles such as "Opening", "Intro", "OP", "OP1", "Заставка" (intro) or "Credits",
// "ED", "Ending", "Титры" (credits). Whole words only: "Operation" is neither.
inline ChapterKind KindOf(const std::wstring& title) {
    const std::wstring folded = library::SearchFold(title);
    std::vector<std::wstring> words;
    std::wstring w;
    for (size_t i = 0; i <= folded.size(); ++i) {
        const wchar_t c = i < folded.size() ? folded[i] : L' ';
        if (std::iswalnum(c) || (c >= 0x400 && c <= 0x4FF)) {
            w.push_back(c);
        } else if (!w.empty()) {
            while (w.size() > 2 && std::iswdigit(w.back())) w.pop_back();  // "op1", "ed2"
            words.push_back(w);
            w.clear();
        }
    }
    auto has = [&words](std::initializer_list<const wchar_t*> set) {
        for (const std::wstring& word : words)
            for (const wchar_t* s : set)
                if (word == s) return true;
        return false;
    };
    // "OP" and "ED" only as the whole title, maybe numbered: "Sonata Op. 27" and "Ed's Story" are
    // neither.
    auto alone = [&words](const wchar_t* s) {
        int named = 0;
        for (const std::wstring& word : words)
            if (!std::iswdigit(word[0])) named += word == s ? 1 : 2;
        return named == 1;
    };
    if (alone(L"op") || has({L"opening", L"intro", L"introduction", L"заставка", L"опенинг", L"вступление"}))
        return ChapterKind::Intro;
    if (alone(L"ed") || has({L"credits", L"ending", L"outro", L"титры", L"эндинг"})) return ChapterKind::Credits;
    return ChapterKind::Other;
}

}  // namespace app::episodes
