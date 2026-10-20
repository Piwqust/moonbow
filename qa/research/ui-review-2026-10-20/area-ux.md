# Area UX: user experience, information architecture, forms, microcopy

Reviewer: UX/IA/copy. Build: `%TEMP%\lumen-qa\uir-build\Lumen.exe` (main @ ff96c7f).
Method: `ui-design-review` sections C, H, I, K, applied to a native Win32 app. Web-only checks (skip links,
HTML validity, breakpoints) skipped. Evidence: the sweep at `%TEMP%\lumen-qa\uir-sweep\` plus three invisible
`--automation` instances of my own (`%TEMP%\lumen-qa\uir-ux-a|b|c`). My screenshots are in
`%TEMP%\lumen-qa\uir-ux-shots\`. Previous audit: `qa/research/UX-AUDIT-2026-10-19.md` (#N below).

## Nielsen's 10 heuristics

| # | Heuristic | Verdict | Strongest evidence |
|---|---|---|---|
| 1 | Visibility of system status | Fair | Good: toasts after most actions ("Плейлист «Мой микс» создан" with «Открыть», scan banner, «Продолжаем с 0:41»). Bad: «Удалить из истории» and an empty rename do nothing visible (UX-9, UX-12). A missing library folder is reported as "no media" (UX-5) |
| 2 | Match with the real world | Fair | «Фильмы / Сериалы», plain errors («Файл повреждён или его формат не поддерживается»). But the word «Сеть» names three places (UX-7). Quick panel and settings use unexplained jargon (UX-21) |
| 3 | User control and freedom | Poor | Alt+Left/Alt+Right are swallowed on card pages, and mouse Back does nothing in the library (UX-2). A wrong library folder cannot be removed (UX-1). Deleting a playlist or a history entry has no undo (UX-9) |
| 4 | Consistency and standards | Fair | The click rules are now one. Still: «Плейлист» means the queue in the player menu (UX-10), two settings both labelled "continue where you left off" (UX-6), a pencil icon that removes (UX-13), Ctrl+F opens a folder picker (UX-15) |
| 5 | Error prevention | Fair | Confirm on delete, URL lines validated as you type. But onboarding accepts `C:\` as «Папка найдена» (UX-16), and one switch under «Запуск» silently drops every confirmation (UX-8) |
| 6 | Recognition rather than recall | Good | Shortcuts in tooltips and menus, F1 list with search, hint toast «Пробел — пауза · F — полный экран · F1 — все клавиши». Weak spots: Favorites hidden inside History (UX-17), playlist reorder only by an unmarked drag (UX-11) |
| 7 | Flexibility and efficiency | Fair | Full key set, YouTube preset, Ctrl+K search, context menus everywhere. No multi-select in library lists. Search does not match words in any order (UX-4) |
| 8 | Aesthetic and minimalist design | Good | Calm, one visual language. Small noise: «0 элементов» next to «Ничего не найдено», a search box on pages with nothing to search (UX-18, UX-19) |
| 9 | Recognize, diagnose, recover from errors | Fair | «Битый файл» badge, specific online errors («TMDB не принял ключ API. Проверьте его в …»). But a missing folder gets the wrong diagnosis (UX-5), and the error card of a single broken file ends at a dead player (UX-22) |
| 10 | Help and documentation | Fair | An (i) description on every setting, a step-by-step key how-to for OpenSubtitles and TMDB. No help/FAQ. Nothing explains which panel controls are per file and which are global (UX-14) |

## Key tasks (steps counted on this build)

| Task | Steps | Path and friction |
|---|---|---|
| Resume the series I was watching | 2 | Home → ▶ on «Продолжить просмотр» card «Series · Дальше: S01E02». Good (`home-after-series.png`) |
| Play an album shuffled | 3 | Музыка → right-click album → «Перемешать» (or open album → «Перемешать»). From code `LibMusic.cpp:462` and the sweep |
| Find a song by part of its name | 3 | Search box (or Ctrl+K) → «umber 3» → Enter. Fails on the default Music tab (UX-3) and with words in another order, e.g. «track 3» (UX-4) |
| Add subtitles from a file next to the video | 0 / 1 / 4+ | Same name: loaded on its own. Drag the .srt onto the video: 1 step, toast «Субтитры добавлены» (doesn't say which track). Otherwise V → «+» → native dialog → pick (dialog not opened, per rules) |
| Make a playlist of 5 songs and reorder it | ~20 | Song: right-click → «Добавить в плейлист» → «Новый плейлист…» → name → Enter (5). Then 4 more songs × 3 clicks (no multi-select). Then «Плейлисты» → open → drag (mouse only, list view only) (UX-11) |
| Change the audio track and remember it for the series | 2 | A → pick the track. Remembered per show automatically (audit #23), confirmed by the toast on the next episode. No visible "for this series / for all" choice |
| Fix a wrong library folder | impossible | No list of library folders anywhere. «Убрать из медиатеки» exists only on the card of a root folder, and a root never shows as a card (UX-1) |

## Strengths

- **English is complete.** In the four `lang-en-*` dumps the only Cyrillic is user data (song and file names). All 694 `Tr()` strings have a Russian pair.
- **Global search is instant.** It covers artists, albums and songs in groups and shows «Результаты: «artist»». Esc clears it and returns home (`search-artist-instant.png`).
- **Forms validate inline and keep input.**
  - URL dialog: «Строка 1 не похожа на ссылку…» appears as you type, and `www.youtube.com/…` without a scheme is accepted.
  - Onboarding: the folder field reports «Такой папки нет…» or «Папка найдена…» as you type.
  - The typed path survives a language switch.
  - New playlist: the field has focus and Enter creates the playlist.
- **Empty states explain and offer an action.** Playlists, Network and History have them, with «Создать плейлист» / «Импортировать плейлист» and «Открыть файл».
- **Destructive copy names its consequences.** «Файлы останутся на диске», «Сами медиафайлы останутся на месте». Clearing history lists everything that goes. Removing items from the queue or a playlist has «Отменить».
- **Settings search works in both languages.** It matches labels and descriptions in RU and EN, so «buffer» finds «Буферизация».

## Findings

### UX-1. A wrong library folder can't be seen or removed anywhere in the app
- Severity: High · Standard: Nielsen #3, #9; Windows guideline "Let users undo / manage what they added"
- Where: Settings › Медиатека; Видео › Папки; header «Ещё»; sidebar «Добавить». Code `src/app/library/Views.cpp:674-683` (the remove item exists only when the folder card *is* a root), `src/core/SettingsSchema.cpp:1959-1995` (no folder list)
- Evidence:
  - Library root was `…\lumen-qa\media`. «Видео › Папки» shows its children «Mixed» and «Videos». Their menu has no «Убрать из медиатеки» because they aren't roots, and the root itself never appears as a card.
  - Settings › Медиатека has no list of folders (`settings-library.dark/final.png`), though the sweep plan describes that scenario as "Settings, Library (folders list)".
  - The «Ещё» menu offers only «Добавить папку…/Пересканировать».
- Impact: anyone who added the wrong folder (onboarding pre-fills the real Videos folder, and `C:\` is accepted, see UX-16) or whose folder moved can't undo it. Their only fix is editing `library.json`.
- Fix: add a «Папки медиатеки» list to Settings › Медиатека, as a `SettingRow` list like the add-ons list in Extensions. Each row shows path, file count and status (found / not found), with «Убрать» (undo toast) and «Показать в проводнике», plus «Добавить папку…» below. Also put «Папки медиатеки…» in the library «Ещё» menu.
- Verify: launch with `-Library <parent folder>` → Settings › Медиатека lists it → «Убрать» → Videos empty state → «Отменить» restores it.

### UX-2. Back/Forward: Alt+Arrows are swallowed on card pages, and mouse Back does nothing in the library
- Severity: High · Standard: Nielsen #3, #4; Windows navigation conventions (Alt+Left, XButton1 = Back)
- Where: any card grid (Видео, Видео › Videos, Альбомы, Плейлисты). Code `src/app/library/ItemGrid.cpp:1103-1110`: VK_LEFT/VK_RIGHT move the selection without checking `mods.alt`. `src/app/player/PlayerView.cpp:334-337`: X1/X2 = previous/next file. The library has no X-button or `WM_APPCOMMAND` handler (grep finds none).
- Evidence (my instance, synthetic WM_SYSKEYDOWN with the automation Alt modifier and WM_XBUTTONDOWN/UP):
  - On «Видео › Videos», Alt+Left left the route at `videos …\Videos` and drew a focus ring on the first card (`altleft-cards.png`).
  - On «Музыка», Alt+Right didn't go forward, but the `nav forward` command did.
  - XButton1/XButton2 on «Музыка» changed nothing.
  - Alt+Left works only on list pages (album → Музыка), so the same key works on some pages and not others. Backspace works everywhere.
- Impact: mouse-with-side-buttons and keyboard users (MPC, Explorer, browser habits) can't go back from the most common pages. In the player, the same mouse button skips the track instead of going back.
- Fix: in `ItemGrid::OnKeyDown`, return false for arrows when `e.mods.alt`. Handle `MouseButton::X1/X2` in the library (`Shell`/`LibraryPane::OnMouseDown`) as `GoBack/GoForward`. In `PlayerView`, X1 should leave the player to the library like Esc (or make that a setting).
- Verify: `extra-input.ps1 -AltKey Left` on `nav videos <folder>` → route goes back. `-XButton 1` on Музыка → route home.

### UX-3. Page search filters only the open tab, and the result says "nothing here" with no way out
- Severity: High · Standard: Nielsen #1, #9; skill H "Poor search UX"
- Where: Музыка (tab Альбомы, the default) and Видео (tab Фильмы). Placeholder «Поиск в «Музыка»». Code `src/app/library/Views.cpp:885` (`Search(filter_, kind_)` within the page)
- Evidence: Музыка › Альбомы, typed «track» → «0 элементов», «Ничего не найдено. Здесь нет совпадений с «track»…» (`music-albums-search.png`), although 4 songs are named "Track Number N" and the global search finds them. Same on Видео.
- Impact: the most natural path for "find a song" (open Music, type) fails. The placeholder promises all of Music, and the tip to "try a shorter word" sends the user the wrong way.
- Fix: when the active tab has no match but other tabs or the whole library do, the empty state should say «В альбомах нет «track». Найдено: 4 песни» with buttons «Показать в «Песни»» and «Искать во всей медиатеке». Or let Music search always show grouped Artists/Albums/Songs like Home.
- Verify: Музыка › Альбомы, type «track» → the empty state offers «Песни · 4», and one click shows them.

### UX-4. Search matches one exact substring, so words in another order or across fields find nothing
- Severity: Medium · Standard: Nielsen #7; skill H "Search functionality"
- Where: `src/library/MediaLibrary.cpp:141-148` (`FillSearchKey`: name + title + artist + album joined by newlines), `:504-511` (`FoldedContains(searchKey, q)` on the whole query). Settings search is the same (`SettingsSchema.cpp:2384`).
- Evidence (global search, my instance):
  - «umber 3» → 1 result.
  - «track 3», «num 3», «alb one» → «Ничего не найдено».
  - Genre isn't indexed.
  - Settings search ignores page and section names and option values (e.g. «EBU R128»), and its empty state is one bare line (`settings-search-none/final.png`), unlike the library's.
- Impact: «artist song» or «song 3» style queries, the way people type into Spotify or Windows search, fail silently.
- Fix: split the folded query on spaces and require every token to be found (in any field); add genre to `FillSearchKey`. Do the same in `settings::Search` and include `Cat`/`Sec` titles.
- Verify: search «track 3» → Track Number 3; «artist a 2» → Track Number 2; Settings «конфиденциальность сеть» → the online-services rows.

### UX-5. A missing library folder is reported as "your folders have no videos"
- Severity: High · Standard: Nielsen #9, #1
- Where: Home and Видео with a root that doesn't exist (unplugged USB drive, renamed folder). Code `src/app/library/LibHome.cpp:607`
- Evidence: instance with `-Library …\uir-ux-missing-folder` (`missing-root-home.png`, `missing-root-videos.png`).
  - Home says «Медиафайлов пока нет. В папках медиатеки нет видео и музыки. Добавьте другую папку или откройте файл.»
  - Видео says «Фильмов нет».
  - Neither names the folder or says it is missing.
- Impact: a user whose drive is unplugged thinks the library was wiped and adds the folder again (creating a duplicate root), or gives up.
- Fix: when a root doesn't exist, show a `ScanBanner`-style bar «Папка «D:\Films» недоступна. Подключите диск или уберите её из медиатеки» with «Убрать» and «Повторить». Keep the cached items greyed rather than empty.
- Verify: launch with a missing root → banner names the path. Create the folder → «Повторить» clears it.

### UX-6. Two different settings are both called "continue where you left off"; onboarding names them differently again
- Severity: Medium · Standard: Nielsen #4; skill K "Inconsistent terminology"
- Where:
  - Onboarding toggle «Продолжать с места остановки» = `privacy.rememberPosition` (`src/app/dialogs/Onboarding.cpp:541`).
  - Settings › Конфиденциальность shows the same key as «Запоминать место остановки» (`SettingsSchema.cpp:935`).
  - Settings › Воспроизведение has a *different* key `playback.resume` labelled «Продолжать с места остановки» (`SettingsSchema.cpp:1093`).
  - Also onboarding «Запоминать, что вы смотрите и слушаете» = Settings «Сохранять недавние файлы» (`privacy.saveHistory`).
- Evidence: `ob-3.png` and `settings-playback.dark/final.png` show the same label for two keys.
- Impact: a user who turned resume off in onboarding finds «Продолжать с места остановки: Всегда» in Playback and doesn't know which one wins.
- Fix: one concept, one place. Keep the `playback.resume` pick (Всегда / Спрашивать / Никогда) and show `privacy.rememberPosition` under it as «Сохранять место остановки». Use the Settings labels in Onboarding word for word (shared `ShellText`).
- Verify: `python tools/dev/check-language.py` (or a grep) finds each label once per key, and onboarding labels equal the settings labels.

### UX-7. «Сеть» names three places; the lyrics consent points to the wrong one
- Severity: Medium · Standard: Nielsen #2, #4
- Where:
  - Sidebar «Сеть» = streams and radio.
  - Settings page «Сеть» = buffering, proxy, yt-dlp.
  - Section «Сеть» inside Settings › Конфиденциальность = LRCLIB, OpenSubtitles, TMDB.
  - String `src/app/strings/online.inc` «… выключить это можно в «Настройки › Сеть»».
- Evidence: the LRCLIB toggle exists only in Конфиденциальность (`SettingsSchema.cpp:951`). The Settings › Сеть page lists no online services (dump of `settings-network.dark`).
- Impact: a user following the consent card's instruction lands on the wrong page and can't find the switch.
- Fix: rename the Privacy section to «Онлайн-сервисы» (the string already exists in `online.inc`) and point every message to «Настройки › Конфиденциальность › Онлайн-сервисы». Use one separator: `›` (dialogs.inc still uses `>` in «(Настройки > Сеть)»).
- Verify: grep the strings for `Настройки >` and `› Сеть` → none. Settings search «LRCLIB» shows the section title «Онлайн-сервисы».

### UX-8. One switch under «Запуск» removes every safety confirmation, and only in Settings
- Severity: Medium · Standard: Nielsen #5; skill I "Missing confirmation for destructive actions"
- Where: Settings › Общие › Запуск › «Показывать подтверждения и ошибки» (`SettingsSchema.cpp:745`). `src/app/settings/SettingsRows.cpp:1376` skips `Confirm` when it is off. Library `ShowConfirm` (`LibraryCommon.cpp:631`) ignores it.
- Evidence (from code):
  - With the switch off, Settings › Конфиденциальность › «Очистить» runs at once. It wipes history, saved positions, per-file settings and online data, and its own description says «Отменить нельзя».
  - Deleting a playlist from the library still asks, so the same switch works in one window and not the other.
  - The switch sits under «Запуск» (Startup), which it has nothing to do with.
- Impact: a user who turns off "error dialogs" (because of noisy errors) unknowingly loses the only guard on irreversible actions.
- Fix: split the switch. «Ошибки воспроизведения: окно / сообщение» goes under Воспроизведение. Irreversible actions always confirm, or switch to an undo toast. Move it out of «Запуск».
- Verify: switch off → Очистить историю still asks (or offers «Отменить» for 8 s).

### UX-9. Deleting a playlist or a history entry has no undo, and Back then leads to a dead end — still open (audit #17)
- Severity: Medium · Standard: Nielsen #3, #5
- Where:
  - «Ещё › Удалить…» on a playlist page: confirm, no undo.
  - «Удалить из истории» (`src/app/library/LibHome.cpp:103-106`): instant, no toast, no undo.
- Evidence:
  - After «Удалить из истории» the History page simply showed «Недавних файлов нет» (`remove-history.png`). The item's resume point went with it.
  - After deleting «Мой микс», the Back arrow led to «Плейлист не найден. Возможно, он был удалён.» (`back-to-deleted.png`), though the app itself had just deleted it.
  - Removing from a playlist or the queue *does* offer «Отменить» (audit #17 fix), so similar actions behave differently.
- Impact: one misclick loses a resume point or a hand-built playlist.
- Fix: use the same undo toast as `Убрать из плейлиста` for «Удалить из истории» and playlist delete (keep the object for 8 s). Drop routes to deleted playlists from the back stack (`App::GoBack` skips routes whose playlist id is gone).
- Verify: delete playlist → toast «Плейлист удалён · Отменить» → undo restores items. Back after a delete skips the missing page.

### UX-10. The queue is called «Плейлист» in the player menu and in settings
- Severity: Medium · Standard: Nielsen #4; skill K "Use consistent terminology"
- Where:
  - Player right-click menu: submenu «Плейлист» lists the *queue*, and its empty item reads «Очередь пуста» (`src/app/player/PlayerView.cpp:221-230`, `menu-context.dark/final.png`).
  - Settings › Интерфейс «Добавлять в плейлист, а не заменять» (`SettingsSchema.cpp:872`).
  - Playback descriptions «…весь плейлист…», «Воспроизводить плейлист в случайном порядке», «последнего файла плейлиста» (`:1160-1193`).
  - A-B: control «Повтор A-B», toasts «Начало петли», «Петля 0:10 – 0:20», «Петля выключена» (`player.inc`).
- Evidence: string table and the menu screenshot.
- Impact: since audit #26 the queue and saved playlists are two separate things («Сохранить как плейлист»). Calling the queue «Плейлист» makes users look for their saved playlists in the player menu, and makes them think the Interface setting edits saved playlists.
- Fix: rename to «Очередь» in `PlayerView.cpp:221`, `library.enqueue` («Добавлять в очередь, а не заменять») and the Playback descriptions. A-B toasts become «Повтор A-B: начало 0:10», «Повтор A-B: 0:10 – 0:20», «Повтор A-B выключен».
- Verify: grep RU strings for «плейлист» where the code means the queue → none. `check-language.py` passes.

### UX-11. Building and ordering a playlist is mouse-only and one song at a time
- Severity: Medium · Standard: Nielsen #7; skill C "Too many steps"
- Where: library lists (`src/app/library/ItemGrid.cpp:946,1014`: Ctrl/Shift+click selects one item only). Playlist page (`LibPlaylists.cpp:266`: reorder only when sort = manual; `ItemGrid.cpp:968`: drag only in list style, not cards).
- Evidence: 5 songs into «Мой микс» took 5 + 4×3 = 17 actions. The playlist row menu has no «Переместить выше/ниже», though the queue has them (`QueuePanel.cpp:686`). Reorder worked by drag (`mymix-reordered.png`), with no handle or hint. In grid view (the default for video-heavy playlists) or any other sort, nothing can be dragged and nothing says why.
- Impact: Spotify/foobar users expect Ctrl/Shift multi-select → «Добавить в плейлист». Keyboard users can't reorder at all.
- Fix: multi-selection in `ItemGrid` (Ctrl toggles, Shift ranges, the menu acts on the selection). Add «Переместить выше/ниже» (Alt+Up/Down) to the playlist row menu as in the queue. Show the drag handle on hover in manual order.
- Verify: Ctrl-click 5 songs → right-click → «Добавить в плейлист» adds 5. Alt+Up moves a row.

### UX-12. An empty playlist name is silently ignored on rename, but blocked on create
- Severity: Medium · Standard: Nielsen #1, #4; skill I "Validation", "Labels"
- Where: «Переименовать плейлист» (`src/app/library/LibraryCommon.cpp:616-618`, `if (text.empty()) return;`). «Новый плейлист» dialog.
- Evidence:
  - Rename: cleared the field, clicked «Переименовать» (button looks enabled). Nothing happened and there was no message (`rename-empty-click.png`).
  - New playlist: «Создать» is visibly disabled until a name is typed (`newpl-dialog.png`).
  - The name field is labelled «Название» in Rename but «Плейлист» in New Playlist.
  - New Playlist lists the file as «01 Track Number 1.mp3» instead of the song title.
- Impact: users click repeatedly and think the app hung.
- Fix: in `ShowTextPrompt`, disable the OK `DButton` while the trimmed text is empty (as the New Playlist dialog does). Label the field «Название» in both. Show song titles in the New Playlist list.
- Verify: clear the field → «Переименовать» greys out. Both dialogs show «Название».

### UX-13. Icons that say something else than they do
- Severity: Medium · Standard: Nielsen #4; Windows icon guidelines
- Where:
  - Audio and Subtitles tabs, «Внешние дорожки»: a pencil icon whose tooltip is «Убрать внешние дорожки» (`src/app/player/TracksPanel.cpp:287`). It is shown even when the list says «Не добавлены».
  - Mini player: an ✕ icon with tooltip «Стоп (S)».
  - The three icons ✎ 🔍 + still have no visible labels (walkthrough-1 leftover).
- Evidence: `panel-tracks-subs.dark/final.tree.txt:244-246`, main-window dump of `uir-ux-b`.
- Impact: a pencil reads as "edit"; a user who clicks it to rename a track loses the external tracks. An ✕ reads as "close the mini player" but stops playback.
- Fix: use the `remove` icon (as in the queue header) and hide or disable it when nothing is added. Give the mini player close button `icon=stop`, or make it close the mini player and keep stop on S.
- Verify: the dump shows `icon=remove tip="Убрать внешние дорожки"`, disabled with «Не добавлены».

### UX-14. The quick panel mixes per-file and global changes under one heading
- Severity: Medium · Standard: Nielsen #1, #10
- Where: quick panel tabs Видео / Аудио / Субтитры, section heading `Tr(L"Defaults")` (`src/app/player/SettingsPanel.cpp:540,759,859`), translated «Основные».
- Evidence: under one heading,
  - «Отражение», «Повернуть» and «Вторые субтитры» act on this file only («…действуют только для текущего файла»);
  - «HDR» and «Язык субтитров» change global settings (`AddConfigChoice`).
  - EN calls the section «Defaults», RU «Основные» (two different meanings).
  - The «Набор настроек: стандартный» bar is still unexplained (walkthrough-1 leftover «Набор по умолчанию»).
- Impact: a user changes «Язык субтитров» for one film and finds every later file affected, or the opposite.
- Fix: two headings, «Для этого файла» and «Для всех файлов», with config-backed rows only under the second. Give the preset bar a one-line explanation («Сохранить текущие значения как набор»).
- Verify: every row under «Для этого файла» resets with «Сбросить для этого файла».

### UX-15. Ctrl+F opens a native folder picker; search is Ctrl+K
- Severity: Medium · Standard: Nielsen #4; Windows keyboard conventions (Ctrl+F / Ctrl+E = find)
- Where: `src/app/Actions.cpp:85` (`OpenFolder` = Ctrl+F), `:92` (`Search` = Ctrl+K). The app menu shows «Ctrl + F Открыть папку».
- Evidence: menu dump of `menu app`.
- Impact: in a library app, Ctrl+F to find opens a modal system dialog. VLC users get their habit, but Explorer, browser and Spotify users don't.
- Fix: make Ctrl+F «Поиск» when the library is on screen, and keep «Открыть папку» on Ctrl+F only in the player, or move it to Ctrl+Shift+O. Alternatively add Ctrl+E as a second search key and show it in the placeholder tooltip.
- Verify: library + Ctrl+F → focus in the search box.

### UX-16. Onboarding accepts a whole drive as the media folder, and its error looks like a hint
- Severity: Medium · Standard: Nielsen #5; skill I "Show specific error messages"
- Where: onboarding page 1 folder field (`src/app/dialogs/Onboarding.cpp:438`)
- Evidence:
  - Typed `C:\` → «Folder found. Lumen will look for videos and music in it.» (dump of `uir-ux-b`).
  - A non-existent path shows «This folder doesn't exist. It won't be added.» in the same grey 12 px style as the helper line (`ob-bad.png`), and «Continue» stays primary.
- Impact: a drive root or the user profile gets scanned (thousands of system files, slow first run). The error is easy to miss.
- Fix: for a drive root, `%USERPROFILE%` or `Windows`, warn «Это весь диск. Lumen будет сканировать все папки — лучше выбрать папку с медиа». Draw the "doesn't exist" line with the error tone and icon.
- Verify: type `C:\` → warning text. Bad path → error tone in the dump.

### UX-17. Favorites live as the 4th tab inside «История», and the action is a noun
- Severity: Medium · Standard: skill H "Hidden or buried features"; skill K "Action-oriented button labels"
- Where:
  - Route `Section::Recent, "favorites"` (`src/app/library/Views.cpp:1271`, `LibHome.cpp:177`).
  - Menu item and control-bar button «Избранное» (`LibraryCommon.cpp:577`, `ControlBar.cpp:732`). The reverse item is «Убрать из избранного».
- Evidence: History tabs «Всё / Продолжить просмотр / Слушать снова / Избранное» (`recent.dark/final.png`). The song menu shows «Избранное» between «Добавить в плейлист» and «Показать в проводнике».
- Impact: favorites aren't history, so users look in the sidebar and in Playlists. A menu item called «Избранное» reads like a place to go, not "add to favorites".
- Fix: label the action «В избранное» (EN "Add to Favorites"). Move Favorites to the sidebar under «Плейлисты», or make it a pinned item at the top of «Плейлисты».
- Verify: song menu shows «В избранное». The sidebar or Playlists page shows Favorites.

### UX-18. Search placeholders break Russian grammar and appear where there is nothing to search
- Severity: Low · Standard: skill K "Microcopy"; Nielsen #8
- Where: `src/app/Shell.cpp:632` `Fmt(Tr(L"Search in “{0}”"), {crumb})` → «Поиск в «{0}»».
- Evidence:
  - «Поиск в «История»», «Поиск в «Сеть»», «Поиск в «Плейлисты»», «Поиск в «Videos»» (folder name in a Russian sentence).
  - «Поиск в «Плейлист»» sits on the «Плейлист не найден» page, and «Поиск в «Сеть»» on the empty Network page (`back-to-deleted.png`, `network.dark/final.png`).
- Impact: reads as machine-made, and the box invites typing where nothing can match.
- Fix: per-section phrases in the nominative-free form: «Поиск в истории», «Поиск по плейлистам», «Поиск по разделу «Сеть»», and for folders «Поиск в папке «Videos»». Hide the box on empty and not-found states.
- Verify: dump `TextBox` placeholder text on each section. No «в «» + nominative.

### UX-19. Counts say «элементов» where the app knows the noun
- Severity: Low · Standard: skill K "Terminology clarity"
- Where: header count label (`CountItems`, `src/app/I18n.cpp:87`)
- Evidence:
  - «3 элемента» on Альбомы, «5 элементов» on a playlist of 5 songs, «8 элементов» on Фильмы.
  - The Songs tab says «8 песен · 6:15» and the 1284-song list «1284 песни».
  - In search, «0 элементов» repeats «Ничего не найдено».
- Fix: pass the page noun to the count (альбом/альбома/альбомов, видео, песня), as `Plural()` already supports. Hide the count when it is 0.
- Verify: worst-plural sweep scenarios show nouns, and search-none shows no count.

### UX-20. Settings split one concept across pages; the accent swatches are nameless greys
- Severity: Low · Standard: skill H "Content categorization"
- Where:
  - Settings › Общие › «Оформление» (language, theme, accent) vs Settings › Интерфейс › «Скин» › row «Оформление» (Lumen/OLED/Контраст/Сепия). «Чёрный OLED» forces the dark theme set elsewhere.
  - `#general.accent` `AccentSwatches` have no names or tooltips (`settings-general.dark/final.tree.txt:272-275`), though Onboarding had «Белый / Жемчуг / Серебро / Сталь / Графит».
