# Area VIS: visual design, typography, color, components (2026-10-20)

Reviewer: visual design. Build `%TEMP%\lumen-qa\uir-build\Lumen.exe` (main @ ff96c7f).
Evidence: the sweep `%TEMP%\lumen-qa\uir-sweep\` (PNGs viewed at 1:1, both themes) plus my own invisible
`--automation` instances (control gallery `--gallery`, library hover, saturated covers with lyrics, volume level
with `audio.enabled=false`). Crops and pixel samples are in `%TEMP%\lumen-qa\uir-vis-crops\`. Contrast ratios
are WCAG relative luminance, computed from sampled pixels. Web-only checks (HTML, breakpoints, skip links) were
skipped. Coordinates are client pixels at 1280x780, scale 1.0, unless noted otherwise.

## Strengths

- **The tokens hold.** Both themes use only the DESIGN.md palette, and elevation reads in each one: the sidebar
  panel sits on the base, menus and dialogs on the scrim. Light is a real theme, not an inversion: white panels on
  `#F2F2F2`, a dark primary button. `check-colors.py` reports 0 opaque colors over pictures. On a saturated
  `#1E78E0` cover the blurred backdrop takes the cover's hue and the lyric lines stay translucent white
  (`uir-vis-crops\cov-18296.png`), so E5 stays fixed.
- **One interaction language.** In the gallery, 13 of 15 controls change on hover
  (`uir-vis-crops\g-hover-pairs.png`). Every focusable control shows the same 1.5 DIP `accentMid` ring at a
  2 DIP offset (`g-focus-sheet.png`).
- **The album page** (`music-album-page.dark`) has Spotify-grade hierarchy: a small "Альбом" eyebrow, a Light 32
  title, the artist, a meta line, then a primary "Воспроизвести все" and a secondary "Перемешать". Times are
  right-aligned under right-aligned "Длина" headers here, in Songs and in History.
- **Empty states form one family** (`home-empty`, `playlists-empty`, `network`): icon, Light title, two lines of
  hint, then a primary and a secondary button. The layout is the same in both themes.
- **Library cards and the mini player.** Card hover shows a play disc, a dimmed thumbnail and a brighter title
  (`lib-hover-62244.png`). The mini player puts its controls on a translucent light pill over the video
  (`mini-player.light`), as visual-rules rule 1 asks.
- **The type scale matches DESIGN.md** (display, title, section, heading, body, mono, small; gallery
  `g-scroll2.png`). Times use tabular numerals.

## Findings

### VIS-1. Picture-in-picture buttons are near-black on a dark disc in the light theme
- Severity: High · Standard: WCAG 2.2 1.4.11 Non-text Contrast; qa/visual-rules.md rule 1
- Where: PiP window, hover overlay, light theme. Code `src/app/player/PipOverlay.cpp:23` (`tone = Tone::Primary`)
  and `:31` (disc `#000000` at 35 %).
- Evidence: `uir-sweep\pip.light\final.png`, zoomed in `uir-vis-crops\pip-btn.png`. The pause glyph is `#181813`
  on its disc `#3A370B` over yellow video, **1.47:1**. Previous is just as dark. In the dark theme the same glyphs are
  white (`pip-btn-d.png`). The icon takes the palette's `textPrimary`, which is `#141414` in light, and draws it over
  video. `check-colors.py` misses this because it only looks for `Color::Mix(videoBg…)`.
- Impact: Every light-theme user who opens PiP sees three dark discs with no visible symbols. The core PiP controls
  can't be told apart.
- Fix: In `PipButton::Paint`, draw the icon in a fixed `Color::Hex(0xFFFFFF, 0.92f)` (disabled:
  `Fade(0.4f)`) instead of the theme tone, as the dark theme already does. Or use the mini player's light-pill
  treatment for both. Extend `check-colors.py` to flag `Tone::`/`P().text*` inside overlay classes that paint over
  video.
- Verify: Run the `pip.light` sweep scenario and crop 150,100-330,170. Expect white glyphs, ≥4.5:1 against the disc
  on yellow and white video.

