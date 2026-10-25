"""Renders the Moonbow app icon (res/moonbow.ico) with Pillow.

Same art as the icon on design/brand-exploration-2026-10-25/moonbow.html: a night tile with a
few stars and a white three-band rainbow that fades into mist at the horizon, each band touched
by one colour of the moon spectrum. 40 px and smaller get the simple version: the tile and two
flat white bands, since glow and stars turn to mush there.
Usage: python -I tools/make-icon.py
"""
import io
import pathlib
import struct

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
S = 1024  # master size; 4x the 256 design grid
K = S / 256


def vertical(stops, alpha=None):
    """S x S image with a top-to-bottom gradient through (position, (r, g, b, a)) stops."""
    img = Image.new("RGBA", (S, S))
    px = img.load()
    for y in range(S):
        t = y / (S - 1)
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                k = (t - p0) / (p1 - p0) if p1 > p0 else 0
                c = tuple(round(a + (b - a) * k) for a, b in zip(c0, c1))
                break
        else:
            c = stops[-1][1] if t > stops[-1][0] else stops[0][1]
        for x in range(S):
            px[x, y] = c
    return img


def tile_mask():
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).rounded_rectangle([8 * K, 8 * K, 248 * K, 248 * K], radius=56 * K, fill=255)
    return m


def tile():
    sky = vertical([(0, (30, 38, 56, 255)), (0.55, (14, 18, 27, 255)), (1, (7, 8, 11, 255))])
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(sky, (0, 0), tile_mask())
    return out


def bands(draw, cy, radii, width, colors):
    # PIL draws the stroke inward from the box, so the box radius is the centerline radius + width/2.
    for r, c in zip(radii, colors):
        R = (r + width / 2) * K
        draw.arc([128 * K - R, cy * K - R, 128 * K + R, cy * K + R], 180, 360, fill=c, width=round(width * K))


def large():
    img = tile()
    mask = tile_mask()
    top = vertical([(0, (255, 255, 255, 26)), (0.35, (255, 255, 255, 0)), (1, (255, 255, 255, 0))])
    img.alpha_composite(Image.composite(top, Image.new("RGBA", (S, S), (0, 0, 0, 0)), mask))

    d = ImageDraw.Draw(img)
    for x, y, r, a in [(64, 58, 1.6, .7), (184, 46, 1.2, .55), (206, 92, 1.6, .65), (96, 88, 1, .5), (150, 70, 1, .45), (44, 120, 1.2, .5)]:
        d.ellipse([(x - r) * K, (y - r) * K, (x + r) * K, (y + r) * K], fill=(255, 255, 255, round(255 * a)))

    radii, width, cy = (84, 60, 36), 15, 182
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bands(ImageDraw.Draw(glow), cy, radii, width, [(246, 203, 187, 191), (232, 238, 220, 191), (198, 220, 244, 191)])
    glow = glow.filter(ImageFilter.GaussianBlur(7 * K))
    bow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bow.alpha_composite(glow)
    bands(ImageDraw.Draw(bow), cy, radii, width, [(251, 231, 223, 255), (246, 247, 238, 255), (227, 236, 251, 255)])
    # The bow fades into the mist: full until y 144, gone at 184 (design grid).
    fade = vertical([(0, (0, 0, 0, 255)), (144 / 256, (0, 0, 0, 255)), (184 / 256, (0, 0, 0, 0)), (1, (0, 0, 0, 0))])
    bow.putalpha(Image.composite(bow.getchannel("A"), Image.new("L", (S, S), 0), fade.getchannel("A")))
    img.alpha_composite(Image.composite(bow, Image.new("RGBA", (S, S), (0, 0, 0, 0)), mask))

    mist = vertical([(0, (201, 214, 240, 0)), (160 / 256, (201, 214, 240, 0)), (182 / 256, (201, 214, 240, 26)),
                     (204 / 256, (201, 214, 240, 0)), (1, (201, 214, 240, 0))])
    img.alpha_composite(Image.composite(mist, Image.new("RGBA", (S, S), (0, 0, 0, 0)), mask))
    ImageDraw.Draw(img).rounded_rectangle([8.75 * K, 8.75 * K, 247.25 * K, 247.25 * K], radius=55.25 * K,
                                          outline=(255, 255, 255, 26), width=round(1.5 * K))
    return img


def small():
    img = tile()
    bands(ImageDraw.Draw(img), 186, (90, 50), 24, [(238, 241, 246, 255)] * 2)
    return img


def write_ico(path, images):
    """ICO with a PNG entry per size (Windows reads PNG entries since Vista)."""
    blobs = []
    for im in images:
        buf = io.BytesIO()
        im.save(buf, "PNG")
        blobs.append((im.width, buf.getvalue()))
    head = struct.pack("<HHH", 0, 1, len(blobs))
    offset = len(head) + 16 * len(blobs)
    entries, data = b"", b""
    for w, blob in blobs:
        side = 0 if w >= 256 else w
        entries += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(blob), offset + len(data))
        data += blob
    path.write_bytes(head + entries + data)


def main():
    big, simple = large(), small()
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    images = [(simple if s <= 40 else big).resize((s, s), Image.LANCZOS) for s in sizes]
    out = ROOT / "res" / "moonbow.ico"
    write_ico(out, images)
    big.resize((512, 512), Image.LANCZOS).save(ROOT / "design" / "moonbow-logo.png")
    print("wrote", out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
