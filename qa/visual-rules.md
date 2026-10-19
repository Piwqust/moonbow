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
