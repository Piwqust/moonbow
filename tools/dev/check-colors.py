"""Finds opaque colors premixed with an assumed background (qa/visual-rules.md, rule 1).

Color::Mix(bg, white, 0.4) is a grey that only looks like "white at 40 %" on a background of
exactly bg. Over a picture (the blurred cover of the music view, video, thumbnails) the real
background differs and the grey sits on it as a dull patch. Such text and lines must be a
translucent color (white.Fade(0.4)) instead.

Flags Color::Mix calls that take videoBg, the color Lumen assumes under pictures, directly or
through a local variable set from it. Usage: python tools/dev/check-colors.py [files...]
Exit code: the number of findings.
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2] / "src"
files = [pathlib.Path(f) for f in sys.argv[1:]] or sorted(root.rglob("*.cpp"))
assign = re.compile(r"\b(\w+)\s*=\s*(?:P\(\)|c|palette|pal)\.videoBg\b(?!\.)")
mix = re.compile(r"Color::Mix\(([^;]*)")
found = 0
for f in files:
    text = f.read_text(encoding="utf-8")
    names = {"videoBg"} | set(assign.findall(text))
    for no, line in enumerate(text.splitlines(), 1):
        m = mix.search(line)
        if not m:
            continue
        args = set(re.findall(r"\w+", m.group(1)))
        if args & names:
            found += 1
            name = f.relative_to(root.parent) if f.is_relative_to(root.parent) else f
            print(f"{name}:{no}: opaque mix with the "
                  f"assumed background; use a translucent color: {line.strip()}")
print(f"{found} opaque color(s) over pictures")
sys.exit(found)