### VIS-2. You can't read the volume level: the filled and empty parts of the wedge are almost the same grey
- Severity: High · Standard: WCAG 2.2 1.4.11 Non-text Contrast; Nielsen #1 Visibility of system status
- Where: Control bar volume wedge, both themes. Code `src/app/player/ControlBar.cpp:322-325`.
- Evidence: At volume 60, filled is `#B5B5B5` against empty `#8F8F8F` in dark (**1.58:1**) and `#4B4B4B` against
  `#5C5C5C` in light (**1.33:1**) (`uir-vis-crops\vol-pair.png`, my instances with audio disabled). At 5 %
  (`uir-sweep\toast-volume\final.png`, 980-1066,742-762) and at 0 % (every player screenshot) the wedge looks like a
  solid full triangle, because the empty track is a bright `textTertiary`/`textSecondary` mix. The muted level
  (`Mix(empty, accentMid, .45)`) is closer still. The wedge has no thumb, so the fill is the only cue.
- Impact: Every user. Volume reads "full" while it is at 0 or 5 %, and partial levels are guesswork. This sits next to
  a muted icon, so the two signals contradict each other.
- Fix: Draw the empty track as a faint shape (`P().lineStrong` in dark, `P().line` in light, or a 1 px `lineStrong`
  outline only). Keep the level `accentMid`→`accent` (≥3:1 against the empty track). When muted, draw the level at
  `textDisabled`.
- Verify: Launch with `audio.enabled=false, audio.startupVolume=60` in both themes and sample the filled and empty
  parts of the wedge. Expect ≥3:1. At volume 0 the wedge must read as empty.

### VIS-3. In the light theme ON and OFF toggles differ only by knob position
- Severity: Medium · Standard: WCAG 2.2 1.4.11; Nielsen #1
- Where: Every toggle in Settings, the quick panel and onboarding, light theme. Code `src/ui/Controls.cpp:~280-291`
  (knob `Mix(knobOff, knobOn, k)`).
- Evidence: `uir-sweep\settings-general.light\final.png`, zoomed in `uir-vis-crops\tog-light.png`. The ON knob is
  `#141414` on a `#D7D7D7` track. The OFF knob is `#555555` on `#E4E4E4`. Both knobs are dark squares (**2.47:1**
  between them) and the tracks differ by 13 levels. The only real cue is the 10 px "вкл/выкл" label. Dark is clearer
  (white knob against grey knob).
- Impact: A column of light-theme toggles has to be read word by word. This is the "state without hue" case that a
  monochrome UI has to solve with fill.
