# Round 3 — performance (build-r3, commit 9e387bb)

Reviewer: performance. Build: `%TEMP%\lumen-qa\build-r3\Lumen.exe`. All instances: `--automation`, own PIDs only, volume 0.

## Measurements

Machine: RTX 4080, 2560x1440 @ 144 Hz, 32 logical CPUs. CPU figures are "% of one core".

### Startup (qa\perfirstframe.ps1 -Runs 6 + perf.ps1 -Runs 2, LUMEN_TRACE, ms from process start)

| Start | window shown | first frame (WARP) | mpv ready | hardware device | file loaded |
|---|---|---|---|---|---|
| library #1..#5 | 55.3 / 52.3 / 54.8 / 51.2 / 53.4 | 88.8 / 85.0 / 87.0 / 82.9 / 84.2 | 84-89 | 208.8 / 195.9 / 198.3 / 216.5 / 207.9 | - |
| file #1..#5 | 45.7 / 53.2 / 58.2 / 52.2 / 52.4 | 67.9 / 84.7 / 83.4 / 78.6 / 81.3 | 72-85 | 187.0 / 197.6 / 205.1 / 208.2 / 195.5 | 249.2 / 259.6 / 261.0 / 271.0 / 257.4 |
| reference 9e387bb | ~45-55 | ~70-87 | | ~190-200 | ~245 |

Verdict: window and first frame are inside the reference band. Hardware device median 203 ms (+1.5 % vs the top of the band, 2 of 10 runs at 208-216). File loaded median 259.6 ms = **+6 % vs 245** (worst 271 = +10.6 %, one run). Not a >10 % regression on the median; noted as drift.

### Steady state (perf.ps1)

| Scenario | Private | WS | CPU (one core) | Threads | Reference |
|---|---|---|---|---|---|
| library idle (5 s) | 96 MB (libopen: 95, 93) | 112 MB | 0 % | 82-88 | 0-0.3 %, ~90 MB -> +5..7 % memory, OK |
| 1080p playing | 227 MB | 160 MB | 5.31 % | 149 | 5-7 % -> OK |
| 1080p paused | 224 MB | 157 MB | 0 % | 149 | round 2 N-06 had paused ~ playing -> **fixed** |
| 4K (sw-decoded 4:4:4) | 621 MB | 471 MB | 94 % | 149 | ~630 MB -> OK |
| close | library 111-117 ms, 1080p 174-197 ms, **4K 376 ms** | | | | round 2: 108-258 ms |

### Open from the library (libopen.ps1, mpv log)

| Run | loadfile -> Starting playback | loadfile -> VO configured | private while playing |
|---|---|---|---|
| r3 #1 | 115 ms | 152 ms | 250 MB |
| r3 #2 | 135 ms | 168 ms | 224 MB |
| old baseline mpv-libdef.log (18.10 08:59) | 101 ms | 135 ms | |

The extra time sits in mpv's own D3D11 VO creation (27 ms -> 46 ms "Initializing GPU context" -> "feature level") and a new ICC profile read; hooks (auto_profiles, new ytdl_hook) cost ~2 ms. Hwdec is still `d3d11va` zero-copy (`d3d11[nv12]`).

### Big library, 3500 files (stress.ps1, warm start, thumbs.ps1)

| What | r3 | Reference | Verdict |
|---|---|---|---|
| First scan (fresh data dir, to CPU settle) | 17.5 s, 55 s CPU total, 113 MB private, 89 threads | 16-19 s | OK |
| Warm start (cache) to CPU settle | 1.47 s incl. startup, 594 ms CPU, 99 MB private | scan ~0.1 s | OK |
| UI frame while scrolling/hovering Home | layout 0.03 ms, paint avg 0.63-0.69 / max 1.32 ms, present avg 2.8-5.9 / max 10.9 ms | layout < 0.1, paint ~0.7 | OK (present max > one 144 Hz frame in 2 of 5 seconds) |
| Videos scroll / search typing | paint 0.71-0.77 ms, present 1.4-2.0 ms | | OK |
| **Opening Music on a warm library** | Genres card "Читаем теги…" for **~35 s**, **32 s of CPU** (~0.9 core), every launch | n/a | **P-01** |
| Thumbnails after fast scroll of 400 clips | all visible cards have frames 0.7 s after the last wheel tick (th-1 = th-2) | ~0.7 s | OK. Private 200 MB after the scroll |

