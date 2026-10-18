# Round 5: leftovers (r5-leftovers)

Branch `worktree-agent-a60a09847e5a9b8e8`, on top of main fe7577d, with main aa462c5 (verify.md) merged in. Base exe for comparison: `%TEMP%\lumen-qa\r5-v8\Lumen.exe`. Evidence: `%TEMP%\lumen-qa\r5lo-*`. Every test instance ran with `--automation` and `-Library` set to a test folder, and was killed by its own PID.

| Item | Status | Commit |
|---|---|---|
| 1. Scroll cues in `ui::ScrollView` | done | eeae4ed, afc76e2 |
| 2. Volume wedge geometry cache | done | bb4231f |
| 3. Tooltips for cut text in the player | done | db97f3b |
| 4. Sweep: medium/high findings | done: 20 → 0 | 9c76f5a |
| VF-01…VF-08, baseline #17 (coordinator's follow-up) | done | 5a37447, 9c76f5a |

## 1. ScrollView: resting thumb, edge fade, no scroll for ≤ 4 DIP

- `ui::ScrollView` (`src/ui/Controls.cpp`) now does what `panel::PanelScroll` did, for every scroller. `PanelScroll` is gone; the popovers use `ScrollView`.
  - It shows a resting thumb while the content is taller than the view: 2 DIP wide, `lineStrong`. The full 4/8-DIP thumb covers it while scrolling.
  - Each cut edge fades over 28 DIP, in 7 bands, into the backdrop of the nearest ancestor that has one. `Widget::Backdrop` is new; Panel, `dlg::Frame`, `Popover` and `LibraryPane` implement it. With no backdrop there is no fade, only the thumb.
  - `Scrollable()` is `MaxOffset() > kNoScroll (4)`. Below that the view takes no wheel, shows no bar, ignores track clicks and `ScrollIntoView`.
- Checked at 720x440 (the popover, the shortcuts dialog) and at 760x520 (Settings, its minimum), in both themes. Base and fixed captures: `r5lo-t1\{base,new}-{dark,light}-*.png`, `cmp-light-settings.png`, `cmp-light-dialog.png`. Settings' nav and content, the shortcuts dialog body and the quick-settings popover each show the thumb and the bottom fade. The base Settings and dialog show neither.
- There is no animation and no timer, so nothing repaints while idle (see Performance).

## 2. Volume wedge

`VolumeControl` keeps both `ID2D1PathGeometry` objects. The track geometry is rebuilt only when the wedge rect changes. The level geometry is rebuilt only when the rect or the volume changes. They are factory resources in DIPs, so a device loss or a DPI change doesn't affect them. The wedge wheel test (12 notches, so 12 level rebuilds) paints as before: 0.24-0.28 ms against 0.23-0.24 ms.

## 3. Tooltips for cut text

Each tooltip below appears only when the text is cut.
- **Queue rows** (popover and side lists), **chapter rows** and **bookmark rows.** While the hovered row paints, the list's `tooltip` gets the full title, plus `\n` and the artist. The host's re-arm handles the change of text, as for ItemGrid. The handle and "×" tips still take priority. Evidence for three worst-case rows (`r5lo-t3\new-queue-row*.txt`, `new-queue-tip.png`): `TooltipBubble [697,204 319x63] "Supercalifragilistic…"`, `"The.Series.Name…"`, `"Привет, мир…"`. The base shows no tip. Chapter and bookmark rows use the same code; they were not tested at runtime, because the test media has no long chapter names.
- **Title bar title.** The label stays part of the caption, so it still drags the window.
  - The host tracks `WM_NCMOUSEMOVE` (HTCAPTION) over a caption widget that has a tooltip (`ncHover_`). It shows the bubble after the usual delay.
  - The bubble goes on `WM_NCMOUSELEAVE`, on a move into the client area, or when the title is pressed to start a drag.
  - These moves skip `DefWindowProc`: its own caption tracking posted a leave at once.
  - Evidence: `TooltipBubble [554,38 319x63]` with the full file name (`r5lo-t3\new-title-tip.png`).
- **Fullscreen title panel.** When the panel cuts the title or the chapter, the video carries the full text while the pointer is on the panel. At `--scale 3` it shows `TooltipBubble [427,54 318x63] "The.Series.Name…"`. It is gone after moving off the panel, and it goes with the chrome.
- **Music view.** Its hover widget (PV-15) works: `TooltipBubble [415,318 321x114] "Short\nDie Ärzte & …"`.

## 4. Sweep

`powershell -File tools\dev\sweep.ps1 -Exe <build>\Lumen.exe -Out %TEMP%\lumen-qa\r5lo-sweep`: 289 scenarios, all ok, 5.8 min.

| | high | medium | low | total |
|---|---|---|---|---|
| Base r5-v8, today's tool (`r5lo-sweep-base`) | 4 | 16 | 14 | 34 |
| verify.md "after" (r5-v8) | 4 | 17 | 14 | 35 |
| **This branch (`r5lo-sweep`)** | **0** | **0** | **16** | **16** |

