# Round 8 fixes: storage (library, net, core, Skins, ImageCache)

Fixer: r8-fix-storage. Findings: qa/round8/raw/storage.md (11 + 9 left out), plus library-settings #1
(the same subtitle-extension issue) and playback #3 (the ui.json part of the takeover reload).

Verified:
- /W4 build with no warnings;
- tests\build-tests.bat: 47 tests, 0 failures;
- check-icons.py, check-settings.py and i18n_check.py: clean;
- `sweep.ps1 -Filter '^(home|home-history|videos|music-songs|music-albums|search|recent)\.(dark|light)$'`:
  14 scenarios, 0 findings.

The new tests were also run against the sources from before the fixes (4ed6a84), and they fail
there; see the "before" column.

## Numbered findings

| # | Status | Fix (file:line) | Checked: before, then after |
|---|--------|-----------------|-----------------------------|
| 1 | fixed | `readOnly_` in Config (Config.cpp:95, :102, :199) and MediaLibrary (MediaLibrary.cpp:525, :530, :550), as in History | Traced: LoadFrom returned false on a lock and kept `path_`, so the next Flush wrote defaults. Test `corrupt_backup`: with settings.json and library.json held open at Load, then a change and an AddRoot, the files used to be overwritten; now they are byte-identical, and they load and save normally once unlocked. |
| 2 | fixed | SaveCache writes cloud-only and not-yet-read files as stale entries that keep firstSeen (MediaLibrary.cpp:353); a scan that only found files saves too (:711, :744, :770, :844) | Traced: `it.offline` was skipped in SaveCache. Test `library_cache_tags`, with two FILE_ATTRIBUTE_OFFLINE files: before, the tagged one lost "Cloud" and the untagged one got a new firstSeen on every scan. Now both survive scans, and the file is read once it is local. |
| 3 | fixed | SubtitleFileName keeps the server's extension only when IsSubtitlePath accepts it, and maps `<>:"/\|?*` and control characters in the stem to `_` (OnlineData.cpp:331, :338) | Test `online_json`: before, `Movie.en.lnk` produced `Movie.en.lnk`, and `watch?v=a:b` and `cam:554` stems produced invalid names. Now they give `.srt` and `watch_v=a_b`. Runtime not run: it needs the OpenSubtitles dialog flow; Online.cpp:479 only calls this function. |
| LS1 | fixed | same change; the extra cases are covered too | Test: `x.a\b`, `x.s/rt` (subfolders), `x.:abc`, `x.srt:s` (NTFS streams), `..\..\evil.srt` and `x.srt\..\y.exe` all give a name with no `\/:*?"<>|` and a subtitle extension. |
| 4 | fixed | FlushStores takeover reloads config_, ui_ (`ui_.clear(); LoadUiState()`) and library_, and rescans when the folders changed (App.cpp:282-290) | Runtime, two --automation instances on one data dir. A saves theme=light. B (read-only) sets autoHide, navigates and resizes. A's last ui.json gets a key and library.json a root, then A is killed. **Before** (pre-fix build): within 3 s B wrote settings.json without A's theme (with its own autoHide) and ui.json without A's key. **After:** settings.json was not rewritten (theme=light kept); ui.json was rewritten with A's key kept plus B's window placement; library.json kept. Covers playback #3. |
| 5 | fixed | Duplicate-key check through a local hash map once an object passes 16 members (Json.cpp:22, :292) | Bench (release, parse of N unique keys): before, 20k keys took 207 ms and 50k took 1545 ms (quadratic, about 25 s for 200k); after, 3.5 ms, 8.6 ms and 41 ms for 200k. Test `json_basics`: 200k keys in under 1.5 s, and the first-position, last-value rule is kept in large objects and across the 16-member threshold. |
| 6 | fixed | `if (!OsHostAllowed(s.base)) s.base.clear();` after the login (Online.cpp:320) | Traced: base_url went straight into DownloadBase() with the Api-Key and Bearer token. No unit test: Online.cpp needs App. In --automation the base is always cleared, so it can't be exercised at runtime either. |
| 7 | fixed | SharedDisk() prunes expired entries once, on creation (Cache.cpp:203). Its first use is always on a net worker (Http Process and Online's Async). The TMDB-only prune is gone | Traced: Prune was called only in MetaStore::Load. Now every feature's first cached request prunes. |
| 8 | fixed | Finish saves only when the lookup chain stops (queue empty, paused, offline or off), after RunNext (Online.cpp:784-794). The unused `key` parameter is gone | Measured: one Save (build plus Serialize, UI thread) costs 1.3 / 5.5 / 12.9 ms for 500 / 2000 / 5000 records. Before, that cost came after every lookup, so a 120-file batch on a 2000-record store spent about 660 ms on the UI thread. After, it is paid once per batch. Trade-off: results finished since the last stop are not saved if the app exits mid-batch. They come back from the 14/30-day HTTP disk cache next session, with no network. |
| 9 | fixed | ClearAll calls Store().Save() after Clear() (Online.cpp:1076). An in-flight save sets saveAgain_, so the empty store is written last | Traced the race: an in-flight Save on worker 1 could finish after the delete on worker 2. Now the empty store always lands after it. |
| 10 | fixed | A 2xx answer is cached only when it parses as JSON (Http.cpp:411). The Http.h comment is updated | Test `http_client` uses a local Winsock server. Before, the HTML 200 was stored in the disk cache. Now it is not (the second request reaches the network), and the JSON 200 is still cached. |
| 11 | fixed | One exported `library::IsReleaseTag` (MediaTypes.cpp:148): the union of both lists, ignoring case and dashes. The OnlineData copy is deleted | The test checks WEB-DL, webdl, Remastered, INTERNAL, x-264, DDP5 and rejects Pilot. Episode titles now also stop at dvd, remux, xvid, extended, imax and the like. The existing episode and GuessVideo tests pass. |

## Left out

| Item | Status | Fix (file:line) | Checked |
|------|--------|-----------------|---------|
| ImageCache::missing_ grows without limit | fixed | Now an unordered_set, cleared in one go past 20 000 entries (ImageCache.cpp:15, :38) | Traced. Clearing only means a file is asked once more (thumbs_.KnownMissing answers). |
| FileSettings::Evict is O(n²) on a hand-edited file | fixed | One nth_element over the entries (FileSettings.cpp:180-191) | Test `file_settings`: a 30 000-entry file loads and evicts in 45 ms and keeps the newest 5000. The new test also runs on the old code. |
| net::Cancel set grows | fixed | An `open` set of undelivered ids; Cancel records only those (Http.cpp:86, :464, :487, :507) | Traced: Cancel of a delivered id stayed in `cancelled` for good. The `http_client` cancel test still passes. |
| A failed DPAPI decrypt drops the key on save | fixed | The undecryptable blob is kept in `sealedKept_` and written back until the key is set again (Config.cpp:168, :178, :339) | Test `secret_settings`: before, a save dropped the blob. Now it stays, and setting a new key replaces it. |
| Proxy password not URL-encoded | fixed | Percent-encoded except unreserved characters (SettingsSchema.cpp:250). FFmpeg's http auth URL-decodes credentials | Test `config_mpv`: `p@ss:w/rd %` gives `p%40ss%3Aw%2Frd%20%25`. |
| FrameAt overread when stride < width | fixed | A stride shorter than a row is refused (Thumbnails.cpp:249) | Traced: only `pitch*h > length` was checked, so the last row could read `w*4 - pitch` bytes past the buffer. |
| Subtitle unique() name gives up after 99 tries | fixed | Returns empty, so nothing is replaced. The folder falls back to the data dir, and the data dir reports "Couldn't save the file." (Online.cpp:494) | Traced: it returned the existing 99th name, and the download then replaced that file. |
| A non-image TMDB body is kept forever | fixed | A network 200 whose content-type isn't image/* is deleted on a worker and not asked again this session (Online.cpp:812) | Traced: keepExisting meant a captive-portal page saved as a .jpg was never fetched again. The round 7 mock sends image/jpeg, so normal posters are unaffected. |
| FitShellText underflows at capacity 1 | fixed | `capacity <= 1` returns "" (ShellText.h:13) | Test `shell_text`: before, capacity 1 returned the whole text plus "…". Now it returns "", and capacity 2 returns "…". |

## Coordinator item (after the pages merge)

| Item | Status | Fix | Checked |
|------|--------|-----|---------|
| Each video card paint calls `Meta()` twice (CardImage in PaintThumb, then the year/rating chip in PaintCard): PathKey, four config reads and a GuessVideo each time | fixed, net side only | `Meta()` keeps its last answer, keyed on the path plus `g_metaStamp`. Every store Put, Erase, Clear and Load, a rejected key, and the three config subscriptions bump the stamp (Online.cpp, `FindOrQueue` + `Meta`). The second ask for the same card returns at once. ItemGrid.cpp is unchanged: the two calls sit in different paint functions, so passing the record would have meant a header change. | Runtime against the round 7 mock (TMDB on, three copies of a real clip named as films). The cards show the backdrop and the "2010 · ★ 8,4" chip, the second Inception search was a cache hit, and metadata.json was written once with all three records (#8). `set privacy.fetchMetadata false` removes the artwork and chips on the next paint, so the stamp invalidates. |

## Outside my file scope

- src/app/App.cpp: the FlushStores takeover block only, as allowed for #4.
- No other files outside the scope.
