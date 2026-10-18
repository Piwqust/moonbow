# Round 7: online features (r7-online)

MARKET.md N-8 (LRCLIB lyrics), N-1 (OpenSubtitles) and N-7 (TMDB posters and descriptions), on the owner's terms: no built-in keys, every feature off by default, the user's own key in Settings.

Branch `worktree-agent-ac8a5c0d1dc7c87bf`, on top of main 4f1b361. Base exe for comparisons: `%TEMP%\lumen-qa\r7on-base\Lumen.exe` (main 4f1b361). Evidence: `%TEMP%\lumen-qa\r7on-shots\*`, `%TEMP%\lumen-qa\r7on-sweep-{before,after}\`. Every test instance ran with `--automation` and `-Library` set to a test folder (`%TEMP%\lumen-qa\r7on-media`, `r7on-media-err`, `r7on-ro` or the shared `media`), and was killed by its own PID.

Network use during testing: LRCLIB for real, 4 requests in all (lrclib.net only). OpenSubtitles and TMDB only against `qa/round7/online-mock.py` on 127.0.0.1. Nothing was downloaded or installed.

## 1. What was built

### The net module (`src/net/`)

| File | What it does |
|---|---|
| `Http.h/.cpp` | Async WinHTTP client. HTTPS only (plain http only to 127.0.0.1/localhost in `--automation` runs). Timeouts per request (15 s, LRCLIB 20 s). User-Agent `Lumen/<version>` (`Lumen v<version>` for OpenSubtitles, whose API requires that form). gzip/deflate through `WINHTTP_OPTION_DECOMPRESSION`. System proxy (`WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`). |
| `Cache.h/.cpp` | Disk cache in `<data>\online\cache` (one file per answer, TTL in the file, key stored to rule out hash collisions, written with `json::WriteFileAtomic`), and a 2 MB in-memory LRU in front of it. |
| `Secret.h/.cpp` | DPAPI (`CryptProtectData`, current user, app entropy) as `dpapi:<base64>`. |
| `OnlineData.h/.cpp` | Pure logic, unit-tested: moviehash, movie/episode guess from a file name, LRCLIB / OpenSubtitles / TMDB JSON mapping, the OpenSubtitles query string. |
| `Online.h/.cpp` | The three features on top: settings, requests, the metadata store, image downloads, error texts. |

How the client works:
- At most two worker threads. They start with the first request and exit after 20 s without work. The last one also closes the WinHTTP session, so no keep-alive socket stays open.
- `net::Send` is called on the UI thread. The callback runs on the UI thread, never inside `Send`. `ui::Host::Post` is not thread-safe (a plain vector), so the workers post to the net module's own message-only window, the same pattern as `Thumbnails` and `MediaLibrary`. The window is created with the first request.
- A cache hit never touches the network: the memory LRU answers on the UI thread (still through the queue), the disk cache on the worker.
- Requests to one host are at least 220 ms apart (OpenSubtitles allows 5 per second). A 429 blocks the host for its `Retry-After` / `ratelimit-reset`; meanwhile requests to it get a local 429 without being sent.
- Offline state: a DNS, connect or timeout failure marks the client offline for a minute. Background requests (lyrics, posters) then fail at once without trying, and say nothing. User actions (search, download) still try and show the reason.
- `LUMEN_TRACE=1` logs every request that reaches the network to `%TEMP%\lumen-net-<pid>.txt` as `<ms> GET host/path -> status`: no query strings, so no keys and no titles.
- `winhttp.dll` and `crypt32.dll` are delay-loaded (`build.bat`). A session with the features off never loads WinHTTP (checked, see section 4).

### Keys and passwords

Every `secret` setting goes to settings.json as `dpapi:...` (`Config::ToJson`) and is decrypted on load (`Config::ApplyDocument`). This covers the two new keys, the OpenSubtitles password and the existing HTTP proxy password. In memory the values stay plain text, so the masked fields show the right number of dots. A blob from another Windows account reads as empty. A plain value from an older file is read as is and saved encrypted next time.

### LRCLIB lyrics (N-8)

- Setting **«Искать тексты песен в интернете (LRCLIB)»** in Privacy › Network, off by default.
- The music view asks only after its own sources answered: the `.lrc` next to the file (loaded on a worker as before) and the embedded tag (`ReadEmbeddedLyrics`, the existing source the tag-parser agent changes). It asks only for audio files with an artist and a title tag. A tag that arrives later still replaces the online lyrics.
- Request: `/api/get?artist_name&track_name&album_name&duration`. On 404, or on a 5xx (their upstream lookup failing, seen live), it falls back to `/api/search` and picks synced lyrics within 3 s of the track's length. Answers are cached for 30 days, a 404 for 3 days.
- Lyrics from LRCLIB show in the existing synced column with a **«LRCLIB · Сохранить .lrc»** button under it: focusable, with a tooltip. Right-clicking the lyrics gives **«Сохранить текст как .lrc рядом с песней»**. Only these two write a file. The write happens on a worker, never replaces an existing file, and reports in a toast (saved, already exists, read-only folder). The file has a BOM, `[ar]`, `[ti]`, `[al]` and `[re:LRCLIB]` tags, and CRLF line ends.

### OpenSubtitles (N-1)

- Setting **«Искать субтитры в интернете (OpenSubtitles)»**, the API key field (masked), and an optional user name and password, all in Privacy › Network.
- **«Найти в интернете»** appears in the Add Subtitles footer whenever a file plays, and as a search button in the subtitle panel's External Tracks title. Both open the **«Субтитры из интернета»** dialog:
  - the title is prefilled from the file name, plus season and episode for SxxEyy names;
  - languages: the preferred subtitle language from Settings, then English; each one alone, or all;
  - **Найти** (Enter in any field) searches.
- The search sends the moviehash (computed on a worker from the first and last 64 KB; files under 128 KB have none), the query, the season, the episode and the languages. Parameters are sorted, values lower case, spaces as `+`: the redirect-free form their docs ask for. Answers are cached for 6 hours.
- Each row shows the release name (cut with a tooltip), the language, the downloads with the right Russian plural, the rating, and flags for hearing-impaired, foreign parts only and machine/AI translation. Files made for this exact file (moviehash match) come first, with a check mark.
- Download: one click or Enter on a row.
  - Logs in first when a user name and password are set: once per session, and never again with credentials the server refused.
  - Then `POST /download`, then the file from the returned link. Only `*.opensubtitles.com` / `.org` links are accepted (loopback in tests).
  - The file is saved next to the video as `<video>.<lang>.srt` (`.2`, `.3`... when taken). When the folder is not writable, it goes to `<data>\subtitles`.
  - It is loaded at once with `sub-add` and remembered in `subtitle-pairs.json`, so a copy in the data folder comes back next time.
- Quota: the footer shows the downloads left (from `/infos/user` when logged in, or from the last download). A 406 shows the daily-limit message with the reset time from `reset_time_utc` in the UI language.

### TMDB (N-7)

- `privacy.fetchMetadata` and `library.metadata` are no longer hidden. They are one switch shown in two places, kept in step by a listener (`net::online::Init`): **«Загружать постеры и описания фильмов и сериалов (TMDB)»** in Privacy › Network, with the key field, and «Названия и обложки» in Library › Metadata. The never-implemented "Don't fetch" choice is gone.
- What is looked up: library videos whose name reads as a movie **with a year** ("Inception.2010.1080p…", "The Matrix (1999)", "Blade Runner 2049 (2017)") or an episode (the existing `ParseEpisode`; a name without the show takes it from the folder above "Season N"). A name without a year ("Holiday at the lake") is never looked up automatically.
- Only the cards a view paints ask, one file at a time. The queue holds at most 120 entries; forgotten ones ask again when painted.
- Requests: `/search/movie` or `/search/tv` (with the year), then `/tv/{id}/season/{s}/episode/{e}` for episodes, in the UI language (ru-RU / en-US). A v4 read token goes in `Authorization: Bearer`, a v3 key in `api_key`. Searches are cached for 14 days, episodes for 30.
- Records live in `<data>\online\metadata.json`, loaded on a worker the first time. Images go to `<data>\online\images`, named by their TMDB path and size (`w342` poster, `w780` backdrop, `w300` episode still), and are drawn through the existing `ImageCache` / thumbnail service at the size each view asks for. An image already on disk is never downloaded again (`keepExisting`, plus the in-memory "have" set). A "not found" is asked again after 14 days.
- On cards (`ItemGrid`): the video's own thumbnail gives way to the episode still or the movie backdrop once downloaded (16:9 fits the cards). A chip **«2010 · ★ 8,4»** sits top right. The card layout doesn't change. "Continue watching" frames stay.
- In Media Information: a block with the poster, the title, `S02E05 · episode title` for episodes, year and rating, the overview (5 lines, full text in a tooltip), **«Найти другое совпадение»**, **«Сбросить»** and «Данные TMDB». It rebuilds when the lookup finishes while the dialog is open. Without a key, or with a rejected one, it says so in one line.
- **«Найти другое совпадение»** (also in the library card menu as «Найти другое совпадение…» / «Сбросить совпадение») opens a dialog: title, year, Фильм/Сериал, Найти, then a list of results with their overviews. Picking one stores a manual match. **Сбросить** stores a manual "none": the card goes back to the file's own thumbnail and is not matched again by itself.
- A 401 stops all TMDB lookups for that key until it changes. A 429 is retried twice, 10 s apart, then the file waits for the next session.
- Attribution: TMDB's sentence "This product uses the TMDB API but is not endorsed or certified by TMDB" is in the setting's description, word for word, with a Russian translation in brackets. About has a new «Онлайн-сервисы» block with links to LRCLIB, OpenSubtitles.com and TMDB, and the sentence.

### Privacy

- All three are off by default (unit test `secret_settings`). Each description says what is sent and to whom:
  - lyrics: artist, title, album and length, to lrclib.net, only for songs without lyrics;
  - subtitles: hash, name, season, episode and languages, to opensubtitles.com, only when you search;
  - TMDB: title, year, season and episode, to themoviedb.org; images from image.tmdb.org.
- «Очистить историю» also clears the online data. A new button in Privacy › Network does it alone: «Загруженные тексты, постеры и результаты поиска › Очистить» (the disk and memory caches, metadata.json, the images). Downloaded subtitle files stay: they are the user's files.

## 2. How to use it

1. Settings › Конфиденциальность › Сеть: turn on what you want and paste your key.
2. Lyrics: play a song with an artist and a title tag and no lyrics; the lyrics column fills in. Click «LRCLIB · Сохранить .lrc» to keep them next to the song.
3. Subtitles: while a video plays, use «Добавить субтитры › Найти в интернете» or the magnifier in the subtitle panel. Click a result to download it and turn it on.
4. Posters: open the video library. Movies named with their year and SxxEyy episodes get their artwork and a year/rating chip. Ctrl+I shows the details. A wrong match: «Найти другое совпадение» there or in the card's menu.

## 3. How to get a key (for the owner)

- **LRCLIB:** no key and no account. Just turn the switch on.
- **OpenSubtitles:**
  1. Create a free account at https://www.opensubtitles.com and sign in.
  2. Open your profile, then **API consumers**, and create a consumer (any name, e.g. "Lumen").
  3. Copy its **API key** into «Ключ API OpenSubtitles».
  4. Optionally enter your user name and password: with an account the daily download limit is the account's (higher with VIP) instead of the anonymous one.
- **TMDB:**
  1. Create a free account at https://www.themoviedb.org.
  2. Open **Settings › API** and request a key (type "Developer", personal use). It asks for a short description and an address.
  3. Copy either the **API Key** (32 characters) or the **API Read Access Token** (a long `eyJ…` string) into «Ключ API TMDB». Both work.

Keys are stored encrypted for your Windows account. A copied settings.json is useless elsewhere.

## 4. Verification

### Unit tests (`tests/core_tests.cpp`, 32 tests, 5770 checks, 0 failures)

| Test | Covers |
|---|---|
| `movie_hash` | moviehash against the reference algorithm (values from a Python implementation of their "Hash Source Codes") for 128 KiB, 200 000 and 1 000 003-byte files; under 128 KiB and missing files fail |
| `guess_video` | movies with year in dots/brackets/WEB-DL, "Blade Runner 2049 (2017)", "1917 (2019)", "2012", Cyrillic titles, no year, SxxEyy with and without the show, the show from the folder above "Season 2", "1x04" |
| `online_caches` | disk TTL, expiry deletes the file, binary bodies, collisions, Prune, Clear; memory LRU order, budget, expiry, oversize entries |
| `online_json` | URL encoding, host checks, LRCLIB records, search pick, `/api/get` path, the saved .lrc reads back as the same synced lyrics; the OpenSubtitles query string, search mapping (hash matches first, flags), download, login, quota, file names, the ISO reset time; TMDB movie/tv search, year pick, episode, key type |
| `secret_settings` | DPAPI round trip; settings.json holds `dpapi:` and never the plain key or password, which come back on load; a foreign blob reads as empty; all online settings off by default |
| `http_client` | through the real message loop, without the internet: plain http to the internet refused before any I/O, the callback never runs inside Send, a refused loopback port gives a network error and then the offline state (the next background request isn't sent), disk-cache and memory-cache answers without the network, Cancel, Async runs on a worker and returns on the UI thread |

Checks: build at /W4 with no warnings; `check-icons.py` 0 missing; `check-settings.py` 0 of 231 settings have no reader; `i18n_check.py` 0 conflicts.

### LRCLIB, real requests (4 in all)

| Step | Requests | Result | Evidence |
|---|---|---|---|
| "Queen – Bohemian Rhapsody", 355 s, no .lrc | `GET lrclib.net/api/get -> 200` | synced lyrics in the column, «LRCLIB · Сохранить .lrc» under it | `r7on-shots\lrclib-phase1.png`, `.txt` |
| click the button | none | `Queen - Bohemian Rhapsody.lrc` (2681 bytes, `[ar:Queen]`…) written; toast «Текст сохранён: …»; a second click: «… уже есть» | `lrclib-saved-crop.png`, `lrclib-saved-again-crop.png` |
| unknown song | `api/get -> 503` (their side), then after the fix `api/get -> 404`, `api/search -> 503` | nothing shown, no toast; the 404 cached, the 503 not | log in this report |
| the first song again, .lrc moved away | none (disk cache) | the same lyrics | phase 3 |
| the first song with its .lrc | none | local lyrics, no button | phase 4 |

### OpenSubtitles and TMDB against the mock

`qa/round7/online-mock.py <port> <log> <images>` binds 127.0.0.1 only. It answers in the services' shapes, logs every request (method, path, User-Agent, whether Api-Key/Authorization were sent), and has error switches: key `bad` gives 401, "ratelimit" in the query gives 429 with `Retry-After: 3`, "slow" answers after 25 s, and downloads are limited to 3 per run, then 406. The hidden settings `privacy.{lrclib,opensubtitles,tmdb,tmdbImage}Server` point Lumen at it and work only in `--automation` runs.

| Case | What happened | Evidence |
|---|---|---|
| OS search | `GET /api/v1/subtitles?languages=en&moviehash=4605ca8601c90482&query=inception`, UA `Lumen v0.1.0`, Api-Key sent; 3 rows, the hash match first with a check, plurals «12 345 загрузок», flags | `os-results.png`, mock log |
| OS download | `POST /download -> 200`, link `-> 200`; `Inception.2010.1080p.BluRay.x264.en.srt` next to the video, selected at once, remembered in subtitle-pairs.json; more downloads `.en.2.srt`, `.en.3.srt` | `os-panel.png` |
| OS quota | 4th download `-> 406`: «Дневной лимит загрузок OpenSubtitles исчерпан. Он обновится через …» | `os-quota.txt` |
| OS read-only folder | video folder denied for new files (icacls on my own test folder, removed afterwards): saved to `<data>\subtitles\Inception (2010).en.srt` and loaded | `os-ro.png` |
| OS 401 | «OpenSubtitles не принял ключ API. Проверьте его в «Настройки › Конфиденциальность».» | `os-401-login.png` |
| OS 429 | «Слишком много запросов к OpenSubtitles. Повторите через 3 с.» | `os-errors.png` |
| OS timeout | after 15 s: «OpenSubtitles не ответил вовремя. Попробуйте позже.» | `os-errors.png` |
| OS no server | closed port: «Нет связи с OpenSubtitles. Проверьте подключение к интернету.» | `os-errors.png` |
| OS login | wrong password: `POST /login -> 401` once, the note in the footer, downloads continue without an account; right password: `/login -> 200`, then `/infos/user` with `Bearer`, downloads left in the footer | mock log, `os-login-*.png` |
| OS off | dialog with disabled fields, the key explanation and «Открыть настройки» | sweep `online-subs-off.*` |
| TMDB cards | `search/tv`, `tv/2316/season/2/episode/5`, `search/movie`, then the still and the backdrop; Inception card shows the backdrop and «2010 · ★ 8,4»; "Holiday at the lake" untouched | `tmdb-videos.png`, `tmdb-episode.png` |
| TMDB restart | no TMDB request at all; the info dialog then fetched only the poster (`w342`), once | net log |
| TMDB info | poster, title, year/rating, overview, actions, «Данные TMDB» | `tmdb-info.png` |
| TMDB rematch | the dialog searched "matrix"; picking The Matrix updated the info block and the card (new backdrop, «1999 · ★ 8,2») | `tmdb-match-matrix.png`, `tmdb-matched.png`, `tmdb-videos-matrix.png` |
| TMDB reset | the card shows the file's own frame again; the block says the match was reset | `tmdb-reset-combo.png` |
| TMDB 401 | one `search/movie -> 401` for three videos, then no more; info: «TMDB не принял ключ API…»; no toast | `tmdb401-info-crop.png` |
| TMDB 429 | three `429` 10.5 s apart for that file, then it waits; the next file got a local 429 (host held back) and was retried | net log |
| TMDB timeout | "Slow Movie (2002)": `error 3` (timeout) after 15 s, then offline: nothing more for a minute, no toast | net log |
| TMDB no server | one `error 2` (connect), then nothing; cards keep their own thumbnails, no toast | net log |
| settings.json | after a quit, `privacy.tmdbKey` is `dpapi:AQAAANCMnd8…`; the plain key from the launch file is gone | `r7on-tmdb\settings.json` |
| Clear online data | Settings › Privacy › «Очистить»: `online\` went from 13 files to none | `settings-cleared.png` |
| Clear history | «Clear history» › confirm (English UI): `online\` went from 5 files to none | `en-confirm.txt` |

### Nothing is sent unless enabled

Default settings: played a song (no lyrics), browsed Videos, played a movie named with its year, opened Media Information. Results:
- no `lumen-net-<pid>.txt` (no request reached the network);
- 0 TCP connections;
- `winhttp.dll` not loaded;
- paused CPU 0 ms over 10 s.

The base exe gives the same (`crypt32.dll` is loaded by both, by other components).

## 5. Performance

| | base 4f1b361 | this branch, defaults | this branch, all three on (mock) |
|---|---|---|---|
| idle library CPU over 10 s | 0 ms | 16 ms¹ | 16 ms¹ |
| paused video CPU over 10 s | 0 ms | 0 ms | 0 ms |
| threads, library page | 85 | 79 | 87 (2 net workers while busy) |
| threads 25 s later | 83 | 77 | 83 (the net workers exited) |
| private memory, library page | 94.1 MB | 93.3 MB | 96.4 MB |
| TCP connections 25 s after the last request | 0 | 0 | 2 (WinHTTP keep-alive to the mock), measured before the fix that closes the session with the last idle worker; not re-measured after it |

¹ One 15.6 ms scheduler tick. The defaults run shares it, and nothing of this module runs there (no thread, no window, no request), so it is measurement granularity, not a loop.

- UI thread: every file touch is on a worker: the moviehash, the .lrc write, the subtitle target check, the metadata store load and save, cache reads and writes, clearing.
- Painting: a card does one `unordered_map` lookup by path key.
- Memory cache: at most 2 MB. Metadata records: one small struct per looked-up video.

## 6. Sweep

The new scenarios are in `qa/round5/scenarios.json`, all named `online-*`:
- `online-settings-privacy`: dark, light, and 1.5 with the defaults;
- `online-subs-off`: dark, light;
- `online-subs-results`: dark, light × 1280x780, min;
- `online-info-tmdb`: dark, light, 1.5;
- `online-tmdb-match`: dark, light × 1280x780, min;
- `online-videos-tmdb`: dark, light;
- `online-lyrics`: dark, light × 1280x780, 1024x640;
- `online-about`: dark, light.

They point at the mock on port 18765. Start `python qa/round7/online-mock.py 18765 <log> <images>` first; without it the screens show their no-connection state.

Filter `^(settings-privacy|settings-library|dialog-subtitles|dialog-info|panel-tracks-subs|player-lyrics|videos-folder|online-)`, 45 captures, `-NoHitmap`:

| Run | Exe | Findings |
|---|---|---|
| `r7on-sweep-before` | base | 4 low (all existing: the PathField breadcrumb, a worst-case info path, editable fields) |
| `r7on-sweep-after` | first build | 2 medium, 2 low. The medium one: the subtitle dialog's status line, made hoverable for its tooltip, was 20 DIP high (`target-size`) |
| `r7on-sweep-after2` | fixed | **0 medium/high**, 2 low (the same existing ones) |
| `r7on-sweep-after3` | final, `^online-` only | **0 findings** |

Reviewed by eye from the captures (contact sheets `r7on-shots\sheet1.png`…`sheet3.png`, `en-combo.png`, `en-subs2-crop.png`):
- both themes, 150 % (settings, info), the minimum window (subtitle results, match dialog), English;
- English labels fit: "Episode" needed the 84-DIP field, fixed;
- the status line's tooltip appears only when the line is cut;
- the lyrics source button stays light on the always-dark music view in the light theme too.

Fixed during the sweep: the target size above, the scenario for episode cards (a route with spaces in `cmd`; it now uses the library root), and the toolbar widths at the minimum size.

## 7. Left out, and why

- **Card titles stay the file names.** The TMDB title appears in the info dialog and the year/rating chip on the card. Changing the card title would mean the entry builders in `Views.cpp` / `LibHome.cpp`, outside my scope, and search and sort would still use the file name.
- **No posters of search results** in «Найти другое совпадение»: every row would cost an image request; the list shows title, year and overview.
- **OpenSubtitles logout** at exit (their best practices ask for it): App.cpp has no exit hook in my scope. The token expires on its own after 24 h, and Lumen keeps it in memory only.
- **The automation `click` command** dereferences the widget after `OnClick` (`typeid(*target)` in `App::AutomationCommand`). When the click closes its dialog (Pick in «Найти другое совпадение», «Найти в интернете» in Add Subtitles), the test instance crashes. Real clicks and posted input are fine. I used coordinate clicks and the sweep's `click` selector. Fix for App.cpp: take the type name before `OnClick`.
- **"Offline" is the client's own view** (a failed connect within the last minute), not Windows' network list manager, which would mean COM and a notification sink for little gain.
- **Last.fm settings** stay hidden (not part of this round).

## 8. Changes outside the brief's file list

All are small and separate hunks:

| File | Change | Why |
|---|---|---|
| `src/core/Config.cpp` | `ToJson` writes `secret` settings as `dpapi:…`; `ApplyDocument` decrypts them | keys and passwords must never be plain text in settings.json; the settings rows only know Config |
| `src/app/SettingsActions.cpp` | «Очистить историю» also calls `net::online::ClearAll()`; new action `clear-online-data` | the privacy requirement; the button lives in the schema |
| `src/app/I18n.cpp` | one `#include "app/strings/online.inc"` | a strings file of its own, so other agents' additions to the shared .inc files don't conflict |
| `src/app/Dialogs.h` | `ShowOnlineSubtitlesDialog`, `ShowMetadataMatchDialog` | entry points for the panel and the library menu |
| `src/app/ImageCache.h` | `Refresh()` (emits `ready`) | in scope (`ImageCache.*`), listed for completeness |
| `tests/build-tests.bat` | compiles `src/net/{Cache,Http,OnlineData,Secret}.cpp` | Config.cpp now uses Secret.cpp; the new tests need the rest |
| `qa/round5/scenarios.json` | the `online-*` scenarios | the round's rule for new screens |
| `qa/round7/online-mock.py` | new | the mock server |

Suggested fix outside my scope: `App::AutomationCommand` "click" reads `typeid(*target)` after `OnClick`. It should take the name first, because the click may have destroyed the widget (section 7).