- Fix: In light mode give the ON state a filled track (`accent` `#141414`) with an `onAccent` knob, and keep OFF as
  an outlined track with a `lineStrong` knob, the way iOS and Fluent express ON by fill. Apply the same token rule in
  dark (`accentDim` track, as DESIGN.md's "фон ON accent.dim" already specifies).
- Verify: In `settings-general.light`, crop the toggle column and sample the tracks. Expect ON and OFF tracks ≥3:1
  apart.

### VIS-4. Titles and subtitles are two nearly equal greys, so content has no clear top level
- Severity: Medium · Standard: Visual hierarchy (skill §B/§E); Nielsen #8
- Where: Home, Videos, Music grids, the songs list and the sidebar. Code: card captions in
  `src/app/library/ItemGrid.cpp` use `textSecondary`/`textTertiary`.
- Evidence: Sampled from `uir-sweep\home.dark\final.png`:

  | Element | Dark | Light |
  |---|---|---|
  | Section heading (20 px) | `#A8A8A8` | `#424242` |
  | Card title | `#A8A8A8` | `#424242` |
  | Card subtitle ("Дальше: S01E01") | `#8B8B8B` | `#616161` |
  | Page title "Главная" (only white text besides the selected nav item) | `#EAEAEA` | `#141414` |

  Title against subtitle is only **1.4:1** in luminance. The largest text on the page, the section headings, sits at
  the same grey as the 14 px card titles.
- Impact: Grids look dim and flat ("muddy"), and scanning by title is slower. Spotify, Apple Music and the
  album page in this same app use primary for titles and grey for metadata.
- Fix: Card and row titles at rest: `textPrimary` (keep the hover brightening as a background or underline
  change). Section headings: `textPrimary`. Subtitles and meta: `textTertiary`. This gives three clear steps:
  heading, title, meta.
- Verify: Re-run `home.dark/light` and `music-albums` and sample the title and subtitle. Expect a step of ≥2:1
  between them.

### VIS-5. Floating surfaces don't look like one family: five corner radii and four fills
- Severity: Medium · Standard: Design-token consistency (skill §B "Component visual consistency")
- Where: Menus, toasts, popovers, dialogs, mini player and volume popup (from code, confirmed in screenshots).
- Evidence:

  | Surface | Radius | Fill | Code |
  |---|---|---|---|
  | `MenuPopup` | 6 | `bgMenu` | `src/ui/Controls.cpp:1230` |
  | `ToastBubble` | 8 | | `Controls.cpp:1711` |
  | `MiniPlayer` | 8 | `bgPanel` | `PlayerView.cpp:1013` |
  | `VolumePopup` | 8 | | `ControlBar.cpp:198` |
  | Color popup | 8 | | `SettingsRows.cpp:450` |
  | `Popover` (quick settings, queue) | 10 | `bgBase` | `PanelKit.h:19`, `PanelKit.cpp:319` |
  | `ui::Dialog` | 10 | `bgRaised` | `Controls.cpp:1656` |
  | `dlg::Frame` dialogs | 14 | `bgPanel`, `lineSubtle` stroke | `DialogKit.cpp:399,467` |

  DESIGN.md §3 says 12 for dialogs and popovers. You can see it side by side in `menu-context.dark` (r6 menu) next
  to `panel-quick-playback.dark` (r10 panel) and `dialog-info.dark` (r14).
- Impact: Each overlay was tuned alone, so the set reads as assembled rather than designed. This is the first
  thing a design lead notices when they flip through the overlays.
- Fix: Add two tokens: `kRadiusPopup = 12` for menus, popovers, toasts, mini player and volume popup, and
  `kRadiusWindow = 14` for Frame dialogs and onboarding. Pick one popup fill (`bgMenu`) and one stroke (`line`).
  Route all of the above through them and delete `ui::Dialog`'s own 10/`bgRaised`.
- Verify: Running `grep -nE "Shadow\(rect, *(6|8|10)"` should return nothing. Contact-sheet the menu, popover, toast
  and dialog side by side.

### VIS-6. Control radii drift off the 2/4/6/12/14 scale
- Severity: Medium · Standard: Design-token consistency
- Where: Inputs and buttons (from code, visible in `dialog-url.light` and `settings-general`).
- Evidence:

  | Controls | Radius | Code |
  |---|---|---|
  | `Button`, `TextBox` | 4 | `Controls.cpp:173`, `:904` |
  | `Segmented`, `Dropdown` | 5 | `Controls.cpp:417`, `:475` |
  | `DButton`, `FieldDropdown`, `TextArea` | 6 | `DialogKit.cpp:100`, `:109`, `:234` |
  | Playlist "+" tile | 5 | `Dialogs.cpp:350`, `:437` |
  | Network tile icon plate | 10 | `LibNetwork.cpp:94` |
  | Music placeholder card | 16 | `MusicView.cpp:1201` |

  In `dialog-url.light`, the address field (r6) sits above the playlist-name field (r4).
- Impact: In one dialog, fields and buttons have visibly different corners. The `Theme::Radius` skin mapping
  (`Theme.h:44`) passes 5, 10 and 16 through unmapped, so skins can't restyle those controls.
- Fix: Buttons, fields, dropdowns and segments get 4. Cards and the music placeholder get 6. Use only design radii
  so `Theme::Radius` maps them.
- Verify: Grep the radius arguments in `Fill` and `Stroke` (`grep -rhoE "p\.(Fill|Stroke)\([^;]*, *[0-9.]+f?\)"`).
  Only 2, 4, 6, 12 and 14 should remain, plus geometry such as pills and thumbs.

### VIS-7. Text fields show focus as a 1 px line at 1.5–1.9:1, unlike the focus ring on every other control
- Severity: Medium · Standard: WCAG 2.2 2.4.7 / 2.4.11 Focus Appearance; 1.4.11
- Where: Search fields, path fields, the URL dialog and settings inputs. Code `src/ui/Controls.cpp:905-906`
  (`TextBox`) and `src/app/dialogs/DialogKit.cpp:235` (`TextArea`). Neither calls `PaintFocusRing`.
- Evidence: In `uir-sweep\dialog-url.light`, the focused address field's border is `#BDBDBD` on `#E6E6E6` and
  `#F2F2F2` (**1.51:1**). In dark it is `#404040` on `#090909` (**1.92:1**). Buttons, segments and dropdowns get the
  1.5 DIP `accentMid` ring (`g-focus-sheet.png`). Placeholders also disagree: `TextBox` uses `textTertiary`, while
  `TextArea` uses `textDisabled`. In the same dialog, the address placeholder is visibly lighter than the playlist
  placeholder.
- Impact: Keyboard users tabbing into a field lose track of focus. Only the blinking caret marks it.
- Fix: In `TextBox::Paint` and `TextArea::Paint`, replace the focused `lineStrong` stroke with
  `PaintFocusRing(p, rect, radius)` (and keep `line` for hover). Use `textTertiary` for every placeholder.
- Verify: Tab into the search field in a light instance and capture. Expect a 1.5 DIP ring ≥3:1 against the
  surroundings, the same as the buttons.

### VIS-8. Light theme, music view: a light placeholder card on a black stage looks like a hole
- Severity: Medium · Standard: Color & theming (skill §F "Dark mode support", not just inverted)
- Where: Music view, song without a cover, light theme (`uir-sweep\player-lyrics.1280x780.light\final.png`,
  `player-lyrics.1024x640.light`).
- Evidence: The stage stays `#000000`-based, but the vinyl placeholder card is drawn in the light `bgCard`
  (`#E4E4E4`, 97,71-553,526), so a pale-grey slab floats on black. The first-run hint toast is white on the same
  black. In dark the card is `#1E1E1E` and blends in (`player-lyrics.1280x780.dark`). Code: the placeholder uses
  `c.bgCard`/`c.line` (`src/app/player/MusicView.cpp:1201,1221`).
- Impact: The music screen is the one emotional surface. In light theme a song without art looks broken next to
  one with art.
- Fix: The stage is always dark, so paint its placeholder with fixed translucent white (for example
  `Color::Hex(0xFFFFFF, 0.06f)` fill, `0.10f` stroke and `0.35f` glyph), as visual-rules rule 1 prescribes for
  pictures.
- Verify: Capture `player-lyrics.1280x780.light`. The placeholder should match the dark-theme one in tone.

### VIS-9. Seek bar: the played part barely separates from the track, and the light track looks like a disabled slab
- Severity: Medium · Standard: WCAG 2.2 1.4.11
- Where: Player control bar, classic, floating and music waveform. Code `src/app/player/ControlBar.cpp:832-866`.
- Evidence: The played fill against the track is `#525252` against `#090909` in dark (**2.55:1**) and `#878787`
  against `#BDBDBD` in light (**1.84:1**). In light the whole track is `lineStrong` `#BDBDBD` with a darker stroke on
  a `#EBEBEB` bar (`uir-vis-crops\bar-light.png`), the heaviest element in the bar, and it reads like a disabled
  field. The thumb carries the position, so this is Medium, not High.
- Impact: Progress is hard to read at a glance. The light-theme bar looks heavy and grey next to the dark
  original.
- Fix: Use `accentMid` for the played part (≥3:1 against the track in both themes). In light, make the track
  `bgDeep` with a 1 px `line` stroke instead of the solid `lineStrong` fill.
- Verify: Sample the played part and the track in `player-video.light.classic.shown`. Expect ≥3:1, and a track
  lighter than the played part.

### VIS-10. Error states use three different visual vocabularies
- Severity: Medium · Standard: Nielsen #4 Consistency; DESIGN.md §1 (`danger` only for the close button)
- Where: In the library, the "Битый файл" badge has a red warning triangle (`src/app/library/ItemGrid.cpp:78`,
  `c.danger`; `videos.dark`). The player error card uses a neutral grey info "i" and a borderless "Закрыть" that
  looks like plain text (`src/app/player/PlayerView.cpp:430`; `error-fake.dark`). URL dialog validation uses an info
  icon in the danger color (`src/app/dialogs/Dialogs.cpp:879-882`).
- Evidence: Screenshots as cited. For the same file (fake.mkv) the library says "warning, red" and the player says
  "info, grey". The card's only action is a ghost button at 14 px semibold with no fill.
- Impact: Severity reads differently depending on where you meet the error, and the player card has no visible
  primary action.
- Fix: Pick one error glyph (`warning`) and one color rule: either allow `danger` for error glyphs (update
  DESIGN.md) or use `textPrimary`. Make the error card's "Закрыть" a `ButtonKind::Secondary` (and "Следующий файл"
  Primary when queued).
