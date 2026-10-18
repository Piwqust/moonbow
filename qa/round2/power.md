# Lumen r2 — power-user re-review ("power")

Persona: MPC-HC / mpv user who works keyboard-first. Build: `build-r2\Lumen.exe`. Data dir: `%TEMP%\lumen-qa\r2-power`, wiped with `-Fresh` for each scenario. Every instance was started with `--new-instance --automation`. I drove them only through `tools\dev\input.ps1` (PostMessage) and `capture.ps1`, and the volume stayed at 0.

Screenshots are in `%TEMP%\lumen-qa\round2\shots-power\`, at half scale. Files named `m*.png` are montages of crops from the numbered shots.

**Short version:** the round-1 fixes are real. There are now OSD toasts for seek, tracks, chapters and A-B; the error card is proper; `.m3u` files open; a missing path is handled; auto-fit works per file; Space no longer gets taken over after a mouse click; delays reset per file; and Stop stays in the player.

The core resume bug is still there, and it is unchanged. Re-opening a finished file still plays a different file, and Previous in a series still never reaches the previous episode.

There is also a new keyboard-focus trap. If you open a file from the library with the keyboard, Space and the arrow keys stop working in the player.

## Re-check of round 1

| ID | r1 sev | Status | Evidence / notes |
|---|---|---|---|
| P01 | Blocker | **Still broken** | Chaptered played to EOF. Its watch_later entry still has `start=89.967000` (plus `aid=2 sid=2`). Re-opening `Chaptered Demo (1080p).mkv` from the command line played **Long Seek Test at 0:50** (`33-reopen-finished-3s.png`). The series entries likewise have `start=11.966759` / `11.966667`. The near-end guard `Player::NearEnd`/`SaveWatchLater` (Player.cpp:521-535) exists, but the entry is still written on the EOF→next transition. |
| P02 | Blocker | **Still broken** | (a) On E03, Home then P within 100 ms: E03 stays and restarts at 0:03 (`m3-titles.png`, `37-home-then-P-3s.png`). App.cpp:742 still reads the stale position, which is > 3 s. (b) P on E03 at EOF: E03 stays at 00:12 in all four captures over 1.2 s (`m5t.png`, `m5b.png`). (c) `Lumen.exe "…\Series S01E02.mp4"` plays **S01E03** (`m6t.png`, `42-open-E02`). |
| P03 | Major | **Fixed** | A missing path opens the library with the toast "Файл не найден: does not exist.mp4" ("File not found: …"), still visible at 3 s, and nothing plays (`52-missing-3s.png`). |
| P04 | Major | **Fixed** | Long Seek Test 720p got a 1300×816 window. After N, the vertical clip got a portrait 760×1184 window, and the next 720p file got 1300×816 again (`74-space-at-eof.png`, `31-next-at-end.png`). |
| P05 | Major | **Fixed (mouse path)** / see N02 | Clicked the subtitles button, Esc, Space: playback paused and the panel did not reopen (`55-sub-panel.png`, `56-space-after-click.png`). The same class of bug now appears through keyboard focus (N02). |
| P06 | Major | **Fixed** | `mixed list.m3u` starts S01E01. The queue shows 2 items, S01E01 and Track 1, and auto-advances (`53-m3u-3s.png`, `54-m3u-queue.png`). |
| P07 | Major | **Fixed** | "Аудио: Русский 660 Hz (rus) · aac" ("Audio: …") and "Субтитры: rus · subrip" ("Subtitles: …") (`03-cycle-audio.png`, `04-cycle-sub.png`). |
| P08 | Minor | **Fixed** | Shift+PgDn shows "Глава 3/3: Ending" ("Chapter 3/3: Ending") (`05-next-chapter.png`). |
| P09 | Minor | **Fixed** | The toasts read "Начало петли 01:21" ("Loop start"), "Петля 01:21 – 01:23" ("Loop") and "Петля выключена" ("Loop off"). The seek bar draws an A–B bracket (`m1-toasts.png`, `m8-abbar.png`). |
| P10 | Minor | **Fixed** | → shows "00:18 / 01:30", Ctrl+← shows "00:00 / 10:00" (`02-seek-right-osd.png`, `m7t.png`). |
| P11 | Major | **Fixed** | Persistent card: "Не удаётся открыть «fake.mkv»" ("Can't open 'fake.mkv'"), an explanation line, "mpv: unrecognized file format" in small text, and buttons "Следующий файл" ("Next file") and "Закрыть" ("Close"). N goes to the next file and Esc closes (`50-fake-3s.png`, `51-trunc-3s.png`, `m10-err.png`). |
| P12 | Major | **Fixed** | J×3 (−150 ms), then N: on the next file K shows "Задержка звука 50 мс" ("Audio delay 50 ms"), so the delay was reset (`29-delay-after-next.png`). No delay keys were written to watch_later. |
| P13 | Minor | **Fixed (code-verified)** | Actions.cpp:375 now reads `audio.volumeStep`. Not exercised, because I kept the volume at 0 (↓ at 0 % only shows "Громкость 0%", "Volume 0%"). |
| P14 | Minor | **Partly fixed (code)** | The toast now uses the configured folder (Actions.cpp:439-441). It is still shown before mpv reports success, and still without the file name. Not exercised, to avoid writing outside the data dir. Also code-only: the Record toast is still hard-coded to "Videos\Lumen" even when `recording.folder` is set (Actions.cpp:460). |
| P15 | Minor | **Fixed** | Remaining time shows "-09:12" in full, without the hours field. It persisted to the next file (`m7b.png`, `74-space-at-eof.png`). Restart not checked. Off-by-one: see N08. |
| P16 | Minor | **Still broken** | With the default `playback.whenFinished = stop`, a single opened file still rolls into the next file in the folder: 4K→Chaptered (`80-4k.png`), Chaptered→Long Seek, Long Seek→Vertical. Ctrl+→ past the end of Long Seek Test jumped into "Vertical phone clip", with the "10:00 / 10:00" toast drawn over the new file (`m7t.png` row 71). This is also what turns P01 into "plays a different file". |
| P17 | Minor | **Fixed** | N on the last item shows "Конец списка" ("End of list") (`m6t.png`, `41-N-at-end`). |
| P18 | Minor | **Fixed** | At 760 px the volume slider collapses to an icon and the seek bar is about 206 px (r1: 148) (`74-space-at-eof.png`). |
| P19 | Minor | **Still broken** | Library › Видео › Mixed lists "broken truncated" and "fake" as normal video tiles: placeholder icon, no duration, no "unreadable" mark (`93-mixed-open.png`). |
| P20 | Minor | **Fixed** | S shows "Остановлено" ("Stopped"), stays in the player at 00:00, paused (`99-stop.png`). Resuming after it is broken in the keyboard flow: see N02. |
| P21 | Minor | **Still broken** | The subtitles panel still shows "Встроенные дорожки" ("Embedded tracks") → "Нет" ("None") as an embedded track, and "Не добавлены" ("Not added") under external tracks (`55-sub-panel.png`). Sidecar label not re-checked. |
| P22 | Minor | **Still broken (slightly worse)** | 4K Mandelbrot: 813 MB working set / **1,244–1,288 MB private** for the first 3 s (r1 peak was 1,047 MB). It drops to about 215 MB working set / 333 MB private after the file. Measured with `Get-Process` every 500 ms. |
| P23 | Polish | **Fixed** | The played part is now a clearly lighter grey than the unplayed part (`02-seek-right-osd.png`). |
| P24 | Polish | **Fixed (code) / partly seen** | `VolumeIcon` now returns none/low/medium/high (Actions.cpp:567). Only the 0 % and "Звук включён" ("Sound on") states could be seen at volume 0. |
| P25 | Polish | **Partly fixed** | The title bar now strips the extension ("Long Seek Test 10min", "silent vp9"). The queue still shows "Series S01E01.mp4" with the extension and no duration, next to a music item with tags and a duration (`54-m3u-queue.png`). |
| P26 | Polish | **Partly fixed** | Record moved to Ctrl+Shift+R, which is good. Still: J/K audio delay, Shift+G/H subtitle delay, H = shuffle, B = next audio track, no Ctrl+Numpad± for speed, no 10 % speed steps, no MPC-HC/mpv preset (Actions.cpp:43-61, 154). |

## New findings

| ID | Severity | Area | What happened | Steps to reproduce | Expected | Evidence |
|---|---|---|---|---|---|---|
| N01 | Blocker | Resume / queue | P01, P02 and P16 together are still the top experience killer. Re-opening a finished movie, or the second episode of a series, plays something else. In a series there is no keyboard way back to the previous episode. | See P01/P02 above. | A finished file starts from 0. Previous loads the previous episode from the start. Opening E02 plays E02. | `33-reopen-finished-3s.png`, `m3-titles.png`, `m5t.png`, `m6t.png` |
| N02 | Major | Keyboard focus | Open a file from the library with the keyboard (Tab or arrows to a tile, Enter). The player opens, but keyboard focus stays on the hidden library tile. **Space and ←/→ do nothing in the player**: three Space presses after S left it paused at 00:00, and Right/Left gave no seek and no toast. Enter starts playback again, apparently by re-activating the hidden tile. S, N and Ctrl+Q still work, because the tile does not consume them. | Library → Видео → Mixed (Enter) → → → to "silent vp9" → Enter → S → Space → Space → Right. | Opening media moves focus to the player, and Space/arrows are player shortcuts. | `m12b.png`, `m13.png` (105-right: still 00:00, 106-enter: playing), `m14.png` (108-left: no seek). Likely cause: Host.cpp:804-808 and 823-827. The focused widget, a hidden library tile with `focusVisible_` true, gets the keys before `onKey`. |
| N03 | Minor | Keyboard in library | In the library, ↑/↓ change the volume (a "Громкость 0%" toast over the library) instead of moving between tiles or sidebar items. Tab works (sidebar → header → tiles → section headers), but a whole row of tiles is a single Tab stop. | Library home, Tab ×7, then → and ↓. | Arrow keys move the selection in grids and lists. Volume keys apply only in the player. | `87-arrows.png`, `86-tab7.png` |
| N04 | Minor | Video transforms | Aspect override (X) and zoom (Z) carry over silently to the next file. After X (16:9) and Z (107 %) on Chaptered, the 720×1280 vertical clip was shown **stretched to 16:9** in a landscape window. Nothing on screen says an override is active. The X cycle also uses a static index (Actions.cpp:468), so it does not follow the real property. | On a video press X and Z, then N until "Vertical phone clip". | Reset per file, as P12 now does for delays, or show a persistent indicator. | `28-next-vertical.png`, `29-delay-after-next.png` |
| N05 | Minor | Mute OSD | M (mute) showed no toast twice in a row. The following M (unmute) showed "Звук включён" ("Sound on"). | Press M and look 300 ms later; press M again. | "Звук выключен" ("Sound off") on mute. | `10-mute.png`, `23-mute` / `24-unmute` in `m2-toasts.png` |
| N06 | Minor | Seek toast timing | The seek toast is computed from the position before the async seek and stays on screen across a file change. Ctrl+→ past the end shows "10:00 / 10:00" over the next file. | Ctrl+→ ×10 on Long Seek Test. | Clamp at the end, no file change (P16), and no stale toast over the new file. | `m7t.png` (row 71) |
| N07 | Polish | Shortcuts dialog | F1 lists only two binding columns ("Клавиша" / "Дополнительно", "Key" / "Additional"). Third default bindings, such as Media Prev/Next and Volume Up/Down, are invisible and cannot be edited there. | F1, look at "Предыдущий файл" ("Previous file"), which should show P, PgUp and Media Prev. | Show all bindings. | `83-f1.png` |
| N08 | Polish | Time display | Remaining time is one second short at the start: "-09:59" at 00:00 of 10:00, "-00:14" at 00:00 of 0:15. | Toggle remaining time, press Ctrl+← to 0. | "-10:00" / "-00:15". | `m7b.png` (row 70), `74-space-at-eof.png` |
| N09 | Polish | Track OSD | The subtitle toast shows the raw codec name: "Субтитры: rus · subrip". The audio toast shows "· aac". | V on Chaptered. | Language or title only, e.g. "Субтитры: Русский". | `04-cycle-sub.png` |
| N10 | Polish | Library focus | Two elements look focused at once: the "silent vp9" tile keeps a white ring and bold label while the "Недавно добавленная музыка" ("Recently added music") header has the focus ring. | Library home, Tab ×15. | A single focus indicator. | `m9-tab.png` (row 89) |
| N11 | Polish | Subtitles panel | The pressed subtitles button stays highlighted after the panel is closed with Esc. | Click the subtitles button, then Esc. | Back to its normal state. | `56-space-after-click.png` |

### Measurements (good)

- **Startup:** the window appears 106 ms after `Start-Process`. The title shows the 4K file at 373 ms.
- **CPU:** 1080p playing about 3 % of one core; paused 0.4 %; library idle 0.0 %.
- **Memory:** Chaptered 1080p about 215 MB working set; library after Ctrl+W 160 MB working set / 185 MB private; idle at launch 128–139 MB. The 4K peak is in P22.
- **Fullscreen and PiP:** F → 2560×1440, Esc → back to the window. Ctrl+Shift+P → 480×270 PiP, Esc → back. Ctrl+\ hides the title bar, the control bar auto-hides after about 3 s, and Esc restores both (`61-fs.png`, `63-pip.png`, `m7t.png`).
- **Other shortcuts:** these all toast correctly: frame step `.` / `,`, `]`, Backspace, L, Z, X, Ctrl+R, D, K/J, Shift+H/G (`m1-toasts.png`).

### Not tested

- **Alt shortcuts:** Alt+Enter, Alt+Home, Alt+←/→, Alt+Z, Alt+± were not tested, because `input.ps1` has no Alt modifier.
- **Snapshot and Record:** not exercised, because both write outside the data dir.
- **Volume steps:** only exercised at 0 %.
- **Second-instance hand-off:** skipped. Other reviewers' instances were running.

## Top 5

1. **Blocker (P01/P02/P16 = N01):** re-opening a finished file or a series episode plays a different file. Previous in a series never reaches the previous episode, and a single file still rolls into the rest of the folder despite "stop".
2. **Major (N02):** open a file from the library with the keyboard and Space and the arrow keys are dead in the player. Focus stays on the hidden tile.
3. **Minor (N04):** aspect override and zoom silently carry over to the next file, so a vertical clip gets stretched to 16:9.
4. **Minor (P22):** the 4K start-up peak is about 1.29 GB private memory, worse than r1.
5. **Minor (N03 + P19 + P21):** the library ignores the arrow keys (↑/↓ change the volume there), unreadable files look like normal videos, and "None" is still listed as an embedded subtitle track.
