# Lumen round 3 - reviewer "library"

Build: build-r3 (commit 9e387bb). Data dir: %TEMP%\lumen-qa\r3-library. Shots: round3\shots-library\

_Status: complete. My processes (69040, 100168) are closed. Own test data: r3-library\own (tagged copies, .lrc), r3-library-big (big-library data dir)._

## Raw log

- Setup: own copies in r3-library\own (Artist B - Second Album: 4 mp3 retagged 2 discs + ReplayGain tags; Artist A - Album Two: 2 mp3 + synced/unsynced .lrc; Show.Name: 3 series copies named S02E01 / 2x02 / S01E01). Library roots = media + own.
- Home: '19 элементов' header gone. Recently added shows raw 'Show.Name.S01E01'. Music cards still no artist line. 01-home.png
- Music hub now shows counts (14 песен, 5 альбомов, 4 исполнителя, 4 жанра). 02-music.png
- Songs list: column titles present, № column present. Album->track OK for Album One/Two, BUT Second Album (2 discs) ordered d1t1, d2t1, d1t2, d2t2 = disc ignored. Windows exposes MP3 TPOS only as System.Music.PartOfSet='2/2'; System.Music.DiscNumber empty (Shell ExtendedProperty), MediaLibrary.cpp:202 reads only PKEY_Music_DiscNumber. 03-songs.png
- Album page (Second Album): header with cover, 'Альбом', title, Artist B, '2019 · Rock · 4 песни · 4:20', Play all/Shuffle, № column. RAW-9 fixed. Track order on the 2-disc album: 1,1,2,2 (B One-One, B Two-One, B One-Two, B Two-Two), no disc grouping/headers. 05-album-second.png
- Artist page (Artist A): round art, 'Исполнитель', '2 альбома', '6 песен · 6:10', Play all/Shuffle, 'Альбомы' block (2 list rows: thumbnail, name, '4 песни'; no year, no covers grid), 'Песни' list with Album column instead of repeating artist (N-8 fixed). Single click on an album row only selects it. 08/09
- Home search 'artist': groups 'Исполнители · 2' (round cards, '6 песен'), 'Альбомы · 3', 'Музыка · 10' as list rows with artist/album/length (RAW-13 fixed). Header '15 элементов'. 11/12
- Search 'РУССКИЙ' (upper-case Cyrillic): 'Альбомы · 1' + 'Музыка · 2'. 13-search-cyr.png
- Series grouping (own Show.Name folder: S01E01, S02E01, '2x02'): sections 'Show Name · Сезон 1' / 'Show Name · Сезон 2', 2x02 parsed into season 2, buttons 'Смотреть S01E01' + 'Воспроизвести все'. Cards still show raw file names (Show.Name.S02E01). 16-show-name.png
- Damaged marks: Mixed grid shows 'broken truncated' and 'fake' dimmed with a red '!' corner dot and 'Повреждён' badge; list view puts 'Повреждён' in the Length column and a tiny red '!' hanging off the thumbnail corner. 18-mixed.png 19-mixed-list.png
- Lyrics (synced .lrc next to a copy): music view shows lines right of the cover, current line white, others grey with fade; at 0:06 line 2 (04.00) is current = correct. Control bar time now '0:02 / 0:50' (N-16 fixed). 21/22
- Lyrics click: clicking a line seeks exactly to its timestamp (mpv log 'playback restart complete @ 27.999978'). After the click the clicked line keeps a white highlight while the list glides on, so two lines are white at once (line 8 and current line 12 at 0:45). 23/24
- mpv also loads the .lrc as an external subtitle track ('Subs --sid=1 lrc [external]') for the audio file.
- Gapless: track change Album Two 1->2 in mpv.log: Lumen issues 'loadfile ... replace' at EOF (Player.cpp:494-501), mpv logs 'EOF code: 3', ao/wasapi 'Thread Reset' + 'Thread Pause', decoder uninit, then a fresh open; 'playback restart complete' 15 ms after 'audio EOF reached'. mpv's playlist only ever holds one file, so gapless-audio=weak and prefetch-playlist (SettingsSchema.cpp:1338-1342) never apply: the AO buffer is flushed between album tracks.
- Mini player hover (N-4): scrim is darker now, but prev/play/next are still drawn straight across the cover text ('AL▶UM'), no button backing. 27b-mini-hover-zoom.png
- Home after watching 6:04 of the 10-min file: 'Продолжить просмотр' card subtitle 'осталось 3:56' (RAW-27 fixed for the card; the thumbnail badge still shows '10:00'). Main window went 1280x780 -> 1300x816 while the video played and came back to 1280x780 after Ctrl+W (N-10 fixed). 29/30
- media Series S01: 'Series · Сезон 1', 'Смотреть S01E01' + 'Воспроизвести все' (RAW-37 fixed). 31-series-s01.png
- Home after E01 + E02 watched to the end: 'Series S01E03 · Следующая серия' next to 'Long Seek Test 10min · осталось 3:56' in 'Продолжить просмотр'. watch_later holds only the 10-min file (finished tracks/episodes keep no resume point). history.json still stores the first item of a queue with its file name as title ('01 A2 First.mp3', 'Series S01E02.mp4'). 34-home-next-ep.png
- Recent: tag titles shown, finished episodes dimmed, '5 элементов' printed once (N-14 fixed). No column titles here, unlike every other list. Filters still only in '…'. 35-recent.png
- Replay of a finished album (Play all on Album Two): starts at 0:01 of track 1, no skipping (N-1 Blocker fixed). 36-replay-album-two.png
- Unsynced .lrc: three lines shown statically at a smaller size (~16 px vs ~20 px for synced), vertically centred. 37-unsynced-lyrics.png
- Title '…' menu now starts with 'Медиатека  Alt + Home' (N-5 fixed). 38-title-menu.png
- Settings > Аудио: 'Воспроизведение без пауз' on by default; 'Выравнивание громкости' section (toggle, EBU R128/Динамический, -23 LUFS, Ночной режим, Replay Gain Выкл./По треку/По альбому, preamp greyed while RG off). Changing RG to 'По альбому' and normalize on applies live (log: 'Set property: replaygain=album', af=loudnorm). Second Album playback: 'Replaygain: Track=-6 Album=-3', 'Applying replay-gain: 0.707946' = correct. Both can be on at once with no hint that loudnorm re-levels what ReplayGain just set (loudnorm also upsamples to 192 kHz). 40/41/42/43
- Big library (3500 hard links, -Fresh): library-cache.json stopped growing at ~16 s; Songs (2000) opens in ~0.3 s, wheel-scrolling and End are instant, Home search 'Song 42-' -> 'Музыка · 20' in <1 s (matches file names, but the rows only show the tag title, so you cannot see why they matched). The 'Жанры' tile said 'Читаем теги…' for ~80 s after launch while the process used ~79% of a core; it then showed '2 жанра' and CPU fell to 0%. The cache entries have no genre field. 44-51

