# Lumen architecture

Native Windows player: C++20, Win32, Direct2D/DirectWrite/DirectComposition UI, libmpv playback.
Design source: `design/DESIGN.md` (tokens), `design/ref/*.png` (57 screens, 1 px = 1 DIP),
`design/ref/components/*.png`, `design/icons/*.svg`.

## Build
`build.bat` (MSVC, VS 2022) compiles every `.cpp` under `src/` into one exe; object files share a
folder, so **file basenames must be unique**. `/W4` must stay warning-free. `LUMEN_OUT=<dir>` builds
elsewhere (parallel builds). After editing `design/icons`, run `tools/gen-icons.ps1`.
`Lumen.exe --gallery` shows every control. `LUMEN_TRACE=1` writes startup timings to `%TEMP%\lumen-trace.txt`.

## Layers
```
src/gfx      Gpu (shared D3D11/D2D/DComp device, per-window Surface), Text (Inter, layout cache), Icons (SVG set)
src/ui       Types, Theme (monochrome palettes), Painter, Anim, Widget (tree + flex layout), Host (window), Controls
src/core     Json, Config + SettingsSchema (settings store/schema), Paths
src/library  MediaLibrary, History, Playlists, Bookmarks/Favorites, Thumbnails
src/app      App (player, queue, navigation), Actions (commands + shortcuts), MainWindow, Shell (title bar,
             sidebar, library pane), LibraryView (page interface), I18n
src/app/player   PlayerView, ControlBar (+ SeekBar, volume, control registry), Panels (popovers), MusicView, MiniPlayer
src/app/library  library pages (CreateLibraryView)
src/app/settings settings window
src/app/dialogs  dialogs, onboarding, customize player
Player.h/.cpp  libmpv wrapper (all commands async; ProcessEvents → PlayerChange mask)
```

## UI framework essentials (`src/ui`)
- **Widget**: rects are absolute window DIPs. Layout = `dir` Row/Column (flex: `width`/`height` =
  `Length::Px(n)`, `Length::Grow(w)`, `Length::Auto()`; `gap`, `pad`, `align` cross axis, `justify` main
  axis), Stack (children overlap), None (override `OnLayout` and call `child->Arrange(rect)`).
  Override `MeasureContent` for intrinsic size, `Paint`/`PaintOver` for drawing. `interactive = true`
  receives mouse; `focusable = true` gets Tab focus + keys. `onClick`, `onContextMenu`, `onDoubleClick`.
  `Invalidate()` repaints, `InvalidateLayout()` re-lays out. Widgets are owned by their parent
  (`Add<T>(...)` returns a raw pointer). Never keep raw pointers to widgets you don't own past their life.
- **Painter**: `Fill/Stroke(rect, color, radius)`, `Text(text, rect, style, color, HAlign, VAlign, flags)`,
  `Measure`, `Icon(name, box, color[, secondary])`, `Image(bitmap, rect, radius, cover)`, `Shadow`,
  `PushClip/PopClip`, `PushOpacity/PopOpacity`, `Clip()` (current visible rect, use it to virtualize).
- **Theme**: `P()` returns the palette (`bgDeep, bgBase, bgRaised, bgPanel, bgChrome, bgCard,
  bgSelected, bgMenu, hover, press, line*, text*, accent, accentMid, accentDim, accentFaint, onAccent,
  scrim, shadow, danger`). Read colors at paint time (theme switches live). No hard-coded colors.
- **Text styles**: `TextStyle::{Display, Title, Section, Heading, Large, Body, BodyMedium, BodyStrong,
  Small, SmallMedium, Micro, Mono, MonoSmall}` (Inter; Mono = tabular digits).
- **Icons**: names = files in `design/icons` without `.svg` (`play`, `pause`, `folder`, `home_active`,
  `eq_eq-rock`, ...). Two-tone parts take the `secondary` color (defaults to `accentMid`).
- **Controls** (`ui/Controls.h`): Label, IconView, Divider, Spacer, Button (Primary/Secondary/Ghost/
  Outline), IconButton (`active` = accent), LinkButton, Toggle, Checkbox, Segmented, Dropdown, Slider,
  TextBox, AccentPicker, ScrollView (`SetContent<T>()`), Panel, menus (`ShowMenu`, `ShowContextMenu`,
  `MenuItem{label, shortcut, icon, checked, enabled, separator, radio, action, submenu}`), `Dialog` +
  `ShowDialog`, `ShowToast`.
- **Host**: a window. `ShowPopup(content, PopupOptions{anchor, place, modal, scrim, ...})`,
  `ClosePopup`, `SetFocus`, `StartTimer/CancelTimer`, `Post(fn)`, `SetFullscreen`, `RequestRender`.
  New top-level windows (settings, onboarding) are `Host` instances with their own root widget and a
  title bar widget whose background has `ncRole = NcRole::Caption` (drag) and maximize button
  `NcRole::Maximize`.
- **Anim**: `AnimFloat a; a.To(target, ms, Ease::Out)`; read `a.Get()` while painting; the host keeps
  rendering while anything animates.

## App essentials (`src/app`)
- `TheApp()` → `App`: `player()`, `queue()` (PlayQueue), `main()`, `Open(paths)`,
  `PlayQueueItems(items, index, name)`, `PlayIndex`, `Next/Previous`, `StopPlayback`,
  `OpenFileDialog/OpenFolderDialog/OpenSubtitleDialog/OpenUrl`, `Navigate(Route)`, `GoBack/GoForward`,
  `ShowPlayer(bool)`, `OpenSettings(category)`, `ShowOnboarding()`, `Quit()`.
  Signals: `playerChanged(mask)`, `routeChanged`, `screenChanged`; `queue().changed`.
  Connect with `ScopedConnection c = Connect(signal, lambda);` stored as a member (auto-disconnects).
- `Player`: `State()` (`PlayerState`: position, duration, paused, volume, tracks, chapters, artist,
  title, hasVideo, hdr...), commands (`SeekAbsolute`, `SetTrack("aid"/"sid"/"vid", id)`, `AddSubtitle`,
  `SetSpeed`, `Screenshot`, `SetRecording`), generic `Command({...})`, `SetProperty(name, value)`,
  `Observe(name, format, cb)`.
- `Actions.h`: every command (`Action::*`) with default keys; `Run(a)`, `Enabled(a)`,
  `ShortcutText(a)`, `AllActions()` (drives the shortcuts dialog and Settings > Controls).
- `Route{Section, path}`; library pages implement `LibraryView` and are created by
  `CreateLibraryView(route, compact)`.
- **I18n**: write UI strings in English as in Figma and wrap them: `Tr(L"Continue Watching")`.
  Add Russian translations to your area's file in `src/app/strings/*.inc` as
  `{L"Continue Watching", L"Продолжить просмотр"},`. `Fmt(Tr(L"{0} items"), n)`, `CountItems(n)`,
  `Plural(n, one, few, many)`.
- Persistence: `core/Config` (settings, schema-driven), `library/*` services (JSON in `%APPDATA%\Lumen`).

## Rules
- UI thread only for widgets, Player calls and services (unless a service says otherwise).
- Never paint opaque pixels over the video hole (`MainWindow::SetVideoRect`); DirectComposition puts
  our UI above mpv's window.
- No blocking work on the UI thread (disk scans, thumbnails → worker threads, post results back).
- Keep `/W4` clean; match existing style (4-space indent, 120 cols, comments only where they help).
