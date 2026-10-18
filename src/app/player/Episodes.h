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

// The library's S01E02 / 1x02 reading of the file name; the show as lowercase letters and digits.
inline Episode Parse(const std::wstring& path) {
    Episode e;
    library::EpisodeInfo info;
    if (!library::ParseEpisode(library::StemOf(path), &info)) return e;
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
    if (has({L"opening", L"intro", L"introduction", L"op", L"заставка", L"опенинг", L"вступление"}))
        return ChapterKind::Intro;
    if (has({L"credits", L"ed", L"ending", L"outro", L"титры", L"эндинг"})) return ChapterKind::Credits;
    return ChapterKind::Other;
}

}  // namespace app::episodes