## Re-check of round 2

Round 2 findings in my area that STATUS.md section 3 marks as done, plus older open items it addresses.

| ID | Status | Evidence / note |
|---|---|---|
| N-1 (Blocker, finished tracks skipped) | Fixed | Play all on an album I had just finished starts track 1 at 0:01. watch_later keeps only the half-watched 10-min file. 36-replay-album-two.png |
| N-2 (play-count inflation, file-name titles) | Fixed (partly) | Counts are 1 per real play. history.json still stores the first item of each queue under its file name ("01 A2 First.mp3", "Series S01E02.mp4"). The UI shows tag titles, so this only shows up in the file. |
| N-4 / RAW-17 (mini player hover) | Still broken (better) | The scrim is darker, but prev/play/next are still drawn straight over the cover art ("AL▶UM") with no button backing. 27b-mini-hover-zoom.png |
| N-5 (no way back to the library) | Fixed | The title "…" menu now starts with "Медиатека  Alt + Home". 38-title-menu.png |
| N-6 (Songs "по номеру трека" ignores album) | Fixed for album; Regressed for disc | Album → track order works (Album One 1-4, then Album Two 1-2). Disc order does not work for MP3: see L-1. 03-songs.png |
| N-7 (heart shifts columns) | Not re-tested | |
| N-8 (artist page repeats artist) | Fixed | The artist page has an Album column. 08-artist-a.png |
| N-9 (folder badge vs header) | Fixed | Mixed shows 3/3 and Videos 6/6. 17-media.png 18-mixed.png |
| N-10 (window grows after a video) | Fixed | 1280x780 → 1300x816 while playing, back to 1280x780 after Ctrl+W. 29/30 |
| N-14 (Home "19 элементов", Recent count twice) | Fixed | 01-home.png 35-recent.png |
| N-15 (Chapters tab for audio) | Not re-tested | |
| N-16 (control bar "00:03") | Fixed | "0:02 / 0:50". 21-music-view-lyrics.png |
| RAW-4 (no column titles) | Fixed | № / Название / Исполнитель / Альбом / Длина on Songs, and Название / Длина / Размер / Изменён on folders. Recent still has none. 03/19/35 |
| RAW-7 (no track-number column) | Fixed | 03-songs.png |
| RAW-9 (no album header) | Fixed | Cover, "Альбом", title, artist, "2019 · Rock · 4 песни · 4:20", Play all / Shuffle. 05-album-second.png |
| RAW-10 (artist page flat) | Fixed (partly) | It now has an "Альбомы" block, but as small list rows with no year and no covers grid. The albums are in name order, not by year. 08-artist-a.png |
| RAW-13 (no artist/album hits) | Fixed | "Исполнители · 2", "Альбомы · 3", "Музыка · 10" as list rows. Upper-case Cyrillic works. 11/12/13 |
| RAW-27 (Continue shows total) | Fixed (card) | "осталось 3:56" under the card. The thumbnail badge still says "10:00". 30-home-continue.png |
| RAW-29 (Recent filters in "…") | Still broken | Listed as open in STATUS section 6. 35-recent.png |
| RAW-37 (series = plain folder) | Fixed | "Series · Сезон 1", "Смотреть S01E01" + "Воспроизвести все". Mixed names (S02E01, 2x02) split into seasons correctly. 16/31 |

