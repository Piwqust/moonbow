# Round 7: per-file settings (r7-perfile)

MARKET.md P2-4, as in SMPlayer: Lumen remembers what you changed for a file and puts it back the next time that file opens.

Branch `worktree-agent-a551b62996672a6d2`, on top of main 4f1b361. Base exe for comparison: the same tree before the change, built into `build-base\`. Evidence: `%TEMP%\lumen-qa\r7pf-out\` (logs, dumps, screenshots), `%TEMP%\lumen-qa\r7pf-sw-base\` and `r7pf-sw-new2\` (sweeps). Test media: `%TEMP%\lumen-qa\r7pf-media\` (A.mkv and B.mkv, each with two audio tracks, two embedded subtitle tracks; A also has a sidecar `A.sidecar.srt`). Every instance ran with `--automation` and `-Library` pointing to the test folder, and was killed by its own PID.

## What was built

**What is remembered.**

| Setting | What it covers |
|---|---|
| "Запоминать настройки для каждого файла" (`playback.perFile`, on by default) | the audio, subtitle and secondary subtitle track, the audio and subtitle delay |
| "Запоминать также скорость, изображение и положение субтитров" (`playback.perFileAll`, off by default, greyed out while the first is off) | also speed, aspect ratio, zoom (the 1:x modes and free zoom), pan, rotation, flip, deinterlacing, the A-B loop, subtitle position and size |

Only what differs from the defaults is stored. Setting a value back to its default removes it. A file with nothing left is removed from the store.

**Store.** The data lives in `library/FileSettings` (`src/library/FileSettings.{h,cpp}`) in `<data>\file-settings.json`. It is a separate store, because watch-later and history did not fit:
- mpv's watch-later deletes the file's entry when the file plays to the end, and works only while "remember position" is on;
- watch-later is keyed by a hash, so it can't list or clear entries;
- `history.json` holds only 500 entries.

The store's rules:
- **Safety.** The same rules as the other stores. On a read error (a locked file) it becomes read-only and is never overwritten. A file of the wrong shape (broken JSON, `{"files":{}}`, `[1,2]`) is backed up as `file-settings.corrupt-<time>.json`, and the store starts empty.
- **Values passed to mpv.** Strings are checked before they reach mpv: the aspect may contain only `0-9 . :`, and the zoom mode and deinterlace mode must come from a fixed list. A hand-edited `"aspect":"16:9,script=x.lua"` therefore can't inject options.
- **Size.** At most 5000 files; the least recently used (changed or opened) go first. Lookup is an `unordered_map` keyed by `PathKey`, so it is case-insensitive and treats `/` and `\` alike. The JSON is compact.
- **Writes.** A change is captured 300 ms after it happens, then written 2 s later. A pending change is captured at exit (`~LiveState`), and the store is flushed then.
- **Load.** The file is read on a one-shot thread started when the window is created. `Store()` joins that thread at the first file open. A full store takes 9-17 ms to parse, and the thread finishes before mpv is ready, so the UI thread never waits.

**Restore** (`LiveState::FileOptionsFor`, `Player::Open`/`QueueNext`):
- **Through `loadfile`.** The stored values go into mpv's `loadfile <file> replace|append -1 <options>`. mpv applies them before the file starts and puts the previous values back when it ends. These are file-local options, the same mechanism as `reset-on-next-file`. So they never leak to the next file, including on a gapless move to the next file. There is no extra step after the load and no flicker.
- **Defaults always passed.** With the setting on, every file also gets an explicit track choice: the stored one, or the configured default (`playback.audioTrack`/`subtitleTrack`). Without that, mpv's own watch-later `aid`/`sid` for the previous file survived a gapless move. B then played A's audio language. This was also true in the base build after a restart (`r7pf-gapless.ps1`: base, restart, "B after gapless: Second B (Русский)").
- **The "all" values.** With "Also remember…" on, every file also gets its speed, deinterlacing and subtitle position and size: stored, or the default. mpv otherwise keeps these from file to file.
- **Sidecar tracks.** An external subtitle or audio track is stored by its file, not by its id. Its id is only the order in which mpv loaded it. After the load, `FixTracks` selects the sidecar, or adds it again with `sub-add`/`audio-add` when mpv didn't load it by itself. It also checks that a stored embedded id still exists (the file may have been remuxed) and falls back to `auto`. `Player` reads the track list once more after `FILE_LOADED` (`mpv_get_property_async`), so this check never runs on the previous file's list.

**Capture** (`LiveState::Capture`). These are the triggers:
- every `LiveState` notification (the observed delays, zoom, rotation, pan, A-B, secondary subtitles, sub position and size, deinterlace, and the Lumen-managed aspect, zoom mode and flip);
- track-list changes;
- speed changes.

Tracks count only when the user picked one. `Player` counts the choices made through `SetTrack`, `CycleTrack`, `AddSubtitle`/`AddAudio` and `SetProperty("aid"|"sid"|"secondary-sid")`. Lumen's own picks (`FixTracks`) are taken out, so mpv's automatic choice is never stored as yours. A stored sidecar that has gone missing stays stored.

A capture runs only while the file is "settled":
- it starts at `FILE_LOADED`;
- it ends at `Player::Open`/`Stop`, where the pending change is captured first (the `beforeLeave` hook), or at mpv's `START_FILE`/`END_FILE`, where it is dropped.

The 300 ms delay keeps the values mpv puts back when it moves on by itself from being taken as this file's.

**The round 5 rule** (FL-03/FL-10) still holds:
- Transforms are reset for the next file, as before. Flip is now reset on every load, not only on a path change.
- Per-file memory brings them back only for the same file, and only with "Also remember…" on.
- The Video tab's note says so when that option is on: "Отражение, поворот, масштаб и соотношение сторон действуют только для текущего файла и запоминаются для него."

**In the player.** At the bottom of the Playback tab of quick settings there is a new section, "Настройки этого файла" (`SettingsPanel.cpp`). It holds:
- the toggle "Запоминать для каждого файла";
- one line per stored value: track names as the track menus show them, sidecars by their file name, delays, speed, aspect, zoom, rotation, flip, deinterlace, A-B, subtitle position and size;
- the button "Сбросить для этого файла", with a tooltip, shown only when something is stored. It forgets the entry and returns the stored values to their defaults (tracks to auto, delays to 0, and so on).

With nothing stored, the section says what will be kept. With the setting off, it says that nothing is remembered.

**Clearing.** Both "Очистить историю" buttons (Recent's menu and Settings > Privacy) call `History::Clear`. That now also runs `History::onCleared`, even when the history is already empty, and `LiveState` clears and writes the store there. The open file is not remembered again until it is opened anew. Files matching `privacy.excludePatterns` are neither stored nor restored.

**A fix on the way: rotation.** `video-rotate` is a choice option, and mpv rejects it when it is set as a double. Ctrl+R and the Video tab's Rotate box did nothing in the base build: `video.rotate` left 0°. They now pass a string, and so do presets and resets. With that, rotation can be set and remembered. `SettingsActions.cpp` "reset-picture" still sets it as a double; that file is outside my scope.

## How to use it

- It is on by default. Pick a track or shift a delay; the next time the file opens, it is back.
- Settings > Воспроизведение > "Настройки для каждого файла" has the two toggles. The second, "Запоминать также скорость, изображение и положение субтитров", makes speed, the picture transforms, deinterlacing, A-B and subtitle position and size per file too.
- In quick settings (gear) > Воспроизведение, at the bottom, you see what is stored for the playing file and "Сбросить для этого файла".
- "Очистить историю" also forgets all per-file settings.

## How it was verified

Scripts are in the scratchpad and logs in `%TEMP%\lumen-qa\r7pf-out\`. The values were read from what quick settings shows: the Audio tab's track dropdown and delay box, the Subtitles tab, the Video tab (Rotate, Zoom, Aspect, Deinterlace, Flip), the Playback tab's speed and the new section. The store file was read too.

1. **A, then B, back to A, then a restart** (`scenario.log`, data `r7pf-s1`, default settings). On A I set sub delay +0.5, audio delay -0.2, audio cycled to track 2, subtitles cycled to off.
   - Store: `{"aid":2,"sid":0,"audioDelay":-0.2,"subDelay":0.5}`.
   - B opened: Main B (English), 0/0 delays, English subtitles. Nothing is stored for B.
   - A again: all four values back.
   - After quit and relaunch: A has them all, B is still clean.
   - Screenshot of the section: `s1-before-reset.png`.
2. **Reset for this file.** Clicked by tooltip. A went back to Main A, the sidecar, 0/0. The store became `{"files":[]}` and the button disappeared (`s1-after-reset.png`).
3. **Everything** (`part2.log`, `playback.perFileAll` on). On A I changed speed to 1.25, aspect to 16:9, rotation to 90°, free zoom to 107 %, subtitles to 110 %, deinterlace to off, set A-B point A, sub delay +0.05 and flip to horizontal.
   - All of them were stored.
   - B opened with every default: 1x, 0°, fit, default aspect, Авто, flip none, 100 %.
   - A again and after a restart: all back. In `s2-a3.png` the picture is rotated and flipped, with the A-B mark on the seek bar.
4. **Gapless move** (`r7pf-gapless.ps1`). A with audio track 2, then jump to 90 % and let the queue move on to B.
   - With a restart in between: the base build gives B "Second B (Русский)", A's language through watch-later. The new build gives Main B.
   - In the same session both give Main B.
5. **Clear history** (`part3.log`). Recent > … > "Очистить историю…" > "Очистить". The store became `{"files":[]}`. Further changes to the still-open file were not stored, and reopening A gave the defaults. The Settings > Privacy button (`#clear-history`) calls the same `History::Clear`.
6. **Corrupt store.** I tested `{not json`, `{"files":{}}` and `[1,2]`.
   - Each was backed up as `file-settings.corrupt-<time>.json`.
   - A played with its defaults.
   - The next change was stored in a fresh file.