The 20 medium and high findings, by group:
- **`text-trunc` medium, title-bar Label** (2). Fixed by item 3. They are now low "ellipsized, tooltip …", like the other cut values that carry their text.
- **`overlap-cross` medium, onboarding-2** (1, baseline #17). The accent name label was 330 DIP wide and ran under "Пропустить". It is 240 now and ends at x 681.
- **`hitmap` high, pip.dark and pip.light** (2, VF-08). This is by design: PiP drags by its video. New allowlist rule `hitmap` + `pip.*` + `VideoArea`.
- **`text-clip` high, menu-context.min and .1024x640** (2). This is noise: in a scrolling menu, the row at the scroll edge is clipped under the fade cue (crop `r5lo-menu-min-full.png`). New allowlist rule `text-clip` + `MenuPopup`.
- **`text-trunc` medium, MusicView** (2). This is noise: the hover widget over the two lines carries the full text (verified above). New allowlist rule.
- **`overlap-cross` medium, player-audio.fullscreen.shown** (11). This is noise: it is MusicView's full-screen rect against the floating bar. Its text boxes are at y 1100-1158 and the bar is at y 1368. New allowlist rule, limited to fullscreen contexts.

The 16 low findings are all `text-trunc`:
- 9 carry a tooltip.
- 7 are grids or the queue, whose per-item tooltips the dump can't see. They are covered by the existing low rule.

## Coordinator follow-up (verify.md)

| ID | Fix | Check |
|---|---|---|
| VF-01 | The collapsed volume's popup closes when the speaker's rect changes or the wedge comes back (`VolumeControl::OnLayout`) | Popup `[1210,592]` open, then F: no popup, the speaker at `[1576,1388]` (`r5lo-t4\new-vol-*.txt`) |
| VF-02 | In fullscreen with the side panel, the bar is centred in the picture to the right of the panel and capped to it; so is the top info panel | 1.5 scale, 1707x960: panel `[0,0 416x960]`, bar `[464,888 1194x56]` (base `[256,888]`) |
| VF-03 | The selected queue row has a 3-DIP accent edge, dimmer while the list isn't focused | `r5lo-t4\new-queue-crop.png` |
| VF-04 | About opens with "Закрыть" focused | `DButton … focus text="Закрыть"` |
| VF-05 | Settings' `Confirm` is built on `dlg::Frame`/`DButton`: Cancel on the left, focused with its ring, the action on the right | `DButton [324,484] focus "Отмена"`, `[624,484] "Сбросить"` (`new-confirm.png`) |
| VF-06 | An untagged file's top info title drops the extension | `text="Фильм — тест «кавычки» 日本語"` |
| VF-07 | Enter or Space on the focused collapsed speaker opens or closes the popup | After Tab: Enter opens `VolumePopup`, then Up shows 5 % |
| VF-08 | The allowlist rule above | sweep |
| #17 | Onboarding label | sweep |

Not changed: the Settings confirm titles keep their "?" ("Сбросить настройки?"). They are translated strings used by four callers; the kit and the layout now match the library's.

## Performance

These numbers are LUMEN_TRACE frame stats, measured in a private `TEMP`, one instance at a time, after the sweep. Columns are base r5-v8 / this branch. "Paint" is the average per frame; its range covers 2-3 runs.

| State | Base | Branch |
|---|---|---|
| Paused, idle 8 s | 0 frames, 0.00 % CPU | 0 frames, 0.00-0.01 % |
| Pointer resting on the wedge, 6 s | 0 frames | 0 frames |
| Library songs (1284), idle after a scroll | 0 frames | 0 frames |
| Settings open, idle 6 s | 0 frames | 0 frames |
| Playing windowed, paint | 0.263-0.278 ms | 0.260-0.262 ms |
| Wheel on the volume wedge, paint | 0.230-0.235 ms | 0.235-0.277 ms |
| Library songs, 10 wheel notches, paint | 0.995-1.130 ms | 0.978-1.100 ms |
| Settings, 32 wheel notches, median per-second paint | 0.61 / 0.66 / 0.63 ms | 0.57 / 0.72 / 0.67 ms |

- **Idle:** still 0 frames everywhere.
- **Steady paint** (playback, the library) is unchanged within noise.
- **Settings while scrolling** paints about 0.04 ms more per frame. That is the requested cue: two scroll views, each with up to 14 band fills and a thumb. Cutting the bands from 10 to 7 (afc76e2) took out 6 fills per edge.

## Checks

- Build: no warnings at /W4.
- `tests\build-tests.bat`: 26 tests, 5527 checks, 0 failures.
- `check-icons.py`: 0 missing of 208.
- `check-settings.py`: 0 of 220 settings without a reader.
- `i18n_check.py`: 0 conflicts, 852 keys.