## New features

| Feature | Verdict | Notes |
|---|---|---|
| Album / artist headers, column titles, № | Works | Good hierarchy. Missing: disc grouping, album year on the artist page, and a link from the album header to the artist (the last two are open in STATUS). |
| Track / disc order | Broken for MP3 | See L-1. |
| Search groups | Works | Artists / Albums / Music groups, with a count in each group header. |
| "осталось N" / "Следующая серия" on Home | Works | After E01 and E02 finished: "Series S01E03 · Следующая серия" next to "Long Seek Test 10min · осталось 3:56". 34-home-next-ep.png |
| Series by season | Works | Cards still show raw file names (Show.Name.S02E01), not "Серия 1". |
| Damaged-file marks | Works | Dimmed card, red "!" dot, "Повреждён" badge. In the list the "!" hangs off the 32-px thumbnail corner. 18/19 |
| Lyrics (.lrc) | Works, small defects | Synced: the current line is highlighted, lines glide, and a click seeks exactly to the line (log @27.999978). Unsynced: shown statically. See L-3 and L-4. 21-24, 37 |
| Gapless | Does not work as designed | See L-2. |
| ReplayGain / normalization | Works | Applied live. Album gain -3 dB → "Applying replay-gain: 0.707946". See L-6. 40-43 |
| Big library | Mostly fine | 3500 files scanned in ~16 s. 2000 songs open in ~0.3 s, and scrolling and search are instant. See L-5. 44-51 |

## New findings

