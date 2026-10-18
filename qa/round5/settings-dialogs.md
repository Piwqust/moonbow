# Round 5: Settings, dialogs, menus, onboarding and frame (tree + vision hunt)

Hunter: r5-settings-eye (branch `worktree-agent-a23a7efcd4ab78ea4`).

- **Exe.** `%TEMP%\lumen-qa\r5-v2\Lumen.exe`, prebuilt; nothing was rebuilt.
- **Instances.** `%TEMP%\lumen-qa\r5se-1` (dark, 1.0, then light), `r5se-2` (`--scale 2`), `r5se-3` (light, `--scale 1.25`), `r5se-4` (`-Onboarding`). All `--automation`, all killed at the end.
- **Input.** `tools\dev\command.ps1` (`nav settings <page>`, `menu`, `action`, `set`, `state`), `input.ps1 -Title "Настройки — Lumen"` for the Settings window, `winpos.ps1`, `hitmap.ps1`, `dump-tree.ps1 -All`, `capture.ps1 -Scale 1`.
- **Evidence.** `%TEMP%\lumen-qa\r5se\`. `*.txt` are tree dumps, `*.png` full captures, `*-crop.png` 2-6x nearest-neighbour zooms.
- **Screen.** 2560x1440 at 100 %, work area 2560x1392.
- **Method.** Every page: dump, scan for `trunc`/`clip`/`overflow`, controls outside the client rect, small targets; then a vision pass with the other theme or size as the reference; then a crop; then a check in the tree or by interacting. Line numbers refer to this worktree at `facf7be`.

Not re-reported (already in round 4 or `flows.md`): W-07, W-08, W-09, W-14, FL-04, FL-11. See "Re-checks" at the end.

## Summary

| ID | Sev | Status | Screen | Finding |
|---|---|---|---|---|
| SD-01 | Broken | confirmed | Shortcuts dialog (F1) | The dialog shows only the first two bindings of an action. "PgDn, Media Next" reads as "PgDn" there. |
| SD-02 | Broken | confirmed | Settings > Расширения, width < ~900 | The add-on toolbar runs off the window: "Добавить" is cut and "Удалить" is off-screen. |
| SD-03 | Broken | confirmed | Settings > Аудио > EQ, width < ~1010 | The equalizer doesn't shrink or scroll. At 760 the 6 kHz to 16 kHz bands are off-window, and band 6k sits over the scrollbar. |
| SD-04 | Broken | confirmed | Main window, 150 % / 200 % | The first-run main window (1280x780 DIP) is never clamped to the work area. At 200 % on 1440p it is 1560 px tall, so the control bar is below the screen. |
| SD-05 | Ugly | confirmed | Settings > Аудио > EQ | Moving a band leaves the preset dropdown on "Нет" or the old preset name. Picking "Нет" doesn't reset the bands. |
| SD-06 | Ugly | confirmed | Settings, all pages | Disabled controls look live: disabled Secondary buttons and dropdowns in the light theme, and the disabled segmented control in both themes. |
| SD-07 | Ugly | confirmed | Main window, any dialog | A menu that is open when a shortcut opens a dialog (F1, Ctrl+P, Ctrl+I) stays open under the dialog's scrim. One Esc closes the dialog, a second the menu. |
| SD-08 | Ugly | confirmed | Settings at 760-900 wide | 25 row labels get an ellipsis and no tooltip ("Подгонять окн…", "Скрывать пане…"), because the controls keep 320 DIP. |
| SD-09 | Ugly | confirmed | Onboarding, step 4 | Two-line descriptions run into the separator below them. The line strikes through «Послушать снова». |
| SD-10 | Ugly | confirmed | Onboarding, summary | "История воспроизвед…" and "Метаданные из интерн…" are truncated, with 450 DIP of empty space beside them. |
| SD-11 | Ugly | confirmed | Settings > Расширения | "Удалить" is enabled with an empty list. Clicking it toasts "Сначала выберите дополнение" although there is nothing to select. |
| SD-12 | Ugly | confirmed | Settings, keyboard | The tab order is search, Close (far right), then all 12 nav items one by one. It takes 15 Tabs to reach the first setting. |
| SD-13 | Ugly | confirmed | Customize Player | Five palette items (Имя файла, Исполнитель, Папка, Плейлист, Свойства) share the same "info" icon. "Скорость воспроизведе-ния" wraps to 3 lines and overflows its cell. |
| SD-14 | Polish | confirmed | Settings, disabled rows | The hint "Доступно, когда задано «X»" doesn't say which value unlocks the row ("Комната" for the spatializer). In search, the widener card is labelled the same as its toggle. |
| SD-15 | Polish | confirmed | Shortcuts editor / dialog | Key names are in English in the Russian UI: Space, Backspace, Volume Up, Browser Back, Media Play. |
| SD-16 | Polish | confirmed | Shortcuts dialog vs editor | The two shortcut tables differ: "—" vs blank, centred vs left, "Сочетание" vs "Клавиша". In the dialog the pencil closes it and opens Settings. |
| SD-17 | Polish | confirmed | Settings at 1920 wide | The page title stays at x=252 while the content is centred at x=634. The shortcut chips end 40 DIP left of every other control. |
| SD-18 | Polish | confirmed | Settings nav | "Общие" and "Производительность" use the same icon. |
| SD-19 | Polish | confirmed | Confirm dialogs | Initial focus is on the header "×", and that button has no tooltip. |
| SD-20 | Polish | confirmed | Settings number fields | Over max is clamped (99999 → 3600), but under min is rejected silently (0 → old value). |
| SD-21 | Polish | confirmed | Settings, language switch | Switching the language rebuilds the window and scrolls the page back to the top (offset 768 → 0). |
| SD-22 | Polish | confirmed | Texts | Small wording and consistency issues: "Сочетания по умолчанию" row, "Усиление"/"Предусиление", "Выкл."/"ВЫКЛ", "…" in one menu only. |

## Findings

### SD-01 (Broken, confirmed): the Shortcuts dialog hides bindings
- **Screen.** Main window, F1 or app menu → "Сочетания клавиш", dark, 1280x780.
- **Steps.** `action app.shortcuts`, or `menu app` then `-Key F1`.
- **Expected.** The same bindings as Settings > Управление > "Изменить сочетания клавиш": "Предыдущий файл: P / PgUp, Media Prev", "Следующий файл: N / PgDn, Media Next".
- **Actual.** The dialog shows "P / PgUp" and "N / PgDn". The media keys aren't listed, and searching for "Media Next" finds the row but doesn't show the key.
- **Evidence.** `s1-menu-then-F1.png` (dialog) vs `s1-shortcuts.png` (Settings editor).
- **Cause.** `src/app/dialogs/Dialogs.cpp:99-101`: `k2` is `keys[1]` only. The Settings editor joins `keys.begin()+1 .. end` (`SettingsShortcuts.cpp:281`).
- **Fix.** Build `k2` like the editor does, with `BindingText(keys.begin()+1, end)`, or join with ", ".
- **Fixed (r5fs).** fixed (`7372ef5`). The second column joins every binding after the first, as the editor does. Before: "N / PgDn" (report). After: `KeyField … text="PgDn, Медиа: далее"`, `text="PgUp, Медиа: назад"` (`A-shortcuts.txt`).

### SD-02 (Broken, confirmed): the Extensions toolbar overflows the window
- **Screen.** Settings > Расширения, Settings window 760x520 (its minimum), dark.
- **Steps.** `nav settings extensions`, then `winpos.ps1 -Title "Настройки — Lumen" -Size 700,480` (clamped to 760x520).
- **Expected.** Search, "Найти расширения", "Добавить" and "Удалить" fit or wrap inside the 484 DIP content column.
- **Actual.** The row needs 625 DIP. `#add-extension [677,314 100x32]` ends at 777 > 760, and `#remove-extension [787,314 100x32]` is entirely outside the window, so removing an add-on is impossible below ~900 DIP.
- **Evidence.** `s1-min-kb.png`, dump `s1-min2-extensions.txt`.
- **Cause.** `src/app/settings/SettingsAddons.cpp:283-303`: a Row with a fixed 210 search field (`:202`), a Spacer, and fixed-width buttons. The Spacer can't go negative and nothing shrinks or wraps.
- **Fix.** Make the search `Length::Grow()` with a minimum of ~140, and put the buttons on a second line when the row is narrower than its content. Alternatively, move "Удалить" into the row's context menu or a per-row "×".
- **Fixed (r5fs).** fixed (`4b0f93f`). `AddonsToolbar` puts the buttons on a second, right-aligned line when search + buttons don't fit. Before: `#remove-extension [787,314]` off a 760 window. After at 760: `AddonsToolbar [252,307 484x93]`, `#remove-extension [636,358 100x32]` (`r5fs\new-min-adv\extensions.txt`).

