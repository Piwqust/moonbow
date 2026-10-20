# Area A11Y: accessibility (UI review 2026-10-20)

Build: `%TEMP%\lumen-qa\uir-build\Lumen.exe` (main @ ff96c7f). Four invisible `--automation` instances
(`%TEMP%\lumen-qa\uir-a11y-1..4`), dark/light, RU and EN, accents White and Graphite. Evidence (screenshots and
UIA dumps) is in `%TEMP%\lumen-qa\uir-a11y-out\`. Contrast script: `%TEMP%\lumen-qa\uir-a11y-contrast.py`.
Web-only checks (skip links, HTML validity, reflow at 320 px, autocomplete) are skipped: they do not apply to a
native Win32 app.

## WCAG 2.2 summary

| Level | Violations | Criteria |
|---|---|---|
| **A** | **14** | 2.1.1 (x4: A11Y-1, -9, -10, -12), 4.1.2 (x8: A11Y-2, -3, -4, -6, -7, -8, -18, -19), 3.3.1/4.1.2 (A11Y-11), 1.3.1 (A11Y-17) |
| **AA** | **6** | 4.1.3 (A11Y-5), 2.4.7 + 2.4.11 (A11Y-13), 1.4.11 (A11Y-14, -15), 1.4.3 (A11Y-16), 1.4.4 (A11Y-20, from code) |
| AAA / best practice | 4 | 2.4.13 (A11Y-14: the ring is 1.5 DIP), 2.3.3 (A11Y-22), 3.1.2 (A11Y-23), Windows contrast themes (A11Y-21) |

Counts are findings, not criteria. A11Y-18 (no Text pattern) is counted under 4.1.2 because an Edit without
caret and selection does not expose its state to assistive tech. Strictly, WCAG asks only for name, role and value.

Severity follows the brief: a Level A failure that blocks a core task (play, pick subtitles, use the queue) is
Critical. Other Level A and AA failures are High.

## Strengths

- **There is now a real UI Automation provider** (`src/ui/Accessibility.cpp`). Buttons, CheckBox+Toggle (switches),
  ComboBox+Value, Slider+RangeValue (seek, volume, speed, subtitle delay), Edit+Value, List/ListItem+SelectionItem
  (library grids), Menu/MenuItem (with disabled state), Tab/TabItem (panel tabs, segmented controls) and Window (popovers,
  dialogs) are all exposed. Names are localized in RU and EN, and tooltips become HelpText. Audit #1 is fixed at the
  basic level.
- **Keyboard model is sound where it exists.** Tab order follows reading order, and lists, tabs and menus use one tab
  stop with arrow keys inside (roving focus). Tab wraps around without traps. Esc closes the top popover and puts focus
  back on the control that opened it (playback-settings panel back to the gear, lyrics menu back to "Текст"). Space and
  Enter activate the focused control only after keyboard navigation.
- **Text contrast passes AA in both themes**: `text.tertiary` is 4.56-5.84:1 in dark and 4.52-6.19:1 in light on every
  surface (audit #4 is fixed). `skin::CheckContrast` (`src/core/Skin.cpp:303`) also guards custom skins.
- **The default focus ring is strong.** `accentMid` #B5B5B5 is 9.4:1 on `bg.base` and 8.1:1 on cards (measured
  pixel 181,181,181 in `tab-8.png`).
- **Target size.** `ui-audit.py` reports 0 `target-size` findings (under 24 DIP and crowded) in all 375 sweep states.
  The 18 DIP settings "i" icons pass through the spacing exception.
- **Time and captions.** Auto-hide can be set to "Never" or 0.5-10 s (2.2.1). A toast with an action stays at least
  8 s and waits while it is hovered or focused (`Controls.cpp:1741`). Subtitles get full low-vision styling: font, size,
  colour, opacity, outline, a background box, and an override of ASS styles (1.2.2). Shortcuts can be remapped (2.1.4).
  Windows "Animation effects" and high contrast are read and followed live (`Host.cpp:39`).

## Findings

### A11Y-1. The auto-hiding control bar drops keyboard focus and cannot be reached with Tab
- Severity: Critical · Standard: WCAG 2.2 2.1.1 (A), 2.4.3 (A), 2.2.1 (A)
- Where: player with the bar over the video, which is the default in fullscreen (`interface.autoHide=fullscreen`)
  and always with "always". Code `src/app/MainWindow.cpp:351-363`: the hide timer keeps the bar only for `Hot()`,
  `Pressed()`, an open popup or pause. It never checks `Focus()`, and Tab (`Host.cpp:1127`) does not call `PokeChrome`.
- Evidence: instance 2 (`autoHide=always`, `hideDelay=1`). Focus was on "Пауза (Пробел)" (UIA `focus`). One second
  after a pointer move the bar hid (`ah-3.png`), and the UIA tree had no focused element. After that, Tab cycled only
  "Меню Lumen" and "Боковая панель" (steps 1-3). The bar controls came back only after mouse movement.
- Impact: keyboard and screen-reader users in fullscreen cannot reach seek, volume, tracks, subtitles or the queue
  with Tab. Focus disappears while they are on a button.
- Fix: in the `chromeTimer_` lambda, keep the bar when `player_->Bar()->IsAncestorOf(Focus())` and focus is visible.
  In `MainWindow::onKey` (or Host before `MoveFocus`), call `PokeChrome()` for Tab and Shift+Tab when `BarOverlays()`.
- Verify: `launch.ps1 -Settings '{"interface.autoHide":"always","interface.hideDelay":1}'`, Tab to Pause, wait 3 s.
  `uia-dump` still shows the button focused and the capture shows the bar. Tab from the hidden state shows the bar.

### A11Y-2. The queue, bookmarks and chapters are one button with every item concatenated in its name
- Severity: Critical · Standard: WCAG 2.2 1.3.1 (A), 4.1.2 (A)
- Where: queue panel (Ctrl+L), tabs Очередь/Закладки/Главы. `QueueList` (`src/app/player/QueuePanel.cpp:212`),
  `BookmarkList` (`:925`), `ChapterList` (`:1140`) and `TracksPanel.cpp:83 ChoiceList` have no `Describe`.
- Evidence: `uia-panel-queue.txt`: `Button "silent vp9 0:10 Фильм — тест «кавычки» 日本語 0:30 4K Mandelbrot 0:08 …
  Chaptered Demo (1080p) 1:30" focus {Invoke}`. There are no list items, no current item and no selection.
