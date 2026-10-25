COMMON RULES (Moonbow round 5: the UI bug hunt)

Moonbow is a Windows media player built with:
- C++20 and Win32;
- Direct2D/DirectWrite and DirectComposition;
- libmpv (`--wid` child window).

The main checkout is C:\Users\wheny\Desktop\CODE\lumen-player. Read these first:
- qa/round5/METHOD.md: the method for this round;
- STATUS.md, sections 6а and 7;
- design/DESIGN.md;
- qa/round4/*.md: last round's findings. Don't re-report fixed items; re-check them.

The owner says the interface "still has many bugs" and wants the app fully polished before new features. The owner's own report from earlier, partly fixed:
- the UI is see-through;
- the queue can't be edited in fullscreen (not reproduced yet);
- the window resizes when switching videos;
- the bottom bar hides and shows badly, covers the video, and has no minimal variant.

Performance is sacred:
- no blocking work on the UI thread;
- no per-frame allocations;
- no CPU use while idle or paused.

Code style: match the surrounding code. UI strings take an English key plus a Russian translation in src/app/strings/<area>.inc. Use only icons that exist in src/gfx/IconData.inc and the tokens in src/ui/Theme.*.

Build. A worktree must first copy third_party\mpv from the main checkout. Build with:

`cmd /c "set MOONBOW_OUT=<dir>\build&& <dir>\build.bat"`

The build must have no warnings at /W4. Then run:
- tests\build-tests.bat;
- python tools/dev/check-icons.py;
- python tools/dev/check-settings.py;
- python qa/i18n_check.py.

Hunters who don't change code use the prebuilt exe given in their brief. They never build into C:\Users\wheny\Desktop\CODE\lumen-player\build, because the owner runs that exe.

Testing tools, all background and invisible:
- `powershell -File tools\dev\launch.ps1 -Exe <exe> -DataDir %TEMP%\moonbow-qa\<yourprefix>-<n> -Fresh [-Settings '{"key":value}'] [-Library <folder>] [files]` prints the PID.
- tools\dev\input.ps1: click, move, drag, wheel, keys, type.
- tools\dev\capture.ps1: PNG; use -Scale 1 for crops.
- tools\dev\dump-tree.ps1: widget tree with rects in DIPs.
- tools\dev\winpos.ps1: -Size w,h / -Pos / -Maximize / -Restore.
- tools\dev\hitmap.ps1: WM_NCHITTEST sweep.

Test media:
- %TEMP%\moonbow-qa\media: videos with chapters, tracks and subtitles; tagged music; broken files;
- %TEMP%\moonbow-qa\media2;
- the worst-case set, when it exists: %TEMP%\moonbow-qa\worst.

You may generate more media with ffmpeg into %TEMP%\moonbow-qa\<yourprefix>-media. Use your own unique prefix for every data dir and temp file.

HARD RULES:
- Never touch the real mouse, keyboard, focus or clipboard.
- Never open native dialogs (file or folder pickers, Explorer, colour picker).
- Never press the file-associations button.
- Kill only PIDs you started. Never `taskkill /IM Moonbow.exe`, and never touch a Moonbow started from build\Moonbow.exe by the owner.
- Never modify %TEMP%\moonbow-qa\media, media2 or any user files.
- No registry or system setting changes.
- Startup volume stays 0.
- Download and install nothing; no network services.
- Never put the PC to sleep.
- Never edit the Figma file.

Git: commit on your own branch after every logical step. Write the message to a file without a BOM and use `git commit -F <file>`. The last line is `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Keep each shell command under 90 s; run long builds in the background.

Final message:
- what you did;
- how you verified each item;
- what you skipped and why;
- your branch name;
- any change needed outside your file scope.
