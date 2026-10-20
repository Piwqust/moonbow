# Area DESK: desktop platform (Windows 11 integration, window management, shortcuts, perceived performance)

Reviewer: desktop platform. Build `%TEMP%\lumen-qa\uir-build\Lumen.exe` (main @ ff96c7f), invisible `--automation`
instances only (data dirs `%TEMP%\lumen-qa\uir-desk-*`). One monitor: 2560x1440 at 100 %. Screenshots in
`%TEMP%\lumen-qa\uir-desk-shots\` and `%TEMP%\lumen-qa\uir-desk-tmp\`. A 4-instance screen sweep ran at the same
time, so every timing below is noisy (treat as an order of magnitude, not a benchmark).
Web-only checks of the skill (HTML validity, skip links, mobile breakpoints, touch targets in px) are skipped.

## Lumen against comparable players

| Feature | Lumen | What users of WMP 2022 / MPC-HC / PotPlayer / VLC / IINA expect |
|---|---|---|
| Time to window / to playing | ~75 ms to window, ~250 ms content, ~290 ms playing (measured) | MPC-HC-class instant start; WMP 2022 is ~1 s |
| Second launch with a file | hands off in 34 ms, plays in ~126 ms (measured) | the same (MPC-HC, VLC "one instance") |
| Resize from the edges | 6 DIP band *inside* the content (hitmap) | invisible 7-8 px band *outside* the visible edge (every Win11 app) |
| Snap Layouts / maximize | HTMAXBUTTON, work-area maximize (tested) | the same |
| Window menu (Alt+Space) | blocked (from code) | always works |
| Fullscreen | current or primary monitor, F/F11/Alt+Enter/double click/Esc, restores maximized (tested) | the same |
| Always on top / PiP | both exist; leaving PiP drops "always on top" (tested) | MPC-HC/PotPlayer: on top "always / while playing", PiP keeps aspect |
| Windows media flyout | title + artist, no cover, no timeline (from code) | WMP, Spotify, browsers: cover art and seek position |
| Taskbar | 3 thumbnail buttons, progress (from code) | the same, readable in light and dark taskbar |
| Jump list / recent files | none | WMP, VLC, MPC-HC: recent files on taskbar right click |
| Drop | files and folders; URLs refused | VLC, PotPlayer, MPC-HC play a dropped link |
| Mouse Back button | Previous item over video, nothing in the library (tested) | navigates back in the library (WMP 2022, Explorer, Spotify) |
| Context menus | right click everywhere; Shift+F10 only in grids (tested) | Shift+F10 / Menu key wherever right click works |
| Theme | Dark default, "System" follows Windows live; no Mica | WMP 2022 follows the system theme, Mica backdrop |

## Strengths

- **Very fast start.** In 3 runs each: window revealed at 75-83 ms, library content at 241-256 ms; with a file argument
  playback starts at 271-291 ms (LUMEN_TRACE). `frames-check.py` passes on all of them: one fade-in, no flash
  (`uir-desk-tmp\sheet-lib1.png`, `sheet-file1.png`).
- **Single-instance hand-off works well.** A second launch with a file exits after 34 ms and the first window is
  playing the file ~126 ms after the second launch; Explorer's one-process-per-file launches are merged into one
  queue (`App::OnCopyData`).
- **Native window behaviour where it counts:** `WM_NCHITTEST` answers `HTMAXBUTTON` for the maximize button (Snap
  Layouts flyout), maximize fills exactly the work area (2560x1392 client, taskbar visible), fullscreen covers the
  monitor (0,0,2560,1440) and Esc returns to the maximized state (tested). PerMonitorV2 manifest and
  `WM_DPICHANGED` uses the suggested rect.
- **Keyboard map follows player conventions:** Space, arrows, F/F11/Alt+Enter, Ctrl+O, Ctrl+W, Ctrl+Q, Ctrl+, , F1,
  media keys, middle click = fullscreen, wheel on the seek bar = 5 s; Shift+F10 opens the grid item menu.
- **Big lists stay smooth:** 40 wheel bursts through 1284+ songs of the worst-case set ran at ~125-145 frames/s,
  paint average ~1 ms (one 34 ms hitch); library scan has a progress banner ("Indexing… N of M") after a delay.
- **OS hooks are all present and switchable:** SMTC, thumbnail toolbar, taskbar progress, tray icon, minimize to
  tray, track notifications, live dark/light following (`WM_SETTINGCHANGE "ImmersiveColorSet"`), high contrast.

## Findings

### DESK-1. Resize zones sit inside the content and eat the scrollbar, the sidebar edge and the caption buttons
- Severity: High · Standard: Windows guideline "Title bar / window frame" (Win11 resize borders are outside the visible edge); Nielsen #4
- Where: every restored window, all edges. `src/ui/Host.cpp:27` (`kResizeBorder = 6`), `:885-896` (`NcHitTest`);
  scrollbar `src/ui/Controls.cpp:1053-1058`
- Evidence: `hitmap.ps1` at 1280x780: `HTRIGHT px [1274..1278]` over `ui::ScrollView` for the whole height,
  `HTTOP` over the minimize/maximize/close buttons and the Lumen menu button, `HTRIGHT` over the close button's
  right 6 px, `HTBOTTOM` over the control bar. The resting scrollbar thumb is drawn at right-7..right-3
  (`ThumbRect`), so most of the visible thumb answers `HTRIGHT`: a real mouse gets the resize cursor and drags
  the window instead of the list (`uir-desk-tmp\scroll-mid.png`, thumb at x≈1274). Corners are only 6x6 px.
- Impact: every mouse user of a restored window: grabbing the scrollbar resizes the window; a click on the top
  edge of a caption button resizes instead of closing; corners are hard to hit.
- Fix: keep the system frame on the left, right and bottom in `WM_NCCALCSIZE` (only remove the top caption, as
  Windows Terminal does) so DWM supplies the invisible outer resize band; keep `HTTOP` only for the top 1-2 px
  of the title bar outside the caption buttons.
- Verify: `hitmap.ps1` shows no `HTLEFT/HTRIGHT/HTBOTTOM` inside the client area; the scrollbar thumb answers `HTCLIENT`.

### DESK-2. Leaving Picture in Picture switches "Keep window above other windows" off
- Severity: Medium · Standard: Nielsen #1 (system state), Windows guideline "Window management"
- Where: PiP exit. `src/app/MainWindow.cpp:275` always posts `HWND_NOTOPMOST`.
- Evidence: `set interface.alwaysOnTop true` → `WS_EX_TOPMOST` set; `action video.pip` twice → window back to
  640,306,1360,746 with `topmost=False` while the setting still says true.
- Impact: MPC-HC/PotPlayer users who keep the player on top lose it silently after one PiP round trip, until restart
  or toggling the setting.
- Fix: in `SetPictureInPicture(false)` call `TheApp().ApplyWindowOptions()` instead of the hard `HWND_NOTOPMOST`.
- Verify: same sequence; `GetWindowLong(GWL_EXSTYLE) & WS_EX_TOPMOST` stays set.

### DESK-3. At the minimum window size the seek bar shrinks to ~124 px
- Severity: Medium · Standard: Fluent "Responsive design", Nielsen #7
- Where: player, 720x440 (the minimum, `MainWindow.cpp:99-100`), control bar layout `src/app/player/ControlBar.cpp`
- Evidence: `uir-desk-shots\a-min-player.png`: seek bar from x≈252 to x≈376 of 720, while 8 buttons, two time
  labels and the volume keep their full size. The minimum width is reachable by Snap (a third of a 1920 screen = 640).
- Impact: the most used control is the hardest to aim in a snapped/narrow window; chapter ticks merge.
- Fix: below ~900 DIP put the seek bar on its own full-width row above the buttons (MPC-HC and YouTube layout), or
  move volume/panel buttons into an overflow "⋯".
- Verify: `winpos.ps1 -Size 720,440`, dump: `SeekBar` width ≥ 600 DIP.

### DESK-4. Shift+F10 and the Menu key open nothing on the video and in the queue
- Severity: Medium · Standard: WCAG 2.2 2.1.1 Keyboard (where the menu holds unique actions), Windows guideline "Context menus"
- Where: player video area (`src/app/player/PlayerView.cpp:310`), queue rows (`QueuePanel.cpp:240`, `:599-616`
  handles keys but not `VK_APPS`/Shift+F10). Only `ItemGrid.cpp:1156` and `SubWords.cpp:930` handle them.
- Evidence: player focused, `-Key F10 -Shift` and `-Key Apps` → `popups=0`; queue row selected, Shift+F10 → no
  menu (`uir-desk-shots\a-queue-shf10b.png`); grid item, Shift+F10 → menu opens (`a-grid-shf10.png`).
- Impact: keyboard and screen-reader users cannot reach the video menu (aspect, tracks, PiP, …) or the queue
  row menu ("play next", "show in library") at all.
- Fix: handle `VK_APPS` and Shift+F10 once in `Host::OnKey`: walk up from `focus_` to the first `onContextMenu`
  and call it with the focused item's rect centre (as `ItemGrid` does); remove the per-widget copies.
- Verify: same key presses → `popups=1` on video, queue row, sidebar item.

### DESK-5. The mouse Back button does nothing in the library
- Severity: Medium · Standard: Windows convention (XButton1 = Back in Explorer, Settings, WMP 2022), Nielsen #3
- Where: `src/ui/Host.cpp:1606-1610` returns TRUE for `WM_XBUTTON*` (so no `WM_APPCOMMAND` either); only
  `PlayerView.cpp:334-337` uses X1/X2 (Previous/Next over the video).
- Evidence: route `music songs` after `music albums`; posted `WM_XBUTTONDOWN/UP` (XBUTTON1) → route unchanged;
  `action media.back` → `music albums`.
- Impact: everyone with a 5-button mouse browsing folders, albums and artists.
- Fix: in `MainWindow` map X1/X2 to `Action::Back` / `Action::Forward` when the library is shown (keep the video's
  own mapping or make it Back to library too).
- Verify: same post → route goes back.

### DESK-6. The Windows media flyout shows no cover and no position
- Severity: Medium · Standard: Windows guideline "System Media Transport Controls"
- Where: `src/app/SystemIntegration.cpp:447-458` (from code; SMTC is off in `--automation`)
- Evidence: `DisplayUpdater` gets Type, Title, Artist, AlbumTitle only; no `Thumbnail`, no
  `UpdateTimelineProperties`, no `PlaybackPositionChangeRequested`; `Shuffle/AutoRepeatMode` not reported.
- Impact: the Quick Settings media card and the lock screen show a generic tile with no seek position, unlike
  WMP, Spotify or a browser playing the same file.
- Fix: set `updater.Thumbnail(RandomAccessStreamReference::CreateFromStream(...))` from the cover `ImageCache`
  already has (video: a frame thumbnail); call `UpdateTimelineProperties` on file change and on seek; handle
  `PlaybackPositionChangeRequested` with `SeekAbsolute`.
- Verify: play a tagged album in a normal instance; Win+A media card shows cover and a moving position bar.

### DESK-7. Taskbar thumbnail buttons are drawn white only
- Severity: Medium · Standard: Windows guideline "Taskbar thumbnail toolbars", WCAG 1.4.11 non-text contrast
- Where: `src/app/SystemIntegration.cpp:38-59` (`MakeIcon`, "white, for the dark taskbar flyout"), `:125-133`
  (made once) (from code)
- Evidence: icons are rendered `#FFFFFF` regardless of `AppsUseLightTheme`/`SystemUsesLightTheme`; Prev/Next are
  always `THBF_ENABLED`, even with a one-item queue; icons are not rebuilt on DPI change.