| ID | Severity | Area | What happened | Steps to reproduce | Expected | Evidence |
|---|---|---|---|---|---|---|
| L-1 | Major | Music sort | Multi-disc MP3 albums ignore the disc. "Second Album" (TPOS 1/2 and 2/2) is listed d1t1, d2t1, d1t2, d2t2 both on the album page and in Songs. There are no disc headers. Windows exposes MP3 TPOS only as System.Music.PartOfSet ("2/2"). System.Music.DiscNumber is empty (checked with Shell ExtendedProperty). Likely cause: src/library/MediaLibrary.cpp:202 reads only PKEY_Music_DiscNumber. | Tag 4 MP3 copies with disc=1/2 and 2/2 (ffmpeg -metadata disc=…), add the folder, open the album. | d1t1, d1t2, d2t1, d2t2, with "Диск 1 / Диск 2" sub-headers. | 05-album-second.png, 03-songs.png |
| L-2 | Major | Gapless | "Воспроизведение без пауз" (on by default) has no effect on queue playback. At the end of each track Lumen sends `loadfile … replace` (src/Player.cpp:494-501). mpv logs "EOF code: 3", then `ao/wasapi Thread Reset` + `Thread Pause`, uninits the decoder and opens the next file fresh. mpv's playlist never holds the next file, so `gapless-audio=weak` and `prefetch-playlist` (SettingsSchema.cpp:1338-1342) never take effect, and the buffered tail is flushed between album tracks. | Data dir with general.extraOptions `--log-file=…`, Play all on an album, read the log at the track change. | The next queue item is appended to mpv's playlist (or loaded before EOF), so the audio device stays open with no reset. | mpv.log excerpt in raw log (r3-library\mpv.log, t=354.08) |
| L-3 | Minor | Lyrics | After you click a lyric line, that line stays white while the list scrolls on, so two lines are highlighted (line 8 and current line 12 at 0:45). | Play a track with a synced .lrc, click a line, wait about 15 s without moving the pointer. | Only the current line is highlighted, and hover clears when the content moves under the pointer. | 24-unsynced.png (named before the track changed) |
| L-4 | Minor | Lyrics / tracks | mpv also loads the sidecar .lrc as an external subtitle track ("Subs --sid=1 'lrc' [external]") for audio files, so the subtitle button and track list get a fake "lrc" subtitle. Not checked visually, only in the log. | As L-3, read mpv.log. | Exclude .lrc from sub-auto for audio, or hide it. | r3-library\mpv.log t=314.257 |
| L-5 | Minor | Big library | For about 80 s after the scan finished, the Genres tile said "Читаем теги…" while the process used ~79% of one core. It then showed "2 жанра" and CPU fell to 0%. The library cache stores no genre, so this second pass looks like it is redone without a cache (code-read: cache entries have p/s/m/d/t/a/l/n/y only). | -Fresh data dir with the big library, open Музыка at once and again after 1 min. | Show genre progress in one scan step, and cache genres. | 45-big-music.png, 50-big-music-later.png, 51-big-music-later2.png |
| L-6 | Minor | Audio settings | ReplayGain and "Выравнивать громкость" (loudnorm) can be on together. loudnorm then re-levels what ReplayGain set, which defeats "По альбому", and loudnorm upsamples to 192 kHz (lavfi graph in log). There is no hint in the UI. | Settings > Аудио: Replay Gain = По альбому, Выравнивать громкость = вкл. | Either disable one when the other is on, or explain in the description. | 43-rg-album-norm.png, mpv.log t=611.59 |
| L-7 | Minor | Search | Searching by file name ("Song 42-") returns rows that show only tag titles ("Track Number 1"), so you can't see why they matched. | Big library, search "Song 42-". | Show the file name or path as a secondary line when the match is not in the visible fields. | 49-big-search.png |
| L-8 | Polish | Series | Episode cards keep raw file names ("Show.Name.S02E01", "Show Name 2x02", "Series S01E01"), even though the season grouping already parsed them. Home "Недавно добавленные" also shows "Show.Name.S01E01". | Open a series folder. | "Серия 1", or "S02E01 · Show Name". | 16-show-name.png, 01-home.png |
| L-9 | Polish | Lyrics | Unsynced lyrics use a smaller font (~16 px) than synced ones (~20 px) in the same slot. | Track with a plain .lrc. | The same type size. | 37-unsynced-lyrics.png |
| L-10 | Polish | Recent | Recent is the only list without column titles, and its filters are still hidden in "…". | Open Недавние. | The same header row as other lists. | 35-recent.png |
| L-11 | Polish | Lists | In list view the red "!" damage dot sits half outside the 32-px thumbnail corner. | Видео > media > Mixed, list view. | Inside the thumbnail, or a marker on the title. | 19-mixed-list.png |

## Compared with the leaders

- **MusicBee / foobar2000 / Plex (music)**: Lumen now has the basics they have: album and artist headers, track numbers, sortable column titles, grouped search and ReplayGain. It still falls short on disc handling (L-1), true gapless (L-2, which foobar and MusicBee are known for), the album year and a covers grid on the artist page, and a link from the album to the artist.
- **Plex / Jellyfin / Infuse (series)**: season grouping, "Смотреть S01E01", "Следующая серия" and "осталось" on Home are now in the same spirit. Gaps: episode titles (raw file names) and watched marks on episode cards.
- **Lyrics (Apple Music / Spotify / MusicBee)**: synced .lrc with click-to-seek is a good first step. The double highlight (L-3) and the size mismatch (L-9) look unfinished. No embedded or online lyrics (owner decision, STATUS section 6).

## Top 5

1. **L-2 (Major)**: gapless is advertised and on by default, but every track change flushes the audio device. That is the one feature album listeners (live, classical, DJ mixes) notice at once.
2. **L-1 (Major)**: multi-disc MP3 albums play d1t1, d2t1, d1t2… The disc is never read for MP3 (PartOfSet is not used).
3. **N-4 / RAW-17 still open**: the mini player hover controls still collide with the cover art.
4. **L-3 + L-4**: lyric polish. The clicked line stays highlighted, and the .lrc also shows up as a fake subtitle track.
5. **L-5**: on a big library, genres show "Читаем теги…" for over a minute at ~80% CPU after the scan has already finished.

Overall, round 2's Blocker (N-1) and most collector complaints (headers, № column, column titles, artist/album search, series, "осталось", way back to the library) are fixed.