### SD-03 (Broken, confirmed): the equalizer runs off the page at narrow widths
- **Screen.** Settings > Аудио, scrolled to "Эффекты", window 760x520.
- **Steps.** `nav settings audio`, resize as in SD-02, then wheel down 14 notches.
- **Expected.** All 11 columns visible: they narrow, or the EQ scrolls horizontally.
- **Actual.** Only Усиление … 3 кГц show. 6 кГц is cut by the window edge and drawn over the page scrollbar; 12, 14 and 16 кГц are off-window (`ui::Widget [864,23 60x412]`, `[932,23 60x412]` with client width 760). Below about 1010 DIP of window width some band is always missing.
- **Evidence.** `s1-min-eq.png`, `s1-min-eq.txt`.
- **Cause.** `src/app/settings/SettingsEqualizer.cpp:160-210`. `EqBody` is a centred Row of 11 fixed `kColumnW` columns plus `gap = 8`, about 748 DIP, inside a 484 DIP page. `justify = Justify::Center` doesn't stop the overflow.
- **Fix.** Derive the column width from the available width (min ~36 DIP, value boxes ~40) or wrap the body in a horizontal ScrollView. Also clip the card to the page.
- **Fixed (r5fs).** fixed (`4b0f93f`, `3aca3a6`). Columns narrow 60 → 52 DIP, then `EqBody` clips and scrolls sideways (Shift+wheel, horizontal wheel, a 4-DIP bar in the bottom padding); a focused band scrolls into view. Before: 6–16 kHz off-window. After at 760: `EqBody [252,191 484x456] hscroll offset=0/178`, Shift+wheel → `offset=178/178` (`D-eq-min-crop.png`, `D-eq-min-scrolled-crop.png`).