## Stability log

- **Toggling the new video settings during playback** (1080p h264, `d3d11va`, Settings window open next to the player; 45 clicks in 14.5 s): quality Быстро/Сбалансированно/Максимум x9, debanding x6, AI upscaling x4, RTX Video HDR x4, upscaler through all 6 values, smooth motion on. No crash, no hang, 0 mpv errors, frames keep changing (vstorm-main1/2 differ in 2376 of 8900 samples). Zero-copy kept all the time: `VO: [gpu-next] 1920x1080 d3d11[nv12]`, stats overlay "h264 · d3d11va (без копирования)". AI upscaling really switches `d3d11vpp scaling-mode=nvidia` ("NVIDIA RTX Super Resolution enabled", VO 2554x1436) and back. RTX Video HDR on an SDR screen is a no-op (by design). Two "Audio device underrun detected" warnings at the VO reconfigures. Exception: the CPU deinterlacer, see P-02.
- **CPU cost of the presets** (1080p30, % of one core): default 5.3 %; Максимум (ewa_lanczossharp) 9.9 %; + smooth motion 18.8 %; + EWA Lanczos Sharp upscaler 25.5 %. Private 322-361 MB while AI upscaling or Максимум is on (227 MB default). Stats overlay with smooth motion: 8 frames dropped at output in ~60 s.
- **Closing with dialogs open**: main window closed with Settings + stats overlay open in 228 ms, clean mpv uninit in the log. 4K close 376 ms (slowest seen; library 111-117 ms, 1080p 174-197 ms).
- **Seek-preview scrubbing** (10-min file, paused, 42 s of WM_MOUSEMOVE sweeps along the seek bar): the preview frame shows under the cursor (scrub.png). Before 206 MB / 151 threads / 952 handles; right after 263 MB / 185 / 1064; 3-23 s later 260 MB / 188-190 / 1080; 28-38 s later 232 MB / 148 / 956 (preview instance unloaded). CPU 55.8 s during the 42 s, idle afterwards (~3 ms/s). A 10-minute scrub was not run because of the 60 s command limit. In 42 s the growth leveled off, and the unload brought threads and handles back. See P-05.
- **Corrupt data files** (copy dir r3-performance-corrupt): no crash. history/settings/ui backups `*.corrupt-20261018-141218.json` created; library.json got none; onboarding re-runs because of ui.json (P-04).
- **Quick switching** (19 files on the command line: 4K, 1080p, sw 720p, vertical, series, fake.mkv, truncated mp4, vp9 without audio, wav, opus, mp3, flac): 22x N in 5.9 s, 12x P, 9x (Space, Right). No crash or hang; ends on a coherent state (unicode film, paused, near the end). Memory: start 181 MB / 976 handles / 146 threads; right after the N storm 144 MB / 835 / 90; after P/Space/Right 301 MB / 1146 / 151; settled 7 s later 281 MB / 1146 handles / 151 threads. Round 2 settled at 172 MB / 962 / 87 on a different last file, so not directly comparable; no runaway growth.

## Re-check of round 2

Round-2 performance/stability findings that STATUS.md marks as addressed:

| R2 ID | Verdict | Evidence (round 3) |
|---|---|---|
| N-06 paused CPU ~ playing CPU | **Fixed** | perf.ps1: 1080p playing 5.31 %, paused **0 %** of one core |
| N-03 corrupt data files dropped silently, no backup | **Partly fixed** | history/settings/ui now get `*.corrupt-*.json` backups; library.json still none, broken ui.json still re-runs onboarding (P-04) |
| P22 4K peak memory | **Holds** | perf.ps1 4K (sw 4:4:4 test file) private 621 MB, reference ~630 MB |
| Warm scan ~0.1 s (cache) | **Holds** | warm start of the 3500-file library settles in 1.47 s including startup, 594 ms CPU |
| Thumbnails after fast scroll (~0.7 s) | **Holds** | th-1.png (0.7 s after the scroll) = th-2.png (2.7 s) |
| Shutdown < 0.3 s | **Holds except 4K** | 111-228 ms; 4K 376 ms (P-06) |

