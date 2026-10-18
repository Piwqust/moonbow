# Round 7: library (r7-library)

Branch `worktree-agent-a1fd40ec38df17446`, on top of main 4f1b361. Base exe for comparison: the same tree before my commits, built into `build\base\Lumen.exe` of the worktree; new exe `build\new\Lumen.exe`. Evidence: `%TEMP%\lumen-qa\r7lb-*` (shots in `r7lb-shots`, caches and diffs in `r7lb-cmp`, sweeps in `r7lb-sw-base`, `r7lb-sw-new`, `r7lb-sw-lib`). Every test instance ran with `--automation`, a data dir under `%TEMP%`, a test library, and was closed or killed by its own PID.

| Item | Status | Commit |
|---|---|---|
| 1. Own tag parser (`src/library/TagReader.*`) | done | 3b1d21e, 72b8e9e |
| 1a. Embedded lyrics for the lyrics view | done | 3b1d21e |
| 1b. Cache version 5 (album artist, first-seen time) | done | 3b1d21e |
| 2. Scan banner, "Новое" and "Битый файл" badges (MARKET P2-6) | done | 72b8e9e |
| Extras: album artist on album cards/pages, Matroska length, embedded covers for thumbnails | done | 72b8e9e |

## 1. The tag parser

### What was built

`src/library/TagReader.h/.cpp`, about 1 500 lines, no dependencies beyond Win32 (`MultiByteToWideChar`) and `json::LegacyCodePage`. The format is sniffed from the content, not the extension.

- **MP3.** ID3v2.2, 2.3 and 2.4: text frames in all four encodings (Latin-1, UTF-16 with BOM, UTF-16BE, UTF-8), NUL-separated 2.4 lists (joined with "; " as the property system does), whole-tag and per-frame unsynchronisation, extended headers, grouping and data-length flags, compressed/encrypted frames skipped, iTunes' non-syncsafe 2.4 sizes, several tags in a row. Fields: TIT2, TPE1, TPE2, TALB, TRCK/TPOS `n/m`, TCON (`(17)`, `17`, `(17)Rock`, `(RX)`, `(CR)`), TYER/TDRC (TORY/TDOR as a fallback), TXXX (album artist, lyrics, the rest kept as `extra`), USLT, SYLT (millisecond stamps; karaoke word stamps joined into lines), APIC. Then the APE tag and ID3v1 at the end (priority ID3v2 > APE > ID3v1, each only fills what is empty).
  - Duration: Xing/Info frame count minus LAME's encoder delay and padding (the gapless length ffmpeg/mpv show), VBRI, else CBR from the first frame (validated by the frame after it).
- **FLAC**: STREAMINFO, VORBIS_COMMENT, PICTURE (an ID3v2 tag in front is skipped). Metadata cut off before the audio gives no length.
- **Ogg Vorbis and Opus**: the comment packet (reassembled across pages), METADATA_BLOCK_PICTURE / COVERART; duration from the last page's granule (Opus minus pre-skip).
- **MP4/M4A/MOV**: mvhd (and mehd for fragmented files); `udta/meta/ilst` with ©nam, ©ART, aART, ©alb, trkn, disk, ©gen, gnre, ©day, ©lyr, covr, ©wrt. No moov, or a moov past the end of the file, is reported damaged.
- **WAV** (`fmt`/`data`, LIST INFO, `id3 `/`ID3 ` chunk) and **AIFF/AIFC** (COMM with the 80-bit rate, ID3 chunk, NAME/AUTH).
- **APE tags** at the end of Monkey's Audio, WavPack and Musepack; durations from the WavPack and Monkey's Audio headers.
- **Matroska/WebM**: the duration only (Segment > Info), so video scans skip the property system too.
- Legacy 8-bit text (ID3v1, Latin-1 frames, RIFF INFO): ASCII as is, valid UTF-8 as UTF-8, else the system ANSI code page from `GetLocaleInfoEx` (the round 5 rule: never CP_ACP, which the manifest makes UTF-8). On this PC that is CP-1251.

Safety: reads go through a `Reader` that checks every size against the bytes that exist, and has a budget of 4 096 reads and 48 MB per file; every loop advances or stops. Only headers and tag regions are read: frames are walked by their headers, so a 10 MB cover in front of the text costs nothing unless the cover is asked for. `FileSource` keeps four 16 KB blocks, so most files cost one or two `ReadFile` calls.

