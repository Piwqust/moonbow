# Brief for UX-fix agents (Lumen, 2026-10-19)

You fix a slice of the UX audit in Lumen, a native Windows media player (C++20, Win32, Direct2D, libmpv,
own UI framework in src/ui). You work in your OWN git worktree on your own branch. Other agents fix other
slices in parallel in other worktrees; the lead merges all branches into `ux-audit-fixes` at the end.

## Read first
- `qa/research/UX-AUDIT-2026-10-19.md` (the audit: findings are numbered 1-31) and the walkthroughs in
  `qa/research/ux-2026-10-19/walkthrough-*.md`. Your task lists the finding numbers you own.
- `ARCHITECTURE.md`, `design/DESIGN.md`, `qa/visual-rules.md`, `qa/ESCAPES.md` (product questions Q1-Q5).
- `qa/REVIEWER-BRIEF.md`: how to drive the app and the HARD RULES for testing.

## Hard rules (the owner is working on this PC)
- Test only invisible `--automation` instances via `tools\dev\launch.ps1 -Exe <your exe> -DataDir %TEMP%\lumen-qa\uxfix-<name>\data-... -Library %TEMP%\lumen-qa\media` (or `%TEMP%\lumen-qa\ux-worst` for worst-case media). Look with `tools\dev\capture.ps1`, drive with `tools\dev\input.ps1`, find coordinates with `tools\dev\dump-tree.ps1`.
- Never touch the real mouse/keyboard/focus, never open native dialogs, Explorer or file pickers, never click file-associations, no registry/system changes, never read the clipboard, never touch media outside %TEMP%\lumen-qa. Kill only PIDs you started; never `taskkill /IM Lumen.exe`. Never use or overwrite `C:\Users\wheny\Desktop\CODE\lumen-player\build\Lumen.exe`. No sleep. Volume stays 0. Use the unique prefix `uxfix-<name>` for every temp folder.

## Build in your worktree
- libmpv is gitignored. Once, in your worktree root: `cmd /c mklink /J third_party\mpv C:\Users\wheny\Desktop\CODE\lumen-player\third_party\mpv` (junction; never commit it — `third_party/mpv/` is in .gitignore).
- Build: `cmd /c "set LUMEN_OUT=%TEMP%\lumen-qa\uxfix-<name>\build&& build.bat"` → your exe is `%TEMP%\lumen-qa\uxfix-<name>\build\Lumen.exe`. Every .cpp under src is built automatically.
- Tests: `tests\build-tests.bat` (builds and runs core tests, /WX). Checks: `python tools/dev/check-icons.py`, `python tools/dev/check-settings.py`, `python qa/i18n_check.py`, `python tools/dev/check-colors.py`. All must pass before you finish.
- Eight agents build at once on the owner's PC: always build at low CPU priority, e.g.
  `cmd /c "start "" /LOW /B /WAIT cmd /c "set LUMEN_OUT=%TEMP%\lumen-qa\uxfix-<name>\build&& build.bat""`
  (same for tests). Long builds: run them in the background and wait for the notification; do not poll with sleep.

## How to change code
- Smallest complete change; match the surrounding code (names, comments explain "why", no chatty comments).
  Reuse what exists (Toast, Confirm, PanelKit, ItemGrid, settings schema, i18n `Tr`).
- Every new UI string goes into the right `src/app/strings/*.inc` with Russian AND English, placed next to related strings (NOT at the end of the file: other agents edit the same files and end-of-file additions conflict).
- New settings go through `src/core/SettingsSchema.cpp` like the existing ones.
- Stay in the files you own (listed in your task). If you truly must touch another slice's file, keep the edit tiny and say so in your final report.
- Visual changes: screenshot dark and light theme, a long title and a saturated cover where relevant; look at the shots like the owner would (qa/visual-rules.md). Moving things: a frame series, not one still.
- Commit after each finding (or a tight group): write the message to a file in `%TEMP%\lumen-qa\uxfix-<name>\MSG.txt` (UTF-8, no BOM) and `git commit -F` it. First line imperative English summary; end the message with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push, merge, rebase or switch branches; never commit the third_party junction or build output.

## Final message (your report to the lead)
Plain Russian, short: for every finding number you own — done / partial / not done, and why; the commits;
how you verified (build, tests, checks, which screenshots you looked at); anything you touched outside your
files; risks or decisions the owner should know.
