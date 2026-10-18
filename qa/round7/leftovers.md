# Round 7: leftovers (r7-leftovers)

The STATUS.md section 7 items "Из раунда 3" and "Не проверено ревьюерами", except exclusive mode (WASAPI exclusive would mute the owner's other apps). Branch `worktree-agent-a8012e1bb77bd665b`, five commits on top of `4f1b361`.

The base for every before/after comparison is `git archive HEAD` (4f1b361), built into `%TEMP%\lumen-qa\r7lo-basebuild`. The fixed build is the worktree's `build\Lumen.exe`. Every test instance ran with `--automation` and a data dir and library inside `%TEMP%\lumen-qa`. Evidence is in `%TEMP%\lumen-qa\r7lo-*` and the scripts are in the session scratchpad (`r7lo-*.ps1`).

New test tool: `command.ps1 -Command "prop <mpv property>"` reads one mpv property, for example `prop hwdec-current`, `prop vf` or `prop frame-drop-count`.

## Summary

| # | Item | Result |
|---|---|---|
| 1 | CPU filter switch during playback | **Broken, fixed.** 7 of 20 live enables on the base were silently disabled by mpv. Now 0 in all runs (more than 150 enables). |
| 2 | Mini player with a real cover | Works: embedded mp3/flac/m4a covers and folder `cover.jpg`. Added a tooltip for a cut title. |
| 3 | R3-16, strip after stretching | Not reproduced: 65 resize sequences and 12 transient captures, all pixel-identical to a forced repaint. |
| 4 | Closing time | The window already hides first (about 2 ms after WM_CLOSE). The rest is invisible. Placement bug found and fixed. Cold-start close: 12 of 12 exit 0. |
| 5 | Recording and snapshots into real folders | **Broken, fixed.** A nested new folder produced no file, but the toast said "saved". A recording started in the same second overwrote the previous one. Added "Открыть папку" / "Настройки" actions. |
| 6 | Handing a file to the running window | **Fragile, fixed.** Files could be handed to any Lumen window, including cloaked test instances. Now only to the same data-folder group. Verified with two instances. |
| 7 | Tray | Code review: cut tooltip with a broken surrogate pair, the keyboard menu opening at the mouse, and "Show Lumen" bringing back a closing window. All fixed. The tray itself can't be tested under `--automation`. |

## 1. CPU filters switched live

**What was broken.** `App::OnConfigChanged` set `vf` first, and `LiveState` set `hwdec` to the copy-back variant about 35 ms later. When a zero-copy d3d11 frame reached the new lavfi filter in that window, mpv logged `failed to configure the filter graph` and `Disabling filter lumen because it has failed`. Playback then went on unfiltered while Settings said the filter was on. It is a race:
- 1 of 2 runs failed in the first probe (`r7lo-fpp-mpv.log`, bwdif at 8.84 s);
- the base failed 7 times in 20 enables (`r7lo-fb3-mpv.log`, 10 cycles of bwdif and deblock at 400 ms).

The same race existed for flip and negate/grayscale (`vf add` before `hwdec`) and for `performance.hwdec` changes made while a CPU filter was on.

**Fix** (`src/app/player/LiveState.cpp`, `src/app/App.cpp`):
- LiveState owns `vf` and `hwdec`; App's generic mpv mapping skips them.
- `ApplyVideoFilters` builds the whole chain: the config's `@lumen` lavfi, then `@lflip`, then `@lfx`, then the upscaler.
- When the chain has CPU filters, it takes the d3d11vpp upscaler out first, then switches hwdec to copy-back, then sets `vf` once `hwdec-current` reports frames in RAM (`-copy`, `no`, or no video).
- `hwdec-current` is briefly empty while mpv re-creates the decoder. That counts as "not yet": the first version of the fix applied the filter in that gap.
- A 1 s fallback timer applies the filters anyway if the report never comes. It exists only while a switch is pending.
- When the last CPU filter goes, the chain is removed first, then hwdec returns to zero-copy, then the upscaler comes back.

**Verification** (`r7lo-filters.ps1`, `r7lo-fmem.ps1`, `r7lo-fblack.ps1`):
- Every setting that forces copy-back was switched live in both directions, with `prop hwdec-current` and `prop vf` checked after each step:
  - `performance.postprocessing` low/medium/high;
  - `video.deinterlaceMethod` bwdif/yadif (with `video.deinterlace` on);
  - `video.sharpen` with `performance.vo=gpu` (the CPU `cas` path);
  - flip horizontal/vertical from the player's Video tab (clicked, 6 and 4 cycles);
  - combinations (yadif + deblock, flip + deblock, then removing one: stays `d3d11va-copy` with the remaining filter);
  - startup with filters in the config, then removing them;
  - `performance.hwdec` d3d11/auto while filters are on (stays copy-back).
  - Each removal of the last filter returned `hwdec-current` to `d3d11va` (zero-copy).
- With Video Super Resolution on (Series 720p in a 1900×1150 window): `vf=@lsr:d3d11vpp=scale=1.5:scaling-mode=nvidia`. Flip on gave `@lflip:hflip` + `d3d11va-copy`, and flip off restored `@lsr` + `d3d11va`. 4 cycles.
- Log check: `Disabling filter` count 0 in every run of the fixed build (`r7lo-fn2…n9-mpv.log`, `r7lo-flip-mpv.log`). The order in the log is always `Set property: hwdec` → `Using hardware decoding (d3d11va-copy)` → `Set property: vf`.
- Frame drops: `frame-drop-count` and `decoder-frame-drop-count` stayed 0 throughout, including 100 changes at 300 ms intervals.
- Black frames: four captures 0–200 ms after each switch kept the video's mean brightness (127 ±1, `r7lo-fk1-shots`).
- Memory: private bytes after groups of 10 bwdif on/off cycles: 254 MB at the start, then 400, 381, 372, 371, 377 MB. A plateau (mpv's copy-back pools), no growth.
- Paused CPU: 0 ms per 5 s with the filter on, measured 2 s after switching (`r7lo-idle.ps1`). Without the filter, paused and in the library: 0 ms.

## 2. Mini player with a real cover

Generated `%TEMP%\lumen-qa\r7lo-media`:
- `Embedded\`: mp3 (ID3 APIC), flac and m4a with an attached 600×600 cover;
- `Folder Art\`: mp3 and flac next to `cover.jpg`.

All five show their cover in the library mini player, both the full card and the compact card, in dark and light (`r7lo-mini1\strip.png`, `r7lo-mini2\strip.png`). A song without a cover keeps the note placeholder. The cover path is `ImageCache` → `Thumbnails::ExtractThumbnail`: the shell thumbnail, then `cover.jpg`/`folder.jpg`/… in the song's folder.

Found: the compact card cut "Embedded Cover S…" with no way to read the full title. The card now sets the full title as its tooltip when it doesn't fit (`MiniPlayer::Paint`, three lines in `PlayerView.cpp`).

New sweep scenarios: `mini-player-cover` (the worst-case set's 6000×6000 cover in both themes; its 3000×300 cover at min and 1024×640).

## 3. R3-16: a strip after stretching the window

Not reproduced. `r7lo-strip.ps1` compares each capture with one taken after a forced full repaint (a 1 px resize and back, or a theme round trip when maximized), pixel by pixel. The sequences:
- 800×500 → 2560×1392 (the original report) and back;
- steps of 50, 137 and 333 px up to 2560×1392 and down to 800×500, without waits;
- 8 fast 800×500 ↔ 2560×1392 toggles;
- 1280×780 → maximize → restore;
- 5 fast maximize/restore;
- 800×500 → maximize.

They ran on the home screen in dark (`r7lo-s1`) and light (`r7lo-s2`), at `--scale 1.5` (`r7lo-s5`), and on a paused 1080p player in dark (`r7lo-s3`) and light (`r7lo-s4`): 65 checks, all "same".

`r7lo-strip2.ps1` also captured 0, 30, 80 and 200 ms after the 800×500 → 2560×1392 resize, with no wait, in 8 dark and 4 light trials: all identical to the settled frame.

Code: `WM_SIZE` resizes the surface and renders synchronously (`Host.cpp`), and `WM_ERASEBKGND` is swallowed. Round 4 and 5 changed that path, so the round 3 strip is most likely gone. One limit: PrintWindow can't see what DWM shows for a frame that is never presented. A real drag with the real mouse is not reproducible here.

## 4. Closing time

Close-path trace points added under `LUMEN_TRACE` (`close: requested`, `window hidden`, `player shutdown`, `watch-later written`, `mpv destroyed`, `destroying the window`, `message loop ended`, `stores saved`, `seek preview stopped`, `window objects freed`, `wWinMain returns`). `r7lo-close.ps1` runs each instance with a private `TMP`, so the trace file is ours alone.

Profile, 1080p playing, 4 runs (`r7lo-c2`):

| step | ms after WM_CLOSE |
|---|---|
| window hidden | 1.6–2.5 |
| watch-later written | 12–19 |
| mpv destroyed (`mpv_terminate_destroy`) | 122–172 |
| message loop ended, stores saved, objects freed | +2–7 ms more |
| wWinMain returns | 133–178 |
| process gone (CRT exit, DLL unload) | 231–281 |

The window already hides first: `onCloseRequest` calls `SW_HIDE` before the shutdown thread starts, so the visual close is instant. The remaining 0.2–0.3 s is invisible:
- about two thirds is mpv's own teardown (VO, audio output, demuxer);
- about one third is process exit after `wWinMain`.

Base and fixed builds measure the same (5 runs each on a loaded PC: base 232–298 ms, fixed 236–308 ms). I left that time alone: killing the process early would skip watch-later or store writes, and nothing in it is visible.

Found and fixed: the close hides the window, and the final `FlushStores` skipped hidden windows. So a resize or move in the last 3 s before closing was lost. `r7lo-place.ps1` moves the window to 123,77 1111×666 and quits within a second:
- base saved `640,306,1920,1086`;
- fixed saves `123,77,1234,743`.

`App::BeginQuit` now saves the placement first. Also, a window minimized from maximized is saved as maximized (`WPF_RESTORETOMAXIMIZED`); this is checked in code only.

Audit #3 still holds: 3 × WM_CLOSE during the cold start on 4K Mandelbrot, exit code 0 in 12 of 12 runs, twice (`r7lo-c3`, and `r7lo-c4` on the final build).

## 5. Snapshots and recordings into real folders

`r7lo-rec.ps1` sets `video.screenshotDir` and `recording.folder` with `command.ps1 set` (they are already set at launch, so nothing ever touches Pictures or Videos).

**Base** (`r7lo-out-r1`): with a new nested folder `shots\a\b`, **no file at all**:
- mpv: `Error creating '…\shots\a\b/….png' for writing: Invalid argument!`;
- recorder: `Failed opening output file. Disabling recording.`

Meanwhile the toasts said "Снимок сохранён в …", "Запись началась" and "Запись сохранена в …". The same happened for a folder whose parent is a file and for a path over 260 characters.

**Fixed** (`src/app/Actions.cpp`):
- Both actions create the folder with its parents (`SHCreateDirectoryExW`) and probe it with a delete-on-close temp file.
- When that fails, the toast says "Не удаётся сохранить в <folder>" with a "Настройки" button (Video or Recording page), and nothing starts.
- A recording started within the same second as the previous one gets " (2)", " (3)" instead of overwriting it.

Results (`r7lo-out-r2`):
- `Chaptered Demo (1080p)-00_00_01.933.png` (1.19 MB);
- `-00_00_09.533.jpg` (202 KB);
- `-00_00_17.267.webp` (45 KB);
- three `Lumen 2026-10-18 HH-MM-SS.mkv` of about 3.5 MB each (ffprobe: matroska with h264, aac and subrip, 4.17 s);
- `rec2\Lumen … .mkv`, `… (2).mkv`, `… (3).mkv`;
- error toasts for the file-parent and over-260 folders, and no files there.

**"Открыть папку".** The toasts had no such action before. "Saved" toasts now stay 4 s and have the button:
- a snapshot opens its folder (`Host::ShellOpen`);
- a recording selects its file (`lib::ShowInExplorer`).

Both are no-ops under `--automation`, so Explorer never opened. A toast with an action takes clicks and shows its full text as a tooltip; plain toasts still let the mouse through (`ui/Controls.cpp`, `ShowToast(…, actionLabel, action)`). Checked by clicking: the toast closed, playback didn't toggle, and nothing opened (`toast-crop.png`). New sweep scenarios: `toast-snapshot` (both themes) and `toast-snapshot-error`. `sweep.ps1` now expands `%TEMP%` in scenario settings.

Not checked: a full disk, and a folder that exists but denies writing (I didn't change ACLs). The write probe covers both cases in code.

## 6. Handing a file to the running window

**Found.** `FindMainWindow` in `main.cpp` matched any unowned `LumenHost` titled "Lumen" or "… — Lumen", so a new launch could hand its files to:
- a `--new-instance` window;
- any agent's cloaked `--automation` window. Seven of them from other worktrees were running while I tested, so a forwarded file would play unseen.

With `--data-dir` the mutex was the real profile's too, so a test instance without `--new-instance` and the owner's Lumen could take each other's files. I didn't reproduce this on the base, because doing so would have sent files into other agents' windows.

**Fix:**
- A main window that takes files carries the `Lumen.InstanceGroup` property: a hash of its data folder.
- `--new-instance` windows have none.
- A `--data-dir` group gets its own mutex name; the real profile keeps `Lumen.SingleInstance.v2`.

**Verified** with `r7lo-handoff.ps1`: two instances on one data dir, both `--automation` without `--new-instance`, started only after checking that no Lumen runs from `lumen-player\build\`. Steps:
- **A:** plays S01E01.
- **B:** a second launch with a new file. B exited with code 0 after 47 ms, and A played "Vertical phone clip" with its folder queue (replace).
- **C:** with `library.enqueue` on, the queue grew 5 → 6 and A kept playing.
- **E:** a `--new-instance` window on the same dir. The next launch went to A, and E stayed empty.
- **G:** launched while A was closing. G opened its own window and played the file (A answers `WM_COPYDATA` with FALSE while closing), and A exited with code 0.

## 7. Tray

Under `--automation` there is no tray (`SyncTray` returns early), so this was a code review. Fixed in `src/app/SystemIntegration.cpp`:
- **Tooltip with a long title:** `wcsncpy_s(_TRUNCATE)` cut "title — Lumen" at 127 units. That dropped " — Lumen" and could split an emoji's surrogate pair. New `FitShellText` (`src/core/ShellText.h`, unit test `shell_text`) cuts at a character boundary, adds "…" and keeps the suffix. The notification's title and text use it too.
- **Menu:** the menu opened at `GetCursorPos`, so Shift+F10 or the menu key on the icon opened it at the mouse. It now opens at the anchor that `NOTIFYICON_VERSION_4` passes in `wParam` (with a fallback when the version call failed). `WM_NULL` is posted after `TrackPopupMenuEx` (KB135788).
- **Quit while minimized to tray, and closing in general:** the icon stayed until process exit, so "Show Lumen" or a click on the icon during mpv's teardown (which can last seconds with a network stream) brought back the hidden, dying window. Tray, thumbnail-button and media-key (SMTC) commands could also reach the player while mpv was being destroyed on the shutdown thread. While quitting, the icon is removed at once and those messages are dropped.
- **Elevated Lumen:** `TaskbarCreated` and the tray callback now pass UIPI (`ChangeWindowMessageFilterEx`).
- **Reviewed and fine:** show/hide from the tray (`SW_RESTORE` restores a maximized window as maximized), minimize-to-tray hiding only when the icon exists, and the menu disabling media items without a file.
- **Not changed:**
  - The left click toggles "minimize" only when Lumen is the foreground window, which it rarely is right after a tray click (the taskbar takes the foreground). In practice the click always shows the window. This is harmless.
  - The tray icon isn't reloaded on DPI changes.

## Checks and perf

- Build at /W4 with no warnings.
- `tests\build-tests.bat`: 27 tests, 5537 checks, 0 failures.
- `i18n_check`: 0 conflicts, 853 keys.
- `check-settings`: 0 of 220 settings without a reader.
- `check-icons`: 0 missing.
- Sweep with `-Filter "mini|toast|player-video"` (35 scenarios):
  - base (`r7lo-sweep-base`): 1 low;
  - fixed (`r7lo-sweep-new2`): 4 low, namely the base's ItemGrid one plus the new toasts' ellipsized path, which now has a tooltip.
  - No medium or high findings. The first fixed run had three medium findings ("ellipsized, no tooltip" on the new toasts); fixing them is the last commit.
- Paused and idle CPU stay 0 ms per 5 s. The only new timer is the 1 s fallback, which exists only while a filter switch is waiting for the decoder.

## Files outside the obvious scope

- `src/app/player/PlayerView.cpp` (subtitle-click agent's file): 3 lines in `MiniPlayer::Paint` (the title tooltip).
- `src/ui/Controls.{h,cpp}`: `ShowToast` takes an optional action; only `ToastBubble` and `ShowToast` changed.
- `src/main.cpp`, `src/app/MainWindow.{h,cpp}`, `src/app/App.{h,cpp}`, `src/app/SystemIntegration.cpp`, `src/app/Actions.cpp`, `src/app/player/LiveState.{h,cpp}`, `src/core/ShellText.h` (new).
- `tests/core_tests.cpp`, `qa/round5/scenarios.json` (4 scenarios after `toast-fullscreen`), `tools/dev/sweep.ps1`, `tools/dev/command.ps1`.
- None in Player.cpp, MusicView, Theme or src/library.