- Verify: Compare the `error-fake`, `videos` and `dialog-url` validation states side by side. Expect the same glyph
  and color, and a filled button on the card.

### VIS-11. Context-menu icons jump between two optical sizes, and 40 DIP rows make the menu 700 DIP tall
- Severity: Medium · Standard: Iconography consistency; Windows 11 menu metrics (Fluent)
- Where: Right-click menu over the video (`uir-sweep\menu-context.dark\final.png`, crop
  `uir-vis-crops\menu-icons.png`). Code `src/ui/Controls.cpp:14` (`kMenuItemH = 40`), icons at `:1264`.
- Evidence: Pause, Stop, Previous and Next are small filled glyphs about 10 DIP wide. Record, Hide UI, Video,
  Audio, Info and the rest are 18 DIP outline icons in the same 20 DIP slot, so the left column is ragged. With 17
  rows the menu runs from y 71 to 771 in a 780 px window. Windows 11 context menus use about 32–36 DIP rows.
- Impact: The menu looks unrefined and covers the whole video.
- Fix: Use the outline 24-grid variants (`play_outline`, and so on) or pad the transport glyphs to the same optical
  box. Lower `kMenuItemH` to 36 (or 32 for context menus).
- Verify: Crop the icon column. All glyphs should share one visual size. Menu height ≤ about 600 DIP for 17 items.