### SD-04 (Broken, confirmed by dump and code): the main window can start taller than the screen
- **Screen.** First run (no saved rect), `--scale 2` on 2560x1440 (work area 1392 px), dark.
- **Steps.** `launch.ps1 -Scale 2.0 -Fresh`, then `dump-tree.ps1 -All`.
- **Expected.** The window fits the work area, as the Settings window does (`client=1048x672`, shrunk to fit).
- **Actual.** `=== window title="Lumen" client=1280x780 scale=2`, i.e. 2560x1560 px on a 1392 px work area. The control bar and the bottom 168 px are below the screen edge and the taskbar. The same thing happens on real hardware at common setups: 1920x1080 at 150 % (work area about 1280x688 DIP) and 2560x1440 at 200 %.
- **Evidence.** `s2-general.txt` (first line).
- **Cause.**
  - `src/app/MainWindow.cpp:94-95` asks for 1280x780 DIP.
  - `src/ui/Host.cpp:281-306` creates and re-sizes the window at `options.width * Scale()` without clamping to `work`, although `work` is computed at `:275-280`.
  - `SettingsWindow.cpp:159-166` does that clamp by hand, so only Settings is safe.
- **Fix.** In `Host::Create`, clamp `w`/`h` to the work area minus a margin, never below the minimum track size. Settings can then drop its own copy.
- **Fixed (r5fs).** fixed (`4f1d6df`, Host.cpp creation/first show only). `Host::Create` clamps the size to the work area − 24 DIP and centres; the first `WM_SHOWWINDOW` (after a restored placement) does the same. Settings dropped its own copy. Before (base exe): `--scale 2` → `client=1280x780 scale=2` (1560 px on 1392); saved rect 2960×1692 → `client=2580x1460`. After: `client=1256x672 scale=2`; saved rect → `client=2536x1368` (`E-first-s2.txt`, `E-restore.txt`).

### SD-05 (Ugly, confirmed): the EQ preset name doesn't follow the bands
- **Screen.** Settings > Аудио > Эффекты, dark, 1048x865.
- **Steps.** `set audio.eq true`, then drag the 60 Hz band (`input.ps1 -Drag 366,375,366,250`).
- **Expected.** The preset dropdown changes to a "Своя"/Custom entry, or clears. Picking "Нет" returns the bands to 0.
- **Actual.** The band reads 16,2 дБ and the dropdown still says "Нет". After a named preset, the name stays although the curve is no longer that preset. Choosing "Нет" only writes `audio.eqPreset` and leaves the bands as they are.
- **Evidence.** `s1-eq-on.png`; `settings.json` holds `"audio.eq60": 16.2` with no `eqPreset` change.
- **Cause.**
  - `SettingsRows.cpp:196-201`: only a preset pick calls `ApplyEqPreset`.
  - `SettingsEqualizer.cpp:79`: a band writes its key only.
  - `Config.cpp:327-333`: `none` isn't a preset, so nothing resets.
- **Fix.** On any band or preamp write, set `audio.eqPreset` to `custom` (add a "Своя" choice) unless the values equal a preset. Make "Нет" (or "Сбросить") zero the bands.
- **Fixed (r5fs).** fixed (`4b0f93f`). The preset dropdown shows "Своя" (empty selection) once the bands differ from the stored preset; "Нет" sets preamp and bands to 0. After: `set audio.eq60 6` → `ChoiceDropdown … selected=-1 text="Своя"`; picking "Нет" → `selected=0 text="Нет"`, every `EqValue text="0,0"` (`D-eq-c.txt`, `D-eq-e.txt`).

### SD-06 (Ugly, confirmed): disabled controls look live
- **Screen.** Settings > Общие ("Файл журнала", "Подробность журнала" with "Сохранять журнал" off), light; Settings > Аудио ("Способ выравнивания"), both themes; Аудио > "Предустановка эквалайзера" at 1.25.
- **Steps.** `launch -Settings '{"general.theme":"light"}'`, `nav settings general`; then `nav settings audio`.
- **Expected.** Disabled controls are clearly flatter than live ones, as `BoolToggle` already does (`SettingsRows.cpp:152-169`).
- **Actual.**
  - Disabled "Обзор" has the same white fill as the live "Настроить"; only the text changes, #848484 vs #5C5C5C.
  - The disabled dropdown "Только ошибки" has the same white fill and border as a live one.
  - The disabled segmented control "EBU R128 | Динамический" draws its selected segment at full contrast in both themes, so it looks identical to the live "Тема" control.
- **Evidence.** `s1-light-disabled-crop.png`, `s1-light-general-hover.png`, `s1-audio2.png`, `s1-search.png` ("Способ выравнивания"), `s3-audio.png`.
- **Cause.**
  - `src/ui/Controls.cpp:154-156`: Secondary `bg = c.bgPanel` whatever `on` is.
  - `Controls.cpp:445`: the dropdown changes only `fg`.
  - `Controls.cpp:382-400`: `Segmented::Paint` never checks `IsEnabled()`.
