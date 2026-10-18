# Audit raw findings: library, net, core, Skins, ImageCache (reviewer A2)

Read only, from code. "checked" means the case was traced in code.

1. **must** — A locked settings.json or library.json gets overwritten with defaults.
   - Where: Config.cpp:97-101, :183-193; MediaLibrary.cpp:517-520, :537-545.
   - Problem: round 5 #10 made the other stores read-only on a read error, but Config and MediaLibrary were left out. While a backup tool locks settings.json, a volume change makes the config dirty, and the flush writes the defaults over it. A locked library.json likewise loses its folder list on the next AddRoot.
   - Fix: add readOnly_ as in History::Load; Save() returns false while it is set. Add a test. (checked)
2. **must** — Cloud-only (OneDrive) songs lose their cached tags and stay "New" forever.
   - Where: MediaLibrary.cpp:376, :726-736.
   - Problem: SaveCache skips offline items, so their entries disappear. Every later scan then shows them with no tags and as new.
   - Fix: keep offline items, written as stale when never read. Add a test with one offline item. (checked)
3. **must** — The server chooses the extension of a saved subtitle (Movie.en.lnk/.url/.hta). Stream-URL stems keep the characters `?` and `:`, so those downloads always fail.
   - Where: OnlineData.cpp:341-353, Online.cpp:477.
   - Fix: keep the extension only if IsSubtitlePath accepts it, else use srt; replace the characters that are invalid in file names. Add tests. (checked)
4. **should** — On takeover, the second instance doesn't reload Config or library.json, so it overwrites the first instance's settings and folders.
   - Where: App.cpp:281-288.
   - Fix: reload config_ and library_ there too. (checked)
5. **should** — The JSON duplicate-key check is O(n²). A skin with many keys, or a 4 MB server body, freezes the UI thread.
   - Where: Json.cpp:286-295.
   - Fix: a local hash map; add a test with 200k keys. (checked)
6. **should** — The OpenSubtitles login answer's `base_url` isn't checked by OsHostAllowed, so the API key and token can go to any host.
   - Where: Online.cpp:316-317 vs :73-81.
   - Fix: if (!OsHostAllowed(s.base)) s.base.clear(). (checked)
7. **should** — The online disk cache is pruned only when TMDB is on, so with only LRCLIB on it grows forever.
   - Where: Online.cpp:697, Cache.cpp:57-69.
   - Fix: prune once on the first cache use, on the worker. (checked)
8. **should** — Every TMDB lookup rebuilds metadata.json on the UI thread, so a large library stutters while the cards fill.
   - Where: Online.cpp:781-790, :714-731.
   - Fix: save in Finish only when the queue is empty. (likely)
9. **should** — "Clear online data" can come back after a restart, through a race with an in-flight Save on the other worker.
   - Where: Online.cpp:1066 vs :731.
   - Fix: Clear(), then Save() the empty store. (likely)
10. **should** — A 200 answer that isn't JSON is cached as a success for weeks.
    - Where: Http.cpp:407-412.
    - Fix: don't cache a 2xx answer that doesn't parse. (checked)
11. **nice** — Two IsReleaseTag lists disagree (MediaTypes.cpp:183 vs OnlineData.cpp:81).
    - Fix: merge them into MediaTypes and delete the copy. (checked)

Left out (9):
- ImageCache::missing_ grows without limit.
- FileSettings::Evict is O(n²) on a hand-edited file.
- The net::Cancel set grows.
- A failed DPAPI decrypt drops the key on save.
- The proxy password is not URL-encoded.
- FrameAt can overread when the stride is smaller than the width.
- The subtitle unique() name gives up after 99 tries.
- A non-image TMDB body is kept forever.
- FitShellText underflows at capacity 1 (no caller does that).