### VIS-12. A disabled menu item keeps a bright shortcut
- Severity: Low · Standard: Interactive states (skill §G "Unclear disabled states")
- Where: "Следующий файл N" in the context menu when there is no next file. Code `src/ui/Controls.cpp:1278`
  (shortcut is always `c.textTertiary`).
- Evidence: `menu-context.dark`. The label is `#646464` but the "N" is `#8B8B8B`, brighter than the label it belongs
  to.
- Fix: Paint the shortcut in `fg` when `!m.enabled` (that is, `textDisabled`).
- Verify: Re-capture `menu-context.dark` and sample the "N". Expect `#646464`.

### VIS-13. The first-run hint toast collides with side panels and with the album art
- Severity: Low · Standard: Layout and visual balance
- Where: Player with the queue or quick panel open, and the music view.
- Evidence: In `panel-quick-playback.dark` and `panel-queue.dark` the toast spans x 417–863 and the panel starts at
  860, so the two touch or overlap. In `player-audio.dark.shown` and `player-audio-worst.1280x780`, the toast
  (y 56–96) sits on the top edge of the cover (y 71+).
- Fix: Centre toasts in the free video area (`VideoArea` rect minus the open panel), and in the music view place
  them above the title block or in the lyrics column.
- Verify: Re-run the two panel scenarios. Expect ≥16 DIP between the toast and the panel.

### VIS-14. The item count touches the search field when the sort and view buttons are hidden
- Severity: Low · Standard: Spacing grid (DESIGN.md §3)
- Where: Search results and History header (`uir-sweep\search.light`, `recent.light`; crop
  `uir-vis-crops\search-count.png`).
- Evidence: Tree dump: Label "4 элемента" ends at x 896 and the TextBox starts at 900, a 4 DIP gap against a 1 px
  border. With the icons present, the gap is 4 DIP plus the icon button's padding.
- Fix: Give the header row a 12 DIP gap before the search field (or right padding on the count label).
- Verify: In the tree dump, the count's right edge should be ≥12 DIP left of the TextBox.

### VIS-15. Shortcuts dialog: the header controls miss the content's edges, and an external-link icon repeats in every key cell
- Severity: Low · Standard: Alignment; visual noise
- Where: `uir-sweep\dialog-shortcuts.light\final.png`.
- Evidence: The Frame spans 116..1164. The search TextBox starts at x 125 and the dropdown ends at 1155, while the
  column labels and rows run from 136 to 1144, so the header is 11 DIP wider on each side. Each of about 40 key
  cells carries the same ↗ icon. The key cells are 270 DIP grey slabs that don't read as keys.
