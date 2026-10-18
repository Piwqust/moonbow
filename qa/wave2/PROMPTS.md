# Промпты для продолжения волны 2

Каждый агент продолжает свою ветку в её же worktree. Общие правила лежат в
[COMMON.md](COMMON.md): сборка, проверки, фоновые экземпляры и жёсткие запреты. Состояние каждой
ветки и что осталось — в [HANDOFF.md](HANDOFF.md).

**Как запускать:**
- **Изоляция.** Agent tool с `isolation: "worktree"` создаёт **новый** worktree от `main`, в котором
  этой работы нет. Поэтому есть два варианта:
  - запустить агента без изоляции и указать ему путь существующего worktree (ниже);
  - запустить с изоляцией и первым шагом сделать `git merge <ветка>`.

  Проще первый вариант: worktree и ветки на месте.
- **Независимость.** Агенты V и A правят один `SettingsSchema.cpp` (разные разделы), U и P —
  `player.inc` и `core_tests.cpp`. В разных worktree они друг другу не мешают, конфликты решаются
  при слиянии (порядок — в HANDOFF.md).
- **Сторожевой таймер.** Агентов, у которых нет прогресса 600 с, снимает watchdog. Поэтому в каждом
  промпте лимит вызовов и правило: команды короче 90 с, сборка — в фоне.
- **Отчёт.** Сразу после остановки коммитить, а не держать работу незакоммиченной: в этот раз
  пауза застала все четыре ветки без единого коммита.

## Общая шапка

> You are continuing a paused task on Lumen, a Windows media player. Read
> C:\Users\wheny\Desktop\CODE\lumen-player\qa\wave2\COMMON.md and follow it exactly, then read the
> section for your branch in C:\Users\wheny\Desktop\CODE\lumen-player\qa\wave2\HANDOFF.md ("Сделано",
> "Где остановился", "Осталось"). Work ONLY inside the existing worktree <WORKTREE> on branch
> <BRANCH> (`cd` there; never touch the main checkout or other worktrees). The last commit is a WIP
> snapshot of the previous agent: read `git show --stat HEAD` and the diff `git diff main...HEAD` first.
> Commit after every finished step (small commits with real messages), not only at the end.
> Budget about 150 tool calls; every shell command under 90 s; builds in the background.
> Final message: what you finished and how you verified it (screenshots, mpv log lines, CPU/memory
> numbers), what is still open, and changes needed outside your file scope.

## V — качество видео и zero-copy

`<WORKTREE>` = `C:\Users\wheny\Desktop\CODE\lumen-player\.claude\worktrees\agent-a9fcae5cb5708195e`,
`<BRANCH>` = `worktree-agent-a9fcae5cb5708195e`.

> TASK (MARKET.md P0-1, P1-1, P1-2, P1-3, P2-1). Finish and verify:
> 1. With crop, sharpening (GLSL CAS) and deinterlace on, the mpv log shows `hwdec-current` = `d3d11va`
>    (not `d3d11va-copy`); measure CPU on the 1080p test file before/after.
> 2. AI upscaling (`video.superResolution`, d3d11vpp scaling-mode nvidia/intel) and RTX Video HDR
>    (`video.trueHdr`) on this RTX 4080: no errors in the mpv log, graceful no-op without d3d11va.
> 3. Custom shaders (P2-1): a `shaders` folder in the data dir, quick-settings list toggles .glsl/.hook
>    files without restart (`glsl-shaders` change-list); "Open folder" only through ui::Host::ShellOpen;
>    then remove `.Hidden()` from `video.shaders`.
> 4. Screenshots of Settings > Video and the player quick settings (dark/light, ru/en), all checks.
> FILE SCOPE: video parts of src/core/SettingsSchema.cpp (BuildVf, video/performance sections only),
> src/app/player/SettingsPanel.*, src/app/player/LiveState.*, src/Player.* (video option code only),
> res/, src/app/strings/settings.inc and player.inc (your "// --- video quality" block), tests/.

## A — аудио, субтитры, скрытые пункты

`<WORKTREE>` = `C:\Users\wheny\Desktop\CODE\lumen-player\.claude\worktrees\agent-ae76cc3ec058ebb23`,
`<BRANCH>` = `worktree-agent-ae76cc3ec058ebb23`.