7. **Locked store.** The file was held with no sharing while Lumen started, then released. The session's changes were not saved, and the file is byte-identical afterwards.
8. **Exclusions** (`privacy.excludePatterns = *\r7pf-media\A.*`). Nothing was stored for A; B was stored.
9. **Switched off.** A store with values for A was present: A opened with defaults, the section says "Выбор дорожек и задержки не запоминаются.", and the store was untouched.
10. **5000+ entries** (`part4.log`). A 653 KB store with 5201 files.
    - Load ran on the thread: 3.3-17 ms, before the window was even shown in two of three runs.
    - A's tracks and delay came back.
    - After the session the file holds 5000 entries: A kept, the 201 oldest dropped.
11. **Unit tests** (`tests/core_tests.cpp`, `file_settings`, 48 checks). They cover:
    - put, find in any spelling, removal at the defaults, no leak to another path;
    - a round trip of every field (Cyrillic paths, sidecars);
    - dropping unsafe strings and bad numbers;
    - backup of the wrong shape and of broken JSON;
    - a locked file left intact;
    - the LRU cap with a Touch;
    - lookup, save and load times at 5000;
    - `History::Clear` running `onCleared` with and without history.

    All 27 tests pass, 5601 checks.
12. **Checks.** `/W4` has no warnings. `check-icons`: 0 missing. `check-settings`: 0 of 222 settings without a reader. `i18n_check`: 0 conflicts.

