#pragma once
// What each version brings: the setup shows it before an update, Moonbow once after one.
// When the version in res/app.rc goes up, add its entry on top: one plain line per change.

#include "Installer.h"

#include <string>
#include <vector>

namespace setup {

struct ReleaseNote {
    const wchar_t* version;
    const wchar_t* en;  // lines split by \n
    const wchar_t* ru;
};

inline constexpr ReleaseNote kReleaseNotes[] = {
    {L"0.3.0",
     L"Any part of the player opens in a window of its own: the cover, the seek bar, the lyrics, each audio meter.\n"
     L"Right-click in the player › Separate windows; they stay where you put them, on any screen.\n"
     L"Redrawn VU meters that grow with the window, calibrated 3 dB lower (0 VU = −7 dBFS).\n"
     L"A double click on the music view goes fullscreen.",
     L"Любую часть плеера можно открыть в отдельном окне: обложку, полосу перемотки, текст песни, каждый индикатор.\n"
     L"Правый щелчок в плеере › Отдельные окна; окна остаются там, где вы их поставили, на любом экране.\n"
     L"Новые VU-индикаторы растут вместе с окном и показывают на 3 дБ меньше (0 VU = −7 dBFS).\n"
     L"Двойной щелчок по экрану музыки включает полный экран."},
    {L"0.2.0",
     L"Moonbow updates itself: a new version downloads in the background and installs when you close Moonbow.\n"
     L"Settings › General › Updates shows the version, checks now and turns updates off.\n"
     L"Every update is signed and checked before it runs.",
     L"Moonbow обновляется сам: новая версия скачивается в фоне и ставится, когда вы закрываете Moonbow.\n"
     L"В Настройки › Общие › Обновления видна версия, там же можно проверить сейчас или выключить обновления.\n"
     L"Каждое обновление подписано и проверяется перед запуском."},
    {L"0.1.0",
     L"The first beta: videos and music in one fast, quiet player.\n"
     L"A library that remembers where you stopped, with playlists and bookmarks.\n"
     L"Song lyrics, subtitles and a sound visualizer.\n"
     L"Its own setup: update to any version and go back to any kept one.",
     L"Первая бета: видео и музыка в одном быстром и тихом плеере.\n"
     L"Медиатека помнит, где вы остановились, есть плейлисты и закладки.\n"
     L"Тексты песен, субтитры и визуализация звука.\n"
     L"Свой установщик: обновление на любую версию и возврат к любой сохранённой."},
};

// The lines of the versions after `from` up to and including `to`, newest first.
inline std::vector<std::wstring> NotesBetween(const std::wstring& from, const std::wstring& to, bool russian) {
    std::vector<std::wstring> lines;
    for (const ReleaseNote& n : kReleaseNotes) {
        if (CompareVersions(n.version, to) > 0 || (!from.empty() && CompareVersions(n.version, from) <= 0)) continue;
        std::wstring_view text = russian ? n.ru : n.en;
        while (!text.empty()) {
            const size_t end = text.find(L'\n');
            lines.emplace_back(text.substr(0, end));
            text.remove_prefix(end == std::wstring_view::npos ? text.size() : end + 1);
        }
    }
    return lines;
}

}  // namespace setup