- Impact: a screen-reader user cannot tell what plays next, cannot pick, reorder or remove an item, and cannot jump
  to a chapter.
- Fix: give each list a `Describe` like `ItemGrid::Describe` (`ItemGrid.cpp:205`). Use role List, one ListItem per
  row named "title, duration", `selected` for the selection, `invokeItem` = play, and `focusedItem` for the keyboard
  row. Mark the playing row in the name ("сейчас играет").
- Verify: `uia-dump` with the queue open shows `List` with 6 `ListItem` children, and SelectionItem works.

### A11Y-3. The active track, theme, page or option is not exposed: "which one is on?" has no answer
- Severity: Critical · Standard: WCAG 2.2 4.1.2 (A)
- Where: tracks panel (subtitle and audio track rows), onboarding theme cards, settings navigation (current page),
  accent picker, library sidebar (current section), the Текст/Очередь toggles, subtitle/queue bar buttons.
- Evidence: `uia-panel-tracks.txt`: `Button "Без субтитров"` and `Button "Фильм — тест «кавычки» 日本語.srt"`. Neither
  has a selected or toggle state, although the .srt row shows a check mark (`panel-tracks.png`). Onboarding:
  `Button "Light" / "Dark" / "System"`, and "Dark" is visibly ringed (`onb2.png`). Settings: `Button "Общие"` is
  the open page with no state. Accent: one `Button "Цвет акцента"` for 5 swatches (`settings.png`).
- Impact: a blind user cannot tell which subtitle or audio track is playing, which theme is chosen, or where they
  are. Choosing subtitles is a core task.