- **Fix.** Share one disabled look for all controls: the toggle's `Mix(bgBase, bgCard, 0.5)` fill, no border, `textDisabled`, and no selected-segment highlight (or a `line`-coloured one) when disabled.
- **Fixed (r5fs).** fixed (`dcad7d8`, Controls.cpp Button/Dropdown/Segmented/Toggle paint only). One disabled look: flat `Mix(bgBase, bgCard, 0.5)`, no border, `text.disabled`; a disabled segmented control outlines the set value instead of the live highlight. Settings' BoolToggle now uses the stock look. After: `BL-general-disabled-crop.png` (light: "Обзор" grey vs live white "Настроить"; flat dropdown), `B-audio-seg-crop.png` / `BL-audio-seg-crop.png` (segmented, both themes). Not fixed here: the disabled path field (TextBox) still draws its live fill and text; TextBox is r5-fix-audit's.

### SD-07 (Ugly, confirmed): a menu survives under a dialog
- **Screen.** Main window, Videos, dark.
- **Steps.** `menu app`, `-Key F1`, `state` → `popups=2`; `-Key Escape` → `popups=1`; `-Key Escape` → `popups=0`.
- **Expected.** Opening a modal dialog closes the transient menus. One Esc returns to the window.
- **Actual.** The app menu stays visible, dimmed, behind the scrim, and the first Esc after the dialog lands on the menu. The same happens with Customize Player.
- **Evidence.** `s1-menu-then-F1.png`, `s1-dlg-app.customize.png` (menu at the left behind the dialog).
- **Cause.** `src/app/dialogs/DialogKit.cpp:497` opens a `modal` popup without closing the `dismissOnOutside` popups below it. `Host.cpp:700` only handles focus for the new modal.
- **Fix.** In the modal-open path of `Host`, close every non-modal popup (menus, popovers, tooltips) first.
- **Fixed (r5fs).** fixed (`7372ef5`). `ShowFrame` and Customize Player call `CloseTransientPopups`, which closes the popups above the topmost dialog. Before: `popups=2` after menu + F1. After: `popups=1`, one Esc → `popups=0` (`A-menu-F1.png`).

