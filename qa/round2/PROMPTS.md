# Промпты ревьюеров, раунд 2

Раунд 2 проверяет исправления раунда 1 и ищет новое. Сборка: `%TEMP%\lumen-qa\build-r2\`
(Lumen.exe + libmpv-2.dll), бриф: `%TEMP%\lumen-qa\REVIEWER-BRIEF.md` (копия — `qa/REVIEWER-BRIEF.md`).

В раунде 1 ревьюеры зависали и их убивал watchdog («no progress for 600s»), поэтому в каждом
промпте:
- лимит около 70 вызовов инструментов, отчёт пишется с первого шага и дописывается;
- каждая команда короче 60 секунд, никаких долгих ожиданий и `Start-Sleep` дольше 5 с;
- если окно не отвечает, записать находку «зависание» и перезапустить экземпляр, а не ждать.

Общая шапка (подставляется перед каждой ролью):

> You are reviewer "<role>" in review round 2 of Lumen, a Windows media player. Read
> C:\Users\wheny\AppData\Local\Temp\lumen-qa\REVIEWER-BRIEF.md and obey its hard rules strictly (isolated
> instance, own PIDs only, never touch the real mouse/keyboard/focus, no native dialogs, no file-associations
> button, volume 0, never edit the repository). Executable: C:\Users\wheny\AppData\Local\Temp\lumen-qa\build-r2\Lumen.exe.
> Launch with tools\dev\launch.ps1 -Exe <exe> -DataDir %TEMP%\lumen-qa\r2-<role> -Fresh [-Library "C:\Users\wheny\AppData\Local\Temp\lumen-qa\media"] [files].
> Round 1 reports are in C:\Users\wheny\Desktop\CODE\lumen-player\qa\round1\ (power.md, library.md, design.md,
> stability.md). First re-check the round 1 findings of your area (mark each Fixed / Still broken / Regressed),
> then hunt for new problems. Budget: about 70 tool calls. Create your report file immediately and append as you go.
> Keep every shell command under 60 s; never wait long on anything. Report: C:\Users\wheny\AppData\Local\Temp\lumen-qa\round2\<role>.md
> (sections: "Re-check of round 1", "New findings" in the brief's table format, "Top 5"). Screenshots:
> ...\round2\shots-<role>\. Close all your processes at the end. Final message: short summary + report path.

## power — пользователь MPC-HC / mpv

Re-check power.md P01–P26. New: keyboard-only use of the whole player (every default shortcut in
src\app\Actions.cpp), seeking edge cases, track switching, fullscreen/PiP transitions, error cards for
fake/broken/missing files and the .m3u, resume and auto-next in Series S01, startup/CPU/memory.

## library — коллекционер музыки и сериалов

Re-check library.md RAW-1…RAW-41. New: Home, Videos, Music (Songs/Albums/Artists/Genres), Favorites,
Playlists (create via the New playlist dialog by typing only), Recent/Continue watching, search (Latin and
Cyrillic), keyboard navigation, the mini player while browsing, light theme and English. Never click Add
folder / Open file / Import / Export / Show in Explorer; never play the radio stations.

## design — продуктовый дизайнер

Re-check design.md D01–D32. New: every surface in dark and light theme and in Russian and English, at the
default size, ~800×500 and maximized; compare with C:\Users\wheny\Desktop\CODE\lumen-player\design\ref\
(ignore the orange hue). Exact pixel-level notes.

## stability — QA и настройки

Re-check stability.md S-01…S-12 and the settings table. New: ~30 settings across categories (change in the UI,
check settings.json, visible effect, persistence after restart), rapid input, quick file switching, corrupt
settings.json / ui.json / library.json / history.json in a copy data dir, closing with dialogs open,
tiny window sizes, shutdown time, CPU idle/playing/paused, memory after 20 files.
