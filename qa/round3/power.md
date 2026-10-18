# Lumen r3 — power-user review ("power")

Persona: an MPC-HC / mpv / PotPlayer user who works keyboard-first. Build: `build-r3\Lumen.exe` (commit 9e387bb).

Setup:
- **Data dirs:** `%TEMP%\lumen-qa\r3-power`, `r3-power2` … `r3-power8`, all fresh.
- **Instances:** every instance ran `--new-instance --automation` through `tools\dev\launch.ps1`. Startup volume was 0, and from instance 2 on I added `--ao=null` through `general.extraOptions`, so no audio could reach a device even while I tested volume gestures.
- **mpv logs:** written with `--log-file` through `general.extraOptions`, to `%TEMP%\lumen-qa\round3\mpv-a.log` … `mpv-d.log`.
- **Input:** only PostMessage to my own windows: `tools\dev\input.ps1`, plus a scratch helper that posts a separate button-down and button-up, to test the hold gesture.
- **Screenshots:** `%TEMP%\lumen-qa\round3\shots-power\`, at half scale. Files named `m*.png` are montages, `*c.png` are crops.
- **Clean-up:** all my processes are closed.

**Short version:**
- **Round 2 blockers fixed:** finished files restart from 0, opening E02 plays E02, Previous reaches the previous episode, and 4K memory roughly halved.
- **Wave-2 player features behave correctly on the RTX 4080:**
  - the stats overlay reports zero-copy correctly;
  - RTX VSR engages without breaking zero-copy;
  - the quality presets and smooth motion apply live, with no errors in the log;
  - hold-for-2×, the wheel gestures, the YouTube key preset, previews, the thin line and the bottom reveal all work.
- **Main remaining problem:** OSD toasts get swallowed when one replaces another, the same root problem as r2 N05. The rest is polish.

## Re-check of round 2

Covered: the findings that STATUS.md §3 marks as fixed, plus the r2 blockers.

| ID | r2 sev | Status | Evidence / notes |
|---|---|---|---|
| P01 | Blocker | **Fixed** | S01E03 played to EOF and stopped at 0:12/0:12, paused. `watch_later` stayed empty. Relaunching the same file from the command line played **S01E03 from 0:01** (`m9-resume.png`, `93-E03-end.png`, `94-E03-reopen.png`). |
| P02 | Blocker | **Fixed** (a, c) | `Lumen.exe "…Series S01E02.mp4"` plays **S01E02** (`90-open-E02.png`). P at about 3 s goes to **S01E01**; P on the first item restarts it. The queue shows 3 items in order (`92-queue.png`). Not re-checked: (b) P at EOF. |
| P16 | Minor | **Resolved by design** | Rolling into the folder's next file is now an explicit setting: "Играть остальные файлы папки" ("Play the folder's other files"), `playback.folderAsPlaylist`, default **on**, like MPC-HC's auto-load. Chaptered → Long Seek still happens with defaults (mpv-c.log `loadfile … Long Seek Test`). With the setting off, the 4K file stayed alone. |
| N01 | Blocker | **Fixed** | P01, P02 and P16 above. |
| P22 | Minor | **Fixed (much better)** | The 4K Mandelbrot file (4:4:4, software decode) settles at **440–466 MB working set / 639–646 MB private** from 0.5 s to 5 s (r2: 813 MB / 1,244–1,288 MB). Scrubbing previews on 4K adds about 120 MB (759 MB private). STATUS's "432 MB" matches the working set, not private memory. |
| P21 | Minor | **Fixed** | The subtitles panel shows "Без субтитров" ("No subtitles") as its own top item. Embedded tracks are eng and rus. External tracks show "Не добавлены" ("None added") with + and edit icons (`m10-tracks.png`). |
| P25 | Polish | **Partly fixed** | The queue shows "Series S01E01" without the extension. Only the current item has a duration (0:12); the other episodes show none (`92-queue.png`). |
| N04 | Minor | **Fixed** | On Long Seek: X (`video-aspect-override=16:9`), then Z (`video-zoom=0.1`), then N to the vertical clip. The clip is shown portrait and undistorted (`74-vertical.png`). The next Shift+Z set the zoom to −0.1, which shows the zoom had been reset to 0. |
| N06 | Minor | **Fixed** | End, then Ctrl+→ ×2 on Chaptered: the next file shows only its own toast, "▶ Long Seek Test 10min", not a stale "1:30 / 1:30" (`m10-tracks.png`, row 3). |
| N08 | Polish | **Fixed** | Remaining time at 0:00 of 10:00 reads "-10:00" (`37c.png`). |
| N09 | Polish | **Fixed, with a nit** | V shows "Субтитры: rus" ("Subtitles: rus") and A shows "Аудио: Русский 660 Hz (rus)" ("Audio: …"); no codec names. The subtitle toast still shows the raw ISO code "rus" rather than "Русский" ("Russian"), see R3-P09 (`m10-tracks.png`). |
| P14 | Minor | Not exercised | Snapshots and recording write outside the data dir. |
| N05 | Minor | **Still broken (root cause found)** | Listed as open in STATUS. The same toast-replacement bug drops the speed and seek toasts; see R3-P01. |

## New features

| Feature | Result | Evidence |
|---|---|---|
| **Stats overlay** (Ctrl+Shift+I) | **Works, numbers plausible.** See below the table. | `03-stats-crop.png`, `65-stats-vsr-smooth.png`, `74-vertical.png` |
| **Wheel over volume** | +2 notches → `volume=10` in a single property set. A small "10%" bubble appears over the slider. Wheel down brought it back to 0. | `m3-speedvol.png` |
| **Right-click on volume** | Mute, and right-click again to unmute (`mute=true` / `mute=false`). | mpv-b.log |
| **Wheel over speed** (Speed added to the bar through `controls.layout`) | 1 → 1.25 → 1.5 with the toast "Скорость 1.5×" ("Speed 1.5×"). Right-click goes back to 1×. Steps are 0.25; mpv and MPC-HC use 0.1. | `30-speed-wheel.png`, mpv-b.log |
| **Wheel over the seekbar** | ±5 s per notch. Three quick notches became one `seek -15`, which is good. No toast, see R3-P03. | `m1-seekwheel.png` |
| **Hold for 2×** | After 400 ms, `speed=2` and a "2×" badge at the top. Release sets `speed=1`, and the release did **not** toggle pause. | `13-hold.png`, `14-release` in `m1-seekwheel.png` |
| **Thin progress line** (`interface.thinProgress=always`) | In fullscreen, after auto-hide, a thin white line along the bottom edge. In a window the bar is docked and never hides, so the "always" option looks the same as "fullscreen". | `40c.png` |
| **Bar on bottom approach** | In fullscreen, moving the pointer to the bottom edge brings the bar back. | `41c.png` |
| **Seek previews, 1080p** (Chaptered, d3d11va) | The frame is already there in the first capture (≤ about 200 ms) at every new position, and the label carries the chapter name ("Middle part · 0:45"). **Accuracy, checked on Long Seek, which has a burned-in seconds counter:** hover 6:31 → frame shows "391"; hover 5:29 → "329". | `m7-preview4k.png` (Chaptered rows), `36-bottom-approach.png`, `39c.png` |
| **Seek previews, 4K** (software 4:4:4) | Frames appear within the first capture at 0:01 and 0:05 and differ correctly (different zoom depth). About +120 MB private memory while scrubbing. | `m8-preview4k.png` |
| **Quality presets** | "Максимум" ("High quality") applies live: `scale=ewa_lanczossharp`, `scale-antiring=0.6`, `hdr-peak-percentile=99.995`, `hdr-contrast-recovery=0.3`. No errors in the log. | `62-quality-high.png`, mpv-c.log |
| **AI upscaling (RTX VSR)** | `vf add @lsr:d3d11vpp=scale=1.33:scaling-mode=nvidia`, and the log says "NVIDIA RTX Super Resolution enabled". **Zero-copy kept:** the chain stays `d3d11[nv12]` and the overlay still reads "без копирования". Software-decoded files (Long Seek 4:4:4) skip it silently. See R3-P05 and R3-P06. | mpv-c.log, `65-stats-vsr-smooth.png` |
| **RTX Video HDR** | Toggling it on this SDR monitor does nothing: no filter argument and no error, which is correct. The UI doesn't say so, see R3-P07. | mpv-c.log, `72-next-vertical.png` |
| **Smooth motion** | `video-sync=display-resample`, `interpolation=yes`, `tscale=oversample`. libplacebo re-enabled interpolation (30 fps on a 144 Hz display). There were 3 dropped output frames during the switch, then none. | mpv-c.log, `65-stats-vsr-smooth.png` |
| **Skip intro** | The "Пропустить заставку" ("Skip intro") button appears during the chapter named "Opening". A click seeks exactly to 20.0 s, the start of "Middle part". | `15-after-skip.png`, mpv-a.log `seek 20 absolute` |
| **"Next episode" card** | **Not testable with the test media.** The card requires `duration > 60` s (PlayerOverlays.cpp:365), and the test series episodes are 12 s long. The queue itself advances E01→E02→E03 and stops after E03. | `92-queue.png`, `93-E03-end.png` |
| **YouTube key preset** | Confirm dialog, then "Включить" ("Turn on"). The key bindings are rewritten and every key verified in the mpv log: `5` → `seek 300 absolute`, `L` → `+10`, `J` → `-10`, `0` → `0`, `K` → pause, `Shift+.` → `speed=1.25`. A-B loop keeps Shift+L. The preset drops Ctrl+= / Ctrl+− for speed, and plain N/P. | `45-confirm.png`, `m4-ytkeys.png`, mpv-b.log |
| **Menu type-to-filter** | Typing filters the current level, Backspace restores, Enter runs the first match ("стат" → stats overlay on). It does not search submenus, see R3-P02. | `m2-menufilter.png`, `17-ctx-filter.png` |

**Stats overlay details:**
- **Decoder:** "h264 · d3d11va (без копирования)" ("no copy"), which matches mpv's "Using hardware decoding (d3d11va)" and `VO: [gpu-next] 1920x1080 d3d11[nv12]`.
- **Frames:** "30.000 в файле · 30.00 фактически · 143.97 у экрана" ("in file · actual · on screen"); 0 dropped.
- **Bitrate:** video bitrate "—" for the first ~3 s, then 9.6 Mbit/s.
- **Other lines:** cache "1.0 с вперёд" ("1.0 s ahead"); A/V sync +0.0 ms.
- **Vertical clip:** shows bt.601 and 720×1280 correctly.
- **Gap:** the overlay never shows active processing, see R3-P06.

## New findings

| ID | Severity | Area | What happened | Steps to reproduce | Expected | Evidence |
|---|---|---|---|---|---|---|
| R3-P01 | Major | OSD toasts | A toast that replaces a recent toast is often dropped. After `L` (toast "0:49 / 10:00"), `Shift+.` changed the speed to 1.5× (mpv log `speed=1.500000`), but no "Скорость 1.5×" toast was on screen at 200 ms or at 800 ms. The same key with no toast on screen shows "Скорость 1.75×" and "Скорость 2×" fine. In the YouTube-keys run, `L` after `5` and `Shift+.` after `L` also showed nothing. This is very likely the cause of r2 N05 (mute without a toast). For a keyboard user, half of the quick actions give no feedback. | Long Seek Test, YouTube keys or defaults: `L`, about 0.7 s, `L`, about 0.7 s, `Shift+.` (or `]`). | Every action shows its own toast, replacing the previous one. | `m5-toast-timing.png` (rows 5–6 empty), `53ac.png` / `53bc.png` (shown when nothing was on screen), `m4-ytkeys.png` rows 2–4. Likely cause: ui/Controls.cpp:1438-1442. The close timer identifies its toast by raw pointer. A replacement bubble allocated at the freed address is closed by the previous toast's timer (ABA). |
| R3-P02 | Minor | Menu filter | Type-to-filter only searches the current menu level. In the video context menu, "скор" (speed) gives an empty list although Воспроизведение › Скорость ("Playback › Speed") exists. The empty state is a lone "—", and the popup keeps its full 17-item height as a tall empty box. "ст" also matches inside words ("На**ст**ройки", "Плейли**ст**") and ranks them equally with prefix matches. | Right-click the video, type `скор`. Esc, right-click, type `ст`. | Search submenus too (a flat command search), show "Ничего не найдено" ("Nothing found"), shrink the popup to the results, rank prefix matches first. | `17-ctx-filter.png`, `m2-menufilter.png` |
| R3-P03 | Minor | Seekbar wheel | The wheel over the seek bar seeks ±5 s per notch but shows no position toast, unlike ←/→. With the cursor on the bar you only see the preview of the hovered time, not where playback landed. | Hover the seek bar and turn the wheel. | The same "0:06 / 1:30" toast as a keyboard seek, or the hover label showing the new position. | `m1-seekwheel.png` |
| R3-P04 | Polish | Speed button | The Speed button shows only an icon. At 1.5×, once the toast is gone, nothing on screen says the speed is not 1×. This is worse combined with R3-P01. | Add Speed to the bar and wheel up over it twice. | Show "1.5×" on the button when the speed is not 1, like PotPlayer and YouTube. | `30-speed-wheel.png`, `m3-speedvol.png` |
| R3-P05 | Minor | AI upscaling / GPU cost | VSR always scales toward the **monitor** size, even in a window. A 1080p video in a 1940×1080 video area runs VSR to **2554×1436**, which the renderer then scales back down. The 720×1280 vertical clip in a ~760×1180 window runs at `scale=2`, giving 1440×2560, then about 0.46× down. That is full-cost RTX VSR on every frame for zero visible gain in a window, plus a downscale that softens what VSR sharpened. | Turn AI upscaling on, play Chaptered (1080p) windowed, read the mpv log. | Scale toward the current video-area size, or engage VSR only when the target is ≥ 1.2× the source. Re-evaluate on fullscreen and resize. | mpv-c.log lines `vf add @lsr:d3d11vpp=scale=1.33…` and `scale=2…`. Cause: app/player/LiveState.cpp:317-323 uses `rcMonitor`, which is by design per the comment. |
| R3-P06 | Minor | Stats overlay | The overlay never shows the processing that is active. With RTX VSR and smooth motion on, it still reads "Вывод: gpu-next · 1920×1080 · d3d11 (nv12)" ("Output: …"), although mpv outputs 2554×1436 through d3d11vpp. There is no line for filters, interpolation, display-resample, the scaler or the quality preset. So the overlay can't answer "is VSR or interpolation actually on?", which is the reason a power user opens it. Frame stats lack vsync ratio and mistimed/delayed frames. | AI upscaling + smooth motion on, then Ctrl+Shift+I. | Lines like "Фильтры: d3d11vpp (RTX VSR ×1.33)" ("Filters: …"), "Синхр.: display-resample, интерполяция oversample" ("Sync: …, interpolation …"), and the output size after filters, as mpv's stats.lua page 1 and MPC-HC's Ctrl+J show. | `65-stats-vsr-smooth.png`, mpv-c.log |
| R3-P07 | Polish | RTX HDR toggle | On an SDR monitor, "HDR из SDR (NVIDIA RTX Video HDR)" ("HDR from SDR …") can be turned on and then silently does nothing, which is correct in the code. The row gives no "needs Windows HDR on this screen" state. Likewise, AI upscaling silently does nothing for software-decoded files. | Settings › Видео, turn on RTX Video HDR on an SDR screen. | Disable the row with a reason, or show "не активно: экран без HDR" ("inactive: screen has no HDR") / "не активно: программное декодирование" ("inactive: software decoding"). | `72-next-vertical.png`, mpv-c.log (no `nvidia-true-hdr` argument) |
| R3-P08 | Polish | Thin progress line | In a window the bar is docked and never auto-hides (4 s idle, still visible), so `interface.thinProgress = always` looks the same as the default "fullscreen" there. The option's "Всегда" ("Always") promises something the window can't show. | thinProgress=always, play windowed, wait 4 s. | Either let the bar auto-hide in a window when "Always" is chosen, or describe the option as "fullscreen and floating bar". | `35c.png` (windowed) vs `40c.png` (fullscreen) |
| R3-P09 | Polish | Track OSD | The subtitle toast shows the ISO code "Субтитры: rus" ("Subtitles: rus"), while the audio toast shows a title. A track without a title should show the language name. | V on Chaptered. | "Субтитры: Русский" ("Subtitles: Russian"). | `95-V.png`, `m10-tracks.png` row 1 |
| R3-P10 | Polish | YouTube preset | Applying "Клавиши YouTube" ("YouTube keys") silently removes Ctrl+= / Ctrl+− for speed (MPC-HC habit) and plain N/P for next/previous; the preset table replaces whole binding lists. The confirm text says "Остальные изменения сочетаний сбросятся" ("Your other shortcut changes are reset") but not that these defaults go too. | Settings › Управление › Изменить сочетания › Клавиши YouTube › Включить, then look at settings.json `controls.keyBindings`. | Keep non-conflicting defaults (Ctrl+=/−), or list what is removed. | `45-confirm.png`. Cause: app/settings/ShortcutStore.cpp:266-290 |
| R3-P11 | Polish | Esc with overlays | Esc in the windowed player goes straight back to the library: the window shrinks and the mini-player appears, even while the stats overlay is open. mpv/MPC-HC users expect Esc to close the overlay first, or at least never to leave the video. | Ctrl+Shift+I, then Esc. | Close the stats overlay first. | `21-latin.png`. app/MainWindow.cpp:133-136 |

### Measurements

- **Startup:** with a 1080p file the stats overlay shows the first frame data by about 3 s; I did not time startup (that is the performance reviewer's area).
- **1080p d3d11va memory:** 217 MB working set / 240 MB private. With RTX VSR and smooth motion on: 234 MB working set / 336 MB private.
- **Long Seek (software 4:4:4, fullscreen 1440p):** 257 MB working set / 274 MB private.
- **4K software decode:** about 460 MB working set / 640 MB private, steady; 759 MB private while scrubbing previews.
- **mpv warnings:** none across all four logs, except a harmless one at startup: `af-remove: item label @lstereo not found`.

## Compared with the leaders

- **Feature checklist:** wave 2 now covers most of an mpv/MPC-HC user's list:
  - stats overlay;
  - hold for 2×;
  - wheel on volume, speed and seekbar;
  - right-click resets;
  - fast cached thumbnails in the style of mpv's thumbfast;
  - RTX VSR with zero-copy;
  - quality presets mapped to mpv's fast and high-quality profiles;
  - display-resample motion;
  - chapter-based skip intro;
  - a YouTube preset.
- **Previews:** instant on 1080p and 4K, and accurate to the second. That is at or above PotPlayer, and well above VLC and MPC-HC (whose previews are slow or off by a keyframe).
- **Stats:** shallower than mpv's stats.lua (no pages, no frame-timing graph, no vsync ratio or mistimed frames, no filter chain) and MPC-HC's Ctrl+J. It is still clean and correct about zero-copy, which neither shows as clearly.
- **Feedback:** behind PotPlayer and mpv, whose OSD messages never get lost (R3-P01) and which show the speed persistently (R3-P04).
- **Menus:** PotPlayer's menus are huge but its command search is flat. Lumen's type-to-filter is a good idea, but limited to one level (R3-P02).
- **AI upscaling:** madVR, MPC-VR and VLC 3.0.21 apply RTX VSR only when actually upscaling to the output size. Lumen upscales to the monitor size even in a window (R3-P05).

## Top 5

1. **Major (R3-P01):** OSD toasts are dropped when they replace a recent toast, so speed, seek and mute changes often give no feedback. This is the root cause of r2 N05. Likely an ABA bug in `ShowToast`'s close timer, ui/Controls.cpp:1438.
2. **Minor (R3-P05):** RTX VSR always scales to the monitor size, even in a small window. That is full GPU cost, then a downscale, for no gain.
3. **Minor (R3-P06):** the stats overlay can't show whether VSR, interpolation or the filters are active, and its output size ignores the filter chain.
4. **Minor (R3-P02):** menu type-to-filter doesn't search submenus, and an empty result is a tall empty box with "—".
5. **Minor/Polish (R3-P03 + R3-P04 + R3-P09):** the wheel seek has no toast, the speed button doesn't show the speed, and the subtitle toast shows the raw "rus".

Untestable with the provided media: the "Next episode" card (needs episodes longer than 60 s; the test episodes are 12 s).
