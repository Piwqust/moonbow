# Brief for UI-review fix agents (Lumen, 2026-10-20)

You fix a slice of the UI/UX review in Lumen, a native Windows media player (C++20, Win32, Direct2D, libmpv,
own UI framework in `src/ui`). You work in your OWN git worktree on your own branch, created from `main` @ ff96c7f.
Eight other agents fix other slices in parallel; the lead merges all branches into `ui-review-fixes` at the end.

The review files are NOT committed yet, so read them from the main checkout by absolute path:
- `C:\Users\wheny\Desktop\CODE\lumen-player\qa\research\UI-REVIEW-2026-10-20.md` (summary, priorities)
- `C:\Users\wheny\Desktop\CODE\lumen-player\qa\research\ui-review-2026-10-20\area-a11y.md`, `area-desktop.md`,
  `area-visual.md`, `area-ux.md`: every finding with evidence, the proposed fix and how to verify it.
Your task lists the finding IDs you own (A11Y-n, DESK-n, VIS-n, UX-n). The proposed fix is a strong hint, not a
contract: if you find a smaller complete fix, or the finding is wrong on this code, say so in your report.

Also read in your worktree: `ARCHITECTURE.md`, `design/DESIGN.md`, `qa/visual-rules.md`, `qa/ESCAPES.md`
(product questions Q1-Q5), `qa/REVIEWER-BRIEF.md` (how to drive the app and the HARD RULES for testing).

## Hard rules (the owner is working on this PC)
- Test only invisible `--automation` instances via `tools\dev\launch.ps1 -Exe <your exe> -DataDir %TEMP%\lumen-qa\uirfix-<name>\data-... -Library %TEMP%\lumen-qa\media` (worst-case media: `%TEMP%\lumen-qa\worst`). Look with `tools\dev\capture.ps1`, drive with `tools\dev\input.ps1`, find coordinates with `tools\dev\dump-tree.ps1`, read the screen-reader tree with `tools\dev\uia-dump.ps1`.
- Never touch the real mouse/keyboard/focus, never open native dialogs, Explorer or file pickers, never click file-associations / "default player", no registry/system changes (do not change Windows settings to test high contrast or text size: use code paths or test env vars), never read the clipboard, never touch media outside `%TEMP%\lumen-qa`. Kill only PIDs you started; never `taskkill /IM Lumen.exe`. Never use or overwrite `C:\Users\wheny\Desktop\CODE\lumen-player\build\Lumen.exe`. No sleep. Volume stays 0. Onboarding pre-fills the owner's real Videos folder: type `%TEMP%\lumen-qa\media` into the folder field before pressing Continue. Use the unique prefix `uirfix-<name>` for every temp folder.

## Build in your worktree
- libmpv is gitignored. Once, in your worktree root: `cmd /c mklink /J third_party\mpv C:\Users\wheny\Desktop\CODE\lumen-player\third_party\mpv` (junction; never commit it).
- Build at low CPU priority (nine agents build at once on the owner's PC):
  `cmd /d /c "start "" /LOW /B /WAIT cmd /d /c "set LUMEN_OUT=%TEMP%\lumen-qa\uirfix-<name>\build&& <worktree>\build.bat""`
  → your exe is `%TEMP%\lumen-qa\uirfix-<name>\build\Lumen.exe`. Use the full path of build.bat (cmd AutoRun changes the folder). `/W4` must stay warning-free. File basenames under `src/` must be unique.
- Tests: `tests\build-tests.bat` (core tests, /WX), same low-priority wrapper. Checks: `python tools/dev/check-icons.py`, `python tools/dev/check-settings.py`, `python qa/i18n_check.py`, `python tools/dev/check-colors.py`, `python tools/dev/check-language.py`. All must pass before you finish.
- Long builds: run them in the background and wait for the notification; do not poll with sleep.

## How to change code
- Smallest complete change; match the surrounding code (names, comments explain "why", no chatty comments). Reuse what exists (Toast, Confirm, PanelKit, ItemGrid, A11y/Describe, settings schema, i18n `Tr`).
- Every new UI string goes into the right `src/app/strings/*.inc` with Russian AND English, placed next to related strings (NOT at the end of the file: other agents edit the same files).
- New settings go through `src/core/SettingsSchema.cpp` like the existing ones.
- Stay in the files you own (listed in your task). If you must touch another slice's file, keep the edit tiny and local and name it in your report. Shared hot spots: `src/ui/Controls.cpp`, `src/ui/Host.cpp`, `src/app/library/ItemGrid.cpp`, `src/app/player/PlayerView.cpp`, `src/core/SettingsSchema.cpp`, strings — edit only the functions your findings name.
- New non-trivial logic (search tokenizer, contrast pairs, aspect math...) gets a small test in `tests/core_tests.cpp` when it lives in testable core code. Add your test function next to related ones, not at the end.
- Visual changes: screenshot dark AND light theme at scale 1, with a long title and a saturated cover where relevant; look at the shots as the owner would (qa/visual-rules.md). Anything that moves: a frame series (`tools/dev/frames.ps1`). Accessibility changes: show the `uia-dump.ps1` lines before/after.
- Startup speed is sacred: the window appears at its first GPU frame (~0.1 s), content fades in once (140 ms). If you touch startup, measure with `LUMEN_TRACE=1` and `tools/dev/frames.ps1` + `frames-check.py`.
- Commit after each finding (or a tight group): write the message to `%TEMP%\lumen-qa\uirfix-<name>\MSG.txt` (UTF-8, no BOM) and `git commit -F` it. First line imperative English summary naming the IDs (e.g. "Keep the control bar while it has focus (A11Y-1)"); end with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push, merge, rebase or switch branches; never commit the junction or build output.

## Final message (your report to the lead)
Plain Russian, short: for every finding ID you own — done / partial / not done, and why; the commits; how you
verified (build, tests, checks, screenshots or UIA dumps you looked at); anything you touched outside your files;
decisions the owner should know.
