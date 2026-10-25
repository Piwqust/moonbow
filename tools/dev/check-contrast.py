"""WCAG contrast of the colour tokens, read from the code: the brand palette and accent tones in
src/core/Skin.cpp, the pairs of skin::CheckContrast, and the built-in skins in res/skins.

Prints one line per failing pair (and, with -v, every pair) for every accent tone, both themes
and every built-in skin. The exit code is the number of failures.
tests/core_tests.cpp runs the same check in C++; this script shows the numbers behind it and
lets a reviewer try a value before changing the code.

Usage: python tools/dev/check-contrast.py [-v] [--skin oled] [--pair line.control]
"""
import argparse
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2]
skin_h = (root / "src/core/Skin.h").read_text(encoding="utf-8")
skin_cpp = (root / "src/core/Skin.cpp").read_text(encoding="utf-8")

# enum Token { kBgDeep, ... kTokenCount } and the names in the same order.
enum = re.search(r"enum Token : int \{(.*?)\};", skin_h, re.S).group(1)
TOKENS = [t for t in re.findall(r"\bk\w+", enum) if t != "kTokenCount"]
NAMES = re.findall(r'"([a-z.]+)"', re.search(r"kTokenNames\[kTokenCount\] = \{(.*?)\};", skin_cpp, re.S).group(1))
assert len(TOKENS) == len(NAMES), (len(TOKENS), len(NAMES))
NAME_OF = dict(zip(TOKENS, NAMES))


def hexes(name):
    return [int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{6})", re.search(name + r"\[kAccentTones\] = \{(.*?)\};", skin_cpp).group(1))]


ACCENTS = {True: hexes("kDarkAccents"), False: hexes("kLightAccents")}
TONE_NAMES = ["White", "Pearl", "Silver", "Steel", "Graphite"]

# BrandColors(): `c[kX] = Rgba::Hex(0x..., alpha)` inside `if (dark) {...} else {...}`, then shared ones.
brand_src = re.search(r"Colors BrandColors\(bool dark\) \{(.*?)\n\}", skin_cpp, re.S).group(1)
dark_part, rest = brand_src.split("} else {", 1)
light_part, shared = rest.split("\n    }\n", 1)


def assigns(text):
    out = {}
    for tok, rgb, alpha in re.findall(r"c\[(k\w+)\] = Rgba::Hex\(0x([0-9A-Fa-f]{6})(?:, ([0-9.]+)f)?\)", text):
        out[NAME_OF[tok]] = (int(rgb, 16), float(alpha) if alpha else 1.0)
    return out


BRAND = {True: {**assigns(dark_part), **assigns(shared)}, False: {**assigns(light_part), **assigns(shared)}}

# CheckContrast's kPairs: {kFg, {kBg, ...}, need},
pairs_src = re.search(r"static const Pair kPairs\[\] = \{(.*?)\n    \};", skin_cpp, re.S).group(1)
PAIRS = [(NAME_OF[fg], [NAME_OF[b] for b in re.findall(r"k\w+", bgs)], float(need))
         for fg, bgs, need in re.findall(r"\{(k\w+), \{([^}]*)\}, ([0-9.]+)\}", pairs_src)]


def rgb(c):
    return [(c >> 16) & 255, (c >> 8) & 255, c & 255]


def mix(x, y, t):
    a, b = rgb(x), rgb(y)
    return sum(round(a[i] + (b[i] - a[i]) * t) << s for i, s in enumerate((16, 8, 0)))


def lum(c):
    def lin(v):
        v /= 255
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = rgb(c)
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def contrast(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def resolve(dark, tone, skin):
    """skin::Resolve: brand, the skin over it, then the accent family unless the skin sets it."""
    c = {k: v[0] for k, v in BRAND[dark].items()}
    over = {}
    if skin:
        over.update({k: int(v[1:7], 16) for k, v in skin.get("colors", {}).items() if not k.startswith(("_", "$"))})
        over.update({k: int(v[1:7], 16) for k, v in skin.get("dark" if dark else "light", {}).items()
                     if not k.startswith(("_", "$"))})
    c.update(over)

    def derived(name, value):
        if name not in over:
            c[name] = value
    derived("accent", ACCENTS[dark][tone])
    derived("accent.mid", mix(c["accent"], c["bg.base"], 0.25))
    derived("accent.dim", mix(c["bg.base"], c["accent"], 0.15 if dark else 0.12))
    derived("accent.faint", mix(c["bg.base"], c["accent"], 0.07))
    derived("on.accent", c["bg.base"])
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", action="store_true", help="print every pair, not only failures")
    ap.add_argument("--skin", help="only this skin (moonbow = no skin)")
    ap.add_argument("--pair", help="only pairs with this foreground token")
    args = ap.parse_args()
    skins = {"moonbow": None}
    for f in sorted((root / "res/skins").glob("*.json")):
        skins[f.stem] = json.loads(f.read_text(encoding="utf-8"))
    failures = 0
    for name, skin in skins.items():
        if args.skin and name != args.skin:
            continue
        modes = [True, False]
        if skin and skin.get("mode") in ("dark", "light"):
            modes = [skin["mode"] == "dark"]
        # A skin accent replaces the tone: one pass is enough.
        sets_accent = skin and any("accent" in skin.get(k, {}) for k in ("colors", "dark", "light"))
        for dark in modes:
            for tone in ([0] if sets_accent else range(len(ACCENTS[dark]))):
                c = resolve(dark, tone, skin)
                label = f"{name} {'dark' if dark else 'light'}" + ("" if sets_accent else f" {TONE_NAMES[tone]}")
                for fg, bgs, need in PAIRS:
                    if args.pair and fg != args.pair:
                        continue
                    for bg in bgs:
                        r = contrast(c[fg], c[bg])
                        bad = r < need
                        failures += bad
                        if bad or args.v:
                            print(f"{'FAIL' if bad else 'ok  '} {label}: {fg} #{c[fg]:06X} on {bg} #{c[bg]:06X} "
                                  f"{r:.2f}:1 (needs {need:g})")
    print(f"{failures} failing pair(s)")
    return failures


if __name__ == "__main__":
    sys.exit(min(main(), 255))