### How it is used

`MediaLibrary::ReadMetadata` opens each file once (`FileSource`), applies the LB-12 header check, then runs the reader (tags for audio, duration only for video). The Windows property system stays as the fallback for formats the reader doesn't know (AVI, WMV/WMA, ...), for durations it couldn't get (CBR-less MP3 junk, Musepack, cut FLAC), and for the tags of `.mka`. It only fills what is still empty.

`library-cache.json` is version 5: `aa` (album artist), `f` (first-seen time), `o` (an old-format entry of an unplugged drive). A version 4 cache is read again from the files, but its paths still count as known, so an upgrade does not make every file "new".

Album cards and the album page use the album artist when it is tagged ("Various Artists" on a compilation instead of "Various artists" computed from mixed track artists).

### Embedded lyrics

`src/app/player/Lyrics.cpp`: `LoadEmbeddedLyrics(path)` (SYLT synced first, then USLT / LYRICS / UNSYNCEDLYRICS / ©lyr / APE Lyrics through `ParseLyrics`, so LRC text in a USLT stays synced) and `LoadLyricsFor(path)` (the `.lrc` wins). The music view's worker thread now calls `LoadLyricsFor` (a 3-line change in `MusicView.cpp`, outside my scope, see the end). The mpv-metadata path stays as the last fallback. New: SYLT (mpv/ffmpeg ignore it) and FFmpeg's `TXXX:USLT`.

### Covers

`Thumbnails::ExtractThumbnail`: when the shell has no thumbnail for a song, the cover the reader finds is decoded with WIC (scaled with Fant) before the `cover.jpg` lookup. This gives covers to Opus/Vorbis (METADATA_BLOCK_PICTURE), WavPack, AIFF and WAV files.

### Verification

- `tests\build-tests.bat`: 30 tests, 0 failures. New: `tag_reader` (a hand-built sample of every format, every field above, CP-1251 ID3v1, APE over ID3v1, unsync, iTunes sizes, SYLT, cut MP4, cut FLAC, Matroska), `tag_reader_fuzz` (every truncation of 13 samples, 4 000 random mutations each with 0xFF/0x7FFFFFFF/0 size fields, 3 000 garbage files behind each magic: about 88 000 parses in ~85 ms, no crash, no hang; and a 1 TB "endless" garbage source behind each magic: at most 4 097 reads and 64 MB), `library_tag_reader` (scan of real files, album artist, gapless MP3 length, damaged M4A, video duration only, first-seen times across scans and sessions), `lyrics_embedded`, and a cover test in `thumbnails`.
- Generated media: `%TEMP%\lumen-qa\r7lb-media` (script `make_r7lb_media.py` in the session scratchpad): ffmpeg ID3v2.3/2.4 with covers, VBR MP3, hand-made ID3v2.2 with PIC, CP-1251 ID3v1, a 2.4 tag in four encodings with SYLT and TXXX, iTunes sizes, an unsynchronised 2.3 tag, APE + ID3v1 MP3, FLAC with PICTURE, Vorbis, Opus, Opus with a METADATA_BLOCK_PICTURE cover, M4A AAC with cover and lyrics, ALAC, WAV INFO, WAV with an id3 chunk, AIFF with ID3, WavPack with APE, and broken files (garbage, a 268 MB ID3 size in 25 bytes, M4A cut in half, FLAC cut at 200 bytes, an empty FLAC).

### Old path vs new, every field (`r7lb-cmp\*.json`, `cmp.py`)

Same cache fields compared item by item (`python cmp.py base.json new.json 0.05`); durations within 50 ms count as equal. The damaged flag (`b`) is identical on every set.

| Set | Files | Differences (new vs old) |
|---|---|---|
| `media` | 19 | 4 MP3 durations: 50.077 → 50.000 s etc. |
| `worst` | 1 415 | 1 334 MP3 durations (1.076 → 1.000 s, 3.972 → 4.000 s), "Ten hours.mp3" 32 727.6 → 36 000 s, "Long lyric lines.mp3" 57.93 → 60.00 s, "Audio with a video extension.mkv" 0 → 4.0 s |
| `big` | 3 500 | 1 332 MP3 durations (gapless, as above); the 500 MKV and 1 000 MP4 durations equal |
| `r7lb-media` | 24 | album artist on 14 files; CP-1251 title/artist/album readable on 2 files; WAV INFO, WAV id3 chunk and AIFF tags (were empty); AIFF duration 0 → 6 s; APE wins over ID3v1 on "12 APE tagged.mp3" (title, artist, album, year, genre, track 12, disc 1); genre "13" → "Pop"; FLAC and WAV year 2019 (was 0); WavPack / AIFF disc 2 (was 0); "id3 huge size.mp3" title "Huge" (its duration still 0) |
| `r7lb-huge` | 24 000 | the same kinds, multiplied by the hard links |