- Impact: with a light taskbar the Prev/Play/Next glyphs are close to invisible on the light preview.
- Fix: pick the glyph colour from `SystemUsesLightTheme` and rebuild on `WM_SETTINGCHANGE "ImmersiveColorSet"`
  and `WM_DPICHANGED`; set `THBF_DISABLED` when there is no previous/next item.
- Verify: light system mode, hover the taskbar button: three readable glyphs.

### DESK-8. No jump list and no recent documents
- Severity: Medium · Standard: Windows guideline "Jump lists"
- Where: no `SHAddToRecentDocs` / `ICustomDestinationList` anywhere in `src/` (from code)
- Impact: right click on the pinned Lumen shows nothing to resume; WMP, VLC and MPC-HC list recent files there.
  History exists in-app (`History`) but the shell never hears about it.
- Fix: call `SHAddToRecentDocs(SHARD_PATHW, path)` when a local file starts (respecting `privacy.saveHistory` and
  exclude patterns), and clear with `SHAddToRecentDocs(0, nullptr)` when history is cleared.
- Verify: open two files, right click the taskbar button: "Recent" lists them.

### DESK-9. A link dropped from a browser is refused
- Severity: Medium · Standard: Fluent "Drag and drop", Nielsen #7
- Where: `src/ui/Host.cpp:206-209` accepts only `CF_HDROP` (from code)
- Impact: dragging a stream/YouTube link from the address bar shows a "no" cursor, though Lumen plays URLs
  (Ctrl+N). VLC, PotPlayer and MPC-HC accept it. Also no `IDropTargetHelper`, so the Explorer drag image vanishes
  over the window.
