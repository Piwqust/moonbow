# Lumen, review round 1: design (product designer / pixel check)

Build: `%TEMP%\lumen-qa\build-r1\Lumen.exe` (rebuilt). Data dir: `%TEMP%\lumen-qa\data-design`. Window 1280x780 at DPI 1.0 (1 DIP = 1 px).
Screenshots: `%TEMP%\lumen-qa\round1\shots-design\`. Colors below were sampled from the screenshots pixel by pixel.

*(Written incrementally. Top 5 is filled in at the end.)*

## Top 5

(pending)

## Findings

### D01 | Major | Player → library navigation | The folder crumb in the title bar opens a music track's folder in the Video section, which is empty
- **What:** While an mp3 from `Music\Artist A - Album One` plays, clicking the title-bar crumb "Artist A - Album One" (tooltip "Show in library") goes to **Видео › media › Music › Artist A - Album One** and shows the empty state "В этой папке нет видео" with "0 элементов". The Video nav item lights up. The mini player sits over an empty page.
- **Steps:** Музыка → Песни → double-click Track Number 2 → click the folder crumb at the top left of the title bar.
- **Expected:** For audio, open Музыка (the album, or Songs with the track selected).
- **Evidence:** `shots-design\20-crumb-to-library-mini.png`
- Likely cause: `src\app\Shell.cpp:149-152` always navigates to `Section::Videos`.

### D02 | Major | Player | Esc and the visible chrome offer no way back to the library from the player
- **What:** In the player (music view), Esc does nothing (`19-after-esc.png`). The only exits are the tiny title-bar crumb (which goes to the wrong place for music, see D01), the side-panel toggle, and Alt+Home. The control bar has no "Library" / back button. The Figma frame has the same chrome, but there the crumb is the obvious target, and here it is broken for audio.
- **Steps:** Play any track → press Esc.
- **Expected:** Esc (windowed, not fullscreen) or an explicit back affordance returns to the library with the mini player.
- **Evidence:** `shots-design\19-after-esc.png`

### D03 | Minor | Mini player | On hover, the pause glyph is drawn on top of the music-note placeholder, producing a garbled icon. The cover is also missing
- **What:** When you hover the mini player during audio playback, the centered pause button (about 24 px) overlaps the 24 px "note" placeholder icon, so the two glyphs merge into one shape. The pause bars cut through the note's stem. Also, the mini player shows the generic note on `#1E1E1E` even though the track has embedded art ("ALBUM" cover shown in the music view and the song list).
- **Steps:** Play Track Number 2 → go to library → hover the mini player.
- **Expected:** Hide the placeholder (or dim it like the Figma video frame does) under the transport controls, and use the album cover as the mini-player art.
- **Evidence:** `shots-design\21-mini-hover.png` (bottom right), `20-crumb-to-library-mini.png`

### D04 | Minor | List rows | Selected and hovered rows look identical
- **What:** In list mode the selected row (after a right-click) is `#1D1D1D` and the hovered row is `#1C1C1C`. Both rows show the "⋯" button and white title text, so you can't tell which one is selected. Selected nav items use `bg.selected #242424`, but list rows don't.
- **Steps:** Видео › media › Videos, list mode → right-click 4K Mandelbrot → hover Chaptered Demo.
- **Expected:** Selection = `bg.selected #242424` (plus a focus ring if keyboard-focused); hover = `bg.hover` (6% white).
- **Evidence:** `shots-design\11-videos-folder-list.png` (rows y 173-212 and 215-254)

### D05 | Minor | Library header | The set and position of header controls change on every page
- **What:** Which buttons the header shows depends on the page: Home has counter + search only (the Figma Home frame has sort + view); Музыка has counter only; Сеть has a grid button but no sort; Недавние has no controls; Плейлисты (empty) shows sort + view for 0 items. As a result the counter jumps horizontally: x=803 (Home), 817 (Музыка), 781 (Сеть), 809 (Недавние), 737 (Плейлисты, Videos folder). On Сеть the counter says "2 элемента" above 3 rows.
- **Steps:** Click through the sidebar items and compare the header strip.
- **Expected:** Fixed slots: sort and view either present on every page or disabled in place, so the counter and search never move.
- **Evidence:** `01-home-dark-ru.png`, `06-music.png`, `07-network.png`, `08-playlists.png`, `09-recent.png`

