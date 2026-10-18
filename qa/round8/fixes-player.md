# Round 8 fixes: player (src/app/player/* without LiveState)

Fixer: r8-fix-player. Branch `worktree-agent-aff7d8bb69a854477`. Findings: qa/round8/raw/player.md,
plus V7-04 and V7-06 from qa/round7/verify.md (the coordinator added them).

Runtime checks ran on background `--automation` instances only: the baseline exe (main at 4ed6a84),
a diagnostic copy of it, and the fixed build. Media were generated into %TEMP%\lumen-qa\r8fp-media:
- q5000.m3u8: 5000 entries over worst\Big;
- words-ass.mkv: .ass track with a left yellow line, \an9 signs, \an1 / \an3 Hebrew, \an9 Arabic;
- slowseek.mkv: 60 s 1080p with one keyframe, so a seek takes seconds;
- words-tiny.mkv: a 48 px line and a 9 px line;
- Lyrics\: a copy of r7vf-media\Lyrics.

| # | Finding | Status | How it was checked, before → after | Where |
|---|---------|--------|-------------------------------------|-------|
| 1 | Queue "Remove" with nothing selected ran one Remove per row (O(n²)) | fixed | 5000-item queue, panel opened before the queue loaded (nothing selected), `click tip=Remove selected`. **Before: 3992 ms UI freeze. After: 17 ms** (a no-op command takes 22 ms). The queue is 1, the playing file stays. | QueuePanel.cpp:736 |
| 2 | ErrorCard::Refresh: GetFileAttributesW on the UI thread | fixed | Code: Refresh runs on every kChangeFile; a failed file on a dead share blocked for the SMB timeout. Now `lib::MaybeExists`, as the library pages do. A dead share isn't reproducible here. | PlayerView.cpp:401 |
| 3 | SubWords lost the .ass styles for the session after one "none" | fixed | Confirmed at runtime as asked: slowseek.mkv playing, seek and pause sent back to back. Diagnostic build: `text-level` went 2 → 0 and stayed there; one block, no \an9, no script styles. mpv answers "none" while it has no playback time. The fallback, `maxLevel_`, `restart_` and `textLevel_` are deleted. **After:** the same burst ends with `blocks=2 ... ass-styles`. | SubWords.cpp:161 |
| 4 | Selection kept when the same text gets fewer words | fixed | words-tiny.mkv, W, End (the last word of the 9 px line), window 720×440, then Smaller subtitles. **Before: Lumen.exe crashed (0xc0000409, Event Log 1000).** After: the 9 px block drops out and the pick goes back to the first word (`sel=0-0 picked="Big"`), no crash. A shared `DropPick()` is used by the rebuild and by ResetInteraction. | SubWords.cpp:390, :843 |
| 5 | RTL lines with left or right alignment on the wrong side | fixed | words-ass.mkv at 15 s, captured. **Before:** \an9 Arabic top-left, \an1 Hebrew bottom-right. At 24 s the \an3 Hebrew bounds were x=33. **After:** top-right, bottom-left and x=928. | SubWords.cpp:539 |
| 6 | Forward/Backward 10 used controls.skipShort; the 15 s buttons skipped the OSD | fixed | Code: the buttons call `SeekBy(±10 / ±15)`, which also shows the position toast. They aren't in the default layout, so there was no runtime click. | ControlBar.cpp:610 |
| 7 | "Skip intro/credits" for "Sonata Op. 27" or "Ed's Story" | fixed | New checks in `player_episodes`: Op. 27, Ed's Story and Ed Sheeran are Other; "OP 2", "02 - OP" and "[ED]" still match. They **fail on the old code (3 failures)** and pass now. | Episodes.h:135 |
| 8 | Speed slider max 2× while the menu has 3× and Faster goes to 4× | fixed | At 4× (Faster ×9), Playback settings: the thumb sits at the end of a 0.25–4 track with 0.25 / 1 / 2 / 3 / 4 labels (capture). | SettingsPanel.cpp:508 |
| 9 | Subtitle style read in three places (lean) | fixed | `ReadSubtitleStyle` and `SubtitleFontSize` in the new SubStyle.h/.cpp. The two copies of HexColor, ChoiceNumber and Style::Read are gone, and so is SubtitleBand's own size table. Net −12 lines. Words and preview were seen drawn at runtime. | SubStyle.cpp; SubWords.cpp:320; SettingsSubtitlePreview.cpp:41; PlayerView.cpp:520 |
| 10 | Every local video started a lyrics worker that parsed the film's tags | fixed | Code: a non-audio file only reads its `.lrc`. Songs are unchanged; the V7-06 run still shows synced SYLT lyrics. | MusicView.cpp:468 |
| L1 | g_menu not cleared | fixed | Code: the pointer outlives the menu, and a later context menu at the same address (the queue's) was closed on the next file change. The kept menu is tagged with an id, and the posted close checks it. | PlayerView.cpp:44 |
| L2 | IsRtl counts U+FEFF as RTL | fixed | Test `subtitle_text`: `!IsRtl(L"\xFEFFHello")`. **Fails on the old code**, passes now (the range ends at U+FEFC). | SubText.h:485 |
| L3 | ReadEmbeddedLyrics makes up to 513 synchronous mpv reads | rejected here (needs Player.cpp) | Real: bounded by 256 tags, and only on metadata changes while there are no lyrics. The small fix is for Player to keep the lyrics tag from the `metadata` node it already observes (Player.cpp:479); that file belongs to the playback fixer. | MusicView.cpp:511 |
| L4 | DurationProbe::known_ only grows | rejected | One entry per distinct file queued in the session (about 100 bytes); a 20 000-file queue costs about 2 MB. It is a cache by design. | QueuePanel.cpp:46 |
| L5 | SortQueue with no current item | rejected | Unreachable: PlayQueue keeps `current >= 0` whenever it has items (Set, Add, InsertNext and Remove all clamp), and SortQueue returns below 2 items. | QueuePanel.cpp:631 |
| L6 | The lookup toast says https only | fixed | Toast: "must start with http:// or https:// and have no spaces", matching what LookupUrl checks. The Russian string is updated too (strings/player.inc). | SubWords.cpp:1040 |
| L7 | Overlong lookup URLs fail silently | fixed | Code: the ShellExecute result was ignored. When it fails (≤ 32), outside automation, a toast says "Couldn't open the link in the browser". A browser can't be opened in tests. | SubWords.cpp:1047 |
| L8 | SubtitleBand ignores the lift | fixed | Code: the next-episode card and the skip button used `min(bottom − bar, bottom − band)`, so over a floating bar they covered the lifted line. They now sit at `bottom − bar − max(20, band + 12)`. | PlayerView.cpp:616 |
| L9 | PiP overlay polls at 8 Hz while paused | fixed | Code: paused, the controls stay up anyway. The poll stops itself and doesn't start while paused; a pause raises the controls, and resuming starts the poll again. | PipOverlay.cpp:54, :70, :113 |
| L10 | ListHeader::compact_ unused; PresetField copies the presets on every paint | fixed | Code: the member is deleted. The name is read on Live().changed; Settings shows "Default Preset" (capture). | QueuePanel.cpp:183; SettingsPanel.cpp:977 |
| V7-04 | Word hit area stale after a live size change | fixed | words-tiny.mkv paused, Bigger / Smaller ×4: the widget rect follows every step (`[505,63 271x639]` → `[557,63 167x638]` and back). Before (verify.md): it stayed until a resize. A changed Bounds() now asks for a layout. | SubWords.cpp:119-125 |
| V7-06 | Player screens show mpv's tags, the library its own | fixed (player screens) | `01 sylt.mp3` in a library. **Before** (verify.md): "Ïåñíÿ â CP1251", "Artist One · UTF-16BE Album". **After:** the music view reads "Песня в CP1251" / "Artist One; Artist Two · UTF-16BE Album". Also switched: the fullscreen top info, the mini player, the bar's artist label, the queue's playing row and the LRCLIB query. `CurrentTags()` (NowPlaying.h) caches per file, mpv tags and snapshot, because the music view paints every frame. | NowPlaying.cpp; MusicView.cpp:205; PlayerView.cpp:653, :909; ControlBar.cpp:522; QueuePanel.cpp:328, :367 |

## Still showing mpv's tags (outside this scope)

These read `PlayerState::title/artist/album` directly. Each one becomes a one-line change to `CurrentTags()`:
- Shell.cpp:204, :209: the window title and the title bar label still show "Ïåñíÿ â CP1251 · Artist One" (seen in the run). Owner: r8-fix-ui.
- SystemIntegration.cpp:413-440: SMTC and the taskbar.
- App.cpp:482: a second into a file, App learns `s.title` into the queue item, so the queue row title goes back to mpv's. Owner: the playback fixer.

## Verification

- Build /W4 into the worktree's build\: no warnings.
- tests\build-tests.bat: 47 tests, 6437 checks, 0 failures.
- check-icons.py: 0 missing. check-settings.py: 0 without a reader. i18n_check.py: 0 conflicts.
- Sweep `-Filter "^(player|pip|mini|queue|panel|music\.)"` (122 scenarios), run on the fixed build and on the baseline:
  - both runs: all 122 ok, 4 low `text-trunc`, the same four widgets;
  - no medium or high finding in either run.

## Touched outside src/app/player

- src/app/settings/SettingsSubtitlePreview.cpp (allowed, #9).
- src/app/strings/player.inc: one changed and one new string (L6, L7).
- tests/core_tests.cpp (allowed).
- PlayQueue.* needed no change.
