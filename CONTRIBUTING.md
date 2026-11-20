# Contributing

Thanks for wanting to help. Bug reports with logs are the most useful thing right now; see
[Reporting a problem](README.md#reporting-a-problem). Можно писать по-русски.

Before a larger change, open an issue to talk about it first: Moonbow keeps its interface small on purpose, and
a feature may already be planned differently.

## Build

You need Visual Studio 2022 with **Desktop development with C++**. CMake is not used.

```bat
powershell -ExecutionPolicy Bypass -File tools\fetch-libmpv.ps1 -Tag 20261017
build.bat              & rem build\Moonbow.exe; "build.bat debug" for a debug build
tests\build-tests.bat  & rem core tests: settings, library, history, playlists, tags, setup
```

`build.bat` compiles every `.cpp` under `src/` into one exe, so file basenames must be unique, and `/W4` must stay
free of warnings. After editing `design/icons`, run `tools\gen-icons.ps1`. `Moonbow.exe --gallery` shows every
control. The code layout and the UI framework are described in [ARCHITECTURE.md](ARCHITECTURE.md), the design
tokens in [design/DESIGN.md](design/DESIGN.md).

## Run it without disturbing your desktop

A test instance does not touch your settings or your library, and with `--automation` its windows stay invisible,
take no focus and open no system dialogs:

```bat
powershell -File tools\dev\launch.ps1 -DataDir %TEMP%\moonbow-test -Fresh "D:\Videos\clip.mp4"
```

The script prints the process id. Drive and inspect that instance with:

| Script | Does |
| --- | --- |
| `tools\dev\input.ps1` | posts clicks, keys and text to it (`-Click x,y`, `-Key Space`, `-Ctrl`, `-Shift`) |
| `tools\dev\command.ps1` | runs a test command: `nav music albums`, `open <file>`, `set <key> <value>`, `wait-idle` |
| `tools\dev\capture.ps1` | saves its window to a PNG |
| `tools\dev\dump-tree.ps1` | prints the widget tree with coordinates |

Never use the real mouse, keyboard focus or `SetForegroundWindow` in tests, and stop only the processes you
started. Test media: `bash qa/make-media.sh <folder>` generates files with chapters, tracks, subtitles, tags and
broken files with ffmpeg.

## Speed

Startup and frame timings matter in this project. With `MOONBOW_TRACE=1` Moonbow writes startup marks and, once a
second, frame statistics to `%TEMP%\moonbow-trace.txt`. The measuring scripts (startup, memory, CPU, a large
library, scrolling) are in [qa/perf](qa/perf/README.md). If a change touches startup, playback or the library,
include before and after numbers in the pull request.

## Rules for changes

- Widgets, the player and the library services are used from the UI thread only. Disk scans, thumbnails and
  network work go to worker threads and post their results back.
- Never paint opaque pixels over the video area: the interface is a DirectComposition layer above mpv's window.
- No hard-coded colours: read the theme palette at paint time, so theme and skin changes apply live.
- Interface strings are written in English and wrapped in `Tr(L"...")`; add the Russian translation to the matching
  file in `src/app/strings/`.
- Match the existing style: 4-space indent, 120 columns, comments only where they help.
- One change per pull request, with a sentence on what it changes for the person using Moonbow.

## Releases

`tools\package-release.ps1` builds the release files from a clean commit: the installer, the portable zip, a
`.sha256` for each, and the debug symbols kept locally for reading crash dumps. User-facing changes go into
[CHANGELOG.md](CHANGELOG.md) and, for the installer's "What's new", into `src/setup/ReleaseNotes.h`.