- Fix: Inset the header row to the content padding (20 DIP). Show ↗ only on row hover or focus. Shrink the cells
  to the key width with a keycap style (`KeyCap` already exists in `DialogKit.cpp:128`).
- Verify: In the tree dump, the TextBox's x should equal the label's x (136). A capture with no hover should show
  no ↗.

### VIS-16. Onboarding titles change color and position from page to page
- Severity: Low · Standard: Consistency
- Where: `uir-sweep\onboarding-1.dark`, `onboarding-2`, `onboarding-3`.
- Evidence: Page 1 "Welcome to Lumen!" is `#EAEAEA` at y about 235. Page 2 "Choose a theme." is `#A8A8A8` at y
  about 78. Page 3 "You're all set." is white at y about 101. Only page 2 is grey, and the title jumps as you go
  forward.
- Fix: Use the same `display` style in `textPrimary` and the same top offset on all three pages.
- Verify: Sample all three titles. Expect the same color and the same y.

### VIS-17. The search query isn't highlighted in results (library and settings), but it is in menus
- Severity: Low · Standard: Nielsen #1; consistency
- Where: Library search (`search.light`, crop `uir-vis-crops\search-hl.png`) and settings search
  (`settings-search.dark`, crop `sset-hl.png`). The sweep's own scenario descriptions expect "the query highlighted".
- Evidence: "Track Number 1" and "Включить звук" render plainly. `MenuPopup` underlines typed matches
  (`Controls.cpp:1288-1291`). Nothing in `ItemGrid.cpp` or `Views.cpp` does (from code: no match marking).
- Fix: Reuse the menu's 2 DIP accent underline (or SemiBold) for matched ranges in `ItemGrid` titles and settings
  row labels.
- Verify: Capture `search.light`. "Track" should be underlined in each row.

### VIS-18. Press and hover feedback is too faint on secondary buttons, toggles and the accent picker
- Severity: Low · Standard: Interactive states (skill §G)
- Where: Code `src/ui/Controls.cpp:161` (Secondary hover `Mix(bgPanel,bgSelected,.6)` ≈ `#202020`, press
  `bgSelected` `#242424`). Gallery evidence in `uir-vis-crops\g-hover-pairs.png` and `g-press-sheet.png`.
- Evidence: Pressed and hover on "Browse" differ by about 4 levels. Toggle hover changes at most 13/255. The accent
  swatch picker shows no hover change at all (diff empty).
- Fix: Use `press` = `Mix(bgPanel, textPrimary, 0.16f)` for Secondary. Raise the toggle track on hover. Give swatches
  a `line` ring on hover.
- Verify: Diff hover and pressed captures in the gallery. Expect a max delta ≥20 for each control.

### VIS-19. The caption buttons' focus ring is clipped at the window's top and right edges
- Severity: Low · Standard: WCAG 2.2 2.4.11 Focus Not Obscured
- Where: Title-bar minimize, maximize and close buttons. Seen in the gallery (`uir-vis-crops\focus-close.png`); the
  main window uses the same `IconButton` focus ring (`src/app/Shell.cpp:101-110`, `Widget.cpp:258`, from code).
- Evidence: The ring is drawn at `rect.Inset(-2)`. The top and right strokes fall outside the client area, so only
  the left and bottom remain.
- Fix: For caption buttons, draw the ring inset (`rect.Inset(2)`) instead of outset.
- Verify: Tab to Close in a main-window instance and crop the top-right corner. Expect all four sides of the ring.

### VIS-20. Still open (ESCAPES O1): a white cover leaves a grey backdrop and dim past lyric lines
- Severity: Low (already tracked) · Standard: qa/visual-rules.md
- Where: Music view, light or white cover (`uir-vis-crops\cov-57980.png`, `#F4F1EA` cover).
- Evidence: The past line is `#8D8C8B` on a backdrop of `#41403D` (**3.32:1**). The next lines reach `#A3A3A2` on
  `#2E2D2B`. Nothing new beyond O1: listed so the record shows it is unchanged at ff96c7f.

Last line: I could not check motion (no frame series of transitions or lyric scroll), the high-contrast and sepia
skins in depth, scale 2.0 rendering crispness, or real hover and pressed states inside the main window's popovers.
The pressed capture of the gallery's primary "Next" came back identical to its rest state (an artifact of the
harness, unconfirmed), so I did not report it.
