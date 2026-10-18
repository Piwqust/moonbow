# Round 5, layers 1 and 2: the player, visually

Hunter: r5-player-eye (branch `worktree-agent-afd86f2077ee53e52`).

- **Exe.** `%TEMP%\lumen-qa\r5-v2\Lumen.exe`, prebuilt; nothing was rebuilt.
- **Instances.** `%TEMP%\lumen-qa\r5pe-1` … `r5pe-5`, all `--automation`, started with `tools\dev\launch.ps1`:

  | Data dir | Scale | Theme | Bar style | Other |
  |---|---|---|---|---|
  | r5pe-1 | 1.0 | dark | floating (default) | |
  | r5pe-2 | 1.5 | light | minimal, later classic/floating live | |
  | r5pe-3 | 1.25 | dark | floating | `interface.autoHide=always` (floating bar windowed) |
  | r5pe-4 | 2.0 | dark | classic | min window 720x440 |
  | r5pe-5 | 1.0 | dark | floating | fresh-instance cross-checks |

- **Media.** `%TEMP%\lumen-qa\media` (read only) and `%TEMP%\lumen-qa\r5pe-media`: 21:9 2520x1080, 9:16 1080x1920, 4:3 1440x1080, 160x90 tiny, a CJK + emoji long file name, MP3 with a long-tag cover, MP3 with synced `.lrc`, untagged MP3, a random-bytes `garbage bytes.mp4`.
- **Evidence.** `%TEMP%\lumen-qa\r5pe\` (full captures `*.png` with tree dumps `*.txt` of the same name; `c-*.png` are crops).
- **Layer 1.** Every capture was dumped with `dump-tree.ps1 -All` and run through a small audit (trunc/clip/overflow, interactive < 24 DIP, child outside a non-scrolling parent, overlapping text/interactive siblings). `hitmap.ps1` was swept windowed, at the minimum size with a menu open, maximized and fullscreen.
- **Layer 2.** Each screen was reviewed against DESIGN.md and a reference (the other theme, scale 1.0, or the same component elsewhere), in full resolution crops, layout pass first and colour/state pass second.
- **Screen.** 2560x1440 at 100 %; higher scales are the `--scale` override.

Line numbers refer to this worktree at `facf7be`. FL-xx items from `flows.md` are not repeated.

## Summary

| ID | Sev | Status | Screen / state | Finding |
|---|---|---|---|---|
| PV-01 | Ugly | confirmed | Control bar fading out/in (fullscreen, and windowed with auto-hide "always"), every style | Only the bar's background fades. The Play button, seek track, thumb, icons and times stay fully opaque, then vanish in one frame. Owner bug 4 ("hides and shows badly"). |
| PV-02 | Broken | confirmed | Music view at scale 1.25 / 1.5 / 2.0 | The blurred cover backdrop is drawn at 1/scale size: a hard-edged black band on the right (1.25) or three quarters of the view black (2.0). Fine at 1.0. |
| PV-03 | Ugly | confirmed | Light theme, every bar style | The unplayed seek track is `#E6E6E6` on a `#EBEBEB` bar (1.04:1); in the minimal bar the track is invisible. |
| PV-04 | Ugly | confirmed | Minimal bar | Chapter marks are never drawn on the slim seek bar (negative tick height). |
| PV-05 | Broken | confirmed | Player, a text file named `.mkv` (`media\Mixed\fake.mkv`) | No error card, no toast: a black player titled "Lumen" with `--:--:--`. In a folder queue it is skipped silently. A binary broken file shows the card correctly. |
| PV-06 | Ugly | confirmed | Tracks popover, Subtitles and Audio tabs | Raw ids: subtitle tracks are "eng" / "rus", detail "subrip" / "aac". The toast for the same track says "Субтитры: Русский". |
| PV-07 | Ugly | confirmed | Bookmarks tab, empty | The hint "Нажмите «+», чтобы добавить закладку на текущий мом…" is cut with an ellipsis and can't be read anywhere. |
| PV-08 | Ugly | confirmed | Chapters tab | Chapter times are always `00:00:20`, while the bar, the seek tip and the queue show `0:20`. |
| PV-09 | Ugly | confirmed | PiP with the overlay shown | Subtitles are drawn under the PiP seek bar: the line is struck through by the track. |
| PV-10 | Ugly | confirmed | Floating bar over video | The bar is 96 % opaque, so the picture's hue shows through, with a visible seam where the video colour changes. Owner bug 1 ("see-through"). |
| PV-11 | Fragile | confirmed | Popovers at small heights (720x440) | Quick settings shows 200 DIP of 1462 DIP of content with no scroll cue; the scrollbar only appears while scrolling. Rows are cut by the footer. |
| PV-12 | Ugly | suspected | Seek preview | Stays an empty box for the 160x90 file (2 of 2 instances, scale 1.0 and 1.5), and once for 9:16 after a 21:9 file. Fresh 9:16 and 4:3 work. |
| PV-13 | Ugly | confirmed (code) | Seek preview at 125–200 % | The 192x108 *pixel* frame is stretched into a 192x108 *DIP* box, so it is upscaled 1.25–2x and blurry. |
| PV-14 | Fragile | confirmed | PiP, mini player | Their seek bars are 12 DIP tall interactive targets (< 24). |
| PV-15 | Ugly | confirmed | Music view, long title | Title and artist share one ellipsized line, so a long title hides the artist completely; no tooltip, no second line. |
| PV-16 | Polish | confirmed | Volume wedge hover | The "0%" bubble straddles the bar's top edge, without a shadow; it doesn't match the seek tip or tooltips. |
| PV-17 | Fragile | confirmed | Video context menu left open | The open menu survives a fullscreen switch and a play/pause change and keeps the stale "Воспроизвести" label while playing. |
| PV-18 | Polish | confirmed | Quick settings, Воспроизведение tab at 1280x780 | The page scrolls by 3 DIP (`offset=0/3`): a wheel notch nudges the whole page for nothing. |
| — | — | — | Design gaps | See the section after the findings (fullscreen bar width, no top OSD, collapsed volume, menu grouping, duplicated tabs and more). |

