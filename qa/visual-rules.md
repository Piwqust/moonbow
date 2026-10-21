# Visual rules

Mistakes that look fine in the code and on a dark test file, but show up on real media. Check every
UI change against them. When the owner finds a new kind of visual bug, it gets a rule here, and
a check where one is possible.

## 1. Over a picture, colors are translucent

**The rule.** Text, icons and lines drawn over a picture use a translucent color:
`white.Fade(0.4f)`, `Color::Hex(0x000000, 0.35f)`, or a palette color on a translucent plate. A
picture here means the blurred cover of the music view, video, thumbnails or posters. Never draw
them over a picture with an opaque grey made by `Color::Mix(bg, white, a)`, or with the palette's
`text*` colors and no plate under them.

**Why.** `Color::Mix(videoBg, white, 0.4)` is "white at 40 %" only on a background of exactly
`videoBg`. The music view draws the cover blurred under the lyrics, so the real background is
blue, red, or anything else. The grey ignores it and sits on it as a dull, dead patch. The owner
saw this in October 2026 on a blue cover with the Spotify-style lyrics.

**How to find it.**
- `python tools/dev/check-colors.py` lists every `Color::Mix` with `videoBg`, the color assumed
  under pictures, used directly or through a local variable. The exit code is the number of
  findings.
- Look at the result on a saturated picture, never only on black. To make a test cover:
  `ffmpeg -f lavfi -i "color=c=0x1E78E0:s=600x600" -frames:v 1 cover.png` in the song's folder.
  On a black or generated cover the bug is invisible: opaque grey and translucent white then look
  the same.
- In a review, any color with a mixed or fixed lightness in code that paints over a bitmap
  (`p.Image`, a blur effect, the video hole) is suspect.

**Fine.** `Color::Mix` of two palette colors on a solid panel (hover tints, the track's played
part). The panel is the real background there.

## 2. Over a picture, no theme text colors

**The rule.** Icons and text drawn straight on the video or the music view's stage take a fixed
white (`Color::Hex(0xFFFFFF, 0.92f)`, dimmed with `Fade`), in both themes. Never `P().text*` or a
`Tone::`, unless the line paints on an opaque plate of its own (the mini player's pills).

**Why.** The palette's text colors are made for the theme's own surfaces. In the light theme
`textPrimary` is `#141414`: over a dark scrim or a dark film it disappears. The picture-in-picture
buttons were near-black on their dark discs in the light theme (1.47:1, UI review 2026-10-20,
VIS-1), and the music view's stand-in art was a pale card on its black stage (VIS-8).

**How to find it.**
- `python tools/dev/check-colors.py` lists every theme text tone in the files that draw over a
  picture (`OVER_PICTURE` in the script: the PiP overlay, the music view). A line on a plate of
  its own says so with the comment `on a plate`. Add a file to the list when it starts painting
  over the video.
- Capture the change in the light theme over a bright and over a dark video, not only in dark.

## 3. A window change shows its new size and its new picture in the same frame

**The rule.** Anything that changes the window's size or state (fullscreen, Picture in Picture,
fit to video, restore after PiP) goes through `Host::ChangeSizeAhead` or the same steps in
`Host::SetFullscreen`: the frame for the new client size is drawn first, the change starts right
after a composition (`DwmFlush`), DWM's min/max animation is off for it, and the window's frame
style never changes (DWM attributes switch corners, border and shadow instead).

**Why.** DWM shows a new window size at once, but a frame drawn in `WM_SIZE` arrives a composition
later about one time in two, and taking `WS_OVERLAPPEDWINDOW` off and back on made DWM rebuild the
frame for 4-7 frames. The owner saw it as fullscreen "wobbling" (2026-10-20). Leaving fullscreen
for a maximized window restored and maximized it again, with Windows' animation in between.

**How to find it.**
- `tools/dev/wincap.ps1` captures the cloaked test window as DWM composes it (60 frames/s), and
  `python tools/dev/wincap-stale.py <dir>` counts frames of a wrong picture per switch.
  `frames.ps1` (PrintWindow) can't see these faults at all.
- Measure switches that shrink the window (start in fullscreen with
  `-Settings '{"video.startFullscreen":true}'`): when the window grows past its size at the start,
  the capture itself smears 1-2 frames.
- Repeat 8-10 times: a race shows in some runs only.
- A real-screen recording (ffmpeg `ddagrab`) only with the owner's OK in the current request,
  only as many as allowed, the window topmost (`interface.alwaysOnTop`) and only its rect.
