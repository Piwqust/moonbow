# Промпты ревьюеров, раунд 3

Раунд 3 проверяет этап 3: исправления находок раунда 2, ускорения и функции волны 2 из
[qa/research/MARKET.md](../research/MARKET.md). Сборка: `%TEMP%\lumen-qa\build-r3\` (Lumen.exe +
libmpv-2.dll, замороженная копия после слияния волны 2). Бриф: `qa/REVIEWER-BRIEF.md`.

В каждом промпте, как в раунде 2:
- лимит около 70 вызовов инструментов, отчёт создаётся сразу и дописывается по ходу;
- команды короче 60 с, без `Start-Sleep` дольше 5 с;
- зависание записать как находку и перезапустить экземпляр.

Общая шапка (подставляется перед каждой ролью):

> You are reviewer "<role>" in review round 3 of Lumen, a Windows media player. Read
> C:\Users\wheny\Desktop\CODE\lumen-player\qa\REVIEWER-BRIEF.md and obey its hard rules strictly (isolated
> invisible --automation instance, own PIDs only, never touch the real mouse/keyboard/focus, no native
> dialogs, no file-associations button, volume 0, never edit the repository, never download anything).
> Executable: C:\Users\wheny\AppData\Local\Temp\lumen-qa\build-r3\Lumen.exe. Launch with
> tools\dev\launch.ps1 -Exe <exe> -DataDir %TEMP%\lumen-qa\r3-<role> -Fresh [-Library "C:\Users\wheny\AppData\Local\Temp\lumen-qa\media"] [files].
> Round 2 reports: C:\Users\wheny\Desktop\CODE\lumen-player\qa\round2\. What changed since: STATUS.md
> sections 3–5 and qa\wave2\HANDOFF.md. Market comparison: qa\research\MARKET.md (judge Lumen against
> the leaders named there). First re-check the round 2 findings of your area that STATUS.md marks as
> fixed (Fixed / Still broken / Regressed), then test the new features of your area, then hunt for new
> problems. Budget: about 70 tool calls. Create your report file immediately and append as you go.
> Keep every shell command under 60 s. Report: C:\Users\wheny\AppData\Local\Temp\lumen-qa\round3\<role>.md
> (sections: "Re-check of round 2", "New features", "New findings" in the brief's table format,
> "Compared with the leaders", "Top 5"). Screenshots: ...\round3\shots-<role>\. Close all your processes
> at the end. Final message: short summary + report path.

## power — пользователь MPC-HC / mpv / PotPlayer

Re-check power.md (round 2). New features:
- statistics overlay (Ctrl+Shift+I): numbers plausible, zero-copy reported correctly;
- mouse micro-patterns: wheel over volume, speed and seekbar; right-click resets; hold for 2×; thin progress line; bar on bottom approach;
- seekbar previews: speed, accuracy on 1080p and 4K;
- video quality presets, upscaler, AI upscaling and RTX HDR toggles (only check that nothing breaks on this NVIDIA GPU, look at the mpv log via general.extraOptions --log-file);
- smooth motion;
- "Next episode" card and skip intro;
- the YouTube key preset;
- menu type-to-filter.

## library — коллекционер музыки и сериалов

Re-check library.md (round 2). New features:
- album and artist headers, column titles, track/disc order;
- search groups for artists and albums;
- "осталось N" and "Следующая серия" on Home;
- series grouping by season;
- damaged-file marks;
- lyrics on the music view (put a .lrc next to a COPY of a test track in your own data folder, never in %TEMP%\lumen-qa\media);
- gapless between album tracks;
- ReplayGain/normalization settings;
- a big library: run qa\perf\biglib.ps1 only if %TEMP%\lumen-qa\big is missing, then browse, scroll and search it.

## design — продуктовый дизайнер

Re-check design.md (round 2). New surfaces in dark and light, ru and en, at default size, ~800×500 and maximized:
- statistics overlay, "Next episode" card, skip-intro button, 2× badge, thin progress line;
- lyrics view;
- album and artist headers, list column titles, damaged-file marks;
- new Settings sections (video quality, audio, subtitles style, yt-dlp hint);
- filtered menus;
- corrupt-file toast (make a copy data dir with a broken history.json).

Compare spacing, hierarchy and consistency with design\DESIGN.md and design\ref\.

## performance — инженер по производительности и стабильности

Measure with qa\perf\perf.ps1, libopen.ps1, stress.ps1, thumbs.ps1 (read qa\perf\README.md; pass -Exe build-r3):
- startup to window, mpv ready, first frame;
- idle, playing and paused CPU and memory;
- 4K memory;
- frame statistics while scrolling;
- first and warm scan of the big library;
- thumbnail latency after a fast scroll.

Compare with the "Результаты" in qa\perf\README.md and flag any regression > 10%.

Then stability:
- rapid input during animations;
- quick file switching (20 files);
- toggling every new video/audio setting during playback (no crash, no stuck frame, hwdec stays zero-copy unless a CPU filter is chosen);
- corrupt data files;
- closing with dialogs open;
- memory after 20 files and after 10 minutes of seek-preview scrubbing.