- Fix: describe `ChoiceList` and the onboarding theme row as List + ListItem with `selected` (radio semantics). Give
  the settings nav and the sidebar `A11y::Item` with `selected`. Expose the accent picker as a List of 5 named items.
  Give toggle buttons (`IconButton::active`) `toggle = active`.
- Verify: `uia-dump` shows `selected` on the .srt row, on "Dark", on "Общие" and on the active accent, and
  `toggle=On` on "Текст" when lyrics are open.

### A11Y-4. State and value changes raise no UIA events
- Severity: High · Standard: WCAG 2.2 4.1.2 (A, "notification of changes")
- Where: `src/ui/Accessibility.cpp:759-773`. The provider raises only `AutomationFocusChanged` and
  `StructureChanged`. There is no `UiaRaiseAutomationPropertyChangedEvent` anywhere in `src` (from code).
- Evidence: toggling "Перемешать" with Space, changing volume with arrows, the seek position, a selection change in a
  list and a title change on track change are not announced. Narrator only reads the new state if the user moves away
  and back.
- Impact: screen-reader users get no feedback after acting on a switch, slider or tab.
- Fix: in `a11y`, add `PropertyChanged(Widget*, PROPERTYID)` and call it from `Toggle::SetValue`/`OnClick`,
  `Slider::SetValue`, `Segmented`/`TabBar::Select` (SelectionItem_ElementSelected), `Dropdown` selection, and the
  volume/seek `Describe` owners when the value changes by keyboard. Throttle seek events (for example 1 per second).
- Verify: an Accessibility Insights event listener sees ToggleState, RangeValue.Value and ElementSelected events.
  Narrator says "on" after Space on a switch.

### A11Y-5. Toasts, the scan banner, track changes and errors are not announced (still open, audit section 11)
- Severity: High · Standard: WCAG 2.2 4.1.3 Status Messages (AA)
- Where: `ShowToast` (`src/ui/Controls.cpp:1756`). There is no `UiaRaiseNotificationEvent` and no LiveSetting in
  `src` (from code). Plain toasts last only `kToastMs = 1200` ms (`src/app/Actions.cpp:132`).
- Evidence: after L, `uia-toast.txt` gets `Text "Повтор всех"` in the tree for 1.2 s, but no event is raised.
- Impact: blind users miss "Повтор всех", "Продолжаем с …", "Субтитры добавлены", "Не удаётся воспроизвести",
  "Убрано. Вернуть". Low-vision and cognitively impaired users get 1.2 s to read them.
- Fix: in `ShowToast`, call `UiaRaiseNotificationEvent(provider, NotificationKind_ActionCompleted,
  NotificationProcessing_ImportantMostRecent, text, L"lumen.toast")` on the window's root element through a new
  `a11y::Announce(Host*, text)`. Use the same call for the scan banner, `interface.notifyTrackChange` and the error
  card. Raise `kToastMs` to at least 3000 ms, or scale it with text length.
- Verify: Narrator (or an Accessibility Insights event log) reports the notification. `frames.ps1` shows a toast for
  3 s or more.

### A11Y-6. Elements not painted in the last recorded frame have empty or stale names
- Severity: High · Standard: WCAG 2.2 4.1.2 (A)
- Where: `NameOf` relies on `Painted()` (`Accessibility.cpp:49-55, 162-204`), which is filled only for widgets drawn
  while a client listens. A widget scrolled out of view, or one never drawn, has no text.
- Evidence: home with "Продолжить просмотр": `Button "" off focus {Invoke}` (Tab step 12) and `List "" [868,424 …]`
  (`uia-settings.txt` main part). The same header has a name once it is painted.
- Impact: Narrator reads "button" or "list" with no name, and the element cannot be found by name.
- Fix: let `Describe` give explicit `a.name` for section headers and lists, from the same string the widget paints.
  Use painted text only as a last resort.
- Verify: scroll home so the first header is off screen, then `uia-dump`. No `""` names remain (grep `'"" '`).

