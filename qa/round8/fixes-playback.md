# Round 8: playback fixes (r8-fix-playback)

Findings: `qa/round8/raw/playback.md` (all except #3, which r8-fix-storage took), the 14 left-out items, the round 7 verify rows V7-01/03/07/09, and three tag follow-ups from r8-fix-player. Branch `worktree-agent-a4178a5da501357f7`, merged with main at `508dac3`.

How it was checked:
- **Base** is `git archive 8e48f42`, built into `%TEMP%\lumen-qa\r8fb-base\build`. **Fixed** is the worktree's `build\Lumen.exe`.
- Every run used a background `--automation` instance with a data dir `%TEMP%\lumen-qa\r8fb-*` and volume 0. Each run had its own `TMP`, so its `LUMEN_TRACE` file (the `xfade[...]` lines) was private.
- The scripts are in the session scratchpad: `r8fb-run.ps1`, `r8fb-many.ps1`, `r8fb-endsession.ps1`, `r8fb-coldclose.ps1`, `r8fb-forward.ps1` and `r8fb-bench\`. Outputs are in `%TEMP%\lumen-qa\r8fb-out`.
- Generated media is in `%TEMP%\lumen-qa\r8fb-media`: `AC3\` (Movie.mp4 + Movie.ac3), `WV1\`/`WV2\` (WavPack songs that share an album tag) and `LYR\` (an .mka with a LYRICS tag).

## Numbered findings

| # | Status | Before → after (how checked) | Where |
|---|---|---|---|
| 1 | fixed | **Windows session end.** Tested with SendMessageTimeout of WM_QUERYENDSESSION and then WM_ENDSESSION(TRUE, LOGOFF), the same messages Windows sends. The process was killed the moment the handler returned, the way Windows ends it. The instance had a paused video at 120.6 s and had changed `general.accent` just before. **Base** (2 runs): no watch-later file, the setting lost, no history position. **Fixed** (4 runs): watch-later `start=120.6`, `general.accent=steel` and the history position 120.68 were all written, in a handler that took 129–150 ms. The handler runs the usual close (SendMessage WM_CLOSE, so the window hides and mpv shuts down on its thread) and then FlushStores and `Live().FlushFileSettings()`. It then waits up to 3 s for `Player::Stopped()`, answering sent messages only (mpv's video window is our child), so the close stays unchanged. Round 5 audit #3 still holds: 3×WM_CLOSE during the cold start on 4K Mandelbrot exited 0 in 12 of 12 runs. With the close at +150 ms and +500 ms it was 6/6 each, and with WM_ENDSESSION during the cold start 8/8. | App.cpp:912 `EndSession`; MainWindow.cpp:471; Player.h `Stopped`; LiveState.cpp:1065 |
| 2 | fixed | **Explorer "open several files".** One process per file, all started together, on one data dir and without `--new-instance`. **Base:** the last file's folder queue (queue=5, Chaptered Demo playing); the other two were lost. This happened both with an instance already running and from a cold start. **Fixed:** queue=3 with exactly those files, and the first one to arrive plays (both cases). The files also join with a 400 ms gap between launches. With a 1500 ms gap they count as separate opens, as before. Forwards within 1 s of each other are collected. When the first one already plays, the queue grows around it and the file isn't opened again. Command-line files count as the start of the group. | App.cpp:952 `OnCopyData`, :348 `Run` |
| 3 | — | Given to r8-fix-storage. | — |
| 4 | fixed | Turning `subtitles.enabled` off and on, and then `audio.enabled`, in Settings while a file plays (per-file on). **Base** stored `"aid":1,"sid":1` for the file. **Fixed** stored nothing. OnConfigChanged now uses `Player::ApplySetting`, which doesn't count the change as the user's track choice. | App.cpp:217; Player.cpp `ApplySetting` |
| 5 | fixed | Opening `AC3\Movie.mp4`: **base** queue=2 (Movie.ac3 queued, and it sorts first). **Fixed:** queue=1. Opening `Movie.ac3` on the fixed build queues only the audio. The fix uses `library::KindOf` in place of the 8-extension list. | App.cpp:658 |
| 6 | fixed | Crossfade 3 s with a per-file A-B loop of 5–23 s on a 24 s song. **Base** (2/2): armed, switched to song 2 at 21 s, and the loop was gone (`ab-loop-b=no`). **Fixed** (2/2): never armed, stayed on song 1 and looped (time-pos 10 s at 28 s). Player observes `ab-loop-a/b`, doesn't arm while either is set, and cancels a fade when one gets set. | Player.cpp:479, :883 |
| 7 | fixed (code) | `App::BeginQuit` calls `Player::Detach` on the UI thread. It moves mpv_ and the crossfader out of the UI's reach, so every later UI-side call is a no-op (the timers' SetProperty, Command, GetProperty...). `Shutdown` then destroys the detached handle. During the cold start the init thread still owns mpv_, so Detach leaves it alone and Shutdown joins first, as before. Close timings: see #1. The race itself is too rare to reproduce. | Player.cpp:824, `Shutdown`; App.cpp:905 |
| 8 | fixed | `DecideArm` returns Wait while the duration is unknown, so CheckCrossfade never records a give-up for the file. A `crossfade_math` check is added and fails on the base: 2 failures, `DecideArm(0,0,…)` gave TooLate. Same as V7-01 below. | CrossfadeMath.h:50; tests/core_tests.cpp `crossfade_math` |
| 9 | fixed, partly shown | `AlbumOf` reads Lumen's tag reader first and falls back to the property system (Matroska). It couldn't be shown on this PC: it has a codec pack with property handlers for .wv/.ape/.opus/.mpc/.tta. Both builds read "Shared Album" from `WV1\one.wv` and `WV2\two.wv` (in different folders) and logged "same album, no crossfade". | Crossfader.cpp:38 |
| 10 | fixed | mpv log at debug level, 5× `set audio.lastVolume` (the key a volume step writes) and one `set audio.normalize`. **Base:** 7 `af` commands. **Fixed:** 1 (normalize, which really rewrites af). `ApplyAudioFilters` now runs only for settings whose mapping writes `af` (`Rewrites(key, "af")`, merged with the vf check). | LiveState.cpp:228 |
| 11 | fixed (code) | `switched_` is set under the lock before `playlist-next`. The window was tiny, so it was never caught at runtime. All crossfade runs below still fade. | Crossfader.cpp:335 |
| 12 | fixed (code) | The thumbnail tooltips use `Tr("Previous"/"Play"/"Pause"/"Next")`. `SystemIntegration::LanguageChanged` updates all three buttons on a language change. One `FillButtons` builds them for both add and update. The taskbar buttons don't exist under `--automation`, so this was checked in code only. | SystemIntegration.cpp:136, :310 |

## Left-out items

| # | Status | Check | Where |
|---|---|---|---|
| 1 | fixed (code) | WM_COPYDATA is refused when a WM_CLOSE is already queued (`PeekMessage PM_NOREMOVE \| PM_QS_POSTMESSAGE`, posted messages only), so the sender opens the file itself. The race window is microseconds wide, so it was not reproduced. | MainWindow.cpp:502 (outside my MainWindow hunk, one condition) |
| 2 | fixed | Stored `aid:1, sid:1` with sound and subtitles off in Settings. **Base:** opened with `aid=1 sid=1`. **Fixed:** `aid=no sid=no`. FixTracks doesn't re-add sidecars either while the kind is off. | LiveState.cpp:858, :944 |
| 3 | fixed | Stored `zoomMode=double`, then Z. **Base** stored only the mode, and the reopen gave `video-zoom=1`. **Fixed** stores `"zoom":1.1` with the mode, and the reopen gives 1.1 with `video-unscaled=yes`. | LiveState.cpp:1030, FileOptionsFor |
| 4 | **rejected** | I switched Actions to LiveState's observed values, then reverted it. The observed values lag behind the async sets while mpv is busy: after two rotates, 2× Z gave `video-zoom=0.1` in 1 of 3 runs, while the base gave 0.2 in 3/3. The synchronous read is the only value that is up to date, and it happens once per key press. | Actions.cpp (unchanged) |
| 5 | fixed (code) | The Handoff path check runs before `mu_` is taken, so `Cancel()` on the UI thread never waits on a synchronous mpv read. The trace MainPts read also moved out of the lock. | Crossfader.cpp:325-340 |
| 6 | fixed | Shuffled Add is one merge pass: the new items in random order, interleaved uniformly with what is still to play. Measured by adding 20 000 to a shuffled 20 000: **base 13.9 ms, fixed 2.4 ms** (`r8fb-bench`). A `play_queue` check (the old items keep their order after the current one, and every item is walked once) passes on both. | PlayQueue.cpp:33-50 |
| 7 | fixed (code) | Clear history and Reset for this file call `Player::RequeueNext`, so the next file already in mpv's playlist is appended again with its options as they are now. | Player.cpp:670; LiveState.cpp:1111, :1127 |
| 8 | fixed | One `SetQuietMpvOptions` (Player.h/.cpp) is used by the crossfade helper and the seek preview; each keeps only its own extras. Crossfade runs and the sweep's seek preview scenarios are unchanged. | Player.cpp:102; Crossfader.cpp `PrepareHelper`; SeekPreview.cpp:313 |
| 9 | fixed (code) | The association list adds .mka .m4b .ape .wv .aiff. I didn't press the button (hard rule). | FileAssociations.cpp:22 |
| 10 | fixed | Dead `PlayQueue::TotalDuration` removed (grep: no users). | PlayQueue.h/.cpp |
| 11 | fixed | Dead `LoopbackCapture::channels_` removed. | LoopbackCapture.h |
| 12 | fixed | Dead `CrossfadePlan::paused` removed (it's armed only while playing). | Crossfader.h, Player.cpp |
| 13 | fixed | The stale `start` comment on `Player::Open` is rewritten. The unused synchronous branch of `Player::Cmd` is removed too: Shutdown now calls mpv_command on its own handle. | Player.h |
| 14 | fixed | The stale AlbumOf comment is fixed together with #9. | Crossfader.cpp:32 |

## Round 7 verify rows (from the coordinator)

| Row | Status | Check | Where |
|---|---|---|---|
| V7-01 | fixed | Same as #8, with the unit test. XF album, crossfade 3 s, album rule off: the **fixed build crossfaded in 15 of 15** runs, opened three ways (from the command line, by `open` after start, and with an early jump to 70 %). The base faded in 15/15 here too, so the race didn't reproduce on this PC. The cause is confirmed in code and by the test that fails on the base. | CrossfadeMath.h:50 |
| V7-03 | fixed | Song 2 has a stored speed of 1.5. **Base:** the tail ran 2.0 s from the switch to the end (3 s of song at 1.5×). **Fixed:** 2.96 s. After the switch the helper no longer takes main's speed; pause, volume and mute still follow it. | Crossfader.cpp:428 |
| V7-09 | fixed (code) | `audio-delay` is copied to the helper with the other audio options. It can't be heard at volume 0. | Crossfader.cpp:219 |
| V7-07 | fixed | D, D with "also remember" on. **Base:** auto → no → yes, and `"deinterlace":"yes"` stored. **Fixed:** auto → no → auto, nothing stored (`files: []`). D now toggles between the default and its opposite (`LiveState::ToggleDeinterlace`), and the toast says the state ("Deinterlace: Off/On/Auto"). | LiveState.cpp:627; Actions.cpp:674 |

## r8-fix-player follow-ups

| Item | Status | Check | Where |
|---|---|---|---|
| SMTC, tray tip, track notification use `CurrentTags()` | done (code) | The title is computed only on state, file or meta changes, not on time updates. SMTC doesn't exist under `--automation`. | SystemIntegration.cpp:426 |
| The queue item learns its title from `CurrentTags()` | done | The CP1251 song from `r7lb-media\MP3` shows "Песня в CP1251" in the queue on the fixed build. The base showed it too in this run, so a before/after can't be shown here. | App.cpp:497 |
| Lyrics tags kept from the observed `metadata` node | done | Player keeps `PlayerState::lyricsTags` (keys passing `IsLyricsTag`). `MusicView::ReadEmbeddedLyrics` reads them instead of up to 513 synchronous mpv reads. `LYR\mka-lyrics.mka`, whose lyrics only mpv's metadata carries, shows "Matroska lyric alpha" in the music view. | Player.cpp:509; MusicView.cpp:519 |

## Checks

- Build at /W4: no warnings (after the merge with main).
- `tests\build-tests.bat`: 47 tests, 6521 checks, 0 failures.
- `check-icons`: 0 missing. `check-settings`: 0 of 241 settings without a reader. `i18n_check`: 0 conflicts, 967 keys.
- Sweep `-Filter "^(player-audio|player-lyrics|player-video|player-paused|panel-quick-perfile|panel-queue|panel-tracks)"`, 59 scenarios: base 2 low, fixed the same 2 low (`panel-queue-worst`, `player-audio-worst.1024x640`). No medium or high finding.
- CPU: idle library 0 ms per 5 s; a paused 1080p video 0 ms per 5 s. No new timers. The EndSession wait loop runs only during a session end.

## Outside my file scope

- `src/app/MainWindow.cpp`: besides the WM_ENDSESSION case, one condition in the WM_COPYDATA case (left-out 1).
- `src/app/player/MusicView.cpp`: the ReadEmbeddedLyrics hunk, as the coordinator asked.
- `src/app/strings/player.inc`: "Deinterlace toggled" is no longer used. I left it for whoever owns the strings.