### Round 4 re-checks

| R4 | Now | Evidence |
|---|---|---|
| P-02 window resizes on aspect change | **Fixed.** 16:9 → 21:9 → 9:16 → 160x90 kept 1280x780. | `p-wide-seek.txt`, `p-tall-seek.txt` |
| P-05 tooltips cover the bar | **Fixed.** The bubble now sits above the bar (it overlaps the bar's top edge by 6 DIP). | `c-tip-gear.png` |
| P-06 / P-18 cramped "выкл" toggle caption | **Still present**, dark and light. | `a-pop-settings.png`, `c-g-pops.png` |
| P-07 queue: only the current row has a duration | **Still present.** | `a-pop-queue.png` |
| P-12 no PiP context menu | **Fixed.** `menu pip` gives Play, Audio, Subtitles, Speed, Back to window. | `c-u-pip.png` |
| P-13 context menu taller than a small window | **Still present** at 720x440: the menu covers the title bar and its end is cut, with only the thin scrollbar as a cue. The menu region answers HTCLIENT (hitmap), so it is reachable. | `c-f-min-b.png` |
| P-16 skip chip over subtitles | **Fixed.** The chip sits above the subtitle band. | `d-1280-bar.png` |
| P-17 quick-settings footer eats the popover | **Partly fixed.** The footer collapses to one row at 720x440, but see PV-11. | `c-f-min-a.png` |

## Findings

### PV-01 (Ugly, confirmed): the bar's contents don't fade with the bar
- **Steps** (r5pe-1 and r5pe-5, 1.0, dark, floating):
  1. `command.ps1 "action video.fullscreen"`, `"action playback.play_pause"`.
  2. `input.ps1 -Move 930,530`.
  3. Capture 16 frames 60 ms apart from 1.3 s after the move (`burst.ps1`, an in-process `PrintWindow` loop).
- **Expected.** The whole bar, its controls included, dissolves as one layer and slides 10 DIP, as the code comment at `PlayerView.cpp:500` intends.
- **Actual.** Frames 5–8: the rounded panel fades, but the white Play button, the black seek track, the played fill, the thumb, the time labels and the icons stay at full alpha and float over the video. They disappear together when `bar_->visible` turns false (frame 9). The same happens on the way in. This is what reads as "the bar hides and shows badly".
- **Evidence.** `c-fs-hide-mid.png` (frames 4, 6, 7, 8), `c-fs-hide-strip.png` (all 16).
- **Cause.** `ControlBar::Paint` (`src/app/player/ControlBar.cpp:707-718`) applies `opacity` only to its own fill, shadow and stroke. Children are painted by `Widget::PaintTree` (`src/ui/Widget.cpp:245-255`) at full alpha. `PlayerView::OnLayout` sets `bar_->opacity` (`PlayerView.cpp:494-496`).
- **Fix.** Wrap the whole subtree in `Painter::PushOpacity(opacity)` / `PopOpacity()` (`src/ui/Painter.h:74-75`, already used by Host for popups). Push in `ControlBar::Paint`, pop in `ControlBar::PaintOver`, or override `PaintTree`. Do the same for anything else tied to `ChromeOpacity` (the skip chip and next-episode card when they ride with the bar).

### PV-02 (Broken, confirmed): the music backdrop is drawn at 1/scale size
- **Steps.**
  - r5pe-4 (`-Scale 2.0`, 720x440): `command.ps1 "open %TEMP%\lumen-qa\r5pe-media\lyr\song with lyrics.mp3"`, `wait-idle`, capture.
  - r5pe-3 (`-Scale 1.25`, 1280x780): open `audio with cover.mp3`, then the lyrics song.
  - Reference r5pe-5 (1.0): the same song.
- **Expected.** The blurred, darkened cover fills the whole music view, as at 1.0 (`c-q-music10.png`).
- **Actual.**
  - At 2.0 only the top-left quarter has the backdrop; it ends in a hard edge at x≈360 / y≈135 DIP, and the rest is flat black (`c-i-music-s2b.png`).
  - At 1.25 a black band covers the right ~18 % of the view, with and without lyrics (`c-h-cover.png`, `c-i-lyr-125.png`).
  - Most Windows laptops run at 125–150 %, so most users see this.
- **Cause.** `MusicView.cpp:377-391` computes the scale `k` from `bmp->GetSize()` (the bitmap's DIPs at 96 dpi), but the blur effect's output is in device-context units (DPI × scale). The drawn image is therefore `1/scale` of the size the code meant. At 1.25 the measured right edge is 1073 DIP; the formula with a 1/1.25 factor predicts 1075.
- **Fix.** Size from the effect output: `ctx->GetImageLocalBounds(out.Get(), &b)` and compute `k` from `b`. Or create the cover bitmap with the context's DPI so `GetSize()` and the effect agree. Test at `--scale 1.25` and `2.0`.

### PV-03 (Ugly, confirmed): light theme seek track is invisible
- **Steps.** r5pe-2 (light, 1.5): the minimal bar (default of that instance), then `set interface.barStyle "classic"`, then fullscreen with `"floating"` and `"minimal"`.
- **Expected.** The track is visible against the bar: ≥ 3:1 for a UI component (WCAG 1.4.11), as the volume wedge already does (`ControlBar.cpp:207-208`, "bgDeep vanishes in light").
- **Actual.** Bar `#EBEBEB`, unplayed track `#E6E6E6`: 1.04:1 (pixels measured in `m-light-classic.png`). In the classic bar you can just guess the groove. In the minimal bar, windowed and fullscreen, only the played part and the dot are visible, so you can't tell where the file ends.
- **Evidence.** `c-m-classic.png`, `c-e1-bar.png`, `c-m-fs.png`.
- **Cause.** `SeekBar::Paint` fills the track with `P().bgDeep` (`ControlBar.cpp:567`), and `bgDeep` is darker than `bgChrome` only in the dark theme.
- **Fix.** Use the same "empty" token as the volume wedge (`Mix(textTertiary, textSecondary, .15)` or a `track` token in `Theme.*`) for the slim track, and `lineStrong` for the full-height track in light. Keep `bgDeep` in dark.

### PV-04 (Ugly, confirmed): the minimal bar never shows chapters
- **Steps.** Any instance: `set interface.barStyle "minimal"`, then play "Chaptered Demo (1080p).mkv", which has 3 chapters.
- **Expected.** Chapter ticks as in the classic and floating bars (`d-1280-bar.png`: ticks at 392 and 671).
- **Actual.** No ticks (`c-e1-bar.png`, `c-m-fs.png` bottom).
- **Cause.** `ControlBar.cpp:594-597` draws ticks `{cx, t.y + 6, 1, t.h - 12}` for every non-compact bar. A slim track is 4 or 8 DIP tall (`ControlBar.cpp:533`), so the height is −8 or −4 and nothing is drawn.
- **Fix.** For `slim`, draw a 2 DIP gap in the track at each chapter (uosc style) or a tick from `t.y - 2` to `t.Bottom() + 2`.

### PV-05 (Broken, confirmed): a text file posing as video gives no error at all
- **Steps.**
  1. Copy `media\Mixed\fake.mkv` (12 bytes, "not a video") into an empty folder.
  2. `command.ps1 "open <that folder>\fake broken file.mkv"`, `wait-idle`, wait 2 s.
  3. Reference: `open <folder>\garbage bytes.mp4` (64 KB of random bytes).
- **Expected.** The error card: "Не удаётся открыть «…»", "Файл повреждён…", Close. The garbage file shows exactly that (`c-k-error3.png`).
- **Actual.** `state`: `file=0 queue=1`; `ErrorCard … hidden`, no toast. The player is black, the title bar says "Lumen", the times read `--:--:--`, and the bar stays fully enabled (`c-k-error.png`, `k-error2.txt`). Opened from `media\Mixed` (a folder queue), the same file is skipped and "silent vp9" plays with no notice (`c-k-trunc.png`).
- **Cause (suspected).** mpv reads a small text file as a playlist and expands its line into a playlist entry. The outer END_FILE then has reason REDIRECT, not ERROR, so `Player.cpp:353-356` never raises `kChangeError`, and `App.cpp:433-437` never sets `failedPath_` (`PlayerView.cpp:512`).
- **Fix.** Treat "the file we asked for ended without FILE_LOADED" as an error, whatever the reason. Or open single files with `--load-unsafe-playlists=no` / `playlist` handling off for non-playlist extensions. Then the card (or the toast in a queue) appears.

### PV-06 (Ugly, confirmed): raw language codes and codec ids in the track lists
- **Steps.** Play "Chaptered Demo (1080p).mkv", click the subtitles button (`click 1167,752` at 1280x780) and the audio button.
- **Expected.** "English", "Русский", the codec as "SRT"/"AAC" or nothing. The toast for the same action already says "Субтитры: Русский" (`n-toast-subcycle.txt`).
- **Actual.** Subtitles: "eng", "rus" with detail "subrip". Audio: "English 440 Hz (eng)" with detail "aac". The quick-settings dropdown also shows "eng".
- **Evidence.** `a-pop-subs.png`, `a-pop-audio.png`, `c-t-qs.png`.
- **Cause.** `TracksPanel.cpp:213-215` uses `t.lang` as is, and `TracksPanel.cpp:232` shows `t.codec` as is. `LanguageName()` exists (`src/app/Actions.cpp:199`) and the toast uses it (`Actions.cpp:160`).
- **Fix.** `label = t.title.empty() ? LanguageName(t.lang) : t.title`, and the suffix `(LanguageName(t.lang))` only when it adds something. Map codec ids to display names (subrip→SRT, ass→ASS, hdmv_pgs_subtitle→PGS, aac→AAC…). Use the same helper in the Subtitles and Audio tabs of quick settings.

### PV-07 (Ugly, confirmed): the empty-bookmarks hint is cut
- **Steps.** `command.ps1 "panel bookmarks"` with a file playing (r5pe-2, light, 1.5; the width is the same at 1.0).
- **Actual.** Tree: `text="Нажмите «+», чтобы добавить закладку на текущий момент." trunc box=[884,222 352x20]`. On screen it reads "…на текущий мом…" (`c-g-pops.png`, left).
- **Cause.** `QueuePanel.cpp:607-609` draws it as one line 352 DIP wide; the Russian text needs about 390.
- **Fix.** Wrap it (two lines, 40 DIP box) or shorten it to "«+» — закладка на текущем моменте".

### PV-08 (Ugly, confirmed): chapter times use a different format
- **Steps.** `panel chapters` on "Chaptered Demo" (1:30 long).
- **Actual.** "00:00:00", "00:00:20", "00:01:00", while the bar shows "1:30" and the seek tip "Middle part · 0:35" (`c-g-pops.png`, second panel).
- **Cause.** `QueuePanel.cpp:823` calls `FormatTime(ch.time, true)` (always hours).
- **Fix.** `FormatTime(ch.time, PS().duration >= 3600)`, as `TimeLabel` does (`ControlBar.cpp:137`).

### PV-09 (Ugly, confirmed): PiP subtitles collide with the PiP seek bar
- **Steps.** r5pe-3: play "Chaptered Demo", `action video.pip`, `wait-idle`, `input.ps1 -Move 300,300` (the overlay shows), capture.
- **Expected.** Subtitles lift above the PiP controls, as they do above the floating bar in the main window.
- **Actual.** "Second line, with italics." is drawn under the seek track at the bottom of the PiP window and is struck through by it (`j-pip.png`).
- **Cause.** `MainWindow::UpdateSubtitleLift` excludes PiP (`MainWindow.cpp:347`: `!pip_`), and `PipOverlay` reserves no subtitle margin.
- **Fix.** In PiP, while the overlay is visible, lift by the overlay's seek row height (≈ 28 DIP), or add a fixed `sub-margin-y` in PiP.

### PV-10 (Ugly, confirmed): the floating bar is see-through
- **Steps.** r5pe-3 (`interface.autoHide=always`, floating, dark, 1.25): play "Chaptered Demo" windowed, move the pointer to show the bar.
- **Expected.** An opaque `bg.chrome` panel (DESIGN.md: the bar is on `bg.deep`/`bg.chrome`; only dialogs imitate blur with 95 %).
- **Actual.** Bar pixels over magenta are (34,27,34) and over cyan (26,33,34), so a visible hue seam runs through the bar where the video colours change (`c-e2-barzoom.png`). In motion the bar shimmers with the picture. This matches the owner's "the UI is see-through".
- **Cause.** `ControlBar.cpp:713`: `P().bgChrome.Fade(0.96f * opacity)`.
- **Fix.** `Fade(opacity)`: opaque at rest, still fading with PV-01's layer.

### PV-11 (Fragile, confirmed): popovers hide most of their content with no cue
- **Steps.** r5pe-4 (2.0, 720x440): click the gear (`click 1292,824`), then the Видео / Аудио / Субтитры tabs (`click 974,152` / `1120,152` / `1290,152`).
- **Actual.**
  - Tree: `ScrollView [300,102 400x200] scroll offset=0/1262` (Видео), `0/911` (Аудио), `0/280` (Воспроизведение).
  - Only two rows are visible. The third (a dropdown) is cut in half by the footer (`c-t-qs.png`, `c-f-min-a.png`).
  - There is no scrollbar, fade or "more" cue at rest, so most of each tab looks absent.
- **Cause.** `ScrollView::PaintOver` (`src/ui/Controls.cpp:983-989`) draws the thumb only while `barVisible_` animates (scrolling) or while the pointer is over the bar.
- **Fix.** When content > viewport, draw a permanent thin thumb (as the context menu does in P-13), or a 16 DIP fade at the cut edge.

### PV-12 (Ugly, suspected): the seek preview stays empty for some files
- **Steps.**
  - r5pe-5 (1.0, fresh): `tall 9x16.mp4` first. The preview works (`c-q-tall.png`).
  - `action playback.next` → `tiny 160x90.mp4`, hover the bar (`-Move 500,752`, rest 1.5 s, move 2 px, rest 1.5 s). The box stays empty, black (`c-q-tiny.png`).
  - `open classic 4x3.mp4`: works again (`c-q-43.png`).
  - r5pe-2 (1.5): Chaptered → 21:9 (works, `c-p-aspects.png`) → 9:16 (empty) → 160x90 (empty, also after a 2.5 s rest and a second position, `c-p-tiny3.png`).
- **Expected.** A frame for every video, as for 4:3 and 21:9.
- **Unconfirmed.** Why. The 160x90 failure reproduced 2 of 2 times. Candidates are the preview's `vf=scale=w=192:h=108:force_original_aspect_ratio=decrease:force_divisible_by=2` upscaling a source smaller than the box (`SeekPreview.cpp:319`), or a failed bucket that never retries (`failed_`, `SeekPreview.cpp:278`). The 9:16 failure (1 of 1 after 21:9) may be the same path.
- **Next step.** Run with the preview trace on (`hover-to-frame` lines, `SeekPreview.cpp:283`) and check whether `Decode` returns null.

### PV-13 (Ugly, confirmed by code): the seek preview is upscaled at high DPI
- **Cause.** The frame is `kW × kH = 192 × 108` pixels (`SeekPreview.cpp:27`, `:319`, `:365`). It is drawn into a `192 × 108` DIP box (`SeekPreview.cpp:492-505`), which is 240×135 px at 125 % and 384×216 px at 200 %. Every preview is therefore bilinear-upscaled 1.25–2x.
- **Fix.** Request `kW·scale × kH·scale` pixels from the preview instance (round to even), and keep the box in DIPs.

### PV-14 (Fragile, confirmed): PiP and mini-player seek bars are tiny targets
- **Evidence.** `j-pip.txt` and `u-pip-menu.txt`: `app::SeekBar [10,254 460x12] interactive`. `j-mini.txt`: `SeekBar #seek [984,692 264x12]`. `u-mini-min.txt`: `[494,398 142x12]`.
- **Why it matters.** 12 DIP is half the WCAG 2.2 minimum. The mini player's bar sits 8 DIP under the title text.
- **Fix.** Keep the 4–6 DIP visual track but make the hit rect 24 DIP tall (e.g. `pad` above and below), as the main bar does (32 DIP rect, 26 DIP track).

### PV-15 (Ugly, confirmed): a long title hides the artist in the music view
- **Steps.** `open r5pe-media\audio with cover.mp3`; tags: a 79-character title, an artist with feat. and CJK.
- **Actual.** One centred line "Очень длинное название песни, которое не помещается ни в одну строку интерфейса · Исполнит…" at 1280x780 (`c-h-cover.png`). At 720x440 it reads "…которое не поме…" and the artist is gone (`c-i-music-s2b.png`). There is no tooltip and no second line; the title bar is truncated too.
- **Cause.** `MusicView.cpp:108` joins `title · artist` into one string, then it is ellipsized as one line.
- **Fix.** Title on line 1 (ellipsized), artist on its own line 2 with the album (the design's "две строки"), plus a tooltip with the full text.

### PV-16 (Polish, confirmed): the volume bubble is unlike every other bubble
- **Steps.** `input.ps1 -Move 1060,752` over the wedge (1280x780).
- **Actual.** "0%" in a 48x24 box at `rect.y - 30`. Its bottom 6 DIP overlap the bar's top edge, and it has no shadow. The seek tip has a shadow and a 26 DIP box (`ControlBar.cpp:648-651`), and tooltips have a bubble style (`c-vol-hover.png` vs `c-tip-gear.png`).
- **Cause.** `ControlBar.cpp:219-224` paints it inline in `VolumeControl::Paint`.
- **Fix.** Reuse the seek tip's geometry: 26 DIP tall, 8 DIP above the bar, `p.Shadow`. Better, draw it in `PaintOver`.

### PV-17 (Fragile, confirmed): an open context menu survives state changes and goes stale
- **Steps.** r5pe-1: `rclick 600,300` (menu open, paused), then without closing it `action video.fullscreen` and `action playback.play_pause`.
- **Actual.** `b-fs-state.txt`: `MenuPopup [600,302 275x701]` still open in fullscreen. Its first row still says "Воспроизвести" although `paused=0`. The same menu also stayed open across `open <file>` and showed over the new file (`c-i-music-s2.png`).
- **Real-input route.** A hardware Play/Pause key, an auto-advance to the next file, or a window resize while the menu is open.
- **Fix.** Close player menus on `kChangeFile` and on fullscreen/PiP changes, or rebuild the labels on `kChangeState`.

### PV-18 (Polish, confirmed): a 3 DIP scroll range
- **Evidence.** `a-pop-settings.txt`: `ScrollView [860,102 400x477] scroll offset=0/3` on the Воспроизведение tab at 1280x780. A wheel notch moves the page 3 DIP.
- **Fix.** Ignore overflow ≤ 4 DIP (treat as no scroll) or trim the 12 DIP top spacer (`SyncPage` first child `[860,102 0x12]`).

## Design gaps (compared with mpv/uosc, VLC 4, IINA, PotPlayer), not bugs

1. **Fullscreen timeline length.** The floating bar is capped at 1180 DIP (minimal: 760). On a 2560-wide screen the seek bar is 548 DIP (minimal: 424), about 13 s per DIP on a 2-hour film. uosc, IINA (OSC "full width" option) and VLC 4 run the timeline edge to edge in fullscreen. Consider a full-width seek row above a centred button row, or a cap at 60–70 % of the screen width (`PlayerView.cpp:503`).
2. **No top OSD in fullscreen.** There is no title, chapter, clock, or back/close. uosc, IINA and PotPlayer show a top bar with the title (and the chapter in uosc) when the pointer moves.
3. **Collapsed volume has no popup.** In the minimal bar and below 900 DIP the volume is only a speaker: the wheel shows a toast and a click mutes. There is no way to *see* the level or click a value. VLC, PotPlayer and IINA show a vertical slider on hover or click.
4. **No hover line on the seek bar.** There is only the tip and the preview. uosc and YouTube draw a hover marker and highlight the hovered chapter segment, which helps aim on a wide bar.
5. **Two popovers both have Audio and Subtitles tabs.** The tracks popover (Видео/Аудио/Субтитры) and quick settings (Воспроизведение/Видео/Аудио/Субтитры) overlap, and their footers differ ("Открыть настройки" as a flat filled bar vs a preset row plus "⚙ Открыть расширенные настройки"). IINA has one quick-settings sidebar with Video/Audio/Subtitles tabs that include the track lists.
6. **Context menu grouping.** "Субтитры ›" sits away from "Видео ›" / "Аудио ›", between "Показать в проводнике" and "Другие настройки". The last nine items have no separators (`a-ctx.png`). PotPlayer and mpv.net keep the Video/Audio/Subtitles submenus together and separate the file actions.
7. **Music view lacks "up next".** The queue is only behind the bar button. IINA's music mode and most music players show the next tracks beside the cover.
8. **Error state with no file keeps the whole bar live.** Audio/subtitles/settings buttons stay enabled with nothing loaded. VLC greys them out.

## Checked and fine

- **Layer 1 on every capture listed above.** No trunc/clip/overflow other than those reported. The long CJK + emoji file name and long tags ellipsize cleanly (no mid-glyph cuts) in the queue, title bar and library grid. PV-15 covers the one place where a value can't be read at all. There are no overlapping siblings and no children outside non-scrolling parents. The only small targets are PV-14.
- **Hit map.** Windowed, maximized and fullscreen are clean. At 720x440 with the context menu over the title bar the menu answers HTCLIENT. Resize bands sit only in the outer 4 px, below the bar's buttons.
- **Subtitle lift.** Fullscreen with the floating bar: lines move above the bar when it shows (`b-fs-bar.png`) and back to the bottom when it hides (`c-r-sub.png`).
- **Skip-intro chip.** It clears the subtitles at 1280x780 and 720x440.
- **Tooltips.** They no longer stick or cover the buttons.
- **Toasts.** Subtitle delay, speed, subtitle cycle and A-B loop are centred at the top and readable in light (`c-n-toast.png`).
- **Statistics overlay.** About 94 % opaque, readable over saturated colours, hidden in PiP.
- **PiP.** Layout, overlay buttons and the new menu fit 480x270.
- **Mini player.** Full and compact cards are fine.
- **Aspect ratios.** 21:9, 9:16, 4:3 and 160x90 letterbox and pillarbox correctly windowed, and the window keeps its size.
- **Lyrics.** Wrap and dim correctly at 1.0 and 1.25.

## Not covered

- **Loading state.** Local files open faster than a 60 ms capture (`c-l-load.png`). It needs a slow network URL, which is outside the no-network rule.
- **Next-episode card.** The `media2` show episodes are too short to hold the card for a capture, and `jump_90` was disabled at the end of the queue.
- **Side panel (Ctrl+B) tabs.** `panel <name>` opens the popovers. The side panel is FL-01/FL-05 territory; I did not repeat it.
- **Startup volume.** It stayed 0, so the wedge was only seen empty. Its fill colour was checked in code (`ControlBar.cpp:207-210`).