### D06 | Minor | Playlists empty state | Two competing "Создать плейлист" CTAs and two compositions on one screen
- **What:** The empty Playlists page shows a 243x243 dashed-style "+" tile "Создать плейлист" at top-left (x228,y130) **and** a centered empty state (icon, title "Плейлистов пока нет", body, primary "Создать плейлист" + secondary "Импортировать плейлист"). The empty-state block is centered on the whole pane (center x≈740), so it collides visually with the tile column. The title sits at y≈425 here but at y≈434 on Недавние.
- **Steps:** Click Плейлисты with no playlists.
- **Expected:** One composition: either the grid with the "+" tile, or the empty state, not both.
- **Evidence:** `shots-design\08-playlists.png`

### D07 | Minor | Context menu | Inconsistent icons; the Figma menu has items this one lacks
- **What:** "Воспроизвести" and "Воспроизвести следующим" use small **filled** glyphs (≈10-12 px), while all other items use 20 px 1.5-px outline icons, so the left icon column looks ragged. "Отметить как просмотренное" uses a bare ✓, which reads as "this option is checked". Separators (`#262626` on `#222222`) are almost invisible. The menu background is `#222222`, while DESIGN.md says menus use `bg.panel #191919`. Compared with `ref\components\file_context_menu`, the menu has no "Свойства / Сведения о медиа" and no "Добавить субтитры"; rows are 40 px instead of 48 px.
- **Steps:** Right-click any video card.
- **Expected:** One icon size and style (outline play triangle as in the reference), a "mark watched" icon that isn't a bare check, and a Media info entry.
- **Evidence:** `shots-design\13-card-context-menu.png`

### D08 | Minor | Videos navigation | The same nav item leads to two different roots; grid and list differ in content
- **What:** The first click on "Видео" opens **Видео › media** (Mixed / Videos folders). Clicking "Видео" again shows **Видео** with one card "media 11". In list mode there is an extra first row "+ Добавить папку", which grid mode doesn't have.
- **Steps:** Click Видео, then click Видео again; toggle grid/list.
- **Expected:** A stable landing level for the nav item, and the same items in grid and list.
- **Evidence:** `03-videos-grid.png`, `10-videos-folder-grid.png` (first capture), `12-sort-menu.png` (list with "Добавить папку")

### D09 | Polish | Text formats | Title and time formats disagree between surfaces
- **What:** The music view caption says "Track Number 2 - Artist A" (title first, hyphen), the title bar says "Artist A — Track Number 2" (artist first, em dash), and the song list uses "Track Number 2 · Artist A". The player shows times as `00:01` / `01:00`, while library cards and lists show `0:08`, `1:30`, `10:00`.
- **Expected:** One "Title · Artist" pattern, and one time format (m:ss, or h:mm:ss when needed).
- **Evidence:** `15-music-albums.png`, `10-videos-folder-grid.png`, `18-library-with-miniplayer.png`

### D10 | Polish | Music hub | Wrong noun in counters
- **What:** Cards read "8 песен", "3 альбома", then "3 элемента" (Исполнители) and "2 элемента" (Жанры).
- **Expected:** "3 исполнителя", "2 жанра".
- **Evidence:** `06-music.png`

### D11 | Minor | Player side library panel | The crumb names the wrong page and the counter is jammed against it
- **What:** With the library panel open in the player while in Песни, the header reads "Музыка 8 элементов": the crumb names the hub, not "Песни", and the 12 px counter sits 5 px after the 16 px crumb with no separator. Russian artist names are truncated hard ("Другой исполн…") because a fixed duration column takes the width in the 370 px pane.
- **Evidence:** `18-library-with-miniplayer.png`

### D12 | Polish | Home | "Недавно добавленные" leads with broken files
- **What:** On first launch the only Home row shows `fake.mkv` and `broken truncated.mp4` as the first two cards: grey placeholders with a film icon and no duration. The Home page therefore opens on two dead tiles.
- **Expected:** Leave unplayable or failed-probe files out of Home, or mark them.
- **Evidence:** `01-home-dark-ru.png`

### D13 | Polish | Sort menu | The selected option is marked with a 6 px dot instead of a checkmark and has no row highlight
- **What:** "Название А–Я" is marked with a small white dot at the right edge (x≈833). The Figma sorting component uses a check plus a highlighted row. Ten options, no grouping.
- **Evidence:** `12-sort-menu.png`, `design\ref\components\Sorting_Options_856-2215.png`