- Fix: also accept `CFSTR_INETURLW` / `CF_UNICODETEXT` that parse as a URL and pass it to `onDropFiles`
  (`App::Open` handles URLs); forward DragEnter/Over/Leave/Drop to `IDropTargetHelper`.
- Verify: `drop` a URL via a test helper / real drag: playback starts.

### DESK-10. Alt+Space is dead, while a lone Alt tap still enters the invisible window menu
- Severity: Medium · Standard: Windows keyboard conventions (Alt+Space = window menu), WCAG 2.1.1
- Where: `src/ui/Host.cpp:1647-1656`: `WM_SYSKEYDOWN` returns 0 for every key but Alt/F4, `WM_SYSCHAR` space
  returns 0; `WM_SYSKEYUP` and `WM_SYSCOMMAND SC_KEYMENU` are not handled (from code)
- Impact: keyboard users cannot open Move/Size/Minimize; after tapping Alt alone DefWindowProc enters menu
  mode on the hidden system menu, so the next Space (play/pause) or arrow is swallowed or opens the window menu.
- Fix: let `WM_SYSCHAR ' '` reach `DefWindowProc` (keep eating only Enter, for Alt+Enter), and answer
  `WM_SYSCOMMAND` with `SC_KEYMENU` and `lParam == 0` (Alt tap) with 0.
