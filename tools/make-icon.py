"""Renders the Lumen app icon (res/lumen.ico) with Pillow.

Same mark as design/icons/lumen.svg: a rounded play triangle with an arc of
light on its left, white on a near-black rounded square.
Usage: python -I tools/make-icon.py
"""
import math
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
S = 1024  # master size; downscaled with LANCZOS for each icon size


def lerp_points(points, k):
    return [(x * k, y * k) for x, y in points]


def rounded_triangle(draw, pts, radius, fill):
    # Polygon inset by the radius plus circles at the corners = rounded corners.
    cx = sum(p[0] for p in pts) / 3
    cy = sum(p[1] for p in pts) / 3
    inner = []
    for x, y in pts:
        dx, dy = cx - x, cy - y
        d = math.hypot(dx, dy)
        inner.append((x + dx / d * radius * 1.9, y + dy / d * radius * 1.9))
    draw.polygon(inner, fill=fill)
    for i in range(3):
        a, b = inner[i], inner[(i + 1) % 3]
        draw.line([a, b], fill=fill, width=int(radius * 2))
    for x, y in inner:
        draw.ellipse([x - radius, y - radius, x + radius, y + radius], fill=fill)


def master():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = S * 0.04
    d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=S * 0.22, fill=(16, 16, 16, 255),
                        outline=(48, 48, 48, 255), width=int(S * 0.012))
    k = S / 24.0
    # Arc of light (left).
    width = 1.5 * k
    box = [3.0 * k, 3.6 * k, 3.0 * k + 16.8 * k, 3.6 * k + 16.8 * k]
    d.arc(box, start=122, end=238, fill=(170, 170, 170, 255), width=int(width))
    # PIL strokes inward, so the centerline radius is the box radius minus half the width.
    rx = (box[2] - box[0]) / 2 - width / 2
    ry = (box[3] - box[1]) / 2 - width / 2
    for ang in (122, 238):
        r = width / 2
        cx = (box[0] + box[2]) / 2 + rx * math.cos(math.radians(ang))
        cy = (box[1] + box[3]) / 2 + ry * math.sin(math.radians(ang))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(170, 170, 170, 255))
    # Play triangle.
    tri = lerp_points([(9.4, 6.3), (9.4, 17.7), (19.1, 12.0)], k)
    rounded_triangle(d, tri, 1.0 * k, (237, 237, 237, 255))
    return img


def main():
    img = master()
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    out = ROOT / "res" / "lumen.ico"
    img.resize((256, 256), Image.LANCZOS).save(out, sizes=[(s, s) for s in sizes])
    img.resize((512, 512), Image.LANCZOS).save(ROOT / "design" / "lumen-logo.png")
    print("wrote", out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