### D14 | Major | Quick settings (player ⚙) | The Audio and Subtitles tabs show "Нет" for the active track
- **What:** In Chaptered Demo, the tracks popover shows ✓ **English 440 Hz (eng)** for audio and ✓ **eng** for subtitles. The quick-settings panel says **Аудиодорожка: Нет** and **Дорожка субтитров: Нет**. When the "Аудиодорожка" dropdown is opened, "Нет" is highlighted as the current item. Two surfaces disagree, and the quick-settings one is wrong. A user who believes it will "re-select" English.
- **Steps:** Open Chaptered Demo (1080p).mkv → click the audio-tracks button (note the check) → click ⚙ → Аудио tab → open "Аудиодорожка". Same on the Субтитры tab.
- **Expected:** The dropdowns reflect the selected track (`aid`/`sid`).
- **Evidence:** `25-pop-audio.png`, `26-pop-subs.png`, `30-quick-audio.png`, `31-quick-subs.png`, `36-quick-audio-track-dropdown.png`
- Likely cause: `src\app\player\SettingsPanel.cpp:221-243` (SelectedTrack / sync of AddTrackChoice) does not see the selected track.

### D15 | Major | Media information dialog | A long path wraps into the neighbouring rows, and the text collides
- **What:** In "Сведения о файле", the value of **Путь** wraps to two lines, but the row keeps its 26 px height. The first path line (y≈155) rises above its own label (y≈164) and touches "Chaptered Demo" in the Название row. The second line (y≈174) sits directly on top of "105 MB" (y≈190), with no leading between the three values. Size also shows as "105 MB" (English unit), while the library list shows "105 МБ".
- **Steps:** Play Chaptered Demo → Ctrl+I.
- **Expected:** Rows grow to fit wrapped values (top-aligned label), or the path is elided in the middle with a tooltip. Use localized units.
- **Evidence:** `shots-design\54-media-info.png`

### D16 | Minor | Media information | The dialog can't be found in any menu
- **What:** "Сведения о файле" is reachable only with Ctrl+I (or by adding the hidden "Свойства"/"Миниатюра" control in Customize). It is not in the title-bar "⋯" menu, the player right-click menu, or the library file context menu. The Figma file menu has "Properties & Metadata".
- **Evidence:** `47-app-menu.png`, `37-player-context-menu.png`, `13-card-context-menu.png`
- Likely cause: `Action::ShowInfo` is only bound in `src\app\Actions.cpp:89` and `ControlBar.cpp:449/454`.

### D17 | Minor | Quick settings | Tab strip is crammed; labels truncate; label/field layout varies
- **What:** The ⚙ panel's 4 tabs "Воспроизведение / Видео / Аудио / Субтитры" are packed left with about 10 px between items and end 8 px from the panel edge. In the 3-tab popovers (tracks, queue) the tabs are evenly distributed. Video tab: the label "Тональная компре…" is truncated. Subtitles tab: the label "Дорожка субтитров" ends 3 px before its dropdown. Audio tab: "Режим стерео" is stacked above a full-width dropdown, while every other row is inline. Section headings ("Основные", "Синхронизация", "Фильтры") start at x=884 and row labels at x=880, a 4 px misalignment. "Повернуть: 0 °" has a space before the degree sign. Audio filters show three identical "Развернуть настройки" rows (EQ, compressor, surround, widener) with no hint of what each expands. Expanding the EQ does not scroll the new content into view (`34-quick-eq.png`).
- **Evidence:** `27-pop-quick.png`, `29-quick-video.png`, `30-quick-audio.png`, `31-quick-subs.png`, `33-quick-audio-scroll2.png`, `34-quick-eq.png`, `35-quick-eq-expanded.png`
- **Expected:** Equal-width tabs (or icons only), 8-px-grid label column wide enough for Russian, one row pattern.

### D18 | Polish | Equalizer | Sliders lack the value fill and editable dB fields shown in the reference
- **What:** EQ bands are bare tracks with a centred thumb and plain "0.0 дБ" text. The Figma `Audio-Equalizer` component has a filled portion up to the thumb and an editable value field per band. Also "дБ" is preceded by a decimal point ("0.0"), while the Russian UI elsewhere uses commas ("87,5 МБ").
- **Evidence:** `35-quick-eq-expanded.png`, `design\ref\components\Audio-Equalizer_6114-13610.png`