- Verify: on a real keyboard in a normal instance: Alt+Space opens the window menu; Alt, then Space toggles playback.

### DESK-11. Restoring a window onto a monitor with another scale probably resizes it by the DPI ratio
- Severity: Medium · Standard: Windows guideline "High DPI / per-monitor v2"
- Where: `src/app/App.cpp:326-341` (`SetWindowPlacement` after the window was created at the primary monitor's
  DPI), `src/ui/Host.cpp:1682-1690` applies the suggested rect; no `WM_GETDPISCALEDSIZE` (from code, unverified:
  this PC has one monitor)
- Impact: a window saved at 1600x1000 on a 150 % secondary monitor comes back scaled again by the DPI change,
  then clamped to the work area by `FitToWorkArea`: a different size on every start for multi-monitor users.
- Fix: create the main window directly at the saved rect (pass it to `CreateWindowExW`), or answer
  `WM_GETDPISCALEDSIZE` with the unchanged size while restoring.
- Verify: two monitors 100 %/150 %: close on the 150 % one, reopen: same rect (`winpos`/`GetWindowRect`).

### DESK-12. The sidebar never collapses on its own in a narrow window
- Severity: Low · Standard: Fluent NavigationView (compact below 1008 epx)
- Where: `src/app/Shell.cpp:321-328` (only manual `SetCollapsed`)
- Evidence: at 720x440 the 200 DIP sidebar takes 28 % of the width; the song list shows 4 rows
  (`uir-desk-shots\a-min-songs.png`).
- Fix: collapse to the 56 DIP rail below ~1000 DIP width unless the user pinned it open.
- Verify: `winpos -Size 900,600`: `NavSidebar` width 56.

### DESK-13. Explorer integration is a single "Open" verb
- Severity: Low · Standard: Windows shell guidelines (verbs, MultiSelectModel)
- Where: `src/app/FileAssociations.cpp:45-67` (from code); written only by the "default player" button
- Impact: no "Play with Lumen" / "Add to Lumen queue" verbs, no folder verb; without `MultiSelectModel=Player`
  Explorer hides "Open" for more than 15 selected files and starts one process per file (Lumen merges them, but
  slower than one command line).
- Fix: register `shell\play` and `shell\enqueue` verbs with `MultiSelectModel=Player`, plus
  `Directory\shell\Lumen.play`.
- Verify: select 20 files in Explorer: "Play with Lumen" opens all 20 in one queue.