## New features

- **WARP first frame** (9e387bb): works. First frame 68-89 ms, 110-130 ms before the hardware device is ready (187-216 ms). No flash or blank frame in the captures.
- **Stats overlay (Ctrl+Shift+I)**: values plausible. 1080p h264 shows "d3d11va (без копирования)", gpu-next 1920x1080 d3d11 (nv12), 30.000 file / 30.00 actual / 143.97 display fps, A/V +1.5 ms. The sw 720p 4:4:4 file shows "h264 · программный", yuv444p. Zero-copy is reported correctly.
- **Quality presets / upscaler / AI upscaling / RTX HDR / deband / smooth motion**: all apply live and keep zero-copy (stability log). The CPU deinterlacer does not work (P-02).
- **Seek previews (persistent instance)**: fast and correct under the cursor; unload after ~28 s idle works (P-05 for the cost).

## New findings

| ID | Severity | Area | What happened | Steps to reproduce | Expected | Evidence |
|---|---|---|---|---|---|---|
| P-01 | Major | Performance / Library | Every time the Music hub is opened in a new session, the genre index re-reads the tags of every song through the Windows Property System. On the 2000-song big library (warm cache, scan already done in 1.5 s) the "Жанры" card says "Читаем теги…" for ~35 s and the process burns 32 s of CPU (~0.9 core) before it shows "2 жанра". Genres are never cached, unlike the rest of the library (cache v3). | qa\perfiglib.ps1 library; start once and let it scan; restart (warm); click Музыка; sample CPU. | Genres are stored in the library cache with the other tags, so a warm start shows them at once and costs ~0 CPU. | shots-performance\warm-music.png (1 s after opening, "Читаем теги…"), genres-done.png (after 36 s, "2 жанра"). Likely cause: srcpp\library\LibMusic.cpp:45-88 (`GenreIndex` keeps genres only in memory, `SHGetPropertyStoreFromParsingName` per file). |
3perf-deint.log lines 428-449. Likely cause: src\core\SettingsSchema.h:175 `kAutoHwdec = "d3d11va,d3d11va-copy"` is used even when `ConfigCpuFilters()` (srcpp\player\LiveState.cpp:100) is true; mpv does not fall back to the copy variant when only the filter fails. |
| P-02 | Major | Video settings / stability | Choosing a CPU deinterlacer (Settings > Видео > Метод = Bwdif or Yadif, "нагружает процессор") does nothing: mpv starts with zero-copy `d3d11va`, the lavfi graph cannot take `d3d11` frames ("Impossible to convert between the formats supported by the filter 'mpv_src_in0' and the filter 'auto_scale_0'"), and mpv drops the filter ("Disabling filter lumen because it has failed"). The picture is not deinterlaced and the user gets no message. The task expected hwdec to switch to copy-back when a CPU filter is chosen; it does not. | launch.ps1 -Settings '{"video.deinterlace":"on","video.deinterlaceMethod":"yadif","general.extraOptions":"--log-file=..."}' "Chaptered Demo (1080p).mkv"; read the mpv log. | With a CPU filter in the chain Lumen asks for `d3d11va-copy` (or `hwdec=auto-copy`), so yadif/bwdif really run. Zero-copy stays for everything else. | round3
| P-03 | Minor | Performance / open from library | Opening the 1080p film from the library is slower than the 18.10 baseline: loadfile -> "Starting playback" 115 / 135 ms (2 runs) vs 101 ms, loadfile -> VO configured 152 / 168 ms vs 135 ms (+13..25 %). The extra time is inside mpvs D3D11 VO setup (27 ms -> 46 ms between "Initializing GPU context" and "feature level") plus a new ICC profile read; Lumens hooks cost ~2 ms. Few samples: a regression to confirm, not a proven one. Cold "file loaded" from the command line moved +6 % on the median (245 -> 260 ms). | qa\perf\libopen.ps1 -Exe build-r3 (x2), compare with %TEMP%\lumen-qa\mpv-libdef.log. | Within 10 % of the baseline (~100 ms to playback start). | %TEMP%\lumen-qa\mpv-r3lib.log, mpv-r3lib2.log vs mpv-libdef.log |
| P-04 | Minor | Stability / data files | Round 2 N-03 is only partly fixed. Truncated history.json, settings.json and garbage ui.json now each get a `*.corrupt-<date>.json` backup. But a corrupt **ui.json** still restarts the onboarding wizard, and a malformed **library.json** (`"roots":"not-an-array"`) gets no backup and is silently treated as no library. | Copy a data dir; truncate history.json and settings.json, write garbage bytes to ui.json, put `"roots":"not-an-array"` in library.json; start. | Every data file gets the same treatment (backup + toast). A broken ui.json is not a first run: skip onboarding when other data files exist. | shots-performance\corrupt-start.png (onboarding again); file list in the stability log |
| P-05 | Polish | Performance / seek previews | Seek-preview scrubbing is heavy and keeps some memory. 42 s of continuous back-and-forth sweeps over a 10-min file (23 sweeps x 120 hover points): **55.8 s CPU (~1.3 cores)**, private 206 -> 263 MB, threads 151 -> 185, handles 952 -> 1064. The idle preview instance unloads 25-28 s after the last hover (threads 148, handles 956: good), but private stays at 232 MB (+26 MB vs before). | Long Seek Test 10min.mp4, pause, PostMessage WM_MOUSEMOVE sweeps along the seek bar for 42 s, then wait 40 s. | Cached hover points cost little CPU; after unload, memory returns near the baseline. | shots-performance\scrub.png (preview shown); numbers in the stability log |
| P-06 | Polish | Performance / close | Closing during 4K playback takes 376 ms, against 111-228 ms for everything else (library, 1080p, Settings open). Round 2 never went above 258 ms. | perf.ps1: 4K Mandelbrot playing 5 s, CloseMainWindow, time to exit. | Close stays under ~250 ms, e.g. by not waiting on the software 4K decoder threads. | perf.ps1 output ("4K playing ... close: 376 ms") |

