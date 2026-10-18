# Lumen round 2 - reviewer "library"

Build: build-r2. Data dir: %TEMP%\lumen-qa\r2-library. Shots: round2\shots-library\

_Status: complete. All my processes (29824, 108856, 106388) closed._

## Raw log

- Home (fresh): "Недавно добавленные" now silent vp9 / S01E03 / S01E02 (fake.mkv + broken mp4 gone), new "Недавно добавленная музыка" row. Music cards have no artist subtitle; header still "19 элементов" on Home. 01-home.png
- Videos root: Mixed / Videos folder cards (no extra "media" level), still generic folder glyph. Folder card "Videos" badge says 8 but the folder page says "6 элементов" (badge counts files recursively, header counts direct children). 02-videos.png 03-videos-folder.png
- No file extensions in grid/list. List mode still has no column headers. 04-videos-list.png
- Music hub: no count shown. Double-click on "Песни" tile only opens Songs (no accidental playback). 05-music.png 06-songs-after-dblclick-tile.png
- Songs: "Неизвестный исполнитель/альбом" filled in; sort menu has "По номеру трека" (default). Still no track-number column, no column headers. 07-songs-sortmenu.png
- Albums: "Неизвестный альбом" by "Неизвестный исполнитель" - consistent with Artists. 08-albums.png
- Album page: Album column removed, Play all / Shuffle buttons, "4 песни · 4:20". Still no album header (cover/artist/year/genre), no track numbers shown. year is parsed (MediaLibrary.h:41) but never displayed. 09-album-one.png
- Code: "По номеру трека" sort (Views.cpp:507-512) compares only track numbers, not album first -> with 2+ tagged albums the default Songs order becomes 1,1,2,2,... (not reproducible with the 1-album test set).
- Artists: round art; Artist A page is still a flat song list (no albums block) and now repeats "Artist A" in every row (the column the album page dropped). 10-artists.png 11-artist-a.png
- Genres OK (Electronic 4 / Без жанра 4). 12-genres.png
- Double-click a song still switches the whole window to the full Music view. Title order now consistent ("Track Number 1 · Artist A" in window title and music view). Context chip still shows the folder name "Artist A - Album One", not album. Control bar time "00:03"/"00:50" vs "0:50" everywhere else. 13-play-from-album.png
- Queue: rows "Track Number 1 · Artist A 0:50", summary "4 элемента · 4:20" (fixed). Queue panel for a music file still offers "Главы" tab; panel covers the right part of the cover (art not re-centred). 14-queue.png
- Side library panel: header clean ("Album One" only, no collision), now-playing glyph on the row. "Перемешать" button rect [272..423] overflows the 360-px pane (pane ends at 416). 15-side-library.png
- Title "..." menu has no "Library"/"Медиатека" entry; the only ways back to the library from the music view are Escape or the sidebar toggle - undiscoverable. 17-title-menu.png
- Mini player now shows the album cover (fixed), but on hover the prev/pause/next glyphs are drawn straight over the cover art with only a faint dim: the pause glyph sits inside the word "ALBUM" ("AL||UM") and is barely readable. 18-after-escape.png 19-mini-hover.png 19b-mini-hover-zoom.png
- Album page: row "Track Number 1" keeps its hover/selected highlight + "…" button after the pointer left (stale hover). 18/19
- Auto-advance 1 -> 2 -> 3 works while browsing; played rows get a thin progress underline on the thumbnail.
- history.json: playCount 1 per track incl. auto-advanced ones (fixed). But Track 1 (first played) is stored with title "01 Track Number 1.mp3" (file name) while 2/3 have tag titles - first item recorded before tags load.
- Backspace now goes back (Album One -> Albums), Down then focuses the album that was opened; Enter opens it and focus lands on the first row. Tab from the list goes to title bar "…" then sidebar toggle - Play all/Shuffle/sort/view still skipped in Tab order (2nd Tab). 20..24
- Home search: header count follows results ("4 элемента", "2 элемента", "1 элемент", "0 элементов"), Cyrillic upper-case "ДРУГОЙ" finds both songs, "фильм" finds the unicode video, empty state "Ничего не найдено" good. Still only song/video hits - no Artist / Album results for "artist a" / "album"; songs as big square cards. 25/28/29/30
- Now-playing card in a grid (Track Number 4 in search results) draws its play/eq glyph right over the cover art text ("AL·BUM"), same overlap as the mini player. 25/30
- Favorites: heart appears on the row and Home gets an "Избранное" section (fixed). On the favourited row the Artist/Album columns shift left (643/879 px vs 655/899 px on other rows) to make room for the heart - columns no longer line up. 32b-songs-after-fav.png 33-home-after-fav.png
- Playlists empty state: single centred empty state with Создать/Импортировать (duplicate tile gone). 34-playlists.png
- New playlist dialog: typing only works, Enter in the name field creates it and opens it; checkbox now has a tick; wording "Добавить текущую очередь (4)". Enter after clicking the checkbox does nothing (focus on checkbox, no default button); "Создать" stays a grey secondary button. 35/36/37/38
- BLOCKER: Play all on the new playlist (= the album I had just listened to) ran Track 1, 2, 3 to their end within one second and landed on Track 4 at 1:20 / 1:20, stopped. watch_later has start=49.75 / 59.76 / 69.74 for the 50/60/70-s tracks - finished tracks keep a resume point at the very end. history.json: all three got playCount 2 with lastPlayed 08:30:25 (same second) and titles reverted to file names "01 Track Number 1.mp3". 39-playall-1s.png 39b-playall-2s.png
- Reproduced from the album page: double-click Track Number 2 -> player lands on Track 4 at 1:20/1:20 within 0.8 s; history: Track 2/3/4 playCount 3, lastPlayed 08:31:10 (same second). Any track you have once finished is unplayable from the library until watch_later is cleared. Likely cause: the resume point is written ~0.3 s before EOF on auto-advance (mpv save-position-on-quit / write-watch-later on file change, src/Player.cpp:127-138), the NearEnd() guard (src/Player.cpp:521-535) only runs in SaveWatchLater/Shutdown. 40-replay-track2.png
- Opening a video resizes the main window (1280x780 -> 1300x816) and it stays that size after Ctrl+W back to the library. 41-long-seeked.png 42-home-continue.png
- Continue watching: card appears with progress bar, still shows total "10:00" (not remaining). Recent: "осталось 7:00", rows show tag titles; "5 элементов" printed twice (header + "Сегодня" group); filters still only in "…". "Слушать снова" on Home lists Track 1-4 that were only skipped through by the resume bug. 45-home-continue2.png 46-recent.png
- Videos list: the only progress cue for the half-watched 10-min file is a ~2-px line under the 32-px thumbnail; no "осталось" / % column. 48-videos-list-progress.png
- Series S01: still a plain folder of 3 cards, no Play all / "Продолжить S01E0x" (album pages got Play all, folders did not). 49-series.png
- Light + English: dialog name field is light grey with dark text (fixed), Podcasts single CTA, playlist card menu without "Add Subtitles". Still mixed case: "Play next" / "Add to Queue" / "Add to Playlist", "New playlist..." (submenu) vs "New Playlist" (dialog title), "Create playlist" tile, nav "Add item". 54/55/57/58/59
- Adding Track 1 to a playlist that already has it: no duplicate written (4 items stay) but no "already in playlist" notice either. 59/60
- LIGHT THEME: the full Music view keeps a pure-black background (whole area between title bar and control bar) while the rest of the window is light. 61-light-music-view.png
- LIGHT THEME: mini player hover scrim is mid grey and the close / expand / prev / pause / next glyphs are dark grey on it - near-invisible (track without cover). 62-light-mini-hover.png