### D19 | Minor | Player popovers | Translucent panels pick up the video's colour
- **What:** The tracks, queue, and ⚙ popovers are semi-transparent. Over the cyan bar of the test pattern, the panel background turns teal (`#0E1414`-ish vs `#140F14` over magenta), so the same panel has two background hues side by side, split exactly at the video's colour edge (x≈1045).
- **Evidence:** `25-pop-audio.png`, `28-pop-queue.png`
- **Expected:** `bg.panel` at ≥95% opacity, or a real blur, so the panel reads as one neutral surface.

### D20 | Minor | Customize player dialog | Truncated instruction; one label shrinks its font
- **What:** At the default window size, the hint line is cut off: "…и верните сюда, чтобы убра…". The label "Скорость воспроизведения" is drawn at about 11 px to fit two lines, while the other two-line labels ("Картинка в картинке", "Выйти из Lumen", "Настройки Lumen") stay at 14 px. "Миниатюра" is a solid grey square, not an icon.
- **Evidence:** `40-customize-player.png`

### D21 | Minor | Keyboard Shortcuts dialog | Group headers have no breathing room
- **What:** Group headers ("Воспроизведение", "Аудио", "Субтитры", "Медиа") sit 13 px below the divider and 28 px above the first row. They use the same 16 px Regular weight in grey, so groups don't separate. Figma has about 50 px above and 30 px below a group title. The "Клавиша" and "Дополнительно" column headers are centered over their fields, while "Действие" is left-aligned.
- **Evidence:** `48-shortcuts-dialog.png`, `49-shortcuts-scrolled.png`, `50-shortcuts-search.png`, `design\ref\Keyboard_Shortcuts_2437-14832.png`

### D22 | Polish | Open Network Stream | Mixed type scale; the error isn't marked as an error; the caret overlaps the placeholder
- **What:** "Адрес потока" and "Название плейлиста" are 16 px headings with 14 px helper text. "Буферизация" is 14 px with 12 px helper text and no divider. The validation message "Строка 1 не похожа на ссылку…" is the same 12 px tertiary grey as the neutral hint "Enter открывает поток…", so it doesn't read as an error. In the empty field the text caret is drawn over the first letter of the placeholder "например…".
- **Evidence:** `51-network-stream-dialog.png`, `52-network-empty.png`

### D23 | Polish | Buttons | The primary button style differs between surfaces
- **What:** The empty states use a white filled primary button ("Создать плейлист"), while dialogs use a grey `#2F2F2F` primary ("Открыть", "Готово", "Закрыть", "Сохранить") that differs from the ghost secondary only by its fill. The Play button is white. Within one app there are two "primary" looks.
- **Evidence:** `08-playlists.png`, `53-network-valid.png`, `54-media-info.png`, `40-customize-player.png`

### D24 | Polish | Player context menu | No current-state marks in the Video submenu; icons inconsistent
- **What:** In Видео ▸, neither "Отключить" nor "#1 · h264" has a check, though track #1 is active (the Audio/Subtitle popovers do show checks). Half the items have icons and half don't. The top-level menu again mixes filled Play/Stop/Prev/Next glyphs with outline icons.
- **Evidence:** `38-player-ctx-video-sub.png`, `37-player-context-menu.png`

### D25 | Major | Light theme: text fields | Single-line text fields in dialogs are pure black, and the typed text is almost invisible
- **What:** In light theme, the name field of **Новый плейлист** and the "Название плейлиста" field of **Открыть сетевой поток** are filled with `#000000`. The placeholder is grey on black. Typed text ("Мой плейлист") is drawn in `text.primary #141414` on `#000000`, so it is effectively invisible. This happens after a clean start with `general.theme = light`, not only after a live switch. In dark theme the same fields are `#000000` instead of `bg.deep #090909` (less visible, still off-token).
- **Steps:** Light theme → Плейлисты → "Создать плейлист" → type a name. Or Ctrl+N.
- **Expected:** `bg.deep #E6E6E6` with `#141414` text, like the multi-line URL field in the same dialog.
- **Evidence:** `83-new-playlist-light.png`, `84-new-playlist-light-typed.png`, `85-network-dialog-light.png`, `51-network-stream-dialog.png` (dark)
- Likely cause: `ui::Color` defaults to opaque black (`src\ui\Types.h:53`, `a = 1`), so `TextBox::background` (`src\ui\Controls.h:226`, "defaults to bgDeep when alpha is 0") never falls back. See `src\ui\Controls.cpp:763`.

