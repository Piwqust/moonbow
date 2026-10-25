"""Finds colors that only look right on an assumed background (qa/visual-rules.md, rule 1).

1. Color::Mix(bg, white, 0.4) is a grey that only looks like "white at 40 %" on a background of
   exactly bg. Over a picture (the blurred cover of the music view, video, thumbnails) the real
   background differs and the grey sits on it as a dull patch. Such text and lines must be a
   translucent color (white.Fade(0.4)) instead. Flags Color::Mix calls that take videoBg, the
   color Moonbow assumes under pictures, directly or through a local variable set from it.
2. The palette's text tones are made for the theme's own surfaces: the light theme's are
   near-black. Files that paint straight over the video or the cover (OVER_PICTURE) must not use
   them, nor a Tone:: (which picks one). A line that draws on an opaque plate of its own can say
   so with the comment "on a plate".

Usage: python tools/dev/check-colors.py [files...]
Exit code: the number of findings.
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2] / "src"
# Everything these files draw sits on the video or the blurred cover, in both themes.
OVER_PICTURE = {"app/player/PipOverlay.cpp", "app/player/MusicView.cpp"}
files = [pathlib.Path(f) for f in sys.argv[1:]] or sorted(root.rglob("*.cpp"))
assign = re.compile(r"\b(\w+)\s*=\s*(?:P\(\)|c|palette|pal)\.videoBg\b(?!\.)")
mix = re.compile(r"Color::Mix\(([^;]*)")
tone = re.compile(r"\b(?:P\(\)|c|palette|pal)\.text\w+|\bTone::\w+")
found = 0
for f in files:
    text = f.read_text(encoding="utf-8")
    names = {"videoBg"} | set(assign.findall(text))
    rel = f.resolve().relative_to(root).as_posix() if f.resolve().is_relative_to(root) else ""
    name = f.relative_to(root.parent) if f.is_relative_to(root.parent) else f
    for no, line in enumerate(text.splitlines(), 1):
        m = mix.search(line)
        if m and set(re.findall(r"\w+", m.group(1))) & names:
            found += 1
            print(f"{name}:{no}: opaque mix with the "
                  f"assumed background; use a translucent color: {line.strip()}")
        if rel in OVER_PICTURE and tone.search(line) and "on a plate" not in line:
            found += 1
            print(f"{name}:{no}: theme text tone drawn over a picture; "
                  f"use translucent white: {line.strip()}")
print(f"{found} color(s) that depend on an assumed background")
sys.exit(found)