## Compared with the leaders

- **Startup**: window in ~50 ms and a first frame in ~80 ms put Lumen ahead of MPC-HC/PotPlayer and on par with mpv itself. File loaded at ~260 ms from a cold process is competitive.
- **Idle and playback cost**: 0 % idle, ~95 MB private in the library, 5 % of one core for 1080p hw playback, 0 % paused. That is MPC-HC territory, far below Electron-based players.
- **Library**: 17.5 s first scan of 3500 files and 1.5 s warm start are fine. Re-reading every genre tag each session (P-01: 32 s CPU) is something MusicBee/foobar2000 never do; they keep tags in their database.
- **Video processing**: zero-copy with presets, RTX Super Resolution and smooth motion matches mpv. A CPU deinterlacer that silently does nothing (P-02) is behind MPC-HC and PotPlayer, which switch to copy-back on their own.
- **Seek previews**: as quick as mpv+thumbfast, but ~1.3 cores during continuous scrubbing (P-05).

## Top 5

1. **P-01** Opening Music on a warm library re-reads every genre tag: "Читаем теги…" for ~35 s and 32 s of CPU on 2000 songs, every session.
2. **P-02** A CPU deinterlacer (Bwdif/Yadif) never runs: mpv keeps zero-copy d3d11va, the filter fails and is disabled with no message.
3. **P-03** Open-from-library latency is +13..25 % vs the 18.10 baseline (mpv VO setup); cold file load +6 % on the median. Needs a re-measure to confirm.
4. **P-04** A corrupt ui.json still re-runs onboarding; a malformed library.json gets no backup.
5. **P-05** Seek-preview scrubbing costs ~1.3 cores and leaves +26 MB after unload. P-06 (4K close 376 ms) is a close sixth.

Overall: no crash, hang or stuck frame in any test (45 live settings changes, a 19-file switching storm, 42 s scrubbing, corrupt files, closing with dialogs open). The 9e387bb reference numbers hold within 10 % (window, WARP first frame, hardware device, idle 0 %, 1080p 5.3 %, 4K 621 MB, first scan 17.5 s). The exceptions are the median "file loaded" (+6 %, inside the band) and the library-open latency in P-03. Not done within the budget: "rapid input during animations" as a separate test (the switching storm sent input during page/toast transitions without a hang), and audio settings toggles (passthrough, exclusive, gapless).