**Sweep** (`sweep.ps1 -Filter "perfile|panel-quick|settings-playback"`). New scenarios in `qa/round5/scenarios.json`:
- `panel-quick-perfile` in dark, light, min, 1024x640, 150 % and English;
- `panel-quick-perfile-empty`;
- `panel-quick-perfile-off`;
- `settings-playback-perfile` in dark and light.

The seeded store contains every field. Results: base 25/25 ok with 0 findings, new 25/25 ok with 0 findings, so no medium or high finding was added. I reviewed the captures (`r7pf-sw-new2\*\final.png`): the list wraps at the minimum size, nothing is cut at 150 %, both themes read well, and the Settings page shows the new section between "Дорожки" and "Порядок".

## Performance

| | Base | New |
|---|---|---|
| First frame (LUMEN_TRACE, 4 runs) | 79-112 ms | 77-109 ms |
| `file loaded` | 251-340 ms | 244-302 ms |
| CPU while playing, per 5 s | 203-438 ms | 156-422 ms |
| CPU while paused, per 5 s / 10-15 s | 0-16 ms (one 15.6 ms tick now and then, in both) | 0-16 ms |
| Working set, paused | 143 MB | 144 MB |
| Store lookup at 5000 files | | 0.2 µs |
| Save at 5000 files (340 KB) | | 5.6 ms |
| Parse at 5000 files | | 8.5 ms (on the loader thread) |