Every difference is an improvement:
- the MP3 lengths are now the gapless ones (LAME delay and padding removed), which is what ffprobe and mpv report (`ffprobe` on "01 No cover.mp3": 4.000000; "Ten hours.mp3": 36000.000000; "01 Long lyric lines.mp3": 60.000000; the MP3 named .mkv: 4.000000). The property system overcounted by 50-110 ms per file and misread the ten-hour file;
- CP-1251 text was mojibake (`������ �� ����� ������`) because the in-process MP3 handler decodes with CP_ACP, which the manifest makes UTF-8; it now reads «Звезда по имени Солнце» / «Кино» / «Последний герой»;
- album artists, disc numbers, WAV/AIFF tags and years are new data; APE beats ID3v1 as in foobar2000 and Mp3tag;
- Opus lengths drop the 312-sample pre-skip (25.0065 → 25.000 s, within tolerance).

## 2. Scan banner and badges

### What was built

- `ScanBanner` (`src/app/library/Pages.h`, `Views.cpp`) replaces the `ScanBadge` pill. It is a slim strip above the scrolling content of Home and every library page (Videos, Music, Network, Playlists, Recent, folders), aligned with the content's sides: refresh icon, "Индексируем медиатеку… 412 из 1 284" ("Indexing your library… 412 of 1,284"; "найдено файлов: N" while files are still being counted), a 2 DIP progress line along the bottom edge, and a ghost "Скрыть" button (an icon button with a tooltip in the compact side-panel pages). Numbers are grouped with a no-break space (Russian) or a comma (English).
  - It shows only once a scan has run for 1 s, so short rescans never flash it, and it goes when the scan ends. While the page's empty state shows ("Сканируем медиатеку…"), the banner stays hidden: the empty state already says it.
  - "Скрыть" hides it for the rest of that scan on every page (the scan generation is remembered).
  - The text gets a tooltip when it is cut.
- `MediaLibrary::Progress()` (atomics: counting / done / total / elapsed) feeds it. The banner polls them on a 250 ms host timer only while a scan runs; with no scan there is no timer and no work.
- Two scan changes make the banner possible now that scans are fast and usually "inline" (one publish at the end): a scan still running after 400 ms posts the notify message once (pages refresh and start following it), and an inline metadata pass that outlives 1 s publishes what it has and continues as a second pass (the files show while slow ones are read).
- Cards and list rows: "Новое" ("New"), a chip on the accent, top left of the card (after the heart) or after the title in lists, for files first found since the user last opened the library. "Битый файл" ("Damaged file") is the old damaged chip with the new word; damage still follows the LB-12 rules (a header that matches no known media, or an MP4 without its index). A damaged file shows only its warning, a played file is never new.
  - "New" means `firstSeen > NewSince()`. `firstSeen` is set by the scanner for files not in the cache under a root the cache knows (a first scan of a library or of a newly added folder marks nothing). `NewSince` is the `seen` time in `library.json`, fixed for the session; when a (non-compact) library page shows a snapshot with newer files, `MarkSeen` saves "now", so what was shown is not new next session. It writes only when there are newer files.

### Verification

- Banner on a 24 000-file hard-link library (`r7lb-huge`, built by `make_huge.py`): `r7lb-shots\huge1-*.png/.tree.txt`. Visible at 0.7-3.5 s with "7 354 из 24 000" … "22 167 из 24 000" and `value=0.31…0.92`, hidden after the scan.
  - Light theme with a real (posted) click on "Скрыть": `bn-light-shown.png`, `bn-light-hover.png`, `bn-light-hidden.png`; the tree after the click and after `nav music songs` shows the banner hidden on the new page too.
  - English at `--scale 1.5`: `bn-en15-shown.png` ("Indexing your library… 664 of 24,000", "Hide").
  - Minimum window (720x440): `bn-min-shown.png`, the text fits (no `trunc`).
  - Compact (the library beside the player, `action media.sidebar` while playing): `compact.png`, "Индексация… 18 905 из 24 000" with the × icon button and its "Скрыть" tooltip.