### SD-08 (Ugly, confirmed): narrow Settings truncates labels with no way to read them
- **Screen.** Settings at 760x520 (minimum) up to about 900 wide, all pages.
- **Steps.** Resize Settings to 760x520, then `nav settings <page>` for each page, then dump.
- **Expected.** Labels wrap, or the control column narrows, or the full label is in a tooltip. Windows 11 Settings wraps labels and stacks the control below them at narrow widths.
- **Actual.** 25 labels are ellipsized with no tooltip on the Label. Examples: Интерфейс "Подгонять окн…", "Скрывать пане…", "Вид панели упр…"; Управление "Сочетания по умол…", "Длинный (Ctrl+Shift…"; Воспроизведение 5 labels; Производительность 6 labels. (The InfoIcon tooltip is the description, not the label.)
- **Evidence.** `s1-min-interface.png`, `s1-min-controls.png`, dumps `s1-min2-*.txt` (lines with `trunc`).
- **Cause.** `kControlW` 320 is fixed for every control (`SettingsRows.cpp:191` and the other rows), and the RowLabel takes what is left. The nav column also keeps 228 DIP at 760.
- **Fix.** Below ~980 DIP, let the control shrink to 220, then wrap the label onto two lines. Or collapse the nav column to icons (48 DIP, like the main sidebar rail).
- **Fixed (r5fs).** fixed (`4b0f93f`). `SettingRow` narrows a 320-wide control down to 200 DIP (crop card 300, startup volume 280) before the label is cut; a label still cut shows its full text as the RowLabel tooltip (with the disabled hint). Rows now disable only their control, so that tooltip also shows on disabled rows. Before (base exe, 760×520): 29 `ui::Label … trunc`. After (760×1380, advanced sections open): 1, "Дорожка субтитров по умолчанию", with `RowLabel … tip="Дорожка субтитров по умолчанию"` (`r5fs\base-min\*.txt` vs `r5fs\new-min-adv\*.txt`; same at `--scale 1.5` light, `r5fs\l15\`).

### SD-09 (Ugly, confirmed): onboarding descriptions collide with the separators
- **Screen.** Onboarding step 4 "Несколько настроек.", 1090x760, dark.
- **Steps.** `launch.ps1 -Onboarding`, then Enter 3 times.
- **Expected.** 16 DIP between the last text line and the separator.
- **Actual.** The second line of "Запоминать, что вы смотрите и слушаете" and of "Загружать названия и обложки…" sits on the separator, and the line crosses the descenders («Послушать», «метаданных.»).
- **Evidence.** `s4-onb-3.png`, `s4-onb-3-crop.png`. The dump has `Label [165,183 670x40]` ending at y=223, while the separator is at y≈219.
- **Cause (suspected).** `src/app/dialogs/Onboarding.cpp:573-590`. The row is measured with the wrapped description at one line (no width yet), so the 16 DIP bottom padding is used by the second line.
- **Fix.** Give the description a known width (`760 - toggle - gap`) before measuring, or measure wrapped labels with the parent's final width. A fixed `height = Length::Px(80)` per row is the quick fix.
- **Fixed (r5fs).** skipped: onboarding went to r5-fix-audit.

### SD-10 (Ugly, confirmed): the onboarding summary truncates its labels
- **Screen.** Onboarding, last step "Всё готово.".
- **Steps.** As SD-09, Enter twice more.
- **Actual.** "История воспроизвед…" and "Метаданные из интерн…" (`Label [225,464 170x20] trunc`). The value column starts at a fixed 411, and the values ("Вкл.") are 30 DIP wide.
- **Evidence.** `s4-onb-5.png`, `s4-onb-5.txt`.
- **Cause.** `Onboarding.cpp:666-690`: the key column is a fixed 170.
- **Fix.** Size the key column to the widest key (about 200 DIP in Russian), or shorten the keys to "История" and "Метаданные".
- **Fixed (r5fs).** skipped: onboarding went to r5-fix-audit.

### SD-11 (Ugly, confirmed): "Удалить" is live with nothing to remove
- **Screen.** Settings > Расширения, no add-ons.
- **Steps.** Click "Удалить" (973,330).
- **Expected.** The button is disabled until a row is selected; it doesn't show at all with an empty list.
- **Actual.** It is enabled (`#remove-extension … interactive`, no `disabled`), and the toast says "Сначала выберите дополнение" on an empty list.
- **Evidence.** `s1-toast.png`, `s1-toast-crop.png`, `s1-extensions.txt`.
- **Cause.** `SettingsAddons.cpp:245-249` and `:301-303`: no enabled binding to the selection.
- **Fix.** `b->enabled = g_list && g_list->Selected()`, refreshed on selection change.
- **Fixed (r5fs).** fixed (`4b0f93f`). "Удалить" is a `RemoveButton` enabled only while an add-on is selected. After: `RemoveButton #remove-extension … disabled` with an empty list (`new-min-adv\extensions.txt`).

### SD-12 (Ugly, confirmed): the keyboard path through Settings is long and out of visual order
- **Screen.** Settings > Общие, dark.
- **Steps.** Open Settings and press Tab 16 times, dumping the focus each time.
- **Actual.** The order is search → Close (top right) → Общие → Интерфейс → … → Расширения (12 stops) → Язык интерфейса. The ring is visible (`s1-tab3-crop.png`), and arrows, Space and Enter work on the dropdown, segmented, swatches and toggle (`s1-kbd-accent-crop.png`). But each nav item is its own Tab stop, although Up/Down already move between them (`SettingsWindow.cpp:544-553`), and Close comes before the nav, against the reading order.
- **Fix.** Make the nav one Tab stop with a roving focus (Tab → the selected item, arrows inside, Tab → page), and put Close last in the order.
- **Fixed (r5fs).** fixed (`4b0f93f`). Roving focus: only the selected nav item is a Tab stop (Up/Down/Home/End move inside); the close button is the last child of the root (a stack lays it over the header), so it is last in the order. After: Tab 1 search, 2 `#nav.general`, 3 "О программе", 4 the first setting; Shift+Tab from the search reaches Close, then `#reset-category` (`B-tab*.txt`, `B-shifttab.txt`).

### SD-13 (Ugly, confirmed): Customize Player palette
- **Screen.** Customize Player (`action app.customize`), 1280x780, dark.
- **Actual.**
  - Имя файла, Исполнитель, Папка, Плейлист and Свойства all use the `info` icon, so they can't be told apart on the bar.
  - "Скорость воспроизведе-ния" wraps to 3 lines and overflows the 86 DIP cell (`PaletteCell … overflow box=[360,228 106x60]`) into the row below.
- **Evidence.** `s1-dlg-app.customize.png`, `s1-dlg-app.customize.txt`.
- **Cause.** `src/app/player/ControlBar.cpp:54-58` (icon `"info"` for five controls); `CustomizePlayer.cpp:30` `kCellW = 86`, `:481` hyphenation.
- **Fix.** Use distinct existing icons (`file`/`music`/`folder`/`list`/`info`; see `IconData.inc`), and shorten the label to "Скорость".
- **Fixed (r5fs).** fixed (`7372ef5`, `3aca3a6`). Icons `file`, `artist`, `folder`, `lists`, `info` (all in IconData.inc; no new SVG needed; ControlBar.cpp table, outside my scope, 4 lines). The palette says "Скорость" (tooltip: full name), cells are 104 DIP with two-line, ellipsized labels inside the cell. Before: 31 `text-overflow` (baseline). After: no flags on `PaletteCell` (`F-customize.txt`, `A-customize-1280x780.png`).

### SD-14 (Polish, confirmed): disabled-row hints
- **Steps.** Hover the label of "Режим объёмного звука" or "Расширение стереобазы" (card) in Аудио, or search "звук".
- **Actual.**
  - The hint is "Доступно, когда задано «Режим объёмного звука»", but the row unlocks only for `audio.spatialMode = room` (`SettingsSchema.cpp:1546-1549`). "Задано" also reads oddly for toggles: «Сохранять журнал» is "включено", not "задано".
  - In search results the widener card shows as a row "Расширение стереобазы" with "Развернуть настройки", disabled, with the hint "Доступно, когда задано «Расширение стереобазы»". It names itself, because the group row reuses the toggle's label.
- **Evidence.** `s1-search.png`, `s1-search.txt`, `s1-acc-hover.txt`.
- **Cause.** `SettingsRows.cpp:1142-1152`.
- **Fix.** For a bool condition, "Включите «X»". For a choice, "Доступно, когда «X» = «Комната»". Give the group rows their own labels ("Параметры расширения стереобазы").
- **Fixed (r5fs).** fixed (`4b0f93f`). Toggle parents: "Включите «X», чтобы изменить"; choice parents: "Доступно, если для «X» выбрано «Y»". Effect cards have their own labels ("Параметры расширения стереобазы", "Параметры компрессора", "Параметры помещения"). After: `RowLabel … tip="Включите «Выравнивать громкость», чтобы изменить"` (`B-audio.txt`).

### SD-15 (Polish, confirmed): English key names in the Russian UI
- **Where.** The Settings shortcut editor, the Shortcuts dialog, and the menu accelerators.
- **Actual.** Space, Backspace, Home, Esc, PgUp/PgDn, Volume Up/Down/Mute, Browser Back/Forward, Media Play/Stop/Next/Prev.
- **Expected.** VLC (ru) and Windows show "Пробел", "Громкость +", "Браузер: назад" and so on. Ctrl, Shift and Alt stay as they are.
- **Evidence.** The text sweep over `s1-*.txt`.
- **Cause.** `src/app/Actions.cpp:293` and the cases around it return literal English.
- **Fix.** Pass the key names through `Tr()` with entries in `strings/*.inc`.
- **Fixed (r5fs).** fixed (`7372ef5`; Actions.cpp `KeyText`, outside my scope). Space, Media Play/Stop/Next/Prev, Volume Up/Down/Mute, Browser Back/Forward go through `Tr()` (shell.inc: "Пробел", "Медиа: пуск/пауза", "Громкость +", "Браузер: назад"…). Keycap labels (Esc, Home, End, PgUp/PgDn, Del, Tab, Enter, Backspace) stay as printed on Russian keyboards. `KeyText(vk, mods, false)` gives the English names; the dialog's key search matches both; stored bindings use ShortcutStore's own names and are unchanged. After: `KeyField text="Пробел"`, `KeyChip text="Громкость +"` (`A-shortcuts.txt`, `C-min-editor.txt`).

### SD-16 (Polish, confirmed): two shortcut tables that disagree
- **Compare.** `s1-menu-then-F1.png` (dialog) with `s1-shortcuts.png` (Settings).
- **Differences.**
  - An empty slot is blank in the dialog and "—" in Settings.
  - Keys are left-aligned in the dialog and centred in Settings.
  - The column header is "Клавиша" vs "Сочетание".
  - The dialog's KeyField shows a pencil, but clicking it closes the dialog and opens Settings (`Dialogs.cpp:129-133`), which the pencil doesn't suggest.
- **Fix.** Share one row widget, or at least the same text and alignment rules. In the dialog, use an "open" arrow icon with the "Изменить в настройках" tooltip.
- **Fixed (r5fs).** fixed (`7372ef5`). The dialog uses the editor's rules: "—" for an empty slot, centred keys, the "Сочетание" header, and an "open" arrow with the tooltip "<keys>\nИзменить в настройках". After: `KeyHeader text="Сочетание"`, `KeyField text="—"` (`A-shortcuts.txt`).

### SD-17 (Polish, confirmed): a wide Settings window strands the title; the shortcut columns are misaligned
- **Steps.** `winpos.ps1 -Title "Настройки — Lumen" -Size 1920,1080`, `nav settings controls`, open the editor.
- **Actual.**
  - The content is centred (`ShortcutRow [634,319 880x40]`), but the header title stays at x=252, 380 DIP to the left.
  - The second key column ends at 1474, while every button and dropdown ends at 1514. That is the fixed 28+12 reset slot (`SettingsShortcuts.cpp:237-242`). The same 40 DIP step shows at the default size (`s1-shortcuts.png`: chips end at 984, controls at 1024).
- **Evidence.** `s1-wide-controls.png`.
- **Fix.** Align `title_` with the page's left padding (`FitPage` side), and put the reset slot inside the 320 control column.
- **Fixed (r5fs).** fixed (`4b0f93f`). The header's gap before the title follows the page's side padding; the reset slot sits left of the chips. Before (base exe, 1920): title `[252,14]`, chips end at 1474, buttons at 1514. After: title `[634,14]`, chips `[1102…1302]`, `[1314…1514]`, buttons end at 1514 (`G-wide-editor.txt`, `Gb-wide-editor.txt`).

### SD-18 (Polish, confirmed): "Общие" and "Производительность" share an icon
- **Evidence.** `s1-general.png` (nav).
- **Cause.** `SettingsSchema.cpp:707` and `:1889` both use `"preferences"`.
- **Fix.** Use a gauge or "speed" icon from `IconData.inc` for Производительность.
- **Fixed (r5fs).** fixed (`4b0f93f`; SettingsSchema.cpp, outside my scope, 1 line). Производительность uses `speed` / `speed_active` (`B-about.png`, nav).

### SD-19 (Polish, confirmed): confirm dialogs focus the "×"
- **Steps.** Settings > Общие > "Сбросить настройки".
- **Actual.** Focus is on `IconButton [712,349 32x32] focus icon=close` with no `tip`, so Enter cancels through an unlabeled button. The Settings close button has "Закрыть".
- **Evidence.** `s1-confirm.txt`, `s1-confirm-crop.png`.
- **Fix.** Focus "Отмена" (safe for destructive actions) and give the header × the "Закрыть" tooltip in `DialogKit`.
- **Fixed (r5fs).** fixed (`7372ef5`, `4b0f93f`). Settings confirms focus "Отмена"; the header × of `ui::Dialog` (from `Confirm`) and of every dialog `Frame` has the "Закрыть" tooltip. After: `Button … focus text="Отмена"`, `IconButton … icon=close tip="Закрыть"` (`B-confirm.txt`).

### SD-20 (Polish, confirmed): number fields clamp only at the top
- **Steps.** Settings > Управление > "Очень короткий", replace the value, Tab.
- **Actual.** 99999 → 3600 (clamped). 0 → the old value, rejected silently although the range hint says "1–3600 с". "2,5" → 3, rounded.
- **Fix.** Clamp both ends, or show the range hint in red until the value is valid.
- **Fixed (r5fs).** fixed (`4b0f93f`). Number fields clamp at both ends before storing; the range toast still says so. After: "0" in "Очень короткий" → `text="1"` (`F-clamped.txt`).

### SD-21 (Polish, confirmed): a language switch resets the scroll
- **Steps.** Settings > Аудио, wheel to offset 768, `set general.language en`.
- **Actual.** The window rebuilds on "Audio Defaults" at offset 0/1462 (`s1-lang0.txt` → `s1-lang1.txt`).
- **Cause.** `SettingsWindow.cpp:177-182` → `BuildUi` → `BuildPage` → `ScrollTo(0)`.
- **Fix.** Keep `content_` scroll across `BuildUi` when the category is unchanged.
- **Fixed (r5fs).** fixed (`4b0f93f`). The language rebuild keeps the content offset (applied after the first layout). After: `offset=1027/1027` before and after `set general.language en` (`C-scroll-before.txt`, `C-scroll-after.txt`).

### SD-22 (Polish, confirmed): wording and consistency
- **"Сочетания по умолчанию" row.** The label beside "Изменить сочетания клавиш" (`SettingsSchema.cpp:929`, en "Defaults") doesn't describe the row. A better label is "Сочетания клавиш" or "Список сочетаний". The two buttons there are 320 wide, while "Настроить" on Общие is 100.
- **EQ.** The column is "Усиление", but its tooltip is "Предусиление" (`SettingsSchema.cpp:1477`). The bands have no dB scale (+20 / 0 / −20); the ticks are bare "-" glyphs.
- **On/off.** Toggles say "ВКЛ"/"ВЫКЛ" (painted lower-case), but Replay Gain and the onboarding summary say "Выкл."/"Вкл.".
- **Ellipsis.** "Добавить папку в медиатеку…" has an ellipsis in the "⋯" menu but none in the sidebar "Добавить" menu; both open a picker.
- **Hint wording.** "Доступно, когда задано" (see SD-14).
- **Fixed (r5fs).** partly fixed (`4b0f93f`, `7372ef5`). The row is "Список сочетаний" (schema, 1 line); setting buttons hug their captions (min 100) instead of 320; the EQ has a +20/0/−20 dB scale and the preamp column's tooltip is "Предусиление: уровень перед полосами"; the sidebar "Добавить" menu says "Добавить папку в медиатеку…" (Shell.cpp, 1 line). Kept: "ВКЛ/ВЫКЛ" on toggles (a badge) vs "Выкл." as a dropdown value — two different controls, as in Windows 11; the onboarding summary is SD-10's.

## Design gaps (not bugs)

Compared with VLC 4, mpv/uosc, PotPlayer and the Windows 11 Settings app:

- **No About dialog** anywhere: no version, licence or mpv/ffmpeg versions. VLC, PotPlayer and Win11 apps all have one. Settings > Общие would be the place.
- **Settings search loses context.** A hit shows the bare row, even when it is disabled by a parent that isn't shown (e.g. "Способ выравнивания" without "Выравнивать громкость"). Win11 links each result to its page and highlights it. There is no hit highlighting and no result count.
- **Menu filter** has no match highlighting. uosc highlights the matched letters.
- **Subtitle font** is a free-text field ("Системный, без засечек"); VLC and PotPlayer offer a font list.
- **Equalizer** has no dB axis or curve preview, and no "save as preset". VLC 4 and PotPlayer have both.
- **Narrow Settings.** No responsive nav: Win11 collapses the nav into a hamburger below ~1000 px (see SD-08).
- **Accent palette** has no "custom" entry (not needed for the monochrome design; noted only).

## Re-checks of earlier rows

- **W-07** (no max width): fixed. `kMaxPageW = 880` keeps the content centred at 1920 (`s1-wide-controls.png`), but see SD-17 for the stranded title.
- **W-08** ("выкл" touches the right end): still present. The text box `[983,362 34x30]` ends at 1017 inside a 1024 track, with ~4 DIP right padding vs ~16 left on "вкл" (`s1-crop-toggle.png`).
  - **Fixed (r5fs, `dcad7d8`).** The knob is 18 DIP and both captions are centred in the free 40 DIP between the knob and the track's inner edge. After: `BoolToggle … text="ВЫКЛ" box=[979,207 40x30]` at 1.0, 1.25, 1.5 and 2.0 with no flag (`r5fs\tg*\interface.txt`); crop `toggle-s2-dark.png`. The `clip` the coordinator saw on BoolToggle is the row cut by the scroll viewport's bottom edge (`BoolToggle [956,856 68x32]` in an 865-high window); fully visible toggles carry no flag.
- **FL-11** (submenu after filter Esc): not re-tested. Filtering itself works ("наст" → 2 items, "zzz" → "Ничего не найдено", `s1-menu-filter.png`).

## Checked and working

- **Esc layering in Settings.** Esc closes the dropdown only (focus goes back to it), Esc while recording a key cancels the recording only, and Esc on a confirm closes the confirm only. With nothing open, Esc closes Settings.
- **Key binding editor.** Conflicts are reported with "Переназначить / Отмена" (`s1-kb-conflict.png`), Escape cancels recording, and plain keys record correctly. (A first "Ctrl + N" reading was a `GetKeyState` artifact of posted input; retried plain.)
- **Live apply.**
  - The theme changes live in both windows, via `set` and via the segmented control (Right arrow).
  - `set audio.eq true` enables the EQ and its dependent rows at once.
  - The accent swatches respond to arrows and have tooltips ("Жемчуг").
- **Disabled EQ.** A drag on a band while the EQ is off changes nothing.
- **Hit map.** Over the Settings header, the search field and Close are HTCLIENT and the rest is caption. In the main window title bar at 720x440, the maximize button answers HTMAXBUTTON (snap layouts).
- **Dialog fit at 1280x780.** Media info, Add subtitles and Customize Player fit, with the scrim present.
- **Toasts** are centred at the top of the window that raised them.
- **Rendering.** At `--scale 2` and `--scale 1.25`, Settings rendered cleanly with no blurry icons in the crops (`s3-crop-icons.png`).

## Not done

- **Snap-like sizes and maximized state for the main window.** Only the minimum (720x440) and default sizes were checked; round 4 covered maximize (W-04).
- **`--scale 1.5` captures.** Covered by the code reading in SD-04.
- **The New playlist and Open URL dialogs.** Open URL was opened (no new issues beyond W-09 and W-14); New playlist was not opened.
- **Tooltips.** Not swept on every control: checked on the nav, info icons, swatches, title bar and caption buttons.
- **Buttons that would open native pickers or Explorer.** "Обзор", "Добавить", "Найти расширения", the colour swatch and "Настроить" (file associations) were never clicked, per the hard rules.

## Fixes (r5fs)

Fixer: r5-fix-settings, branch `worktree-agent-adee3f9f72f919e4c`. Evidence in `%TEMP%\lumen-qa\r5fs\shots\` (`A-`…`G-` runs, `Eb-`/`Gb-` = the base exe at `facf7be`+main) and the sweep `%TEMP%\lumen-qa\r5fs-sweep2` (72 settings/dialog scenarios, `-Filter "settings|dialog|shortcut|customize|info|url"`).

**Baseline items (audit-baseline.md) in this scope:**
- **#1 Shortcuts dialog at 720×440:** fixed. Key columns narrow 270 → 120 and the gap 89 → 16 before the action name drops below 200; the dialog uses the window height − 24. After: `Label "Действие" [44,118 200x25]`, list `ScrollView 672x248` (was 3 DIP and ~30 DIP), `A-shortcuts-min.png`.
- **#2 Customize Player at 1024×640 and min:** fixed. The frame takes `min(1048, client − 32)`: `Frame [16,40 992x560]` at 1024×640, `[16,12 688x440]` at 720×440 (was `-164,22 1048x340`).
- **#3 Media info, unbreakable name:** fixed. Values wrap at path separators (zero-width breaks), are capped at three lines with "…" and clipped to their row; the full value is the value's tooltip. After: no overprint (`info-worst-crop.png`), audit: only "ellipsized, with tooltip".
- **#5 Settings at min size:** = SD-08.
- **#6 Customize palette labels:** = SD-13.
- **#14/#15 dropdowns:** a dropdown whose value is cut shows it as its tooltip (`Dropdown::Paint`, so the quick-settings audio dropdown gets it too). After: `ChoiceDropdown … trunc … tip="Название с дополнительными символами"`.
- **#16 stream dialog buffering row:** fixed. `SideRow` measures and lays out the description at the width it gets: `SideRow [44,469 936x68]`, no `outside` finding in the sweep.

**Sweep after the fixes:** every remaining `text-clip` ("taller than its box") in the 72 scenarios is a row cut by a scroll viewport's bottom edge (e.g. `settings-interface.dark` BoolToggle/Label at y=856–862 in an 865-high window, `dialog-info.*` rows at the info list's bottom). ui-audit could skip widgets that cross their ScrollView's edge. The `ItemGrid` findings in `dialog-shortcuts.*` belong to the library behind the scrim.

**Design gaps:**
- **About dialog:** added (`7372ef5`). Version from the VERSIONINFO resource, libmpv (`mpv-version`) and FFmpeg (`ffmpeg-version`) when mpv is up, licence links (GPL-3.0 for Lumen, mpv Copyright, FFmpeg legal, Inter OFL). Reachable from the app menu ("О программе") and from the bottom of the Settings nav (`B-about.png`).
- **Menu filter match highlighting:** skipped. It is a MenuPopup paint change, and MenuPopup is being edited by r5-fix-audit (r5-fix-flows also changed `MenuPopup::Refilter`); a parallel edit there would conflict.

**Changes outside the file scope (small, listed for review):** `src/app/Actions.cpp/.h` (`KeyText` localized, SD-15), `src/app/player/ControlBar.cpp` (4 icon names, SD-13), `src/core/SettingsSchema.cpp` (Performance icon, "Список сочетаний"), `src/app/Shell.cpp` (About menu item, "…" on the sidebar item), `src/app/Dialogs.h` (`ShowAboutDialog`).
