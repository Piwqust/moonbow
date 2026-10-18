# Round 5: audit baseline (layers 1–3)

The first full run of the layer-1 audit over the layer-2 state matrix, with the layer-3 worst-case data.

- **Exe:** `%TEMP%\lumen-qa\r5-v2\Lumen.exe`, main `facf7be` with tree dump v2, `command.ps1` and `--scale`.
- **Run:** `%TEMP%\lumen-qa\r5sw-baseline`; open `index.html` there for the contact sheet.
  - 289 scenarios, all `ok`, 6.2 min with 4 instances.
  - The 7 onboarding scenarios were re-run after a scenario fix (see "Tool notes") and merged in.
  - `-Reaudit` then re-applied the final allowlist.
- **Data:** `%TEMP%\lumen-qa\media`, `media2`, and `%TEMP%\lumen-qa\worst` from `qa/make-worstcase.ps1` v2: 1417 files, 87 MB.
- **Evidence paths** are relative to the run folder. Each scenario folder holds:
  - `final.png`: full resolution;
  - `final.tree.txt`, cited as "line N";
  - `final.audit.md`;
  - `final.hitmap.txt`, when the scenario sweeps WM_NCHITTEST.

## Numbers

| | count |
|---|---|
| Findings left after the allowlist | 126, in 65 of 289 scenarios (34 high, 52 medium, 40 low) |
| Allowlisted | 191: 157 text-flag false positives verified by crops, 20 drag areas, 14 by-design layouts |
| By check | text-trunc 63, text-overflow 31, overlap-cross 12, clip-parent 11, clip-window 8, text-clip 1 |
| Zero | hitmap, overlap (siblings), target-size, zero-size, offscreen |

**Distinct root causes.** After grouping repeats (same widget, same cause, several sizes or themes), the 126 findings come down to about 30 causes:
- **Real:** 19 causes, all confirmed by a crop or by the tree, listed below.
- **Low-value or by design:** about 8. These are long worst-case names ellipsized on cards and in queue rows, in a fixed-size layout.
- **Suspected:** 1, the music-view title under the fullscreen bar.
- **Measurement noise:** the rest, now in the allowlist.

The brief asked for the top 30. The audit yields 19 distinct real bugs, so the table has 19 rows. Padding it with repeats of the same cause at another size would not help the fixers.

## Real bugs (confirmed)

Severity uses METHOD.md: Broken, Ugly or Fragile.