What the feature costs:
- **While it is off:** a `GetBool` per file open. No thread, timer or observer.
- **While it is on:**
  - a one-shot loader thread at startup;
  - one more `track-list` read per file load;
  - two cheap observers (`video-pan-x/y`) plus `deinterlace`;
  - a one-shot 300 ms timer after a change;
  - a one-shot 2 s timer after a stored change.

Nothing repeats, so idle and paused stay at 0.

## What I left out, and why

- **A moved file found by size and mtime.** That needs a `stat` of the opened file on the UI thread for every open, which can block for seconds on a sleeping network share, plus a second index. Not cheap enough. A file is identified by its path.
- **Reloading the store when this instance takes over the data folder** from a first instance that exits (`RetryClaimDataDir`). That reload lives in `App::FlushStores`, outside my scope. Until a restart, a second instance doesn't save its per-file changes (the data folder is read-only for it), as with the other stores before their reload.
- **The "Damaged file replaced" toast** names the backup by its raw file name, `file-settings.json`, because `App.cpp`'s table of friendly names doesn't know it. The toast appears only when the loader finishes before the app collects the notices, which it did in my runs.

**Needed outside my scope:**
- `App.cpp`: add `{L"file-settings.json", L"per-file settings"}` (ru "настройки файлов") to the `kWhat` table.
- The clear-history copy should mention per-file settings:
  - `Views.cpp`: "Список недавних файлов и сохранённые позиции воспроизведения будут удалены.";
  - Settings > Privacy: "Забыть все недавние файлы и сохранённые позиции."
- Recent's "Очистить историю…" is disabled while the history is empty, even when per-file settings exist (`Views.cpp`). The Settings button always works.
- `SettingsActions.cpp` "reset-picture" sets `video-rotate` as a double, so it doesn't reset rotation. It should pass `"0"`.
- `PlayerOverlays.cpp` (episode language carry; found by reading the code, not run) picks the previous episode's language on the next episode even when that episode has a stored track choice.
  - Effect: the carried pick wins, and the next capture stores it.
  - Fix: skip the carry when `Live().FileSettingsStored().HasTracks()`.
- mpv's own watch-later still stores `aid,sid,secondary-sid`.
  - With per-file settings on, its track values are now always overridden by the explicit loadfile options.
  - With per-file settings off, the base behaviour stays, including the gapless leak after a restart.
  - Dropping the tracks from `watch-later-options` would fix the leak, but it would change "remember position" behaviour for users who turned per-file settings off, so I left it.
- `privacy.saveHistory` off does not stop per-file memory. These are playback choices with their own switch, as in SMPlayer, and "Очистить историю" and the exclusions still apply.
