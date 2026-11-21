# Moonbow next to VLC and MPC-HC

The same files on the same PC, measured from outside by one tool. It starts a player, reads the pixels of
its window and decodes marks drawn into every frame of the test clips: a colour square and the frame
number in binary. So it can tell when the first frame is on screen, which frame it is and where a seek
landed, the same way for every player and without any player-specific API.

## Results, 21 November 2026

Moonbow 0.2.0 (installed build), VLC 3.0.24 and MPC-HC 2.8.5 (official archives, default settings). Medians.

| | Moonbow | VLC | MPC-HC |
| --- | --- | --- | --- |
| Window on screen | **70 ms** | 1 094 ms | 1 518 ms |
| First frame, 1080p H.264 (15 starts) | **420 ms** | 1 583 ms | 2 398 ms |
| First frame, 4K 60 fps HEVC 10-bit (8 starts) | **430 ms** | 1 619 ms | 2 353 ms |
| Window with no file (5 starts) | **62 ms** | 1 007 ms | 1 108 ms |
| Close: command to process exit | 239 ms | 230 ms | 581 ms |
| Idle, no file: private memory | 105 MB | 15 MB | **11 MB** |
| Idle, no file: threads | 86 | 16 | **10** |
| 1080p playback: CPU, one core | 8.0 % | 3.4 % | **2.1 %** |
| 1080p playback: private memory | 243 MB | **164 MB** | 234 MB |
| 1080p playback: GPU memory | 193 MB | 125 MB | **114 MB** |
| 4K 60 fps playback: CPU, one core | 7.6 % | 7.9 % | 7.8 % |
| 4K 60 fps playback: private memory | 499 MB | 383 MB | **365 MB** |
| Paused: CPU | 0.5 % | 0.5 % | 0.4 % |
| Frames shown, 1080p, 8 s | 193 of 193 | 193 of 193 | 192 of 192 |
| Frames shown, 4K 60 fps, 8 s | 481 of 481 | 481 of 481 | 475 of 481, then 479 of 480 |
| Seek during playback (8 seeks) | 97 ms, frame-exact | 151 ms, frame-exact | **38 ms**, up to 14 frames off |
| Colour error, worst channel of 255 | 0.5 | 1.0 | 1.0 |

What stands behind them:

- **Start.** Moonbow shows its window before the video engine is ready. VLC and MPC-HC spend about a
  second before their window exists, with or without a file.
- **Idle and 1080p.** Moonbow loads libmpv at start, and mpv draws each frame with its own shaders, while
  VLC and MPC-HC hand the frame to the GPU's video processor. Of Moonbow's CPU during 1080p, 2.9 % is in
  mpv's threads, 2.0 % in the graphics driver and 0.7 % in Moonbow's own UI thread.
- **Seek.** MPC-HC jumps to the nearest keyframe, which is fast and inexact. Moonbow and VLC land on the
  requested frame.
- **Colour and pacing.** No difference worth naming.

## Conditions, and what they mean for the numbers

- One PC: Intel Core i9-14900K, 64 GB, NVIDIA RTX 4080, Windows 11. Main display 3840x2160 at 240 Hz with
  HDR on, where the players opened; a second display without HDR was used for the colour test.
- Windows Defender real-time protection and Windhawk (with three mods) were running. Both load into all
  three players. How much of VLC's and MPC-HC's first second is theirs was not separated, so on another
  PC the gap can be smaller.
- Warm starts: the players and the clips were already in the disk cache. Starts after a reboot were not measured.
- VLC ran from the archive with the plugin cache its installer builds (`vlc-cache-gen`); MPC-HC with the
  bundled LAV Filters and MPC Video Renderer, its first-run question about updates answered beforehand.
- A capture of a 1600 to 1900 px wide window takes about 20 ms. Every time above is the end of the capture
  that saw the event, so the event happened up to one capture earlier. Moonbow's own log
  (`MOONBOW_TRACE`) put "playback started" at 349 ms in a run where the tool saw the first pixel at 382 ms.
- "First frame" is the first capture where the square in the middle of the picture is brighter than 9 % of
  full. Moonbow fades the picture in from black over 200 ms; it reached full brightness at 517 ms.
- CPU, memory and GPU were sampled for 20 s with the window at 1600x960 and no capture running.
- On an HDR display a window capture of Moonbow comes out about three times too bright: mpv shows SDR
  video through an HDR surface, and `PrintWindow` converts it wrongly. The screen is right. That is why
  the colour test runs on the display without HDR (`-MoveTo`).

Raw results of each series are in [`results-2026-11-21`](results-2026-11-21).

## Run it

Unpack the players next to each other, VLC as `<Players>\vlc\vlc-3.0.24` and MPC-HC as `<Players>\mpc`
(default `%TEMP%\moonbow-qa\players`). They keep their settings in their own folders, so installed copies
are not touched. Moonbow is taken from its install folder and runs with `--new-instance --data-dir`.
The players' windows do show up on the desktop while this runs.

```
bash qa/perf/compare/make-media.sh "$TEMP/moonbow-qa/compare-media"      # the three clips, needs ffmpeg
powershell -File qa\perf\compare\bench.ps1 -Mode probe                  # one start each, with a screenshot
powershell -File qa\perf\compare\bench.ps1 -Mode start -Keep -Runs 15   # start with the 1080p file (start4k: 4K)
powershell -File qa\perf\compare\bench.ps1 -Mode empty -Keep -Runs 5    # start without a file, idle cost
powershell -File qa\perf\compare\bench.ps1 -Mode load                   # CPU, memory, GPU, pacing, pause (load4k: 4K)
powershell -File qa\perf\compare\bench.ps1 -Mode seekplay -Runs 8       # seek during playback (seek: paused)
powershell -File qa\perf\compare\bench.ps1 -Mode colors -MoveTo "3860,200,1300,820"
```

`-Keep` keeps each player's settings between runs (an everyday start); without it every run is a first
run. `-Only vlc,mpc` picks players, `-Out file.json` saves the series.