- No flash: the tree dumped every ~30 ms for 3 s during first scans of `media`, `big` and `worst`: the banner was never visible (`media` 101 dumps, `big` 118, `worst` 113).
- Badges across sessions (`r7lb-newbadge.ps1`, `r7lb-shots\nb-*`): session A (first scan) 0 "Новое"; files added, session B: 3 on Home cards, 2 song rows + 1 "Битый файл" (a cut M4A), 1 video card + 1 "Битый файл" (a text file named .mp4); session C (library.json kept with `seen`): 0 "Новое", the damaged chips stay.
- Sweep, library scenarios (`-Filter '^(home|videos|music|worst|recent|search|network|playlist)'`, 81 scenarios): base 8 findings, all low `text-trunc` (pre-existing); new: the same 8, no medium or high.
- New scenarios in `qa/round5/scenarios.json`: `lib-new-songs` (dark/light), `lib-new-home` (dark/light × 1024x640/1920x1080), `lib-new-scale` (English, 150 %), `lib-damaged` (dark/light). They seed `library.json` with a `seen` time and a version 5 cache that lacks two files. Run `r7lb-sw-lib`: 9 scenarios, 0 findings; the trees show 1 "Новое" (songs, Home), 7 "New" (scale), 2 "Битый файл" (Mixed folder).
- `check-icons.py`, `check-settings.py`, `i18n_check.py`: clean. Build: no warnings at /W4.

## Performance

First scan, `-Settings {"library.thumbnails":false}`, fresh data dir, time = `library-cache.json` written with every file minus process start; CPU = the process total. Base and new alternated (`r7lb-perf.ps1`, `r7lb-perf.txt`), files in the OS cache (hard links).

| Set | Base (3 runs) | New (3 runs) |
|---|---|---|
| `worst` (1 415 files, incl. the 1 284-file `Big`) | 5.12 / 5.17 / 5.05 s, CPU 17.6 / 17.5 / 17.0 s | 0.153 / 0.148 / 0.154 s, CPU 0.50 / 0.58 / 0.42 s |
| `big` (3 500: 1 332 MP3, 668 FLAC, 1 000 MP4, 500 MKV) | 17.6 / 17.1 / 17.4 s, CPU 59.5 / 57.4 / 56.6 s | 0.335 / 0.325 / 0.295 s, CPU 0.83 / 0.81 / 0.73 s |
| `r7lb-huge` (24 000, one run each) | 145.8 s, CPU 442.9 s | 2.19 s, CPU 5.1 s |

The parser alone (`tagdump.exe` over the folders, warm): 0.07-0.10 ms per file (3 500 files in 332 ms on one thread, MKV included). Before, the property system cost ~10-16 ms of CPU per file.

Idle after a scan of `big` with Home shown, 10 s: base 16 ms CPU, new 0 ms; 85 threads (base 83); working set 109 MB / private 98 MB (base 122 / 108). The banner's timer exists only during a scan (the tree shows `timer` while it runs and not after). Nothing new runs on the UI thread: reading stays on the scan threads; `MarkSeen` writes `library.json` only when files newer than the saved time are on screen (once per batch of new files).

## Left out, and why

- **The banner is not in the sweep.** Every sweep library now scans in well under a second, so a scenario can't catch it deterministically; it was checked with the 24 000-file library instead (above).
- **MPEG-frame SYLT stamps** (format 1) are skipped: rare, and they need the frame duration of the stream.
- **AVI, WMV/WMA, Musepack durations** stay with the property system.
- **No new damage rules.** A truncated MP3 or FLAC is not called "Битый файл" (LB-12 asked for no labels on playable files); a FLAC whose metadata is cut gets no length from the reader and the property system decides, as before.
- **Covers are not cached separately.** The embedded-cover fallback re-reads the tag on a thumbnail worker; it runs only for songs the shell has no thumbnail for.

## Changes outside my file scope

- `src/app/player/MusicView.cpp`, `RefreshLyrics`: the worker calls `LoadLyricsFor(media)` instead of `LoadLyricsFile(lrc)` (3 lines), so embedded SYLT/USLT lyrics reach the lyrics view.
- `qa/round5/scenarios.json`: the four `lib-*` scenarios.