### DESK-14. The PiP window does not keep the video's aspect ratio when resized
- Severity: Low · Standard: Windows/Chrome PiP conventions
- Where: no `WM_SIZING` handling anywhere (`MainWindow.cpp`, from code)
- Fix: in `WM_SIZING` while `pip_`, adjust the dragged edge to the video's aspect.
- Verify: drag a PiP corner: no black bars.

### DESK-15. The track notification promises a cover it never shows
- Severity: Low · Standard: Nielsen #2, Windows guideline "Notifications"
- Where: `src/core/SettingsSchema.cpp:917` ("with the title and cover"); `SystemIntegration.cpp:225-235` uses
  `NIIF_USER | NIIF_LARGE_ICON` without `hBalloonIcon` (app icon only), via a legacy tray balloon (from code)
- Fix: set `nid.hBalloonIcon` from the cover, or drop "and cover" from the description.

### DESK-16. Text boxes have no right-click Cut / Copy / Paste menu
- Severity: Low · Standard: Windows convention for edit controls
- Where: `ui::TextBox` in `src/ui/Controls.cpp` (keyboard shortcuts exist at `:852`, no `onContextMenu`) (from code)
- Fix: give `TextBox` a default `onContextMenu` with Cut, Copy, Paste, Select all (disabled as appropriate).

### DESK-17. Hardware volume keys change Lumen's volume, not Windows'
- Severity: Low · Standard: Windows convention (product question)
- Where: `src/app/Actions.cpp:58-60` binds `VK_VOLUME_UP/DOWN/MUTE`; `Host` swallows the key, so no
  `WM_APPCOMMAND` reaches the shell (from code; confirm on hardware)
- Impact: with Lumen focused the volume keys move the in-app slider and probably no Windows volume flyout appears;
  VLC does the same, MPC-HC and WMP leave them to Windows.
- Fix: unbind the hardware volume keys by default (keep Up/Down), or make it a setting.

### DESK-18. The title bar has no inactive state and narrow caption buttons
- Severity: Low · Standard: Windows 11 title bar guidelines (46x32 epx caption buttons, dimmed when inactive)
- Where: `src/app/Shell.cpp:101-114` (`CaptionButton`, 36x40, never reads `Host::IsActive()`)
- Impact: with Settings and the main window open the user cannot tell which one is active.
- Fix: dim title text and caption glyphs to `textTertiary` when `!IsActive()`; widen caption buttons to 46 DIP.

### DESK-19. First start on a large library: ~350 ms of an empty window, without caption buttons
- Severity: Low · Standard: Skill J "blank screens during load"
- Where: `src/app/MainWindow.cpp:379-381` (`ContentCapMs` 400/700), `src/ui/Host.cpp:1435-1441`
- Evidence: worst-case library, fresh data dir: revealed at 273 ms, plain background until 584 ms, content at
  618-653 ms (`uir-desk-tmp\sheet-worst.png`, trace "content shown (cap reached)"); presses are ignored meanwhile
  (by design, bce7160). Timing noisy.
- Fix: paint the title bar (caption buttons usable) and sidebar in the first GPU frame and fade only the page.

### DESK-20. Default theme is Dark, not "System"; no Mica
- Severity: Low · Standard: Fluent "Theming", Windows app mode
- Where: `src/core/SettingsSchema.cpp:733-735`; `Host::ApplyDwmAttributes` (`Host.cpp:400-411`) sets no
  `DWMWA_SYSTEMBACKDROP_TYPE`
- Fix: default to `system` (onboarding pre-selects it); Mica is optional for the monochrome look and can stay out.

### DESK-21. Media keys do nothing until a file is loaded
- Severity: Low · Standard: Product (WMP/Spotify resume the last item on Play)
- Where: `src/app/SystemIntegration.cpp:443` (`IsEnabled` only with `fileLoaded`) (from code)
- Fix: keep SMTC enabled when history has a resumable item; Play resumes it.

Could not check: multi-monitor and mixed-DPI moves (one 100 % monitor), SMTC/taskbar/tray/notifications/jump lists
in action (disabled in `--automation`), real-mouse Snap Layouts flyout, Aero Snap, Win+arrows and the system menu
(no real input or native menus allowed), power requests (`powercfg /requests` needs admin), and a real screen reader.
