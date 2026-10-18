# Lumen round 2 — reviewer "stability" (QA engineer + settings tinkerer)

Build: `%TEMP%\lumen-qa\build-r2\Lumen.exe` (repo HEAD 37bf44e). Data dirs: `%TEMP%\lumen-qa\r2-stability1…11`.
Evidence: `%TEMP%\lumen-qa\round2\shots-stability\` (`*-stack.png` are vertical strips of several shots; labels on each strip).
All instances were `--automation`, volume 0, driven only via PostMessage tools; every PID was closed at the end.

## Re-check of round 1

| R1 ID | Verdict | Evidence (round 2) |
|-------|---------|--------------------|
| S-01 skip amounts ignored | **Fixed** | skipShort=30: → 00:00→00:30; Shift+→ +3 s (very short); Ctrl+→ 00:33→01:33 (medium 60). `a-stack-skips.png` |
| S-02 click/wheel mouse settings ignored | **Fixed** | singleClick=none: clicking the playing video keeps it playing (00:01→00:02). wheel=seek: 2× wheel down 00:02→00:00 with OSD "00:00 / 10:00", 2× wheel up → 00:34. `b-stack.png`, `c-stack.png` (top) |
| S-03 auto-hide "Always" did nothing in a window | **Fixed** (but see N-02) | autoHide=always, hideDelay=2: control bar gone 3 s after the last input in a window. The in-app title bar stays. `b-stack.png` (b04) |
| S-04 decorations off removed the title bar in the library | **Fixed** | decorations=false: player shows only video + bar (`e-stack.png` e01); library keeps title bar with menu and caption buttons (`c-stack.png` c01, `e-stack.png` e02) |
| S-05 live RU→EN left the library in Russian | **Fixed** | Switched while Home was open: sidebar, "Home", "0 items", "Frequently Visited Folders", search placeholder "Search" all English at once. `e05-main-after-en.png` |
| S-06 live Dark→Light left the search field black | **Fixed** | Search field is light right after the live switch. `e-stack.png` (e02) |
| S-07 in-app title kept the file name | **Fixed** | titleFileName=false: in-app title "Lumen", OS title "Lumen". `b-stack.png` |
| S-08 log path field shows %APPDATA% | **Still broken** | Save a log = on → file created at `r2-stability3\lumen.log`, nothing in `%APPDATA%\Lumen`; field still reads "%APPDATA%\Lumen\lum…". `d-stack.png` (top). Only affects `--data-dir`/portable runs → Minor |
| S-09 number fields clamp silently | **Still broken** | Controls → Medium skip: typed 99999 (no range hint while typing) → Enter → silently becomes 3600, json `controls.skipMedium: 3600`. `h2-stack.png` |
| S-10 autoPlay=off ignored | **Fixed** | Restart with a file on the command line: opens paused at 00:00 with ▶ after 3 s. `a01-autoplay-off-3s.png` |
| S-11 Previous after EOF stuck on the last episode | **Fixed** | Fresh dir, open E03, let it end, PgUp → E02, PgUp → E01, PgUp → stays E01 (titles from MainWindowTitle). `f3-stack.png` |
| S-12 library.enabled=off did nothing | **Resolved by hiding** (side effect N-01) | Row hidden (`SettingsSchema.cpp:1396`). Library still indexes with a stored `false` (`c-stack.png`), which is fine now that the switch is gone. |

Round-1 settings table: every "no effect" row is now either fixed (skipShort, singleClick, wheel, autoHide, autoPlay, titleFileName) or hidden on purpose (developerMode, library.enabled, skipLong). The "works" rows that were re-touched (language, theme, saveLog, decorations) still work. Not re-run: alwaysOnTop, autoResize, keyBindings, speed, seekPreview, brightness/aspect, subtitle size/position, maxVolume, eq, thumbnails, hwdec.

## New findings

| ID | Severity | Area | What happened | Steps to reproduce | Expected | Evidence |
|----|----------|------|---------------|--------------------|----------|----------|
| N-02 | Major | Settings / Interface | "Show controls in fullscreen" (Панель в полноэкранном режиме) = off does the opposite of its description. The control bar **never hides**: fullscreen, playing, 5 s with no input, and the bar is still there. It also stops auto-hide in a window. | Interface → Show controls in fullscreen = off; play a video; press F; wait 5 s. | Description says "When off, use the keyboard only": the bar stays hidden in fullscreen, even on mouse move. | `g-fs2.png` (playing, 5 s idle, fullscreen + window), `g04-fullscreen-controls-off-mousemove.png`. Likely cause: `src\app\MainWindow.cpp:233-236` `ChromeAutoHides()` returns false when the setting is off, so the bar is pinned instead of suppressed. |
| N-01 | Minor | Settings / Library | A stored `library.enabled=false` (left over from round 1 or an older config) still greys out "Сканировать папки при запуске" and "Читать длительность и теги". Both look ON and disabled, and no visible control can re-enable them because the parent row is hidden. | settings.json with `"library.enabled": false` → Settings → Медиатека → Дополнительные параметры. | Hidden settings drop out of `.When()` checks, or the stale value is ignored or migrated. | `d04-library-adv-libdisabled.png`. Likely cause: `SettingsSchema.cpp:1401,1405` `.When("library.enabled","true")` on visible rows. |
| N-03 | Minor | Robustness / data files | Corrupt data files are handled without a crash, but unevenly: a truncated settings.json gets a `settings.json.bad` backup and defaults are used (good). A corrupt **ui.json** silently re-runs the onboarding wizard. A corrupt **history.json** / **library.json** is silently treated as empty (Recent: "Недавних файлов нет", Home: welcome state), with no backup, so the next save overwrites whatever was recoverable (e.g. the valid third history entry). | Copy a data dir; truncate settings.json; write garbage bytes to ui.json; put `"roots":"not-an-array"` in library.json and a half-valid items array in history.json; start. | Same treatment for every file: keep a `.bad` copy, salvage valid entries, ideally say "history could not be read". | `n01-corrupt-start.png` (onboarding again), `n-stack.png` (empty Home/Recent); file listing in the robustness log. |
| N-04 | Polish | Library / small window | The window cannot go below 720×440 (good), but at that size the breadcrumb "Недавние" is cut to "Н…" while the search box keeps its full width. | Resize the main window to the minimum; open Recent. | Shrink the search box first, or keep at least one breadcrumb word. | `o01-tiny-library.png` |
| N-05 | Polish | Library / Home | Home header says "0 элементов" / "0 items" while a "Частые папки / Frequently Visited Folders" card is shown under it. | Fresh dir without library roots; play a file from Series S01; Esc to Home. | The count matches what is shown, or no count on Home. | `e-stack.png` (e02), `e05-main-after-en.png` |
| N-06 | Polish | Performance | Paused CPU is almost as high as playing CPU: 0.13 % vs 0.18 % of 32 logical CPUs (≈4 % vs ≈6 % of one core) for the 1080p film with loudnorm + compressor + CAS on. The library is idle at 0.01 %. Caveat: the "playing" sample was taken right after Esc left fullscreen. | Play the film, sample `TotalProcessorTime` for 6 s; pause; sample again. | A paused frame with no animations should be near idle. | Robustness log below. |

## Settings table

Changed in the Settings window (Ctrl+P), unless marked *(file)* = set through launch.ps1 `-Settings` to re-check round 1. "Persisted" = value still in settings.json **and** behaviour seen again after a restart, unless noted. Saves to settings.json are debounced by about 2–4 s (a toggle was not in the file after 2 s but was after 4 s).

| # | Key | Changed to | settings.json | Visible effect | Persisted after restart | Verdict |
|---|-----|-----------|---------------|----------------|-------------------------|---------|
| 1 | general.language | English (live) | "en" ✔ | Both windows translate at once, library too | ✔ | works |
| 2 | general.theme | Light (live) | "light" ✔ | Both windows light, search field light | ✔ (json) | works |
| 3 | general.saveLog | On | true ✔ | lumen.log created in the data dir; path field wrong (S-08) | ✔ | works, wrong label |
| 4 | interface.rememberWindow | Off | false ✔ | Resized to 900×600 → restart opens 1300×816 default | ✔ | works |
| 5 | interface.controlsInFullscreen | Off | false ✔ | Bar never hides (N-02) | ✔ (still broken after restart) | **broken (inverted)** |
| 6 | interface.osdMessages | Off | false ✔ | Seek with → shows no OSD (compare b03 with OSD) | ✔ | works |
| 7 | interface.trayIcon | On | true ✔ | Enables "Minimize to tray" live. No icon under `--automation` by design (`SystemIntegration.cpp:174`) | json ✔ | not testable here |
| 8 | interface.minimizeToTray | On | true ✔ | SC_MINIMIZE → window just minimized (no tray in automation) | json ✔ | not testable here |
| 9 | interface.decorations *(file)* | Off | false | Player frameless, library keeps title bar | ✔ | works |
| 10 | interface.autoHide / hideDelay *(file)* | Always / 2 s | ✔ | Bar hides in a window after ~2 s | ✔ | works |
| 11 | interface.titleFileName *(file)* | Off | false | In-app and OS title "Lumen" | ✔ | works |
| 12 | privacy.saveHistory | Off | false ✔ | After playing E01, history.json not touched (still only the earlier file) | ✔ | works |
| 13 | privacy.rememberPosition | Off | false ✔ | not checked separately (code: `App.cpp:393`) | json ✔ | — |
| 14 | controls.skipShort *(file)* | 30 | 30 | → seeks 30 s | ✔ | works |
| 15 | controls.skipMedium | 99999 | clamped to 3600 silently (S-09) | — | json ✔ | works, no validation |
| 16 | controls.singleClick *(file)* | Do nothing | "none" | Click does not pause | ✔ | works |
| 17 | controls.wheel *(file)* | Seek | "seek" | Wheel seeks, with OSD | ✔ | works |
| 18 | controls.reverseScroll | On | true ✔ | not exercised: the default wheel action is volume, and volume must stay 0 | json ✔ | — |
| 19 | playback.autoPlay *(file)* | Off | false | Opens paused at 00:00 | ✔ | works |
| 20 | playback.repeat | Текущий файл | "one" ✔ | After restart, the 12 s E01 is at 00:02 after 14.5 s (looped, same title) | ✔ | works |
| 21 | playback.folderAsPlaylist | Off | false ✔ | masked by repeat=one, not judged | json ✔ | — |
| 22 | playback.stopAfterEach | On | true ✔ | masked by repeat=one, not judged | json ✔ | — |
| 23 | playback.seekPrecise | Off | false ✔ | not judged visually | json ✔ | — |
| 24 | performance.frameDrop | Off | false ✔ | log `Set property: framedrop="no"` live | json ✔ | works |
| 25 | audio.normalize | On | true ✔ | `af=@lumen:lavfi=[loudnorm=I=-23:TP=-1.5:LRA=11]` live; re-applied 0.2 s into the next start | ✔ | works (note: loudnorm makes the chain run at 192 kHz double) |
| 26 | audio.nightMode | On | true ✔ | `acompressor=…` prepended to the chain live | ✔ | works |
| 27 | subtitles.bold | On | true ✔ | `sub-bold="yes"` live | json ✔ | works |
| 28 | subtitles.shadow | Off | false ✔ | `sub-shadow-offset="0"` live | json ✔ | works |
| 29 | subtitles.autoLoad | Off | false ✔ | `sub-auto="no"` live; after restart no sidecar text on screen at 00:04 (not conclusive) | ✔ | works (per log) |
| 30 | video.startFullscreen | On | true ✔ | Restart opens 2560×1440 fullscreen | ✔ | works |
| 31 | video.sharpen | slider → 8 | 8 ✔ | `vf=@lumen:lavfi=[cas=strength=0.8]` live | json ✔ | works |
| 32 | video.saturation | slider → −68 | −68 ✔ | Picture visibly desaturated live and after restart | ✔ | works. `k03-restart-startfs-sat-noautoload.png` |

Hidden on purpose, as stated in the brief (not counted against the build): developerMode, minorWarnings, logFormat, collectStats, customSkin, embedVideo, compactMode, lastfm*, skipLong, webRemote*, pauseOnAudioFocus, menuLanguage, wallpaper, visualizer, library.enabled, network.buffering/cipher, encoder/decoder, clock*, most recording.*.

## Robustness log

- **Shutdown time** (CloseMainWindow → exit): 241, 169, 129 (with Settings open), 159, 229, 258, 245, 192, 179, 207, 120, 172 (19-item playlist), 108 ms. All clean, no orphan windows. When onboarding is open, `CloseMainWindow` targets the Setup window: Setup closes and the main window stays (correct). A WM_CLOSE to the main window then exits.
- **Closing with dialogs open**: main closed with Settings open → exit 129 ms; minimize hides the owned Settings window and restore brings it back.
- **Rapid input / quick switching**: 19 files (incl. fake.mkv, truncated mp4, opus, wav, flac, 4K, 1080p) on the command line, then 20× PgDn (5.8 s), 12× PgUp, 15× Space, 10× (→,←). No crash or hang. Ends on the last item at "next", ends paused after an odd number of Space presses, and broken files are skipped (log: "Failed to recognize file format", "moov atom not found"). `m-stack.png`
- **Memory**: start 80 MB private / 589 handles / 73 threads. Peak right after the PgDn storm 548 MB / 1280 handles / 150 threads (4K + 1080p decoders in flight). Settled 7 s later at **172 MB / 962 handles / 87 threads**: no leak seen. Library idle: 81 MB. 1080p film with filters: 202 MB.
- **CPU** (32 logical CPUs; % of the whole machine): playing 1080p + filters 0.18 %, paused 0.13 %, library idle 0.01 % (see N-06).
- **Corrupt files** (copy dir `r2-stability10`): no crash in either run. settings.json → `.bad` backup + defaults. ui.json → onboarding again, ui.json rewritten with only `window.placement`. history.json/library.json → treated as empty, no backup (N-03).
- **Tiny window**: SetWindowPos 120×80 and 1×1 are clamped to a 720×440 minimum; the layout stays usable (N-04).
- **Number field input**: typing worked after End+Backspace. Two attempts that used Ctrl+A first typed nothing; this may be the PostMessage tool and not the app, so it is not reported.

## Top 5

1. **N-02** "Show controls in fullscreen = off" pins the bar on screen instead of hiding it. Anyone who turns it off for a clean fullscreen gets the opposite.
2. **S-09 (still)** number fields silently clamp (99999 → 3600) with no range hint.
3. **N-03** corrupt ui/history/library files are silently discarded with no backup (only settings.json gets `.bad`), and onboarding re-runs with no explanation.
4. **S-08 (still)** the log-file field shows `%APPDATA%\Lumen\…` while the log is written to the data dir.
5. **N-01** a stale hidden `library.enabled=false` leaves two library options greyed out with no way to re-enable them.

Overall: round 2 is a clear step up. 10 of the 12 round-1 findings are fixed or resolved by hiding, and 22 of the 32 settings rows above work as described. Of the other 10, 1 is broken, 2 cannot be tested under `--automation`, and 7 were not judged. No crashes, hangs or leaks under input storms, corrupt files or tiny windows. Shutdown is always under 0.3 s.