### D26 | Major | Live theme switch | The library search box stays dark after switching to light
- **What:** After switching Тема → Светлая in Settings, the whole shell turns light except the 324x32 header search field, which stays `#151515` (dark `bg.raised`) on the `#F2F2F2` header, a black slab in the light UI. It becomes correct only after a restart.
- **Steps:** Dark theme → Ctrl+P → Общие → Тема: Светлая → close Settings → look at any library page.
- **Expected:** Every surface re-reads the palette on theme change.
- **Evidence:** `74-library-light-list.png`, `75-library-light-grid.png`, `76-home-light.png`, `77-playlists-light.png`, `78-network-light.png` (before restart) vs `79-home-light-restart.png` (after)
- Likely cause: `src\app\Shell.cpp:358` `search_->background = P().bgRaised;` is captured once at construction (same pattern: `Dialogs.cpp:488`, `SettingsWindow.cpp:52`, `PanelKit.cpp:548`, `SettingsRows.cpp:920`, `SettingsAddons.cpp:208`).

### D27 | Major | Light theme: player | The volume wedge is invisible
- **What:** In light theme, the control-bar volume wedge is `#E6E6E6` on the `#EBEBEB` bar, about 1.05:1 contrast, so it can't be seen at all at volume 0. The muted speaker icon floats next to an empty gap. In dark theme the wedge is `#090909` on `#1B1B1B` and is visible.
- **Evidence:** `71-player-light.png` (x 1018-1104, y 744-760)
- **Expected:** An empty track at least `line.strong #BDBDBD`, as in the Figma "volume" component.

### D28 | Minor | Light theme: popovers | Popovers are strongly tinted by the video
- **What:** Same issue as D19, much stronger in light: the ⚙ panel background is pink-white `#EFEEF0` over the magenta bar and blue-white `#EBF1F2` over the cyan bar, with a hard seam where the video's colours change.
- **Evidence:** `73-player-light-quick.png`

### D29 | Major | Contrast | Disabled rows are effectively invisible in both themes, and tertiary text fails AA
- **What:** Disabled settings rows ("Файл журнала", "Подробность журнала", Last.fm user/password/URL, Сеть › buffering values, "Плёночное зерно", "Целевой уровень", "Предусиление Replay Gain") are drawn at **`#212121` on `#0E0E0E`** in dark (about 1.2:1, darker even than the `text.disabled #404040` token) and **`#DEDEDE` on `#F2F2F2`** in light (about 1.2:1). Users can't read what is disabled or why. Nothing explains the dependency (e.g. "enable Сохранять журнал"). The settings sidebar group labels ("Приложение", "Медиа", "Система") are `#8A8A8A` on `#FFFFFF` in light (3.4:1), below 4.5:1 for 14 px text.
- **Evidence:** `55-settings-first.png`, `57-set-privacy.png`, `64-set-network.png`, `60-set-video.png`, `61-set-audio.png`, `70-set-general-light.png`
- **Expected:** Disabled text at least `text.tertiary` with the control visibly dimmed, plus a short hint naming the parent toggle.

### D30 | Minor | Mini player | In light theme the paused video's mini player is a blank white rectangle
- **What:** With Chaptered Demo loaded (paused) in light theme, the mini player's 264x150 video area is solid `#FFFFFF`: no frame, no placeholder, no controls. It looks like an empty card. (Screenshots are taken with PrintWindow, which also shows the video in the full player, so this isn't a capture artefact of the main video surface.)
- **Evidence:** `74-library-light-list.png`, `76-home-light.png`, `78-network-light.png`
- **Expected:** The last frame (or the thumbnail) on `videoBg`.

### D31 | Minor | Collapsed sidebar | The rail tooltip covers the next icon
- **What:** With the sidebar collapsed to the 48 px rail, hovering an icon shows the tooltip **below** it ("Музыка" at y 186-214), covering the next item (Сеть). Rail tooltips should appear to the right of the rail.
- **Evidence:** `80-sidebar-collapsed-light.png`

### D32 | Polish | Recent list | Mixed semantics in one column; time column not tabular
- **What:** The middle column shows "осталось 0:18" for some rows and the artist "Artist A" for others. The right-aligned clock column uses proportional digits: "22:11" ends at the same x but starts 2 px later than "22:21". DESIGN.md requires `tnum` for times. Durations in list views have the same issue.
- **Evidence:** `82-recent-light.png`
