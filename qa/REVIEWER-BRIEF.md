# Moonbow — brief for reviewer agents

Moonbow is a new minimalist media player for Windows (C++20, Win32, Direct2D/DirectComposition, libmpv).
Design reference: a Figma "VLC Redesign" file, re-styled to a strict **monochrome** palette
(white accent on graphite; light theme mirrors it). Goal: polished, fast, MPC-HC-like simplicity
with a modern library. The app is in Russian by default (English available in Settings).

You are a **demanding new user**. Be harsh and concrete. You do not fix code; you find problems.

## What you test

- Executable: `%TEMP%\moonbow-qa\build-rN\Moonbow.exe` (N = round number given in your task).
  This is a frozen copy — do not use `C:\Users\wheny\Desktop\CODE\lumen-player\build`.
- Test media (generated, safe to use): `%TEMP%\moonbow-qa\media\` — Videos (chapters, 2 audio + 2 subtitle
  tracks, 10-min file, vertical, 4K, unicode name with sidecar .srt, a 3-episode series), Music
  (tagged mp3 album with covers, flac with Cyrillic tags), Mixed (wav, opus, vp9 without audio, a
  truncated mp4, a fake .mkv, an .m3u playlist).
- Design reference PNGs: `C:\Users\wheny\Desktop\CODE\lumen-player\design\ref\*.png` and
  `design\ref\components\*.png` (colors there are the old VLC orange — ignore hue, judge layout,
  spacing, hierarchy, completeness). Tokens: `design\DESIGN.md`.
- Source is at `C:\Users\wheny\Desktop\CODE\lumen-player\src` — you may read it to understand behaviour
  or locate a bug, but **never edit anything in the repository**.

## How to drive the app (never touch the real mouse/keyboard — the owner is using this PC)

Always start your own isolated instance:

```powershell
$dd = "$env:TEMP\moonbow-qa\data-<yourname>"   # your own settings/history/library
New-Item -ItemType Directory -Force $dd | Out-Null
$p = Start-Process "$env:TEMP\moonbow-qa\build-rN\Moonbow.exe" -ArgumentList "--new-instance","--automation","--data-dir",$dd,"`"<optional file>`"" -PassThru
```

- To skip onboarding put `{"onboarded":"1"}` in `$dd\ui.json` (or test onboarding on purpose with an empty dir).
- Library roots: `$dd\library.json` = `{"version":1,"roots":["C:\\Users\\wheny\\AppData\\Local\\Temp\\moonbow-qa\\media"]}`.
- Settings file: `$dd\settings.json` = `{"version":1,"values":{"key":value}}` (keys: see `src\core\SettingsSchema.cpp`).
  Set `"audio.startupVolumeMode":"fixed","audio.startupVolume":0` so nothing plays out loud.
- Shortcut that does all of the above and prints the new PID:
  `tools\dev\launch.ps1 -DataDir <dir> -Exe <exe> [-Fresh] [-Library <folder>] [-Settings '{"key":value}'] [-Onboarding] [files...]`
  (startup volume is always forced to 0; `-Onboarding` keeps the first-run window).
- `--automation` instances are **invisible by design**: every window is cloaked, has no taskbar button and
  never takes focus, so the owner keeps working undisturbed. You cannot see the window on screen; always look
  at it through `capture.ps1`, which still captures it fully (video, fullscreen, dialogs). Native Windows
  dialogs are not cloaked, which is one more reason never to open them.

Tools (in `C:\Users\wheny\Desktop\CODE\lumen-player\tools\dev\`), all take `-ProcessId <your pid>`:

- `capture.ps1 -Out shot.png [-Title "<window title>"] [-Scale 0.5]` — screenshot of a window
  (works while it is in the background; includes the UI layer). Settings window title: "Настройки"/"Settings".
- `input.ps1 -Click x,y [-Right] [-Double] | -Move x,y | -Wheel x,y -Delta -1 | -Drag x1,y1,x2,y2 | -Key <WinForms Keys name> | -Type "text"`
  plus `-Ctrl` / `-Shift` to hold modifiers. Coordinates are **client pixels** of the window
  (the window may be DPI-scaled; use the dump to find positions). Pass coordinates as a quoted string: `-Click "300,200"`.
- `dump-tree.ps1` — prints the widget tree with rects (DIPs; multiply by the window DPI scale, usually
  1.0–1.5, to get client pixels — compare with the screenshot size). Use it to find buttons.
- Look at your screenshots with the Read tool.

Window resizing: use `SetWindowPos` via a small Add-Type helper on **your** window handle if you need it
(never on other windows).

## Hard rules

1. Kill/close only processes you started (by PID). Never `taskkill /IM Moonbow.exe`; other Moonbow instances
   belong to the owner or other reviewers. Close yours at the end (`$p.CloseMainWindow()`, then `Stop-Process -Id`).
2. Never move the real mouse, never `SetForegroundWindow`/focus windows, never send global input (SendInput, SendKeys).
3. Do NOT click: "Make Moonbow the default player" / "Настроить" (file associations — writes the registry),
   "Show in File Explorer", "Open scripts folder", Browse… buttons, "Add folder", "Open file", "Custom color…",
   or anything that opens a native Windows dialog or Explorer. If a native dialog opens by accident, close it
   by posting WM_CLOSE to that dialog window of **your** process only.
4. Never modify, move or delete media files, and never use media outside `%TEMP%\moonbow-qa\media`.
5. No system setting changes, no registry writes, no installs, no network uploads.
6. Keep volume at 0 / muted.

## Beyond defects: product, design and motion

Most of what the owner still finds is not broken by the checklist above: it is unconsidered.
Read [qa/ESCAPES.md](ESCAPES.md) first: it lists what slipped past earlier rounds and why. Then,
besides hunting defects, answer its questions:

- **Product (Q1-Q3).** For each behavior and default: is it right for a song, a film, a series
  episode and a stream? Does it continue what the user just saw (Explorer, the library, a drop)?
  Would a Spotify, Apple Music, YouTube, VLC or IINA user be surprised?
- **Design (Q4-Q5).** Capture at scale 1, not 0.5, on realistic media: the worst-case set's
  `Music\Covers with lyrics` (white, black, saturated and busy covers with synced lyrics), both
  themes, long text. Ask what a designer at Spotify or Apple would change first, and what looks
  cheap, flat or dull. Run `python tools/dev/check-colors.py` and read
  [qa/visual-rules.md](visual-rules.md).
- **Motion.** Anything that moves (lyrics, fills, bars, transitions) gets a series of frames, not
  one: it must move one way, at an even pace, without a jump.
- **Combinations.** Try every input (left, right, double click, wheel, keys, drop) on every state
  of a screen (with and without lyrics, cover, queue, panel), not only on the first one you meet.

Report these as `Ugly` or `Product` findings next to the defects.

## What to report

Write your report to the file given in your task (Markdown). Structure:

- **Top 5** — the five things that most hurt the experience.
- **Findings** — up to 40 items, each:
  `ID | Severity (Blocker/Major/Minor/Polish) | Area | What happened | Steps to reproduce | Expected | Evidence (screenshot path)`.
  Include crashes, hangs, wrong behaviour, missing feedback, confusing UX, visual defects (misalignment,
  clipping, truncated Russian text, inconsistent spacing/sizes/radii, contrast, hover/focus states,
  light theme), performance (slow open, jank, CPU), and gaps vs. the Figma reference.
- Only report what you actually observed (screenshot or log), not guesses. If you read the code to find the
  cause, add a one-line "Likely cause: file:line".
- Don't pad the list: a few precise Blockers/Majors beat many vague nits.