| # | Sev | Screen / state | What is wrong | Evidence | Likely fix |
|---|---|---|---|---|---|
| 1 | Broken | Keyboard shortcuts dialog, min window (720×440) | The action column collapses to 3 DIP: the header "Действие" sits under "Клавиша", no action name is readable, and the list area is about 30 DIP tall | `dialog-shortcuts.min/final.png`, tree lines 213, 226 (`Label 44,186 3x20`) | Give the action column a min width (or stack key under name below ~800 DIP); cap the header/search block height |
| 2 | Broken | Customize player at 1024×640 and min | The dialog `Frame` stays 1048 DIP wide: it is cut by 12 DIP on each side at 1024 and by 164 DIP at 720. The title, Cancel and Reset sit off-window, and the third palette row is hidden | `dialog-customize.min`, `dialog-customize.1024x640`: line 226 (`Frame -164,22 1048x340`) | Clamp the frame to the client minus margins, let the palette wrap or scroll |
| 3 | Broken | Media info, unbreakable file name (worst case) | The wrapped path is taller than its 60-DIP row and is drawn over the Size and Duration rows ("69,8 КБ" and "0:02" overprint ".mp4") | `dialog-info-worst/final.png`, line 217 | Measure the wrapped value height for the row (or break the path at `\` and allow N lines with ellipsis + tooltip) |
| 4 | Broken | Onboarding page 4 (privacy) | Each description wraps to 2 lines (40 DIP), but its row is 47 DIP with the title on top. The second line is cut in half at the next row ("«Послушать снова».") | `onboarding-4/final.png`, lines 209, 227 (`Label … outside Widget … by bottom 20`) | Row height from the wrapped label (measure with the real width) |
| 5 | Ugly | Settings window at min size (760×520), every page | Row labels shrink to 120–148 DIP and are ellipsized with no tooltip: "Подгонять окн…", "Скрывать пане…", "Вид панели упр…", "Устройство вывода", "Конфигурация колонок", "Алгоритм увеличения", "Сочетания по умолчанию", "Длинный (Ctrl+Shift+стрелки)"… The 320-DIP controls keep their width | `settings-interface.min` lines 235/266/271, `settings-audio.min` 240/245, `settings-video.min` 262, `settings-controls.min` 235/239/1197 | Below ~900 DIP put the control under the label (or shrink controls first); add the label as tooltip when cut |
| 6 | Ugly | Customize player palette, every size | The labels are wider than the 86-DIP cells. "Вперёд на 10 с" and "Назад на 10 с" run into each other, and "Скорость воспроизведе-ния" wraps to 3 lines with a hyphen | `dialog-customize.dark/final.png`, lines 238–257 (31 `text-overflow`) | Wider cells (≥ 100 DIP) or two-line wrap at word boundaries inside the cell; shorter strings ("Вперёд 10 с") |
| 7 | Ugly | Side panel, Bookmarks tab, empty | The empty-state hint is ellipsized in a 400-DIP panel: "Нажмите «+», чтобы добавить закладку на текущий мом…" | `panel-bookmarks.dark/final.png`, line 210 | Wrap the hint (it is a sentence) |
| 8 | Ugly | Library breadcrumbs, 1024×640, 8 folders deep | The current folder crumb collapses to 15 DIP and shows "L…". Round 4 claimed "текущая папка в узкой шапке не теряется": this is a regression or a missed case | `worst-nested.1024x640/final.png`, line 39 (`CrumbButton 565,63 15x24`) | Collapse earlier crumbs into "…" first; give the last crumb a min width ≈ its text up to 50 % of the bar |
| 9 | Ugly | Albums page, min window | The current crumb "Альбомы" is ellipsized at 51 DIP while the back/forward and sort buttons keep their size | `music-albums.min`, line 33 | Same rule as #8 |
| 10 | Ugly | Songs list, every size | The "№" column title is ellipsized (28-DIP column) | `music-songs.dark`, line 49 (also `worst-big-list.*`, `music-songs.max`) | Widen the track column to fit "№ ↑" or drop the sort arrow into the padding |
| 11 | Ugly | Songs list, min and 1024×640 | The columns never drop: at 472 DIP the titles, artists, albums and the "Исполнитель" header are all cut ("Track Numb…", "Album…") | `music-songs.min` (14 strings), `music-songs.1024x640`, `worst-songs.1024x640`, line 49 | Hide Album, then Artist, below a width; keep the title readable |
| 12 | Ugly | Onboarding page 6 (summary) | "История воспроизведения" and "Метаданные из интернета" are ellipsized at 170 DIP, no tooltip | `onboarding-6`, lines 229, 234 | Wider label column or wrap |
| 13 | Ugly | Onboarding page 5 (shortcuts) | The right column of key caps ends 3 DIP past the window's right edge, so there is no margin and the caps touch the border | `onboarding-5/final.png`, lines 230–251 (`KeyCombo 853,… 240x46`, window 1090) | Shrink the column gap or right-align inside the 40-DIP page margin |
| 14 | Ugly | Quick settings, Audio tab | The audio track dropdown shows "English 440 Hz (e…" with no tooltip; the panel has room for a wider field | `panel-quick-audio.dark`, line 210 (`Dropdown 1052,151 188x48`) | Wider dropdown or tooltip with the full track name |
| 15 | Ugly | Settings > Subtitles | The "matching" dropdown value "Название с дополнительными символами" is ellipsized, no tooltip | `settings-subtitles.dark`, line 259 | Shorter label ("Название + символы") or tooltip |
| 16 | Fragile | Open network stream, 1024×640 | The buffering description wraps to 2 lines, but its row stays 48 DIP. The text runs 20 DIP into the dialog's bottom padding, and the scroll view reports `offset=0/0`, so the overflow is not scrollable | `dialog-url.1024x640`, line 229 | Same as #4: the row measures its wrapped description |
| 17 | Fragile | Onboarding page 2 (theme) | The accent-name label is 330 DIP wide and its rect runs under the "Пропустить" link (703–771 DIP). "Белый" is short, but a long name (English UI, a future accent) would overlap | `onboarding-2`, line 214 | Limit the label to the space left of the buttons |
| 18 | Fragile | Music view, long tags (worst case) | The "title · artist" line is ellipsized with no tooltip: the artist and album can't be read anywhere on the screen | `player-audio-worst.1280x780`, line 157; title bar same, `player-worst-title.1024x640.shown` line 11 | Tooltip on the title line; wrap to 2 lines in the music view |
| 19 | Fragile | Home music cards, playlists, worst-case cards and queue rows | Ellipsized names with no widget tooltip (per-item tooltips may exist: the dump can't show them) | `playlists.dark` line 43, `worst-video-names.1280x780` line 45, `panel-queue-worst` line 213 | Verify hover tooltips on cards and queue rows; add them where missing |

**Suspected:**
- **Music view in fullscreen** (`player-audio.fullscreen.shown`, line 157). The title string's box lies under the floating bar: 11 `overlap-cross` findings with every bar control. The crop shows no visible overlap, because the bar is painted on top. So either the title is hidden behind the bar (wrong layout) or the box is stale. Check the MusicView layout against the fullscreen bar height.

**Not reported again:**
- **Side panel dead in fullscreen** (overlay order). Known; a fixer is on it.

**Not found by the audit:**
- **Hit map.** No region over a control in any swept scenario: home, player in three bar styles, fullscreen, maximized library, PiP, mini player, Settings and onboarding. So the earlier caption-over-button class looks fixed.
- **Target size.** No undersized interactive widget without the 24-DIP spacing exception. For example, the 18-DIP info icons in Settings stand alone.
- **Scale 125/150/200 %.** No tree findings. Crops of home at 1.25 and the player bar at 1.5 look sharp. At 2.0 the window (2560×1560) is taller than the 1440-px monitor, an expected effect of the override.

## Noise and what the allowlist does (`qa/round5/audit-allow.json`)

Every rule has a reason in the file. In short:

**Text-flag false positives** (157 findings): the v2 `clip`/`trunc`/`overflow` flags on strings that 2–3× crops show whole.
- Settings nav group titles: 17-DIP line box, "Приложение"/"Медиа"/"Система". These alone are 139.
- Onboarding cards and titles.
- The onboarding-1 paragraph: it is wrapped, but `trunc` and `overflow` are set.
- The lyric "· · ·".

Report to the dump author: the natural height seems to include the line gap, and wrapped text is flagged `trunc` even when it fits.

**Drag areas:**
- the frameless onboarding window and PiP answer HTCAPTION over their background;
- the controls in both answer HTCLIENT.

**By design:**
- Customize player edits the real bar in place, outside the dialog body.
- An empty ItemGrid keeps a zero-height slot.
- Editable fields scroll their text, so truncation there is fine.

**Overlay and canvas types:**
- MiniPlayer, PipOverlay, ToastBubble, TooltipBubble and ScanBadge float over the layout.
- VideoArea (the video surface) and PreviewWindow (the mock player behind the onboarding theme page) are not text or input.

**Downgraded to low, not hidden:** ellipsis inside grids and lists (ItemGrid, QueueList, BookmarkList, MenuPopup…). The dump has no per-item tooltips. Item #7 is real despite the downgrade.

## Tool notes for the next run

- **Posted Ctrl+letter keys also produce WM_CHAR.** TranslateMessage generates the character, so a focused text box receives "k" after Ctrl+K and "n" after Ctrl+N. `input.ps1` has the same problem. Scenarios avoid it with `command.ps1`, Backspace, or a click on the box.
- **The default onboarding folder is the real `%USERPROFILE%\Videos`.** The first baseline pass of onboarding-2..6 accepted it, so the test library scanned the owner's Videos (read-only). The scenarios now type the test media path first; the merged results are from the fixed run.
- **`hitmap.ps1` cannot parse type names that contain spaces** (`` `anonymous namespace' ``). Its "over:" column then names a parent. `ui-audit.py` re-derives the widgets from its own tree, so the check is still exact for dense regions.
- **The worst-case clips were 2 s in v1 of the media set.** A scenario that opened one showed the next file of the folder. v2 uses 20-s clips.
- **The audit reads only the captured window.** With `-All` the main window is also in the dump of a Settings or onboarding scenario. Before this change the library behind the Settings and onboarding windows was reported again in each of their scenarios.

## How to reproduce

```
powershell -File qa\make-worstcase.ps1
powershell -File tools\dev\sweep.ps1 -Exe %TEMP%\lumen-qa\r5-v2\Lumen.exe -Out %TEMP%\lumen-qa\r5sw-baseline
powershell -File tools\dev\sweep.ps1 -Reaudit -Out %TEMP%\lumen-qa\r5sw-baseline   (after editing the allowlist)
python tools\dev\ui-audit.py <dir>\<scenario>\final.tree.txt [--hitmap <dir>\<scenario>\final.hitmap.txt]
```