## Re-check of round 1

| ID | Status | Evidence / note |
|---|---|---|
| RAW-1 | Fixed | Home no longer leads with fake.mkv / broken mp4; new "Недавно добавленная музыка" row. Header still "19 элементов" on Home (see N-14). 01-home.png |
| RAW-2 | Still broken (partly better) | The extra "media" level is gone (Видео -> Mixed / Videos), but folder cards are still a bare glyph with no preview; the badge counts files recursively (8) while the page header counts children (6). 02/03 |
| RAW-3 | Fixed | No extensions in grid or list. 03/04 |
| RAW-4 | Still broken | List mode has no column headers and no click-to-sort. 04/48 |
| RAW-5 | Fixed (unchanged, OK) | Sort menu works. 07 |
| RAW-6 | Fixed | Music hub shows no misleading count. 05 |
| RAW-7 | Still broken (partly) | Untagged rows now say "Неизвестный исполнитель/альбом" and the "По номеру трека" sort exists, but there is still no track-number column and no column headers. 06/07 |
| RAW-8 | Fixed | "Неизвестный альбом · Неизвестный исполнитель", same as Artists. 08/10 |
| RAW-9 | Still broken (partly) | The redundant Album column is gone and Play all / Shuffle and "4 песни · 4:20" were added, but there is no album header (cover, artist, year, genre) and no track numbers. year is parsed but never shown. 09 |
| RAW-10 | Still broken | Artist page is still a flat song list with no albums block, and it now repeats "Artist A" on every row. 11 |
| RAW-11 | Fixed | Backspace goes back. 20 |
| RAW-12 | Fixed (OK) | Genres and totals are correct. 12 |
| RAW-13 | Still broken (partly) | The count now follows the results, and Latin, Cyrillic and upper-case search all work, but there are still no Artist or Album hits and songs show as big cards. 25/28/29/30 |
| RAW-14 | Still broken (partly) | Title order is now consistent ("Title · Artist"), but double-click still takes over the whole window and the chip shows the folder name. 13 |
| RAW-15 | Fixed | Queue rows show the artist, "0:50" and "4 элемента · 4:20". The control bar still shows "00:03". 14 |
| RAW-16 | Fixed | The side panel header is clean. 15 |
| RAW-17 | Still broken (changed) | The cover now shows, but the hover controls are drawn straight on the cover with a weak scrim ("AL\|\|UM"), and in the light theme they are almost invisible. 19b/62 |
| RAW-18 | Fixed (OK) | Auto-advance while browsing works. 17/19 |
| RAW-19 | Not re-tested | No toast was captured this round. |
| RAW-20 | Fixed | One empty state with Create / Import. 34 |
| RAW-21 | Fixed | The checkbox has a tick and the wording is "Добавить текущую очередь (4)". 36 |
| RAW-22 | Regressed | Fixed for normal playback (count 1), but replaying a finished track now cascades through the queue and adds +1 to every skipped track (count 3 after about 2 s of real listening). 39/40 |
| RAW-23 | Not re-tested | |
| RAW-24 | Fixed (partly) | No duplicate is created, but there is no "already in playlist" notice. 60 |
| RAW-25 | Fixed | The playlist card menu has no subtitles entry. 58 |
| RAW-26 | Not re-tested | It opens a dialog close to the Browse controls, so I skipped it. |
| RAW-27 | Still broken | The Continue card still shows "10:00" total, not remaining time. The list only has a ~2-px line under the thumbnail. 45/48 |
| RAW-28 | Not re-tested | The "Частые исполнители" section did not appear in this data set. |
| RAW-29 | Still broken | Recent filters are still hidden in "…", and "5 элементов" is printed twice. 46 |
| RAW-30 | Not re-tested | |
| RAW-31 | Not re-tested | |
| RAW-32 | Not re-tested (rule) | The radio station must not be played. Network rows still open on a single click (Podcasts). 57 |
| RAW-33 | Fixed | A single CTA, and the Network header has no wrong count. 56/57 |
| RAW-34 | Fixed | Double-click on the Songs tile no longer starts a song. 06 |
| RAW-35 | Regressed | Same as RAW-22: auto-advance itself is fine, but the near-EOF resume causes inflation. |
| RAW-36 | Fixed (partly) | Down focuses the grid, focus returns to the opened item on back, and Enter puts focus on the first row. Tab still skips Play all / Shuffle / sort / view. 21-24 |
| RAW-37 | Still broken | Series is a plain folder with no Play all / Continue. 49 |
| RAW-38 | Fixed | Heart on the row and an "Избранное" section on Home. 32b/33 |
| RAW-39 | Still broken | "Play next" vs "Add to Queue", "New playlist..." vs "New Playlist", "Add item". 55/59 |
| RAW-40 | Fixed | The light dialog field is readable. 54 |
| RAW-41 | Not re-tested | |