> TASK (MARKET.md P0-2, P1-6 audio, P1-7, P1-8, N-2 local). Finish and verify:
> 1. Passthrough (`audio-spdif`), exclusive mode, gapless + prefetch in an audio queue: confirm the
>    options in the mpv log; EQ still works together with every normalization mode (BuildAf).
> 2. Subtitle background box, opacity, blur, bottom margin, black bars, secondary position:
>    screenshots on the test file with subtitles.
> 3. yt-dlp: hint when yt-dlp.exe is absent; with it absent ytdl stays off. Never download yt-dlp.
> 4. List every previously hidden setting you implemented and every one still hidden (with the reason)
>    in your final message; all checks.
> FILE SCOPE: audio/subtitle/network/playback/general sections of src/core/SettingsSchema.cpp (BuildAf;
> stay out of BuildVf and video sections), src/core/Config.*, src/Player.* (audio/subtitle/ytdl option
> code only), src/app/Actions.cpp (seek step), src/app/dialogs/Dialogs.cpp, src/app/strings/settings.inc
> and dialogs.inc (your "// --- audio and subtitles" block), tests/.

## U — UX плеера

`<WORKTREE>` = `C:\Users\wheny\Desktop\CODE\lumen-player\.claude\worktrees\agent-ad20c2736c1c47044`,
`<BRANCH>` = `worktree-agent-ad20c2736c1c47044`.

> TASK (MARKET.md P0-3, P1-5, P0-6 player part, P2-2, P2-3). Finish and verify:
> 1. Replace the own episode parser in src/app/player/Episodes.h with library::ParseEpisode
>    (src/library/MediaTypes.h); one parser only.
> 2. Missing gestures: wheel over volume, right-click on volume = mute, wheel over the seekbar = 5 s per
>    notch; carry the chosen audio/subtitle languages to the next episode.
> 3. Long press = 2× must not break single/double click or the controls.* mouse settings; CPU with the
>    stats overlay hidden equals main's (qa/perf/perf.ps1); screenshots of the stats overlay, the
>    "Next episode" card and "Skip intro" (make a test clip with chapters named Intro/Credits via ffmpeg
>    into %TEMP%\lumen-qa\<yourname>-media).
> 4. The settings you read with config.GetBool(...) (stats overlay, thin progress line) are not declared
>    in SettingsSchema.cpp: list the exact schema lines to add (ru/en labels, defaults) in your final
>    message — do not edit SettingsSchema.cpp yourself.
> FILE SCOPE: src/app/player/PlayerView.*, ControlBar.*, PanelKit.*, QueuePanel.*, TracksPanel.*,
> PlayerOverlays.*, Episodes.h, new files in src/app/player/, src/app/Actions.*, src/app/strings/player.inc
> ("// --- player ux" block), tests/.

## P — превью, тексты песен, раскладка клавиш

`<WORKTREE>` = `C:\Users\wheny\Desktop\CODE\lumen-player\.claude\worktrees\agent-a15e58971e4bd552a`,
`<BRANCH>` = `worktree-agent-a15e58971e4bd552a`.

> TASK (MARKET.md P0-4, P1-6 lyrics, P1-9). Finish and verify:
> 1. The idle-shutdown race in SeekPreview.cpp (mark not-running and clear state under one lock before
>    closing the instance); then hover-to-first-thumbnail time, CPU while scrubbing, process memory on
>    1080p and a 4K 4:2:0 clip you generate with ffmpeg — before (main) and after (LUMEN_TRACE writes
>    %TEMP%\lumen-preview.txt).
> 2. Lyrics on the music view (MusicView.*): current line highlighted, smooth scroll, click a line to
>    seek, unsynced text static, hidden when none; sources: `<track>.lrc` and the embedded tag via the
>    mpv `metadata` property; parse at track load only.
> 3. "YouTube" key preset in the shortcuts editor (ShortcutStore.*, SettingsShortcuts.*) with
>    confirmation; add "jump to N %" actions as a minimal append at the end of the Action enum in
>    src/app/Actions.h/.cpp (branch U edits that file too — keep the change small and separate).
> FILE SCOPE: src/app/player/SeekPreview.*, MusicView.*, Lyrics.*, src/app/settings/ShortcutStore.*,
> SettingsShortcuts.*, src/app/strings/player.inc and settings.inc ("// --- previews and lyrics",
> "// --- key presets"), tests/, the minimal Actions.* addition.
