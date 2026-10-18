# Round 7: verification of the merged features (r7-verify)

Fresh eyes on the seven round 7 features after the merge.

- **Exe:** `%TEMP%\lumen-qa\r7-m7\Lumen.exe` (main 4b06089, all merged). Nothing was built, and no code was changed.
- **Test instances:** every one ran with `--automation`, a data dir `%TEMP%\lumen-qa\r7vf-*` and `-Library` set to a test folder, and was killed by its own PID.
- **Media:** `%TEMP%\lumen-qa\r7vf-media`: copies of the shared test files plus a few generated ones (`Lyrics\`, `XF\`, `PF\`, `Words\`, `TM\`).
- **Evidence:**
  - `%TEMP%\lumen-qa\r7vf-out\`: captures, crops, logs, `perf.txt`;
  - the sweeps in `%TEMP%\lumen-qa\r7vf-sweep`, `r7vf-sweep-contrast` and `r7vf-sweep-oled`.
- **Scripts:** in the session scratchpad: `vf.ps1` (helpers) and `t-*.ps1` (one per check below).
- **Network:** only two local addresses:
  - the online agent's mock, `qa/round7/online-mock.py` on 127.0.0.1:18765;
  - a closed localhost port, 18799.
  
  No request went to lrclib.net or any other real host. Real LRCLIB requests used: 0 of 3.

## Summary

Interactions come first.

| Id | Severity | Status | Area | Finding |
|---|---|---|---|---|
| V7-01 | Broken | confirmed | crossfade | Crossfade is silently skipped in about 4 of 10 song changes. The fade is decided before mpv reports the duration, is judged "too late", and is never retried for that file. |
| V7-02 | Ugly | confirmed | TMDB × "Битый файл" | A damaged file is looked up on TMDB (a request is sent), then shows a movie backdrop and a "2010 · ★ 8,4" chip. |
| V7-03 | Ugly | confirmed (code + run) | crossfade × per-file speed | After the switch, the outgoing song's tail plays at the incoming song's stored speed. |
| V7-04 | Ugly | confirmed | subtitle words × subtitle size and lift | Changing the subtitle size while paused redraws the words, but their hit area stays where it was until the next layout. |
| V7-05 | Ugly | confirmed (code) | "Очистить историю" × per-file × online | The two clear-history buttons clear different data. Neither confirmation text mentions both per-file settings and online data. |
| V7-06 | Ugly | confirmed | tag parser × player | The library shows the new parser's tags and the player shows mpv's: "Песня в CP1251" in the list, "Ïåñíÿ â CP1251" in the music view. |
| V7-07 | Fragile | confirmed | per-file × deinterlace | Pressing D twice stores `"deinterlace":"no"` for the file. The default is "auto", so from then on the file forces deinterlacing off. |
| V7-08 | Fragile | suspected (code) | toasts × scan banner | Library toasts sit at y = 122 DIP, right on top of the new scan banner (y 110–162). |
| V7-09 | Fragile | suspected (code) | crossfade × per-file audio delay | The crossfade helper doesn't copy `audio-delay`, so a song with a stored delay is out of step with itself during the handoff. |
| V7-10 | Fragile | suspected (code) | toasts with actions | The new "Открыть папку" and "Настройки" buttons on toasts can't be reached from the keyboard, and they disappear after 4–5 s. |

**Checked and fine** (details in "Passed" below):
- per-file flip and speed with the live CPU filter switch;
- embedded SYLT/USLT and `.lrc` lyrics winning over LRCLIB;
- per-file subtitle size with the word picker, across a restart;
- every new screen under the High contrast and OLED skins (sweeps: 0 medium/high);
- the banner under a skin at 150 %;
- a corrupt `file-settings.json`, and a skin file that is broken, then fixed, then deleted;
- every online feature with the network unreachable;
- rapid toggling of every new setting;
- restart persistence;
- performance (no regression with the features off; paused and idle stay at 0).

## Findings

### V7-01 Broken, confirmed: crossfade skipped in about 40 % of song changes

- **Steps.** Use `XF\XF Artist - Album A` (three 24 s MP3s) with these settings:
  - `audio.crossfade` on;
  - `crossfadeSeconds` 3;
  - `crossfadeAlbums` off;
  - visualizer off.

  Open the folder and let it play, polling `state` and `time-pos` every 250 ms (`t-xf2.ps1`).
- **Expected.** The helper starts at about 16 s (+8 threads), and the queue moves to song 2 at 21 s (24 − 3).
- **Actual.** It varies from run to run:

  | Variant | Runs | Faded | Skipped |
  |---|---|---|---|
  | defaults (per-file on) | 4 | 1 | 3 |
  | defaults + debug log | 4 | 3 | 1 |
  | per-file off | 1 | 1 | 0 |
  | per-file + "also remember" | 1 | 1 | 0 |
  | first run, with a seek to 70 % | 1 | 0 | 1 |

  In the skipped runs:
  - no helper starts (threads +1 only);
  - song 1 plays to its end;
  - mpv moves on gaplessly at 24 s.

  The debug log of a skipped run (`r7vf-out\xf-fail-mpv.log`) shows the `loadfile … append` for song 2 at 2.150 s. Then there is no `playlist-next`, only `EOF code: 1` at 25.9 s.
- **Suspected cause.**
  - `Player::CheckCrossfade` (src/Player.cpp:836-843) runs on the first time or state update once the append reply has set `queuedEntry_`.
  - When mpv hasn't reported `duration` yet, `EffectiveFade` returns 0 (src/audio/CrossfadeMath.h:33). `HelperStart` then returns −1, and `DecideArm` gives `TooLate` (CrossfadeMath.h:50).
  - CheckCrossfade stores `xfSerial_`/`xfEntry_` before that check (Player.cpp:840-841), so the file is never armed again.
  - Whether the append reply or the duration arrives first decides the run. That explains why the results vary.
- **Fix.** In `CheckCrossfade`, return before recording `xfSerial_`/`xfEntry_` while `!(state_.duration > 0)`, or make `DecideArm` return `Wait` for an unknown duration. Add a `crossfade_math` unit test: duration 0 → Wait.

### V7-02 Ugly, confirmed: damaged files get TMDB artwork and a rating

- **Steps.** `t-tmdb.ps1`, with TMDB on and pointed at the mock.
  - Library `r7vf-media\TM`:
    - "Inception (2010).mp4" is a text file (damaged);
    - "Holiday at the lake.mp4" is a valid video;
    - after the first session, "The Matrix (1999).mp4" is added (a new file).
  - Open Videos in both sessions.
- **Expected.** A damaged file shows only its "Битый файл" warning. No TMDB lookup is made for it, and it gets no backdrop or rating chip.
- **Actual.**
  - The mock log shows `GET /3/search/movie?query=Inception&year=2010…`, then the backdrop `w780/…jpg`.
  - The damaged card shows the backdrop, "2010 · ★ 8,4" top right and "Битый файл" bottom right, so it looks like a playable film (`r7vf-out\tm-videos.png`).
  - The new and the plain card are right: "Новое" + "1999 · ★ 8,2", and the file's own frame.
- **Cause.**
  - `ItemGrid.cpp:380-381` swaps in `net::online::CardImage` and `ItemGrid.cpp:450-452` draws `net::online::Meta`, both without `!e.damaged`.
  - `net::online::Meta` (src/net/Online.cpp:926-945) also queues the lookup when it is first called from paint.
- **Fix.** Skip both calls for `e.damaged`. Damaged entries then never reach `Meta()`, so no request is queued.

### V7-03 Ugly, confirmed by code and a run: the crossfade tail takes the new song's speed

- **Steps.** `t-xf2.ps1 speed`:
  - per-file settings and "also remember speed…" on;
  - `file-settings.json` stores `speed: 1.5` for "02 A Two.mp3";
  - crossfade 3 s.
- **Observed.**
  - The fade starts at 21.1 s, and main's `speed` goes 1.0 → 1.5 at the switch. That is right for song 2.
  - The helper is still playing song 1's tail until 23.3 s (threads 94 → 87).
- **Cause.**
  - The speed observer calls `SyncCrossfade` (src/Player.cpp:443), which passes `state_.speed` to `Crossfader::SetSpeed` (Player.cpp:870-875).
  - `ApplyRequests` sets it on the helper whenever it changed (src/audio/Crossfader.cpp:422-425), even after `switched_`.
  - So the last 3 s of song 1 jump to 1.5×, and the fade timing in `RunFade` (`elapsed += … * speedApplied_`) runs 1.5× fast too.
  - Not listened to: test instances stay at volume 0.
- **Fix.** Once `switched_` is set, stop mirroring speed to the helper: the tail keeps the plan's speed. Pause, volume and mute still follow main.

### V7-04 Ugly, confirmed: stale hit area of the subtitle words after a live size change

- **Steps.** `t-words.ps1`, `t-words3.ps1`, "Words SRT.mp4", 1280×780.
  1. Pause on a two-line subtitle: the overlay is up, with `mpv-subs-off`.
  2. Press `subtitles.bigger` three times (sub-scale 1.3).
- **Expected.** The words and their hit rects grow with the text.
- **Actual.**
  - The drawn text grows and matches mpv's own rendering. The bright-pixel box is 412–862 px for the overlay and 412–862 px for mpv (`wd-overlay2.png`, `wd-mpv2.png`).
  - The widget stays at `[459,625 362x84]` (the size before the change, `builds=5`).
  - Hover probes: at 640,650 nothing is hit, and a click at 430,650 opens no menu.
  - After a 10 px window resize the rect becomes `[412,603 467x106]` and 640,650 hits word 1.
  - After a restart with the stored per-file `subScale 1.3`, the rect is right at once (`[407,603 467x106]`): only live changes are affected.
  - Any change that rebuilds the line while it stays visible takes the same path, for example `sub-margin-y` (the lift over the bar) or a new line after a seek while paused. Those weren't probed separately.
- **Cause.** `SubtitleWords::Reevaluate` (src/app/player/SubWords.cpp:146-154) calls `Rebuild()`, but `InvalidateLayout()` runs only when `visible` changes. The rect comes from `PlayerView` layout only (`words_->Arrange(words_->Bounds())`, src/app/player/PlayerView.cpp:601).
- **Fix.** Call `InvalidateLayout()` when `Rebuild` changes `bounds_` while visible.

### V7-05 Ugly, confirmed by code: the two "Очистить историю" paths diverge after the merge

- **Settings › Privacy › "Очистить историю".** `SettingsActions.cpp:29` calls `History::Clear` (which clears per-file settings through `onCleared`, LiveState.cpp:229) and `net::online::ClearAll()`.
- **Recent › "Очистить историю…".** `Views.cpp:1118-1123` calls only `History::Clear`. Downloaded lyrics, TMDB metadata and images, and search caches stay.
- **The texts:**
  - the Recent confirmation, "Список недавних файлов и сохранённые позиции воспроизведения будут удалены." (`strings/library.inc:203-204`), mentions neither per-file settings nor online data;
  - the Settings description (`SettingsSchema.cpp:914-915`) mentions per-file settings but not online data.
- **The Recent item** is disabled while the history is empty (`Views.cpp:1124`), even when per-file or online data exist.
- **Fix.**
  - Add one `App::ClearHistory()` that clears history, per-file settings and online data, and call it from both places.
  - Update both texts, e.g. "…, сохранённые позиции, настройки файлов и загруженные из интернета данные будут удалены."
  - Enable the Recent item when any of the three has data.

### V7-06 Ugly, confirmed: player and library disagree on the same tags

- **Steps.**
  1. Play `r7vf-media\Lyrics\01 sylt.mp3`, a copy of r7lb's "09 ID3v2.4 encodings sylt.mp3".
  2. Open Music › Songs.
- **Library row:** "Песня в CP1251", "Artist One; Artist Two" (the new TagReader: 8-bit text through the ANSI code page, a 2.4 list joined with "; ").
- **Music view:** "Ïåñíÿ â CP1251", "Artist One · UTF-16BE Album" (mpv/ffmpeg's Latin-1 reading, and only the first artist).
- **Cause.** The player screens take title, artist and album from mpv's metadata (`PlayerState`). The library now has better data for the same file. This was invisible before round 7, because both sides used to be equally wrong.
- **Fix.** For local files in the library, prefer the cached library tags (title, artist, album) in the music view, the title bar, the mini player and SMTC, with mpv's as the fallback.

### V7-07 Fragile, confirmed: D, D leaves a stored deinterlace override

- **Steps.** `t-pf.ps1`: "also remember…" on. During 20 rapid filter toggles on A.mkv, press D (`video.deinterlace`, `cycle deinterlace`) four times.
- **Result.** It ends at `deinterlace=no`, and the store gets `"deinterlace":"no"` (`r7vf-pf\file-settings.json`).
- **Why that matters.** `DefaultDeinterlace()` is "auto" (video.deinterlace = auto), so `Capture` treats "no" as a user choice (LiveState.cpp:1017-1018). The file now forces deinterlacing off. The quick settings section lists "Деинтерлейсинг: выкл", which the user never chose as such.
- **Fix.** Make `video.deinterlace` cycle between the default and its opposite, or have Capture drop a value equal to what the file opened with.

### V7-08 Fragile, suspected (code): library toasts cover the scan banner

- **Where.** `MainWindow.cpp:122-125` puts library toasts at y = 122 DIP, "below the header". That was written before the banner existed. The banner now occupies `[200,110 653x52]` at 1280×780 (tree dump, `t-scan.ps1`).
- **Effect.** A toast during a long scan (bookmark, playlist and similar) covers the banner's text and progress line for 2–4 s. The toast is passthrough, so the "Скрыть" button still works.
- **Fix.** Have `toastTop` add the banner's height while it is visible.

### V7-09 Fragile, suspected (code): crossfade ignores a stored audio delay

- **Where.** `Crossfader::Create` copies `volume-max, af, audio-device, …` (src/audio/Crossfader.cpp:219-221) but not `audio-delay`. Per-file settings now restore `audio-delay` for songs too.
- **Effect.** Main's output is shifted by its delay, and the helper is aligned by PTS. During the 0.25 s handoff the two play the same audio offset by the delay (an echo), and the tail stays offset.
- **Fix.** Copy `audio-delay` to the helper.

### V7-10 Fragile, suspected (code): toast actions are mouse-only

- **Where.** `ShowToast` with an action (`ui/Controls.cpp:1663-1690`) uses `focusContent = false`, and nothing moves focus to the toast.
- **Effect.**
  - The new "Открыть папку" (snapshot or recording saved) and "Настройки" (can't save) buttons can't be reached with Tab.
  - They close after 4 s or 5 s.
  - WCAG 2.2.1 (timing) and 2.1.1 (keyboard).
- **Fix.** Either of these:
  - add a shortcut while the toast shows, and say it in the tooltip;
  - pause the timer while the toast is hovered or focused, and let F6 or Tab reach popups.

## Passed

| Check | Result | Evidence |
|---|---|---|
| Lyrics priority (lyrics online on, mock) | SYLT song shows "Synced one…", `.lrc` song "Local LRC line one", USLT song "Embedded USLT line one", none of them asks LRCLIB; only the plain song requests `/api/get` and shows the mock lines with "LRCLIB · Сохранить .lrc" | `t-lyrics.ps1`, mock log |
| Per-file × live CPU filter switch | stored flip=1/speed 1.25 on A: `vf=@lflip:hflip`, `d3d11va-copy`; B: `vf=` `d3d11va`, 1×; deblock on A then B, off on B, back to A; 20 toggles at 100 ms + 4 × D: ends `@lflip:hflip`, copy-back; "Disabling filter" / "failed to configure" in the mpv log: 0 | `t-pf.ps1`, `r7vf-pf\lumen.log` |
| Words × per-file × restart | paused with words up and sub-scale 1.3, quit: store holds only `subScale 1.3`; relaunch: `sub-visibility=yes`, sub-scale 1.3, overlay matches mpv (412–862 px both) | `t-words.ps1`, `wd-*.png` |
| Overlay vs mpv | the default line: overlay 464–810 × 637–701, mpv 464–810 × 639–703 | `wd-overlay1.png`, `wd-mpv1.png` |
| "Новое" / "Битый файл" / TMDB chips together | chips don't collide: New top left, TMDB top right, damaged bottom right; the first session marks nothing new | `tm-videos.png` |
| Network unreachable (port 18799 for all three services) | Videos and a song: no toast, cards keep their own frames, no lyrics button; command round trip 16–28 ms; paused 25 s later: CPU 0 ms, TCP 0, threads 89 → 86 (workers exited) | `t-robust.ps1` |
| Corrupt `file-settings.json` | backed up as `file-settings.corrupt-<time>.json`, the next change stored in a fresh file | `t-robust.ps1` |
| Broken skin file, then fixed, then deleted | not JSON: Lumen skin used, line and column in `skin.log`; fixed on disk: re-read within 1.5 s, contrast warnings logged; deleted: "cannot read the file; the Lumen skin is used"; process alive throughout | `t-robust.ps1`, `cor-skin-fixed.png` |
| Rapid toggles while a song plays with the visualizer | 20 × visualizer style, 20 × crossfade, 12 × skin, 10 × word picker, 10 × per-file, 10 × theme: alive, threads 87 → 86, private 97 MB, visualizer running after (`viz=2 frames=42`) | `t-xf.ps1` |
| Restart persistence | visualizer scope, skin sepia, crossfade on, 7 s, word picker off: all in settings.json and back after relaunch (`viz=2`) | `t-xf.ps1` |
| Crossfade + visualizer together | the visualizer keeps drawing through the switch (frames 353 → 369 across it), threads back after the helper | `t-xf.ps1` |
| Worst library (1 417 files) first scan | the banner never flashed (37 dumps in 4 s); idle after 4.2 s with thumbnails | `t-scan.ps1` |
| Banner, 24 000 files, High contrast light, 150 % | "Индексируем медиатеку… 11 604 из 24 000", progress line, "Скрыть"; fits at the smallest size and at 1280 | `banner-contrast-min-0.png`, `banner-contrast-1280.png` |
| The online agent's suggested App.cpp fixes | in main: `file-settings.json` → "per-file settings" in `kWhat`; reset-picture rotates with "0"; the episode carry checks `HasTracks()` | code |

## Sweep and visual review

The filter was `viz|crossfade|perfile|file-settings|words|skin|online|lib-new|lib-damaged|toast|mini-player`, with the mock on port 18765.

| Run | Scenarios | ok | Findings |
|---|---|---|---|
| default skin (`r7vf-sweep`) | 99 | 99 | 4 low (three ellipsized toast paths with a tooltip; one ItemGrid artist name) |
| High contrast (`r7vf-sweep-contrast`, `-NoHitmap`) | 87 | 87 | the same 4 low |
| OLED (`r7vf-sweep-oled`, `-NoHitmap`) | 87 | 87 | the same 4 low |

No medium or high findings.

**The skin runs are new.** The skins agent swept every skin before the other features were merged, so the visualizer, subtitle words, online dialogs, banner, badges and action toasts had never been captured under a skin.

**Visual review.** I reviewed contact sheets (`sheet-contrast1.png`, `sheet-skins2.png`) with the screen descriptions from scenarios.json, against the default-skin captures of the same screens. Then I made full-resolution crops of the LRCLIB button (`crop-lrclib-pair.png`) and the per-file section (`crop-perfile-pair.png`), in two passes: layout, then colour and state. Nothing new beyond the rows above. Notes:
- the online dialogs, the match dialog over Media Information, the per-file section and the TMDB cards take the skin's colours in both themes;
- the music view, the visualizer, the lyrics, the LRCLIB button and the word overlay stay on the fixed dark video palette under every skin (`videoBg` isn't a skin token). That is by design, so it isn't reported as a bug;
- the subtitle word menu sits above the line and clear of the "Пропустить заставку" chip.

## Performance

All in `perf.txt`, from `t-perf.ps1`, `t-pause.ps1` and `t-ff.ps1`.

- **Runs:** library `media`, two runs per variant, alternating.
- **CPU:** process CPU over the window.
- **Memory:** working set / private.
- **Variants:**
  - "r7 off": per-file, word picker, visualizer, crossfade and online all off, skin Lumen;
  - "r7 on": per-file + "also remember", word picker, visualizer spectrum, crossfade, skin sepia, and all three online features with keys, pointed at the closed port (no network).

| | r5-final | r7 off | r7 on |
|---|---|---|---|
| idle library, CPU per 10 s | 0 / 0 ms | 0 / 0 ms | 0 / 0 ms |
| idle library, memory | 114/95, 113/96 MB | 109/99, 109/99 MB | 107/95, 111/99 MB |
| paused 1080p video, CPU per 10 s | 15 / 15 ms | 16 / 0 ms | 0 / 0 ms |
| paused video, memory | 170/297, 171/292 MB | 164/291, 166/295 MB | 165/290, 167/299 MB |
| song playing, CPU per 5 s | 47 / 109 ms | 94 / 47 ms | 422 / 422 ms (the visualizer: ≈ 8 % of a core, as music.md measured) |
| paused song, CPU per 10 s | 0 / 0 ms | 0 / 0 ms | 172 / 0 ms¹ |
| paused song, memory | 133/112, 133/114 MB | 129/114, 129/116 MB | 129/116, 132/118 MB |

¹ Per second after the pause, 3 runs (`t-pause.ps1`): 78–94 ms in the first second (the cover fade-in and the capture thread stopping), then 0 with an occasional 15.6 ms scheduler tick. Paused steady state is 0.

**Startup** (`firstframe.ps1` logic with an own prefix and a private TMP; 3 library and 3 file starts per exe):

| | window shown | first frame | file loaded |
|---|---|---|---|
| r5-final | 54–66 ms (library), 45–47 ms (file) | 87–101 ms / 69–71 ms | 227–252 ms |
| r7-m7 | 45–62 ms / 43–45 ms | 70–89 ms / 64–72 ms | 234–257 ms |

No regression with the features off or on. The cost of the "on" state is the visualizer while it draws, which is expected.

## Not done, and why

- **The audible effect of V7-03 and V7-09.** Test instances stay at volume 0. They are shown by code and timing only.
- **OpenSubtitles download with the network unreachable.** The download needs a click in the search dialog. The online agent's "no server" message was confirmed against the closed port in their run. Here only the background services (lyrics, TMDB) were checked offline.
- **Real LRCLIB requests.** None were needed: the mock covered the lyrics priority.
- **Per-file crossfade with Repeat One or A-B loop.** Not tried.
