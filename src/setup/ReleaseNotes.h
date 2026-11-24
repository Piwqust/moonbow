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
    {L"0.4.0",
     L"Skip intro works without chapters: Moonbow finds the opening two episodes share by sound, on this computer.\n"
     L"Anime and other series numbered without a season are recognized; Series Settings… fixes a folder by hand.\n"
     L"Missing subtitles can download by themselves, with your own free OpenSubtitles key.\n"
     L"Scrobbling to Last.fm and ListenBrainz, and the song you play in your Discord profile.\n"
     L"Podcasts and audiobooks go on where you stopped.\n"
     L"DVD and Blu-ray discs with their menus; smoother motion on AMD Radeon; less power on battery.\n"
     L"VU meters read 3 dB lower by default.",
     L"«Пропустить заставку» работает и без глав: Moonbow находит общую заставку двух серий по звуку, на этом компьютере.\n"
     L"Сериалы и аниме с номерами без сезона распознаются; «Настройки сериала…» поправляют папку вручную.\n"
     L"Недостающие субтитры могут скачиваться сами, с вашим бесплатным ключом OpenSubtitles.\n"
     L"Скробблинг в Last.fm и ListenBrainz, а в профиле Discord видно, что играет.\n"
     L"Подкасты и аудиокниги продолжаются с места остановки.\n"
     L"DVD и Blu-ray с меню; плавнее движение на AMD Radeon; меньше расход на батарее.\n"
     L"VU-индикаторы по умолчанию показывают на 3 дБ меньше."},
    {L"0.3.0",
     L"Any part of the player opens in a window of its own: the cover, the seek bar, the lyrics, each audio meter.\n"
     L"Right-click in the player › Separate windows; they stay where you put them, on any screen.\n"
     L"Redrawn VU meters that grow with the window and read 3 dB lower.\n"
     L"Right-click a part of the music view or a button on the control bar to hide it.\n"
     L"A double click on the music view goes fullscreen.",
     L"Любую часть плеера можно открыть в отдельном окне: обложку, полосу перемотки, текст песни, каждый индикатор.\n"
     L"Правый щелчок в плеере › Отдельные окна; окна остаются там, где вы их поставили, на любом экране.\n"
     L"Новые VU-индикаторы растут вместе с окном и показывают на 3 дБ меньше.\n"
     L"Правый щелчок по части экрана музыки или по кнопке на панели скрывает её.\n"
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