### A11Y-7. Wrong accessible names from the "label before" heuristic
- Severity: High · Standard: WCAG 2.2 4.1.2 (A), 1.3.1 (A), 2.5.3 Label in Name (A), 2.4.6 (AA)
- Where: `LabelBefore`/`NameOf` (`Accessibility.cpp:71-85, 162-204`), `TextBox` name = placeholder (`:134`).
- Evidence (all from dumps):
  - Music page: `List "Альбомы Исполнители Песни Жанры"` (it takes the segmented control's labels).
  - Album page: `List "Альбом Album One Artist A 2024 · Electronic · 4 песни · 4:20 Воспроизвести все Перемешать"`.
  - Panels are named after their first text: `Window "Основные"` (tracks panel), `Window "6 элементов · 12:33"` (queue).
  - Groups: mini player `Group "3 элемента"`, control bar `Group "Не удаётся открыть «fake.mkv» Файл повреждён…"`.
  - Settings: `Edit "%TEMP%\lumen-qa\uir-a11y-1\lumen.log"` (placeholder, not "Файл журнала"),
    `ComboBox ""` (Подробность журнала), `Group "Включите «Сохранять журнал», чтобы изменить"`.
    Onboarding: `Edit "Folder with your videos or music"` instead of its visible label.
  - Shortcuts dialog: `ComboBox ""` (filter). Every TabBar and Segmented: `Tab ""`.
- Impact: misleading or empty names. Voice-control users cannot say the visible label ("click Файл журнала").
- Fix: stop `LabelBefore` at interactive siblings and Segmented controls, and never cross a section. Give panels and
  popovers an explicit title (`A11y.name` = panel title), and give the bar and mini player explicit names
  ("Панель управления", "Мини-плеер"). For `TextBox`, prefer `LabelBefore` and put the placeholder in HelpText.
  Settings rows should set `a.name` from their own label. Name TabBars after their panel.
- Verify: `uia-dump` on music, album, tracks panel, queue panel, settings General and the shortcuts dialog. Each name
  matches its visible label, and no `""` remains on an interactive element.

### A11Y-8. Home shelves expose items that are not laid out; Invoke on them silently does nothing
- Severity: High · Standard: WCAG 2.2 4.1.2 (A)
- Where: `ItemGrid::Describe` (`ItemGrid.cpp:205-222`) lists every entry, but `Selectable()` (`:231`) needs a laid-out
  slot.
- Evidence: home "Недавно добавленные" exposes 7 ListItems. Items 4-7 are `off` with no rect.
  `uia-dump -Invoke "Chaptered Demo (1080p)"` returned "invoked" and nothing opened (`player.png` still shows home).
  The Right arrow stops at item 3.
- Impact: a screen-reader user picks a film from the list and nothing happens, with no error.
- Fix: in `Describe`, expose only `Selectable(i)` entries, or make `invokeItem` open entries without a slot. Also
  expose "Ещё" (the header link) as the way to the rest.
- Verify: `uia-dump` on home lists only the visible cards, or invoking an `off` item opens it.

### A11Y-9. Mini player controls exist only while the pointer hovers
- Severity: High · Standard: WCAG 2.2 2.1.1 (A), 4.1.2 (A)
- Where: `PlayerView.cpp:971` `overlay_->visible = hover_.Get() > 0.01f`.
- Evidence: without hover, the UIA tree and Tab reach only "Очередь (Ctrl + L)" and "Текст" (`uia-mini.txt`, Tab steps
  19-20). With a synthetic hover, Stop, Open player, Prev, Play and Next appear (`uia-music-player.txt`, first dump).
- Impact: keyboard users cannot reach "Открыть плеер" or Stop from the library. They can only use global shortcuts
  they must know.
- Fix: keep the overlay buttons in the tree. Show the overlay when any of its children has visible focus
  (`ShowFocusRing`), as for hover.
- Verify: Tab from the library reaches Stop, Open player, Prev, Play, Next, Queue. The capture shows the overlay
  while it has focus.

### A11Y-10. Hover-only content and controls outside the Tab order
- Severity: High · Standard: WCAG 2.2 2.1.1 (A), 1.4.13 (AA)
- Where:
  - Settings "i" descriptions are tooltip-only and not focusable (`src/app/settings/SettingsRows.cpp:100-107`
    `InfoIcon`: `interactive`, not `focusable`). The text is not on the control as HelpText either.
  - Player: the seek bar (`SeekBar::SeekBar`, `ControlBar.cpp:750`, not focusable), the remaining-time toggle
    `Button "0:30"` and the title-bar folder chip `Button "Videos"` are never reached by Tab (player Tab walk, steps 1-11).
  - Tooltips appear only on mouse hover (`Host::UpdateTooltip`), so icon-only buttons never show their label to
    keyboard users.
- Impact: keyboard users cannot read any setting description, cannot switch to remaining time, and cannot seek
  except by unlisted arrow shortcuts.
- Fix: make `InfoIcon` focusable (Enter or Space shows the text in a popover), and set the row's control `HelpText`
  to the description. Make `SeekBar` focusable with Left/Right/Home/End/PageUp/PageDown in `OnKeyDown`. Make
  `TimeLabel` focusable. Show the tooltip after keyboard focus too (`SetFocus(…, true)`, then the tooltip timer).
- Verify: Tab in settings reaches each "i". Tab in the player reaches the seek bar and the time. `capture.ps1` shows a
  tooltip after focusing an icon button.

### A11Y-11. The playback error card is not a dialog and its message is not exposed
- Severity: High · Standard: WCAG 2.2 3.3.1 (A), 4.1.2 (A), 4.1.3 (AA)
- Where: `PlayerView::ErrorCard` (`src/app/player/PlayerView.cpp:422`).
- Evidence: fake.mkv shows "Не удаётся открыть «fake.mkv» / Файл повреждён…" (`err.png`). In UIA there are only
  `Button "Подробнее"` and `Button "Закрыть"` loose in the window. The message text is not an element. It shows up only
  as the bar group's name. Focus is not moved to the card, and no event is raised.
- Impact: a blind user hears nothing, and finds a second "Закрыть" next to the window's own Close.
- Fix: give the card `Describe` → `Role::Window` (or Group) named with the title and the message. Move focus to its
  first button when it appears, and announce the message (A11Y-5).
- Verify: `uia-dump` shows `Window "Не удаётся открыть «fake.mkv»"` with the message Text, and focus is on "Закрыть".

### A11Y-12. Player context menu cannot be opened from the keyboard
- Severity: Medium · Standard: WCAG 2.2 2.1.1 (A)
- Where: player video area. `VK_APPS`/`Shift+F10` are handled only in `ItemGrid.cpp:1156` and `SubWords.cpp:930`.
- Evidence: in the player, Apps and Shift+F10 open nothing (`uia-dump` shows no Menu). Right-click shows 17 items
  with 7 submenus (`menu-ctx.png`). Most items have shortcuts, but "Показать в проводнике", "Другие настройки" and
  "Открыть медиа" submenus do not.
- Fix: in `MainWindow::onKey` (player shown), map Apps and Shift+F10 to the same menu the right click opens,
  anchored to the video centre.
- Verify: `input.ps1 -Key Apps` in the player, then `uia-dump` shows `Menu` with focus on the first item.

### A11Y-13. Focus lands on invisible controls (no scroll into view, collapsed sections stay in the Tab order)
- Severity: High · Standard: WCAG 2.2 2.4.7 Focus Visible (AA), 2.4.11 Focus Not Obscured (AA)
- Where: `Host::MoveFocus` (`src/ui/Host.cpp:1084`) does not scroll the focused widget into its `ScrollView`.
  `CollectFocusable` includes children of collapsed expanders.
- Evidence: home Tab steps 12 and 19. Focus was on `Button "" off` and `ListItem "voice memo" off`, and the page did
  not scroll (`lib-tab12.png` and `lib-tab19.png` are identical, with no ring on screen). Playback-settings panel Tab
  step 11: `CheckBox "Запоминать для каждого файла" off focus` inside the collapsed "Набор настроек" section
  (`panel-tab11.png`).
- Impact: sighted keyboard users lose track of where they are, and Space can toggle a hidden switch.
- Fix: after `SetFocus(w, true)`, call a `ScrollIntoView(w)` on the nearest `ScrollView` ancestor. In
  `CollectFocusable`, skip widgets clipped by a collapsed `Expander` (or set those children `visible=false` when
  collapsed).
- Verify: Tab through home and the playback panel. No step shows `off focus` in `uia-dump`, and each capture shows
  the ring.

### A11Y-14. The focus ring fails 3:1 with the darker accents and is 1.5 DIP thin
- Severity: High · Standard: WCAG 2.2 1.4.11 (AA), 2.4.7 (AA); 2.4.13 Focus Appearance (AAA)
- Where: `Widget::PaintFocusRing` (`src/ui/Widget.cpp:258`): `accentMid` (= Mix(accent, bgBase, .25)), 1.5 DIP.
- Evidence (computed, and measured in `light-ring.png`: ring #9C9C9C on bar #EBEBEB next to a #808080 button):

  | Accent | Theme | Ring | on base | on panel | on card | on chrome (bar) |
  |---|---|---|---|---|---|---|
  | White | dark | #B5B5B5 | 9.41 | 8.57 | 8.13 | 8.40 |
  | Steel | dark | #6F6F6F | 3.84 | 3.50 | 3.32 | 3.43 |
  | Graphite | dark | #545454 | **2.55** | **2.32** | **2.20** | **2.27** |
  | Steel | light | #898989 | 3.12 | 3.50 | **2.75** | **2.93** |
  | Graphite | light | #9C9C9C | **2.45** | **2.75** | **2.16** | **2.30** |
- Impact: users who pick Graphite (or Steel in light) get a barely visible focus indicator.
- Fix: draw the ring in `textPrimary` (or `accent` when it is at least 3:1 against `bgBase`) instead of `accentMid`,
  2 DIP wide. Add `{ring, all bgs, 3.0}` to `skin::CheckContrast` (`Skin.cpp:314`).
- Verify: rerun `uir-a11y-contrast.py`. Every accent and theme gives at least 3:1. Sample pixels around a focused
  card in `capture.ps1`.

### A11Y-15. Component boundaries under 3:1: text fields, slider tracks, the played part of the seek bar
- Severity: High · Standard: WCAG 2.2 1.4.11 Non-text Contrast (AA)
- Where: `TextBox::Paint` (`Controls.cpp:899`: fill `bgDeep`/`bgRaised`, stroke `line`), `Slider::Paint` (`:602`:
  track `bgDeep` + `line`), `SeekBar::Paint` (`ControlBar.cpp:866`: played = Mix(track, accent, 0.32)).
- Evidence (dark / light): `line` on `bg.base` 1.28 / 1.30, on panel 1.16 / 1.45. Field fill vs panel 1.13 / 1.25.
  Search box on home: fill vs background about 1.06. Seek bar played #525252 vs unplayed #090909 = 2.55:1 (measured,
  White accent). With Graphite it is 1.35:1 (computed). The unplayed seek track vs the bar is 1.16:1.
- Impact: low-vision users cannot find the edges of input fields or see how much of the video has played. Only the
  2 DIP playhead carries the position.
- Fix: stroke fields and slider tracks with `lineStrong` raised to at least 3:1 against panel (dark ≈ #6B6B6B, light
  ≈ #949494), or a new `line.control` token. Draw the played part in `accentMid` or `accent` (as the thin variants
  already do: `strongPlayed`).
- Verify: rerun the contrast script for `line.control`, and sample seek-bar pixels in a capture (played vs unplayed
  at least 3:1).

### A11Y-16. Information drawn in the "disabled" colour, and accent text under 4.5:1
- Severity: High · Standard: WCAG 2.2 1.4.3 Contrast (AA)
- Where: Menu rows with `enabled=false` paint `textDisabled` (`Controls.cpp:1259`). The lyrics menu uses them for
  information ("Текста нет", "Нет файла .lrc, нет текста в тегах…"). The "ВКЛ" label of a switch is `accent` on
  `accentDim` (`Controls.cpp:286`).
- Evidence: lyrics menu text measured #646464 on #222222 = **2.69:1** (`music-lyrics.png`). In light, #848484 on
  #FFFFFF = 3.74:1. Switch label with Graphite: 3.20:1 (dark), 3.11:1 (light). With Steel light: 4.39:1.
- Impact: the only explanation of why there are no lyrics is unreadable for low-vision users. WCAG exempts inactive
  controls, but not information.
- Fix: draw informational menu rows as a non-item header in `textSecondary` (an `A11y::Item` role Text), not as
  disabled items. Draw the switch label in `textPrimary` (or `onAccent`), not in `accent`. Add accent-as-text pairs to
  `CheckContrast` with need 4.5.
- Verify: sample the lyrics-menu text in a capture (at least 4.5:1). Rerun the contrast script for every accent.

### A11Y-17. Structure is flat: no headings, tables or "current" markers
- Severity: Medium · Standard: WCAG 2.2 1.3.1 (A), 2.4.6 (AA)
- Where: section titles (`Text "Оформление"`, `Text "Основные"`), library section headers as plain Buttons,
  shortcuts dialog and album track list as loose Text and Button. No `UIA_HeadingLevelPropertyId`, no Grid/Table
  pattern (from code, `GetPropertyValue` at `Accessibility.cpp:412-447`).
- Evidence: `uia-dlg-keys.txt`: `Text "Воспроизведение / пауза"` followed by `Button "Пробел"`,
  `Button "Медиа: пуск/пауза"`, then `Text "Стоп"`, `Button "S"`. A key button's name says nothing about its action.
  Album list: `Text ""` header row, and ListItems with no duration or track number.
- Impact: Narrator heading navigation (H) finds nothing. In the shortcuts dialog the user hears "S, button" without
  the action.
- Fix: add `A11y::headingLevel` and return it for section titles. Name shortcut buttons "Стоп: S", or describe the
  shortcuts list as a Table with row headers. Add duration to the track ListItem names.
- Verify: `uia-dump` (extend it to print HeadingLevel) shows headings. Shortcut buttons include their action.

### A11Y-18. Edits have no Text pattern (still open, audit section 11)
- Severity: High · Standard: WCAG 2.2 4.1.2 (A); Windows UIA guidance for Edit controls
- Where: `Accessibility.cpp:329-336`. The Node implements Value but not `ITextProvider`/`ITextRangeProvider`.
- Evidence: every `Edit` in all dumps has `{Value}` only (search, speed, subtitle font, onboarding folder, shortcuts
  search).
- Impact: Narrator and NVDA cannot read the caret position, the character or word under it, or the selection. Arrow
  keys in a field are silent, and editing a path or a search is guesswork.
- Fix: implement a minimal `ITextProvider` over `TextBox::Text()` with `GetSelection` from the caret and selection
  (`TextBox` already tracks them), `DocumentRange`, and `RangeFromPoint`. Raise `UIA_Text_TextSelectionChangedEventId`
  on caret moves.
- Verify: Accessibility Insights shows the Text pattern on the search box. Narrator reads characters as the caret
  moves.

### A11Y-19. Sliders and menus lack text values and patterns
- Severity: Medium · Standard: WCAG 2.2 4.1.2 (A), 1.3.1 (A)
- Where: `SeekBar::Describe` (`ControlBar.cpp:808`) reports raw seconds. Menus (`Controls.cpp:1386`) have no
  ExpandCollapse for submenus and no AcceleratorKey. Button names embed shortcuts ("Пауза (Пробел)",
  "Очередь (Ctrl + L)"), which come from tooltips.
- Evidence: `Slider "Полоса перемотки" range=3.17/0..30`, read as "3,17". `MenuItem "Воспроизведение"` and 6 other
  submenu rows look like plain items (the ">" in `menu-ctx.png` is not exposed). `Menu ""` has no name.
- Fix: add Value pattern on sliders with "0:03 из 0:30" (and "1,25×", "100 %"). Give submenu MenuItems
  ExpandCollapse. Move the shortcut from the name into `UIA_AcceleratorKeyPropertyId`.
- Verify: `uia-dump` shows `value="0:03 из 0:30"` on the seek bar, `{ExpandCollapse}` on submenu rows, and names
  without "(…)".

### A11Y-20. Windows "Text size" is ignored and the app has no text zoom (from code)
- Severity: Medium · Standard: WCAG 2.2 1.4.4 Resize Text (AA), via WCAG2ICT platform scaling
- Where: there is no `UISettings.TextScaleFactor` and no `interface.*` text-size setting. Only DPI scaling (`Host.cpp`
  `WM_DPICHANGED`) and the developer flag `--scale`. Lyrics and subtitles have their own sizes.
- Impact: users who raise Settings › Accessibility › Text size to 150-225 % (a common low-vision setup) get Lumen at
  100 % text. The only way out is display scaling, which enlarges everything.
- Fix: read `Windows.UI.ViewManagement.UISettings::TextScaleFactor` at start and on change (WM_SETTINGCHANGE), and
  multiply `gfx::Text` sizes and row heights by it. Clamp layouts, and let the sweep run with `--text-scale 2`.
- Verify: sweep with the factor at 2.0. `ui-audit.py` shows no new `text-clip`/`text-overflow`.

### A11Y-21. High contrast uses a fixed skin, not the user's contrast theme (from code)
- Severity: Medium · Standard: Windows contrast-themes guidance; WCAG 1.4.11/1.4.3 under the user's settings
- Where: `Skins.cpp:134` picks the built-in `res/skins/contrast.json` (yellow accent) only when the skin is "lumen" and
  `interface.skinPicked` is false. No `GetSysColor(COLOR_WINDOW/COLOR_WINDOWTEXT/COLOR_HIGHLIGHT/COLOR_HOTLIGHT)`.
- Impact: Aquatic, Desert, Dusk and Night sky users get Lumen's own black/yellow or white/blue colours, not theirs.
  Anyone who once picked a skin gets no contrast mode at all.
- Fix: when `Host::HighContrast()` is on, build the palette from system colours (text, background, highlight,
  hotlight, gray text, button face) and ignore `skinPicked`, as Windows apps are expected to.
- Verify: `LUMEN_TEST_HIGH_CONTRAST=1` plus a fake system-colour provider in the sweep. Tokens equal the system colours.

### A11Y-22. Reduced motion stops only transitions; continuous motion keeps running (from code)
- Severity: Low · Standard: WCAG 2.2 2.3.3 (AAA), 2.2.2 (A, only if the motion auto-starts)
- Where: `AnimFloat::SetReducedMotion` (`Anim.cpp:94`) affects `AnimFloat::To` only. The visualizer (MusicView,
  `audio.visualizer`), karaoke line fill and lyrics scrolling never check `ReducedMotion()`.
- Impact: with "Animation effects" off, the visualizer still pulses and lyrics still glide. The visualizer is off by
  default and monochrome, so no flash risk under 2.3.1 was found.
- Fix: when `ReducedMotion()` is on, freeze the visualizer to a static frame and jump lyrics line by line.
- Verify: `LUMEN_TEST_NO_ANIMATION=1` and `frames.ps1` on the music view: identical frames except the line change.

### A11Y-23. The UI language is not exposed to UIA
- Severity: Low · Standard: WCAG 2.2 3.1.1 (A, satisfied by the platform for most screen readers), 3.1.2 (AA)
- Where: `GetPropertyValue` never returns `UIA_CulturePropertyId` (from code). The UI is Russian by default, and file
  names are often English or Japanese.
- Impact: on an English Windows, Narrator may read Russian labels with an English voice.
- Fix: return the LCID of `general.language` for the window root (UIA inherits it).
- Verify: Accessibility Insights shows Culture = ru-RU on elements.

## What could not be checked
Real Narrator or NVDA speech: the PC owner's session must not be disturbed, so announcements and events are concluded
from code (A11Y-4, -5). Synced lyrics: the only `.lrc` test file is outside `%TEMP%\lumen-qa\media`. Windows High
Contrast, Text size and Animation effects: system settings were not changed, so A11Y-20 to A11Y-22 are from code.
Fullscreen auto-hide: tested with `autoHide=always` in a window, not in fullscreen (same code path).
