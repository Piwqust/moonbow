# Round 8 fixes: library pages, settings, dialogs, tools (r8-fix-pages)

Findings: qa/round8/raw/library-settings.md, #2–#12 and the left-out list. #1 (OpenSubtitles
extension) belongs to r8-fix-storage (= storage #3) and is skipped here. V7-02 and V7-05 from
qa/round7/verify.md were added by the coordinator.

Branch: `worktree-agent-aede89deb2b1d2755`. Temp prefix `r8fg`.

Checks after the fixes:
- build at /W4: no warnings;
- tests\build-tests.bat: 47 tests, 6435 checks, 0 failures;
- check-icons.py: 0 missing; check-settings.py: 0 without a reader; i18n_check.py: 0 conflicts;
- `tools\dev\sweep.ps1 -Filter 'library|settings|home|music|network|recent|playlist|dialog|subtitle' -MaxPriority 1`:
  110 scenarios ok, 0 high, 0 medium, 5 low (text-trunc: the subtitles.matching dropdown, music-songs at
  1024x640, playlists). None of those touch code changed here.

## Numbered findings

| # | Status | How it was checked (before → after) | Where |
|---|---|---|---|
| 1 | skipped | r8-fix-storage's (storage #3). | — |
| 2 | fixed | Code trace. Before: Save wrote only the ticked rows of `model_->subs`, so a pair downloaded into the data folder, or one from another source folder, was erased; in file mode the remembered subs weren't even ticked. After: remembered subs the dialog doesn't list are kept, only the newly picked ones are `sub-add`ed to the playing file (no duplicate tracks), and file mode pre-ticks the remembered subs it lists. No test: the pair store is app code, which core_tests can't link. | AddSubtitles.cpp:530-556, :990-996 |
| 3 | fixed | New check in `config_values`: `ApplyEqPreset("none")` returns true, sets the preset and flattens the bands. It fails on the old Config.cpp (4 failures, run with the old file swapped in) and passes now. The SettingsRows branch is gone: the row calls `ApplyEqPreset` for every value. | Config.cpp:344-348, SettingsRows.cpp:217, core_tests.cpp:549-553 |
| 4 | fixed | Code trace. Before: Home's Recently Added passed no history entry, so a played file kept "Новое"; the pages counted a file played under 2 % as new. After: one rule, `ShowsNew(m, played)`, where "played" means "in the history at all". Home builds a PathKey set of the history once per rebuild, and pages read `HistoryInfo::played`. No per-card `History::Find` (that is 500 PathKeys each, and the first scan makes every file new). | LibraryCommon.cpp:262, LibHome.cpp:293, :327, Views.cpp:552 |
| 5 | fixed | Runtime check (r8fg-seen.ps1): session 0 scans a folder with one video, then a second video is added and session 1 starts with a file (player screen). Old exe: library.json `seen` was written behind the player (1792357778417), so the New chip never showed. New exe: no `seen` behind the player; it is written once `nav library` shows Home. `MarkSeenIfShown` covers only Home/Videos/Music in the main window, and the screenChanged handlers call it when the library shows again. | Views.cpp:263, :397, :609, LibHome.cpp:201, :555, Pages.h |
| 6 | fixed | Code trace. Menus now share one `SharedQueue` (`shared_ptr<const vector<QueueItem>>`), so each menu opening makes one copy of the queue instead of one per lambda and per MenuItem copy. This covers MediaMenu, AddToPlaylistMenu, FolderMenu, the FolderPage and Music "…" menus, and GroupMenu. Not timed: there's no 20k-song test library. | LibraryCommon.h, LibraryCommon.cpp:450, :491, Views.cpp:660, :689, :821, LibMusic.cpp:260, :423, LibHome.cpp:98 |
| 7 | fixed | Code trace; not run, because no automation selector reaches the panel toggles. Reset picture now goes through LiveState's public API: `flip = 0`, `ResetPicture()` (brightness…hue, negate and grayscale, which also applies flip in the same vf rebuild), `SetZoomMode("fit")` (zoom 0 and video-unscaled no) and `SetAspect("auto")`. LiveState's copies match mpv and are saved per file correctly. LiveState itself is untouched. | SettingsActions.cpp:38-51 |
| 8 | fixed | Code trace. `lib::ClearHistory()` clears the history (and per-file settings through `onCleared`), flushes, clears online data and emits libraryChanged. Settings and Recent both call it. `rescan-library` calls `lib::RescanLibrary()`, which emits. | LibraryCommon.cpp:397, SettingsActions.cpp:29, :77, Views.cpp:1112 |
| 9 | fixed | Run: `-Out %USERPROFILE%\Desktop\r8fg-outside` now throws "-Out must be a folder inside %TEMP%" and creates nothing. A %TEMP% folder with a file but no stamp throws "not a worst-case set" and the file is still there. Before, the second case ran `Remove-Item -Recurse`. | make-worstcase.ps1:22-30 |
| 10 | fixed | Code trace. SaveState keeps the scroll offset only when no filter is active. Back to a page that was searched restores the selection, and ApplyViewState brings it into view. | Views.cpp:340, LibHome.cpp:233 |
| 11 | fixed | Code trace. Range is tested on the typed value before clamping, so "0" in a 1–3600 field stores 1 and shows "Enter a value from 1 to 3600". | SettingsRows.cpp:985 |
| 12 | fixed (partial for one check) | Deleted: ItemGrid::SelectedOr and VisibleCount (grep: no callers, tests included), and the AlbumOf/ArtistOf/GenreOf copy helpers. FolderPage::SortMenu now uses SortMenuItems (new `fallback` argument, "name"). One `lib::Grouped` replaces two: the AddSubtitles copy put a no-break space in English. One `lib::LooksLikeFolder` replaces LibMusic's and LibHome's. The third check, Shell.cpp:564, is r8-fix-ui's file and is left as is. | Views.cpp:699, :709, LibraryCommon.cpp:236, :240, LibMusic.cpp, ItemGrid.h/.cpp |

## Coordinator rows (qa/round7/verify.md)

| Row | Status | How it was checked | Where |
|---|---|---|---|
| V7-02 | fixed | Code trace. Damaged entries skip `CardImage` (no lookup, no backdrop) and the year/rating chip. The media menu no longer offers or looks up "Find Another Match" for a damaged library file. The info dialog (playing file) is unchanged: a file that plays isn't damaged. | ItemGrid.cpp:374, :445, LibraryCommon.cpp:539 |
| V7-05 | fixed | One helper (#8). Both texts now list what goes, in English and Russian: history, positions, per-file settings and online data. The Recent item is always enabled, since per-file and online data can exist without history. The Settings text lives in SettingsSchema.cpp (src/core), a two-line string change outside my scope that the coordinator asked for. | Views.cpp:1105-1112, library.inc:203, SettingsSchema.cpp:914-917 |

## Left out (12)

| Item | Status | How / why | Where |
|---|---|---|---|
| Meta() runs twice per card paint | rejected here | Real, but `CardImage` takes a path and calls `Meta` itself. Calling it once needs a `CardImage(const MediaMeta&)` overload in src/net, which is r8-fix-storage's area. With TMDB off, `Meta` returns at its first check. | ItemGrid.cpp:374, :445 |
| AddScripts overwrites a same-named script | rejected | Replacing a same-named file the user picked is how a newer version of a script gets installed. Refusing would need a new prompt, which is new UI. | SettingsAddons.cpp:299 |
| Network Stream playlist emits no libraryChanged | fixed | Code trace: Create plus AddPaths with no emit. It now emits. | Dialogs.cpp:981 |
| Stale search answer in Find Match / Online Subtitles | fixed (Find Match); rejected (Online Subtitles) | Find Match had no guard, so it now drops answers from older searches with a serial. Online Subtitles already refuses a new search while `busy_` is set, so a stale answer can't arrive. | Dialogs.cpp:1346-1351, :1410 |
| AttachRemembered calls PathFileExistsW on the UI thread | fixed | It uses `lib::MaybeExists`, which skips network paths (a NAS that is off blocked for its timeout). `sub-add` is async, so a missing network sub just fails in mpv. | AddSubtitles.cpp:105 |
| Onboarding drops the old root without a save | fixed | Code trace. Removing the old root alone (the new text isn't a folder) now saves and rescans. | Onboarding.cpp:398-410 |
| Newline in a stream rename splits the entry | fixed | TextBox paste inserts clipboard text as is. `Encode` now turns tab/CR/LF in a URL or title into a space. | LibraryCommon.cpp:644-656 |
| Network group counts ignore the filter | fixed | Saved and Recently Played headers count only the rows that match the search and hide at 0. | LibNetwork.cpp:251-262 |
| AlbumOf/ArtistOf/GenreOf return copies | fixed | Deleted. Callers read the fields, and `Groups` binds a const reference. | LibMusic.cpp:535 |
| input.ps1 / command.ps1 don't guard the owner's PID | fixed | They refuse a window without WS_EX_NOACTIVATE, which ui/Host.cpp:308 sets only under `--automation`. I used that rather than the CIM command line, which costs about 630 ms per call. winpos.ps1 gets the same guard. command.ps1 was already safe in practice (MainWindow.cpp:467 ignores commands without automation), but now says so instead of waiting. The sweep, which drives its instances through winpos.ps1, still runs. | input.ps1:49-50, command.ps1:112-113, winpos.ps1:27-28 |
| ListFiles runs on the UI thread in Add Subtitles | rejected here | Real for a big or network folder, but moving it off the thread needs a worker plus an async dialog state. That's not a small, safe fix. | AddSubtitles.cpp:111 |
| A language switch loses the scroll position | rejected here | The rebuild-on-language code is in Shell.cpp:466 (r8-fix-ui's file). | Shell.cpp |

Tests the audit listed as missing: hostile SubtitleFileName names is storage's. subtitle-pairs.json,
ParseBinding (ShortcutStore) and FindStateEntry are app code that tests\build-tests.bat doesn't
link (it builds core, library, net and two app files). Adding them would mean moving code or growing
the test build, so they weren't added.

## Outside my file scope

- src/core/SettingsSchema.cpp:914-917: the Clear history description, for V7-05, asked for by the coordinator.
- src/core/Config.cpp: the `ApplyEqPreset` hunk (#3), assigned to me in the brief.
- Needed from others: a `CardImage(const MediaMeta&)` overload in src/net (Meta twice);
  Shell.cpp:564's folder check could use `lib::LooksLikeFolder` (r8-fix-ui).