## New findings

| ID | Severity | Area | What happened | Steps to reproduce | Expected | Evidence |
|---|---|---|---|---|---|---|
| N-1 | Blocker | Music playback / resume | Any track you once listened to the end is skipped instantly the next time: Play all on a playlist, or double-click Track 2, ran Track 1/2/3 to their end within one second and stopped on Track 4 at 1:20/1:20. watch_later holds start=49.75/59.76/69.74 for the 50/60/70-s tracks. Likely cause: on auto-advance mpv writes the resume point ~0.3 s before EOF (src/Player.cpp:127-138, save-position-on-quit / write-watch-later on file change); the NearEnd() guard (src/Player.cpp:521-535) runs only in SaveWatchLater/Shutdown. | Play Album One with auto-advance through Track 3, then Play all or double-click any played track. | A finished track starts from 0:00, as the code comment intends. | 39-playall-1s.png, 39b-playall-2s.png, 40-replay-track2.png |
| N-2 | Major | History / stats | As a result of N-1, every skipped track gets playCount +1 and lastPlayed = the same second. "Слушать снова" and Recent fill with tracks nobody heard. history titles fall back to file names ("01 Track Number 1.mp3"). | As N-1, then read history.json and look at Home. | Count only real plays; keep tag titles. | 45-home-continue2.png, history.json dump in raw log |
| N-3 | Major | Light theme | The full Music view background stays pure black in the light theme; only the placeholder card is light. | Light theme, double-click a song. | A light background, or a deliberate art-tinted backdrop, not a black slab. | 61-light-music-view.png |
| N-4 | Major | Mini player | Hover controls have no backing: in dark they merge with the cover art (the pause glyph inside "ALBUM"), and in light they are dark-grey glyphs on a mid-grey scrim, almost invisible. | Play a song, Escape, hover the mini player (dark and light). | Clear contrast (white glyphs on a dark scrim, or circular buttons). | 19b-mini-hover-zoom.png, 62-light-mini-hover.png |
| N-5 | Major | Navigation | There is no visible way back from the full Music view to the library: the title "…" menu has no Library item, and only Escape (undocumented) or the sidebar toggle work. Double-click on a song always forces this view. | Double-click a song, look for "Library". | A Library item / back button, or play in place with the mini player. | 13-play-from-album.png, 17-title-menu.png |
| N-6 | Minor | Songs sort | The default Songs sort "По номеру трека" sorts by track number only, so with several tagged albums the list becomes 1,1,2,2... (album/disc is not the primary key). Code-read; the test set has only one tagged album. Likely cause: src/app/library/Views.cpp:507-512. | Library with 2+ tagged albums, Songs. | Sort by album artist, then album, then disc, then track. | 07-songs-sortmenu.png |
| N-7 | Minor | Songs list | On a favourited row the heart pushes the Artist/Album columns ~12-20 px left, so the columns no longer line up. | Favourite Песня 1 and look at Songs. | Reserve the heart slot on every row. | 32b-songs-after-fav.png, 55-light-song-context.png |
| N-8 | Minor | Artist page | Every row repeats the artist's own name in a column (the same redundancy the album page just fixed); there are no albums. | Music > Artists > Artist A. | Album column instead, or albums grouped. | 11-artist-a.png |
| N-9 | Minor | Videos | The folder card badge "8" disagrees with the folder header "6 элементов" (recursive vs direct). | Видео, then open Videos. | One counting rule. | 02-videos.png, 03-videos-folder.png |
| N-10 | Minor | Window | Opening a video resizes the main window (1280x780 -> 1300x816) and it stays that size back in the library. | Open Long Seek Test from the library, Ctrl+W. | Restore the library size, or don't autofit while docked. | 41-long-seeked.png, 42-home-continue.png |
| N-11 | Minor | New playlist dialog | After you click the checkbox, Enter does nothing (no default button), and "Создать" stays grey even with a valid name. Enter works only inside the name field. | Type a name, tick the queue box, press Enter. | Enter submits; Create looks primary. | 36-newpl-filled.png, 37-after-create.png |
| N-12 | Polish | Side library panel | The "Перемешать" button rect (272-423) overflows the 360-px pane (ends at 416) and touches the video area. | Play a song, open the sidebar toggle on the album page. | Buttons fit or wrap. | 15-side-library.png |
| N-13 | Polish | Lists | A stale hover/"…" stays on a row after the pointer leaves (Track Number 1 on the album page after a double-click elsewhere). | Double-click Track 1, Escape back, move the pointer to the mini player. | Hover clears. | 18-after-escape.png, 19-mini-hover.png |
| N-14 | Polish | Home / Recent | Home header always says "19 элементов" (what 19?). Recent prints "5 элементов" in the header and again in the "Сегодня" group. | Open Home, open Recent. | No count on Home; a single count on Recent. | 45-home-continue2.png, 46-recent.png |
| N-15 | Polish | Queue panel | For an audio file the panel still offers a "Главы" tab and covers the right third of the cover (the art is not re-centred). | Play a song, open the queue. | Hide chapters for audio; shift or shrink the art. | 14-queue.png |
| N-16 | Polish | Time format | The control bar uses "00:03 / 00:50" while the library, queue and mini player use "0:50". | Play any song. | One format (m:ss). | 13-play-from-album.png |

## Top 5

1. **N-1 (Blocker)**: finished tracks keep a resume point ~0.3 s before the end. Replaying an album or playlist skips straight to the last track. For a music collector this breaks the basic "play this album again".
2. **N-2 / RAW-22 regressed**: N-1 inflates play counts and floods "Слушать снова" / Recent with tracks that were never heard. History titles revert to file names.
3. **N-5 + RAW-14**: double-clicking any song throws you out of the library into a full-screen Music view, with no visible way back (only Escape).
4. **N-3 + N-4**: in the light theme the Music view is a black slab, and the mini player hover controls are almost invisible. In dark they collide with the cover art.
5. **RAW-9 / RAW-10 / RAW-7 / RAW-4 still open**: album and artist pages are still flat lists with no header (cover, year, genre), no track numbers, no albums on the artist page and no column headers. Collectors coming from iTunes, MusicBee or Plex will find the library shallow.