# Lumen round 1 — reviewer "stability" (QA engineer + settings tinkerer)

Build: `%TEMP%\lumen-qa\build-r1\Lumen.exe` (rebuilt with fixes). Data dirs: `%TEMP%\lumen-qa\data-stability*`.
Evidence: `%TEMP%\lumen-qa\round1\shots-stability\`.

_Status: in progress (report is written incrementally)._

## Top 5

(to be filled at the end)

## Findings

| ID | Severity | Area | What happened | Steps to reproduce | Expected | Evidence |
|----|----------|------|---------------|--------------------|----------|----------|
| S-01 | Major | Settings / Controls | Skip amounts ("Very short/Short/Medium/Long skip") are saved but ignored: with Short skip = 30 s, → still jumps 10 s (01:17 → 01:28). | Settings → Controls → Short skip = 30 → Enter; play the 10-min file; press →. | Seek by 30 s (and the shortcut label "Seek forward 10 s" should follow). | s15-controls-changed.png, s16-stack-click-right-wheel.png (rows 2→3). Likely cause: app\Actions.cpp:314-318 hardcode SeekBy(10/60/3). |
| S-02 | Major | Settings / Controls | "Single click on the video" = "Do nothing" still pauses on click; "Scroll wheel" = "Seek" does nothing (wheel down over the video did not seek). | Controls → Single click = Do nothing, Scroll wheel = Seek; click the playing video; wheel down over it. | Click does nothing; wheel seeks. | s16-stack-click-right-wheel.png (row 1: paused after the click; rows 3→4: no seek). Likely cause: nothing outside SettingsSchema.cpp reads `controls.singleClick/doubleClick/wheel/hwheel/reverseScroll`. |
| S-03 | Major | Settings / Interface | "Auto-hide the controls" = "Always" (Hide after = 5 s) never hides the bar in a window: 7 s into playback the control bar is still there. "Always" is identical to "In fullscreen only". | Interface → Auto-hide = Always, Hide after = 5 s; open a video in a window; wait 7 s. | Bar and cursor hide after 5 s in windowed mode too. | s13-restart-playing-1s.png vs s13b-playing-7s.png. Likely cause: app\MainWindow.cpp:256 PokeChrome() returns when !Immersive() (fullscreen / hide-UI / PiP only). |
| S-04 | Major | Settings / Interface | "Show window decorations" = off also removes the title bar in the library: no close/minimize/maximize, no app menu, no drag area. Only Alt+F4 / Ctrl+P get you out. | Interface → Show window decorations = off; look at the main window on the Home page. | Hide only the frame in the player, or keep a minimal drag/close strip. | s10-main-nodecorations.png |
| S-05 | Major | Settings / live switch | Switching language RU→EN live leaves the open library page in Russian ("Главная", "19 элементов", "Недавно добавленные") until you navigate; the search placeholder "Поиск" stays Russian even after navigation. | General → Interface language = English while Home is open. | The whole UI re-translates immediately. | s06-main-light-en.png, s07-main-videos-after-switch.png, s07b-main-home-after-nav.png |
| S-06 | Major | Settings / live switch | Switching theme Dark→Light live leaves the library search field black on the light page; it stays black after navigating between pages. | General → Theme = Light while the library is open. | Search field restyled with the light palette. | s06-main-light-en.png, s07-main-videos-after-switch.png |
| S-07 | Minor | Settings / Interface | "Show the file name in the title bar" = off changes only the OS/taskbar title (it becomes "Lumen"); Lumen's own title bar still shows "Long Seek Test 10min". | Interface → Show the file name… = off; restart with a video. | In-app title hides the name too (the description says "title bar and taskbar"). | s13-restart-playing-1s.png. Likely cause: app\Shell.cpp:196 sets title_ before the titleFileName check. |
| S-08 | Minor | Settings / General | The Log file field shows "%APPDATA%\Lumen\lum…", but the log is actually written to the data dir (`data-stability\lumen.log`). | General → Save a log = on; compare the field with the real file. | Show the real default path. | s05-accent.png |
| S-10 | Major | Settings / Playback | "Start playing as soon as a file opens" = off is ignored: after a restart with a file on the command line the video plays straight away (00:07 after 5 s). | Playback → Start playing… = off; restart Lumen with a video. | File opens paused on the first frame. | s22b-restart2-unicode-5s.png; lumen.log shows `Set property: pause=false` right before loadfile. Likely cause: app\App.cpp:706 PlayIndex() calls SetPause(false) unconditionally. |
| S-11 | Major | Playlist / Previous | After the last episode ends, Previous item (PgUp, P or the ⏮ button) never goes back: 1st press restarts E03 (OK), every later press only resumes/continues E03. Reproduced on a fresh data dir with default settings. In the middle of an episode PgUp works (E03→E02→E01). | Open Series S01E01, PgDn ×2 (E03), let it play to the end, press PgUp, wait 1.5 s, press PgUp again (and again). | Second press opens E02. | s37-stack-eof-titles.png + s37-stack-eof-bar.png (fresh dir); s33-stack-prev*.png, s35-stack-main-*.png (main dir); control: s34-stack-q-titles.png works mid-episode. |
| S-12 | Major | Settings / Library | "Use the media library" = off does nothing visible: after a restart Home/Videos still list and index all media. | Library → Use the media library = off; restart; open Videos. | Library pages hidden or an "library is off" empty state; no indexing. | s21-library-adv.png, s23-library-disabled-thumbs-off.png |
| S-09 | Polish | Settings / number fields | Number fields clamp or revert without a message: "99999" in Very short skip becomes 3600, "abc" in Medium skip reverts to 60. | Controls → type 99999 / abc → Enter. | Inline validation hint ("1–3600 s"). | s15-controls-changed.png |

## Settings table

| # | Key | Changed via UI to | settings.json | Visible effect | Persisted after restart | Verdict |
|---|-----|-------------------|---------------|----------------|-------------------------|---------|
| 1 | general.language | English | "en" ✔ | Settings + sidebar switch live; open library page stays RU (S-05) | ✔ (EN after restart) | works (live refresh partial) |
| 2 | general.theme | Light | "light" ✔ | Both windows go light; search field stays dark (S-06) | ✔ | works (live refresh partial) |
| 3 | general.accent | 3rd swatch | "silver" ✔ | Toggle knobs/selection change tone | ✔ | works |
| 4 | general.developerMode | On | true ✔ (≈1 s debounce) | none observed | ✔ | no effect |
| 5 | general.saveLog | On | true ✔ | lumen.log created in data dir | ✔ | works |
| 6 | general.logLevel | Information | "info" ✔ | log shows `msg-level="all=info"` | ✔ | works |
| 7 | interface.alwaysOnTop | On, then Off | true / removed ✔ | WS_EX_TOPMOST set and cleared live (ex=40108 → 40100) | ✔ | works |
| 8 | interface.decorations | Off | false ✔ | Title bar removed everywhere (S-04) | — | works, but harmful |
| 9 | interface.titleFileName | Off | false ✔ | OS title "Lumen"; in-app title still shows the name (S-07) | ✔ | partly works |
| 10 | interface.autoResize | Never | "never" ✔ | Window stayed 1280×780 when a 1080p file opened | ✔ | works |
| 11 | interface.autoHide | Always | "always" ✔ | no hiding in a window (S-03) | ✔ | no effect |
| 12 | interface.hideDelay | 5 s | 5 ✔ | could only be judged with autoHide (S-03) | ✔ | — |
| 13 | interface.osdMessages | Off | false ✔ | see below | ✔ | — |
| 14 | privacy.excludePatterns | `*Vertical*` | ✔ | see below | ✔ | — |
| 15 | controls.skipShort | 30 | 30 ✔ | → still seeks 10 s (S-01) | ✔ | no effect |
| 16 | controls.skipVeryShort | 99999 | clamped to 3600 | not checked separately | ✔ | — |
| 17 | controls.singleClick | Do nothing | "none" ✔ | Click still pauses (S-02) | ✔ | no effect |
| 18 | controls.wheel | Seek | "seek" ✔ | Wheel does not seek (S-02) | ✔ | no effect |
| 19 | controls.keyBindings (Stop) | S → F9 | `"playback.stop": {"key": "F9; MediaStop"}` ✔ | S no longer stops, F9 does; conflict warning for K ("already used by Audio delay +50 ms") with Reassign/Cancel | ✔ (F9 + per-row reset icon after restart) | works |
| 20 | playback.autoPlay | Off | false ✔ | file still auto-plays (S-10) | ✔ | no effect |
| 21 | playback.speed | 1.5× | 1.5 ✔ | after restart 5 s of wall time = 7.5 s of video, scaletempo2 inserted | ✔ | works (startup only, not live) |
| 22 | playback.seekPreview | Off / On | false ✔ | Off: only the time tooltip; On: frame preview | ✔ | works |
| 23 | video.brightness | +69 | 69 ✔ | picture visibly washed out live (`brightness="69"`) | ✔ | works |
| 24 | video.aspect | 4:3 | "4:3" ✔ | pillar-boxed live | ✔ | works |
| 25 | Video "Reset to defaults" | — | brightness/aspect removed, other categories kept | picture back to normal live; in-app confirmation dialog | ✔ | works |
| 26 | subtitles.size | Huge | "huge" ✔ | `sub-font-size=64`, visibly bigger | ✔ | works |
| 27 | subtitles.bold | On | true ✔ | bold text | ✔ | works |
| 28 | subtitles.position | 50 % | 50 ✔ | subtitles in the middle of the frame | ✔ | works |
| 29 | audio.maxVolume | 200 % | 200 ✔ | `volume-max=200` | ✔ | works (volume kept at 0) |
| 30 | audio.eq | On | true ✔ | `af=""` (flat preset), nothing audible to check | ✔ | — |
| 31 | library.thumbnails | Off | false ✔ | grid shows icons instead of thumbnails | ✔ | works |
| 32 | library.enabled | Off | false ✔ | library still indexed/shown (S-12) | ✔ | no effect |
| 33 | performance.hwdec | Off | "off" ✔ | `hwdec="no"` applied live | ✔ | works |

## Robustness / crash / shutdown / resources log

