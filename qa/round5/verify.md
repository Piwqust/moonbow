# Round 5: verification of the merged fixes (r5-verify)

Verifier: r5-verify (branch `worktree-agent-a1d45e9c15261136f`).

- **Exe.** `%TEMP%\lumen-qa\r5-v8\Lumen.exe` (main `fe7577d`, every round-5 branch merged). Nothing was built.
- **Instances.** All `--automation`, started with `tools\dev\launch.ps1 -Fresh` and a test library. Each was killed by its own PID.

  | Data dir | Scale | Theme | Library | Used for |
  |---|---|---|---|---|
  | `r5vf-1` | 1.0 | dark | `%TEMP%\lumen-qa\media` | the owner's four complaints, the queue, the volume popup, Continue Watching |
  | `r5vf-2` | 1.5 | light | `%TEMP%\lumen-qa\worst` | crumbs, Back/Forward, tooltips, About, confirms, the menu filter, fullscreen at 1707 DIP |

- **Input.** `command.ps1`, `input.ps1`, `winpos.ps1`, `dump-tree.ps1 -All` (v2), `ui-audit.py`, `capture.ps1`. The helpers are `%TEMP%\lumen-qa\r5vf\snap.ps1` and `burst.ps1` (an in-process PrintWindow loop, with pixel probes per frame).
- **Evidence.** `%TEMP%\lumen-qa\r5vf\`: `<shot>.png` is a full capture, `<shot>.tree.txt` a dump, `c-*.png` a crop.
- **Sweep.** `%TEMP%\lumen-qa\r5vf-sweep` (289 scenarios, all ok, 5.9 min). For a fair "before", the baseline was re-audited with today's `ui-audit.py` and allowlist into `%TEMP%\lumen-qa\r5vf-base-reaudit`. That copy holds only the dumps; the baseline folder itself was not touched.
- **Screen.** 2560x1440 at 100 %.

## Summary

**Regressions: none found.** Every fix re-run on r5-v8 holds. Two are only partly done, and in both cases the gap was there before the merge; the merge didn't cause it:
- **LB-17.** Settings still has the old confirm dialog.
- **The Continue Watching frame.** It still shows a frame from 2:20 for a 4:4:4 H.264 file. That limit was already known.

**New bugs** (all in features added this round):

| ID | Sev | Status | Where | Finding |
|---|---|---|---|---|
| VF-01 | Ugly | confirmed | Volume popup (minimal bar / < 900 DIP) | The popup keeps its position after the bar moves. After F it floats in the middle of the fullscreen picture, 800 DIP from the speaker. It stays until the pointer moves. |
| VF-02 | Fragile | confirmed | Fullscreen with the side panel, screens narrower than about 2430 DIP | The wider fullscreen bar (70 %, ≥ 1180 DIP) is centred on the whole screen, so it covers the bottom 56 DIP of the 416-DIP side panel. This happens at 1920x1080 (100 %) and at 2560x1440 at 150 %. |
| VF-03 | Ugly | confirmed | Queue (popover and mini player), both themes | The selected row, the playing row and the hovered row are three greys 6–8 levels apart (dark 36/30/28, light 220/226). You can't see which row Del will remove or Enter will play. No focus ring appears after arrow keys either. |
| VF-04 | Fragile | confirmed | About dialog | Initial focus is on the first licence link, with no visible ring. Enter, the habitual "close", opens gnu.org in the browser instead of closing the dialog. |
| VF-05 | Ugly | confirmed | Settings confirms vs library confirms | Two confirm layouts ship side by side. Library confirms (LB-17) use `dlg::Frame`: Cancel on the left, 100-DIP buttons, a title without "?". Settings' "Сбросить настройки?" uses `ui::Dialog`: Cancel next to the action on the right, an 86-DIP Cancel, a title with "?". Its focused Cancel has no ring when the dialog is opened with the mouse, while the filled "Сбросить" looks like the default button. |
| VF-06 | Polish | confirmed | Fullscreen top info bar | The title of an untagged file keeps its extension ("Фильм — тест «кавычки» 日本語.mp4"), while the window title, the queue and the library show it without one. |
| VF-07 | Polish | confirmed (code) | Collapsed volume, keyboard | The collapsed speaker is a Tab stop, but Enter and Space don't open the slider popup (only the arrows step the volume). The level can't be seen from the keyboard. |
| VF-08 | Tool | confirmed | Sweep, `pip.*` | Two new `hitmap` (high) findings are by design. `hitmap.ps1` now names the widget under PiP's HTCAPTION area (VideoArea), so the `hitmap-odd` allowlist rule no longer covers it. |

## Job 1: regression check

Verdicts: **holds**, **regressed**, **partly**. The "How" column says what was re-run on r5-v8. "Sweep" means the item's scenario is in `r5vf-sweep`, and its finding from the baseline is gone.

### The owner's four complaints

| Complaint | Verdict | How (evidence) |
|---|---|---|
| 1. The UI is see-through | **holds** | Fullscreen, video playing over saturated bars. Pixels of the floating bar at six x positions: all 27,27,27. Top info bar: 27,27,27. Side panel: 25,25,25. Queue popover at four corners: 14,14,14. No hue bleeds through (`fs-side.png`, `fs-queue-del.png`). The light theme is opaque too (`w-fs15.png`). |
| 2. The queue can't be edited in fullscreen (FL-01) | **holds** | Fullscreen, side panel (Ctrl+B). Dump order: `VideoArea` and then `ui::Panel [0,0 416x1440]`. A click on the "Videos" row selected it, and the video stayed paused. A double click and the wheel on the panel left fullscreen on and the video paused (`fs-side.png`, `fs-side-click.png`). Queue popover (Ctrl+L) in fullscreen: a click selects, Down then Del removed "Chaptered Demo" (5 → 4 rows, no volume toast). Selection moved to the next row (`c-fs-queue-del.png`). A drag on the handle moved "4K Mandelbrot" from row 0 to row 2 (`fs-queue-drag.tree.txt`). Ctrl+Up moved it back up one row. End then Enter on the paused playing row resumed it (`fs-queue-keys.tree.txt`). |
| 3. The window resizes when switching videos | **holds** | Default `interface.autoResize=never`. N/P through 4K, Chaptered, Long Seek, 9:16 phone clip and the CJK film kept 1280x820 every time (`resize2-*.tree.txt`). With "first": one fit (1300x816), then four more switches kept it (`resize3-*`). |
| 4. The bottom bar hides and shows badly, has no minimal style, covers the video | **holds** | Fade: 45 PrintWindow frames at 70 ms from a posted move, in fullscreen. In: frame 0 partial, frame 1 opaque. Out at about 2.0 s: frames 26–28 blend the Play button, the bar background and the top info bar by the same factor. Nothing pops at full alpha (the probe log is in this report's transcript, `fade-cycle.png`). Minimal style exists, 40 DIP. Windowed, `VideoArea` ends at the bar's top in all three styles (floating, classic, minimal): `[0,40 1280x684]` + bar `[0,724]`; minimal `[0,40 1280x700]` + bar `[0,740]`. The bar doesn't cover the picture (`sheet-bar-styles.png`). |

### flows.md (FL)

| ID | Verdict | How |
|---|---|---|
| FL-01 | holds | See complaint 2. |
| FL-02 | holds (code) | `MainWindow.cpp:160-165`: `Enqueue` while `PlayerPanelOpen(Queue)`, else `Open`. `dropHint` is set in `FocusQueue` and reset in the popover's `onDismiss` (`Panels.cpp:128-133`, `:204-213`). An OLE drop can't be posted. |
| FL-03, FL-10 | not re-run | Code unchanged by the merge (`Player.cpp` reset list). |
| FL-04 | holds | Windowed, playing. Right-click the video, then left-click the video: the menu closed and `paused=0` stayed. |
| FL-05, FL-14 | holds (partly seen) | In fullscreen the panel rail stayed in the player. Restart was not re-run. |
| FL-06 | holds; see VF-03 | The keys work (complaint 2). The selection fill is too weak to see. |
| FL-07 | holds (code) | `Host.cpp:1383-1391` still sends `MouseEvent{…, cancelled = true}`. `QueuePanel.cpp:463` drops the drag. |
| FL-08, FL-13 | not re-run | — |
| FL-11 | holds (code) | `MenuPopup::Refilter` is still called on every filter change (`Controls.cpp:1091`, `:1278`). |
| FL-12 | holds (code) | `removeCold_` survives the merge (`QueuePanel.cpp:345`, `:414`, `:478`). |

### player-visual.md (PV) and design gaps

| ID | Verdict | How |
|---|---|---|
| PV-01 | holds | See complaint 4. |
| PV-10 | holds | See complaint 1. |
| PV-06, PV-07, PV-15 | holds (sweep) | Their baseline rows (#14, #7, #18) are gone or now carry a tooltip. The `MusicView` trunc in `player-audio-worst.*` is the false positive the fixer described: its tooltip is on a hover widget. |
| PV-14 | holds | PiP's HTCAPTION now stops at y = 244 (base 252), so the seek bar is taller. See VF-08 for the tool side. |
| P-02 (round 4) | holds | See complaint 3. |
| Gap 1 (wider fullscreen bar) | holds; see VF-02 | 1600 DIP at 2560, 1194 at 1707 DIP. |
| Gap 2 (top info bar) | holds; see VF-06 | It fades with the bar (frames 26–28). Title and chapter: "Chaptered Demo │ Opening". |
| Gap 3 (volume popup) | partly; VF-01, VF-07 | Opens on hover, `VolumePopup [1210,592 44x176]`. |

### settings-dialogs.md (SD) and audit-baseline.md

| ID | Verdict | How |
|---|---|---|
| SD-19 / LB-15 (Cancel focused) | holds | Library delete-playlist confirm: `DButton … focus text="Отмена"`, with a ring when it is opened from the keyboard (`c-delconfirm.png`). Settings reset confirm: `Button … focus text="Отмена"`; Tab moves the ring to "Сбросить", Esc closes (`c-confirm-pair.png`). See VF-05. |
| SD-06 (disabled looks) | holds | Settings › Аудио, the passthrough sub-toggles disabled in light and dark: flat fill, outline knob, dimmed text (`c-disabled-light.png`, `c-disabled-dark.png`). |
| About dialog | holds; see VF-04 | Version 0.1.0, libmpv and FFmpeg versions, four licence links. At 720x440 (1.5) it scrolls 45 DIP; audit: 0 findings (`w-about-min.tree.txt`). |
| Baseline #1–#6, #12, #13, #15, #16; SD-02, SD-03, SD-08, SD-13 | holds (sweep) | None of their scenarios has a finding left. #15 now reports `text-trunc` low **with** the full text as tooltip. |
| Baseline #17 (onboarding accent label under "Пропустить") | still open | Not claimed by any fixer. The same `overlap-cross` is in `onboarding-2`. |

### library.md (LB)

| ID | Verdict | How |
|---|---|---|
| LB-02 | holds | Albums (worst, 1.5). End: `offset=2070/2094 selected=29`. Enter, then Backspace: the same offset and selection come back. A back pressed in the middle of a 5-notch wheel scroll, then forward, restores the scroll target (480). |
| LB-03, LB-04, baseline #8/#9 | holds | 12 levels at 1280 (1.5): `… › Level 8 … Level 12`, current crumb whole (`c-w-nest12.png`). "…" opens a menu: Видео, Videos, Nested, Level 1–7. End then Enter went to Level 7 and re-selected Level 8 there. |
| LB-06 | holds | "Long names" at 1.5. Hovering a cut card gives `TooltipBubble` with the full name. Moving to the next cut card switches the text within 120 ms. A card that isn't cut has no tip (`w-tip*.tree.txt`). |
| LB-17 | **partly** | Library dialogs are unified. Settings' confirm still uses the old kit (VF-05). |
| Continue Watching frame | holds with its known limit | For "Long Seek Test 10min" paused at 5:01 (the frame reads "301"), the tile shows "140": the shell's frame at 2:20. library.md already says this file is H.264 4:4:4, which Media Foundation can't decode (`c-cw-compare.png`). Files MF decodes were not re-checked. |
| LB-10, LB-11, LB-13, LB-14, LB-16, baseline #10/#11 | holds (sweep) | Their scenarios are clean. |
| LB-01 | holds (dialog) | The confirm appears from the Delete key. It wasn't confirmed, so as not to change the test state further. |

## Job 2: new bugs

### VF-01 (Ugly, confirmed): the volume popup doesn't follow the bar
- **Steps.** r5vf-1, 1300x816, `set interface.barStyle minimal`, playing.
  1. Move to the speaker (1230,796), then to (1232,796).
  2. After 300 ms `VolumePopup [1210,592 44x176]` opens.
  3. Press `F`.
- **Expected.** The popup closes, or moves above the speaker of the fullscreen bar (`VolumeControl [1576,1388]`).
- **Actual.** It stays at `[1210,592]` in the middle of the picture, detached from its bar. The speaker keeps a stale `hot` flag. It closes only after the pointer moves. Because a popup is open, the chrome doesn't auto-hide until then. The same happens on any relayout: a window resize, a bar style change, the bar collapsing.
- **Evidence.** `volpop-fs.png`, `c-volpop-fs.png`, `volpop-fs.tree.txt` (popup line vs ControlBar line). Compare the queue popover: opened windowed, then `F`, it re-anchors to `QueueTab [1240,468]` above the fullscreen bar (`queue-fs-toggle.tree.txt`).
- **Cause.** `src/app/player/ControlBar.cpp:386-398`: `OpenPopup` passes a fixed `PopupOptions::anchor` computed once, and `VolumePopup` (`:164-253`) never re-places itself. Player popovers re-run `place()` in `Popover::OnLayout` (`src/app/player/PanelKit.cpp:281-282`).
- **Fix.** Give `VolumePopup` a `place` callback that reads the speaker's current rect (`VolumeControl::rect`, `Parent()->rect.y`) and apply it in `OnLayout`, as `Popover` does. Simpler: close the popup in `VolumeControl::OnLayout` when its rect changes or `collapsed_` becomes false.

### VF-02 (Fragile, confirmed): the wide fullscreen bar covers the side panel
- **Steps.** r5vf-2 (`--scale 1.5`, so fullscreen is 1707x960 DIP), play Chaptered Demo, `F`, Ctrl+B, move the pointer.
- **Expected.** The bar sits beside the 416-DIP panel (centred in the video area to its right), or is narrowed to fit.
- **Actual.** `ControlBar [256,888 1194x56]` overlaps `ui::Panel [0,0 416x960]` by 160 DIP. The bar is drawn on top (FL-01 keeps the bar last), so the panel's bottom rows are under Prev/Play/Next and can't be clicked there. The same arithmetic gives an overlap of 128 DIP on any 1920-wide fullscreen at 100 %: `clamp(0.7 w, 1180, 1600)` → 1344, x = 288. Only screens of about 2432 DIP or more are clear. At 2560 the bar starts at 480, so the owner's screen at 100 % doesn't show it.
- **Evidence.** `w-fs15-side.png`, `c-fs15-side.png`, `w-fs15-side.tree.txt`.
- **Cause.** `src/app/player/PlayerView.cpp:554-560` centres the floating bar on `rect`, ignoring `side_`, which is full-height in fullscreen (`:534-537`). The older 1180 cap already overlapped at 1920 (by 46 DIP); gap 1 made it wider.
- **Fix.** When `side_->visible && immersive`, lay the bar out in `{rect.x + sideW, …, rect.w - sideW}`: centre it there and cap `w` at `that width - 32`. The top info bar (`PaintTopInfo`, `:618-625`) could use the same area.

### VF-03 (Ugly, confirmed): the queue's selection can't be told from the playing and hovered rows
- **Steps.** Fullscreen (both instances), Ctrl+L, click a row or press Down.
- **Expected.** The row that Enter plays and Del removes is clearly marked. The FL-06 fix relies on this.
- **Actual.**

  | Theme | Selected (focused) | Playing | Hover | Background |
  |---|---|---|---|---|
  | Dark | 36,36,36 | 30,30,30 | 28,28,28 | 14,14,14 |
  | Light | 220,220,220 | 226,226,226 | — | 242,242,242 |

  Selected and playing differ by 6 levels. Only the playing row's bold text tells them apart. After the arrow keys there is no focus ring, because `ShowFocusRing()` stays false. In `c-fs-queue-del.png`, "Long Seek Test" (selected) and "Фильм…" (playing) look the same.
- **Evidence.** `fs-queue-del.png`, `c-fs-queue-del.png`, `w-fs15.png` (light, 1.5: "Chaptered Demo" playing and "Vertical phone clip" selected).
- **Cause.** `src/app/player/QueuePanel.cpp:328-335`. The playing row fills `accentFaint`, the selection `bgSelected`, the hover `hover`; with the default neutral accent all three are near-equal greys. The ring is painted only when `ShowFocusRing()` is true.
- **Fix.** Mark the selection with something other than fill:
  - a 2-DIP accent bar on the left edge, or `PaintFocusRing` whenever the list is focused, after keyboard **or** mouse;
  - or a clearly stronger `bgSelected` (≥ 3:1 against the panel).

  Keep `accentFaint` for the playing row.

### VF-04 (Fragile, confirmed): Enter in About opens a web page
- **Steps.** Menu ☰ → "О программе", from the keyboard (type "прог", Enter) or with the mouse.
- **Expected.** Focus on "Закрыть" (Enter and Esc close), as in the other dialogs.
- **Actual.** `AboutLink [380,417 238x24] interactive focus text="Lumen: GNU GPL 3.0 или новее" tip="https://www.gnu.org/licenses/gpl-3.0.html"`. No focus ring is drawn (`c-about.png`), so Enter would start the browser on gnu.org. (Enter was not pressed: it would open the owner's browser.)
- **Evidence.** `w-about.tree.txt` line 216, `w-about-min.tree.txt`, `c-about.png`.
- **Cause.** `ShowAboutDialog` (`src/app/dialogs/Dialogs.cpp:1031`, added in `7372ef5`) calls `ShowFrame(host, std::move(frame))` (`:1072`) without the `focus` argument (`DialogKit.h:150`). The frame then focuses its first focusable widget, which is the first link.
- **Fix.** Pass the "Закрыть" `DButton` as the frame's initial focus (as `ShowNewPlaylistDialog` passes `name`).

### VF-05 (Ugly, confirmed): two confirm dialog styles after the merge
- **Steps.**
  1. Settings › Общие › "Сбросить настройки" (`r5vf-2`, Settings window).
  2. Playlists › select "Тест" › Delete.
- **Expected.** One confirm style. LB-17 chose `dlg::Frame`: Cancel on the left, the action on the right, 100x32 buttons.
- **Actual.**
  - Settings: `ui::Dialog [294,339 460x188]`, title "Сбросить настройки?", `Button [540,475 86x32] focus "Отмена"` next to `Button [634,475 100x32] "Сбросить"`, both on the right.
  - Library: `dlg::Frame`, title "Удалить плейлист", `DButton [160,271 100x32] focus "Отмена"` on the left, a spacer, then `DButton [460,271] "Удалить"`.
  - Opened with the mouse, the Settings Cancel has focus but no ring, while the filled black "Сбросить" reads as the default. Enter cancels with no visible cue.
- **Evidence.** `w-confirm.tree.txt`, `c-confirm-pair.png`, `w-delconfirm.tree.txt`, `c-delconfirm.png`.
- **Cause.** r5-fix-library moved `ShowConfirm`/`ShowTextPrompt` to `dlg::Frame` (LB-17). The Settings `Confirm` (`src/app/settings/SettingsRows.cpp:1367-1385`, SD-19, r5-fix-settings) still builds a `ui::Dialog` with Spacer + Ghost "Cancel" + Primary button in its footer. It is used by `SettingsWindow.cpp:468` (reset category), `:521` (reset shortcuts), `SettingsShortcuts.cpp:227` and `SettingsAddons.cpp:311`. Each branch was right on its own; together they disagree.
- **Fix.** Build the Settings `Confirm` on `dlg::Frame`/`DButton`, as the library's `ShowConfirm` does (or call it), with Cancel focused and its ring shown, so all confirms share one look and behaviour.

### VF-06 (Polish, confirmed): the top info bar shows the file extension
- **Steps.** Play `Фильм — тест «кавычки» 日本語.mp4` (no title tag), `F`, move the pointer.
- **Expected.** "Фильм — тест «кавычки» 日本語", as in the title bar and the queue.
- **Actual.** "Фильм — тест «кавычки» 日本語.mp4" (`fs-side.png`, top centre).
- **Cause.** `src/app/player/PlayerView.cpp:612` uses mpv's `media-title`, which is the file name with its extension when there are no tags, while the window title strips it.
- **Fix.** Use the same display-title helper as the title bar (strip the extension when `s.title` equals the file name).

### VF-07 (Polish, confirmed by code): the volume popup can't be opened from the keyboard
- **Steps.** Minimal bar, Tab to the speaker, then Enter or Space.
- **Actual.** `VolumeControl::OnKeyDown` (`src/app/player/ControlBar.cpp:377-382`) handles only the arrows. The collapsed control has no `onClick`, so Enter and Space do nothing, and the level isn't shown anywhere for a keyboard user.
- **Fix.** When `collapsed_`, open the popup on Enter/Space (`OpenPopup()`). The arrows keep stepping the volume and the popup shows the new level.

### VF-08 (Tool, confirmed): PiP's drag area is reported as a dead zone
- `pip.dark` and `pip.light` give `hitmap` high: HTCAPTION over `VideoArea < PipOverlay`. In the baseline the hit map couldn't name the widget (`over:` was empty), and the allowlist rule `{"check": "hitmap-odd", "context": "pip.*"}` covered it.
- **Fix** (`qa/round5/audit-allow.json`): add `{"check": "hitmap", "context": "pip.*", "region": "^HTCAPTION", "type": "VideoArea", "reason": "…"}`.
- PiP's 4-px bottom resize band (`HTBOTTOM [12,268 .. 468,268] over SeekBar`) does lie over the bottom pixels of the now 24-DIP seek bar. That is the normal resize border, not new.

### Checked and fine (new features)
- **Crumb bar.** "…" is 14x24 DIP, with the 24-DIP spacing exception (the next crumb is at 342). The menu is reachable from the keyboard. The disabled "Вперёд" arrow is lighter (132 vs 92).
- **Back/Forward.** Interrupting a wheel scroll with Back keeps the target offset.
- **Menu filter highlight.** "прог" underlines "прог" in "О программе", aligned at 1.5 (`c-menu-filter.png`). A filtered command runs on Enter.
- **Tooltips on cut text.** Shown only for cut items, they follow the hovered item.
- **Disabled looks.** Consistent in both themes.
- **Drops that append.** The code path is consistent: the hint text is reset when the popover is dismissed.
- **The fullscreen top info bar.** Opaque, fades with the bar, title │ chapter.
- **The wider bar.** 1600 DIP at 2560 and 1194 at 1707; the minimal bar keeps 760.

### Not covered
- A real OLE drop (it needs the real mouse).
- Restart persistence (FL-14).
- The PiP and mini-player queues.
- The scale 1.25/2.0 combinations beyond the sweep's own scale scenarios.
- The "ё/е" menu-filter folding: no player-menu item contains "ё".

## Job 3: the full sweep

`powershell -File tools\dev\sweep.ps1 -Exe %TEMP%\lumen-qa\r5-v8\Lumen.exe -Out %TEMP%\lumen-qa\r5vf-sweep -Prefix r5vf`: 289 scenarios, all ok, 5.9 min.

"Before" is the baseline (`r5sw-baseline`, r5-v2) **re-audited with today's tool and allowlist** (`r5vf-base-reaudit`). It gives exactly the original 126 findings, so the numbers compare like for like.

| Check | Severity | Before | After |
|---|---|---|---|
| text-overflow | high | 31 | 0 |
| text-clip | high | 1 | 2 |
| clip-window | high | 2 | 0 |
| clip-window | medium | 6 | 0 |
| clip-parent | medium | 11 | 0 |
| hitmap | high | 0 | 2 |
| overlap-cross | medium | 12 | 12 |
| text-trunc | medium | 23 | 5 |
| text-trunc | low | 40 | 14 |
| overlap, hitmap-odd, target-size, zero-size, offscreen | — | 0 | 0 |
| **Total** | | **126** (34 high, 52 medium, 40 low) in 65 scenarios | **35** (4 high, 17 medium, 14 low) in 23 scenarios |
| Allowlisted | | 191 | 22 (the glyph-ink clip flag, f4bdf30, removed most text-flag false positives) |

**What is left, and what is new.** Keys are scenario + check + widget type; eight are new.
- **`menu-context.min` and `menu-context.1024x640`, text-clip high.** The partly visible row at the scrolled edge of a scrolling menu ("Статистика", "Открыть медиа"). The menus are the same size as in the baseline (`MenuPopup [360,8 275x424]`, `[512,8 275x624]`). The P-13 fade cue dims that row (`c-menu-min-bottom.png`). This is the dump's new ink-based clip flag, not an app change. Allowlist candidate: `text-clip` on `MenuPopup` rows at the scroll edge.
- **`pip.dark`, `pip.light`, hitmap high.** VF-08, a tool change.
- **`settings-interface.min`, `settings-subtitles.*`, `dialog-info-worst`, text-trunc low.** These carry a tooltip with the full text now (SD-08, #3, #15): improvements reported at low.
- **`player-audio-worst.1280x780`, `panel-queue-worst`, text-trunc.**
  - Title-bar label and `MusicView`: as in the baseline, under a different key.
  - Queue rows: cut names with no tooltip. Baseline #19 for queue rows, never in a fixer's scope. **Still open.**
- **Unchanged.**
  - `overlap-cross` in `player-audio.fullscreen.shown` (11): MusicView's whole-screen rect against the bar; the false positive explained in player-visual.md.
  - `onboarding-2`, the accent label under "Пропустить": baseline #17, not fixed.
  - The title-bar `Label` cut with no tooltip in the worst-case players.
  - The worst-case grid names (low).

## Final cleanup notes

Instances r5vf-1 (PID 79736) and r5vf-2 (PID 93724) were killed by PID. Sweep instances were started and killed by `sweep.ps1` under prefix `r5vf`.
