# Audit raw findings: playback core, queue, lifecycle, shell integration (reviewer A1)

1. **must** — Windows shutdown or logoff loses the resume point. Nothing handles WM_QUERYENDSESSION/WM_ENDSESSION, and watch-later is written only in Open, Stop and Shutdown. Up to 3 s of store changes and the pending per-file save are lost too.
   - Where: MainWindow.cpp:441, SystemIntegration.cpp:309, Player.cpp:783-789, :186.
   - Fix: on WM_ENDSESSION(TRUE): BeginQuit, the Shutdown thread, pump up to about 3 s, FlushStores plus the per-file flush. (checked)
2. **should** — Several files opened together from Explorer replace each other, because each process forwards one file and each Open() overwrites the last.
   - Where: App.cpp:908-924, :671-675, main.cpp:136-141.
   - Fix: append a forward that comes within about 1 s of the last one. (path checked; the Explorer behaviour is likely)
3. **should** — On takeover, config_ and ui_ are not reloaded. This is the same as storage #4, plus ui.json (presets, layout).
   - Where: App.cpp:280-298.
   - Fix: config_.Load(); ui_.clear(); LoadUiState(). → given to r8-fix-storage together with storage #4. (checked)
4. **should** — Turning subtitles or audio on or off in Settings is counted as the user's own track choice for the current file, through Player::SetProperty → CountTrackChoice.
   - Where: App.cpp:213-214, Player.cpp:558-560, LiveState.cpp:975-995, :844-845.
   - Fix: a non-counting setter for OnConfigChanged. (checked)
5. **should** — BuildFolderQueue has its own 8-extension audio list without .ac3/.dts, so Movie.ac3 is queued after Movie.mkv.
   - Where: App.cpp:641-657.
   - Fix: use library::KindOf. (checked)
6. **should** — Crossfade ignores A-B loops: playlist-next breaks a loop near the end of the song.
   - Where: Player.cpp:830-864, Crossfader.cpp:307-336.
   - Fix: don't arm, and cancel, while ab-loop-b is set. (checked)
7. **nice** — UI timers (areaTimer_, vfTimer_, the sleep timer) can call mpv while Shutdown destroys it on the other thread.
   - Where: MainWindow.cpp:169-185, Player.cpp:791-809.
   - Fix: take mpv_ out on the UI thread before handing it to the shutdown thread. (checked; hitting it is rare)
8. **nice** — The crossfade is skipped for good when the duration isn't known yet at FILE_LOADED.
   - Where: Player.cpp:839-844.
   - Fix: return without recording when duration <= 0. (likely)
9. **nice** — Crossfader::AlbumOf uses the shell property store, not library::ReadTagsFromFile, so .ape/.wv/.opus fall back to the folder.
   - Where: Crossfader.cpp:32-44, :128-131. (checked)
10. **nice** — Every volume step rebuilds @lstereo, because audio.* matches by prefix.
    - Where: LiveState.cpp:218, :628-637.
    - Fix: a RewritesAf(key) check like RewritesVf. (checked)
11. **nice** — Crossfader sets switched_ after playlist-next, so the race cancels the fade.
    - Where: Crossfader.cpp:332-334.
    - Fix: set it before the command. (likely)
12. **nice** — The taskbar thumbnail button tooltips are not translated.
    - Where: SystemIntegration.cpp:141, :159. (checked)

Left out (14):
1. A WM_COPYDATA file accepted just before the close is dropped (App.cpp:909).
2. A stored per-file aid/sid overrides "enable audio/subtitles = off" (LiveState.cpp:844-845).
3. Z/Shift+Z zoom under a zoom mode is not remembered (LiveState.cpp:1010-1011).
4. Synchronous mpv property reads on the UI thread in Actions.cpp; LiveState already observes most of them.
5. Crossfader::Handoff holds mu_ during a synchronous mpv_get_property.
6. PlayQueue::Add with shuffle is O(n·m).
7. Clear history and Reset for this file don't reach the already appended next file.
8. Two near-copy quiet-mpv option lists (Crossfader.cpp:191 vs SeekPreview.cpp:313).
9. FileAssociations kVideo/kAudio is a fourth extension list, missing .mka/.ape/.m4b.
10. Dead: PlayQueue::TotalDuration.
11. Dead: LoopbackCapture::channels_.
12. Dead: CrossfadePlan::paused.
13. Stale comment: Player.h:129-130.
14. Stale comment: Crossfader.cpp:32.