- Impact: "change how it looks" lives on two pages under one word. Five unnamed greys are a guessing game (audit #5 removed them from onboarding only).
- Fix: move theme, accent and skin into Интерфейс › «Оформление». Keep only language in Общие. Give swatches the onboarding names as tooltips and accessible names.
- Verify: settings search «тема» returns one page. The UIA dump names every swatch.

### UX-21. Jargon without explanation in the panel and settings
- Severity: Low · Standard: Nielsen #2; skill K "Unclear or technical jargon"
- Where: `player.inc` and `SettingsSchema.cpp`
- Evidence:
  - «Деинтерлейсинг», «Тональная компрессия» / «Отсечение», «ИИ-апскейл» next to «Алгоритм увеличения».
  - «Радиус устранения полос», «Плёночное зерно», «Способ выравнивания: EBU R128 / Динамический».
  - «Сетевые потоки 60000 мс».
  - «Аудиодорожка по умолчанию: Авто» (a number box) in Воспроизведение, next to «Предпочтительный язык звука» in Аудио: two ways to pick the same thing.
  - «Страница с видео откроется через yt-dlp».
- Fix: plain labels with the term as a subtitle (e.g. «Убрать гребёнку (деинтерлейсинг)», «Выравнивание громкости: стандарт EBU R128 / мягкое»). Buffering in seconds. Hide the numeric default-track fields under «Дополнительные параметры», since the language preference covers 95% of cases.
- Verify: `check-language.py` (or a word list) flags no bare term without a plain-language label.

### UX-22. A broken single file ends in a dead player
- Severity: Low · Standard: Nielsen #9, #3
- Where: `PlayerView::ErrorCard` (`src/app/player/PlayerView.cpp:422`)
- Evidence: «Не удаётся открыть «fake.mkv»» shows an (i) info icon, «Подробнее», and one button «Закрыть» (`error-fake.dark/final.png`). After closing, the player stays with «--:--» and a disabled bar. «Следующий файл» shows only with a queue (audit #11).
- Fix: for a single file, offer «Назад в медиатеку» (primary) and «Показать в проводнике». Use the warning icon used by the «Битый файл» badge.
- Verify: open `fake.mkv` → the card has two actions, and the primary returns to the previous route.

### UX-23. Small terminology and tone inconsistencies
- Severity: Low · Standard: skill K "Consistent terminology"
- Evidence (strings in `src/app/strings/*.inc`, `SettingsSchema.cpp`):
  - «Показать в проводнике» (menus) vs «Как в Проводнике» (Settings). The Windows name is «Проводник», capitalised.
  - «клик» («колесо меняет, клик выключает звук») vs «щелчок» everywhere else.
  - «Удалить из истории» vs «Убрать из плейлиста/очереди/медиатеки».
  - «Не удаётся открыть» vs «Не удалось воспроизвести».
  - History empty state «Недавних файлов нет» after the section was renamed «История» (audit #13).
  - «Предыдущий/Следующий» on buttons vs «Предыдущий/Следующий файл» in menus.
  - EN mixes Title Case and sentence case («Add to Queue» / «Remove from queue», «Continue Watching» / «Open file»), and «Everything» vs RU «Всё».
- Fix: one glossary row per concept in `qa/` and a check in `check-language.py`. EN in sentence case (Windows guideline).
- Verify: the grep for each pair returns one form.

### UX-24. «Открыть сетевой поток» mixes a one-off open with a playlist and a global setting
- Severity: Low · Standard: skill I "Too many fields", "Group related fields"
- Where: URL dialog (`dialog-url.dark/final.png`)
- Evidence:
  - Under the address the dialog has «Название плейлиста (необязательно)». Its hint «Плейлист сохранится…» uses «Любимые каналы» as the example.
  - «Буферизация … Действует для всех потоков» changes a global setting from inside an open dialog.
- Impact: a person who wants to play one link meets three decisions, one of which silently changes all future streams.
- Fix: show the name field only when 2+ lines are entered («Сохранить как плейлист» checkbox + name). Move buffering to Settings › Сеть with a «Настроить буферизацию…» link.
- Verify: one URL → the dialog shows only the address and «Открыть».

### UX-25. The player's right-click menu is ordered by implementation, not by task
- Severity: Low · Standard: skill H "Menu organization"
- Where: `src/app/player/PlayerView.cpp` (`ShowVideoContextMenu`, ~150-238)
- Evidence (`menu-context.dark/final.png`):
  - Top level: Пауза, Стоп, Предыдущий/Следующий файл, **Запись**, Скрыть интерфейс, Воспроизведение›, Видео›, Аудио›, Сведения, Статистика, Показать в проводнике, **Субтитры›** (separated from Видео/Аудио), «Другие настройки›», «Плейлист›», «Открыть медиа›», Выход.
  - «Запись» (rare, and it creates files) sits in the first group.
- Fix: group as Playback / Видео · Аудио · Субтитры / Очередь / Сведения · Статистика · Проводник / Открыть / Настройки. Move «Запись» into «Воспроизведение›». Rename «Другие настройки» to «Настройки».
- Verify: dump `menu player` → Субтитры directly after Аудио, no Запись at top level.

---
Not checked:
- Native dialogs (Browse, Add folder, Open file, subtitle file picker), forbidden by the rules, so the "add subtitles from a non-matching file" and "add the right folder" steps after the dialog are counted, not run.
- Real online services: no API keys, and I didn't start the mock.
- A real screen reader.
- Real mouse side buttons and the real Alt key. Both were simulated with posted WM_XBUTTON* and WM_SYSKEYDOWN plus the automation modifier message, so an OS-generated `WM_APPCOMMAND` path was not exercised.
- The onboarding pre-fill of the owner's Videos folder: the field was empty in my instance.
