"""Checks a frame series from tools/dev/frames.ps1 for flashes, pops and half-loaded states.

Usage: python tools/dev/frames-check.py <frames dir> [--window 2500] [--sheet sheet.png]

Only frames a user could see count: from the first "revealed" frame on (builds without the
reveal mark show their window at once, so from the first frame). Pixels that still change at the
end of the series (a playing video, its clock) are intended motion and left out. What is left
should not change after the window appears, except:
  - the window's content fading in once from its plain background: the first change, from a
    frame that shows nothing but the background, a single rise of opacity (every frame on the
    way from that frame to the final one, never back or sideways) over at most FADE_MS;
  - after an "OPEN": the switch to the opened file, in one step (one event of at most 2 frames);
  - one toast (a small change centred horizontally) per window or open.
Everything else is reported (a pop = one-frame change, a fade = several) and fails the verdict,
as does a window that changes size after it appeared. --sheet writes a contact sheet of the seen
frames (for looking at them, which numbers don't replace).
"""
import argparse
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

W = 192  # analysis width in pixels
STEP_DIFF = 0.6  # mean abs difference (0-255) over the compared pixels that counts as a change
PIXEL_DIFF = 24  # a pixel changed when it moved by more than this
PIXEL_SHARE = 0.002  # ... and when at least this share of the compared pixels changed
FADE_MS = 220  # the content fade-in (140 ms) from its first to its last changed frame, plus capture jitter
FADE_RESIDUAL = 3.0  # mean distance (0-255) of a fade frame from the straight blend: more = something else changed


def load(path, size):
    img = Image.open(path).convert("L")
    return np.asarray(img.resize(size, Image.BILINEAR), dtype=np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--window", type=int, default=2500, help="ms after the reveal / an open to check")
    ap.add_argument("--sheet", default="")
    a = ap.parse_args()

    frames, opens = [], []
    for line in open(os.path.join(a.dir, "frames.txt"), encoding="utf-8-sig"):
        m = re.match(r"\s*(\d+) f(\d+) (\d+)x(\d+) at (-?\d+),(-?\d+)(.*)", line)
        if m:
            t, i, w, h = (int(m.group(k)) for k in (1, 2, 3, 4))
            v = re.search(r"video=(-?\d+),(-?\d+),(\d+),(\d+)", m.group(7))
            frames.append({"t": t, "i": i, "w": w, "h": h, "revealed": "revealed" in m.group(7),
                           "video": tuple(int(x) for x in v.groups()) if v else None})
        elif "OPEN" in line:
            opens.append(int(line.split()[0]))
    if not frames:
        print("no frames")
        return 2
    files = {int(f[1:4]): f for f in os.listdir(a.dir) if re.match(r"f\d{3}_\d+\.png$", f)}
    marked = any(f["revealed"] for f in frames)
    seen = [f for f in frames if f["revealed"]] if marked else frames
    reveal_t = seen[0]["t"]
    print(f"{len(frames)} frames, first at {frames[0]['t']} ms; seen from {reveal_t} ms"
          f" ({'reveal mark' if marked else 'no reveal mark: window shown at once'})")

    failures = []
    for f in seen[1:]:
        if (f["w"], f["h"]) != (seen[0]["w"], seen[0]["h"]):
            failures.append(f"{f['t']} ms: window resized to {f['w']}x{f['h']} after it appeared")
            break

    size = (W, max(1, round(W * seen[0]["h"] / seen[0]["w"])))
    imgs = [load(os.path.join(a.dir, files[f["i"]]), size) for f in seen]
    # The video window: its picture moves by itself. Flat while shown = no frame yet (an empty
    # black or grey player), which is what a user must never see.
    def video_box(f):
        if not f["video"]:
            return None
        x, y, w, h = f["video"]
        k = size[0] / f["w"]
        return (max(0, int(x * k)), max(0, int(y * k)), min(size[0], int((x + w) * k + 1)), min(size[1], int((y + h) * k + 1)))
    def plain(im):  # nothing but the window background (before its content shows)
        return float((np.abs(im - np.median(im)) > PIXEL_DIFF).mean()) < 0.01

    # Intended ongoing motion: pixels that change over the last 2 s of the series (a clock too).
    end_t = seen[-1]["t"]
    tail = [im for f, im in zip(seen, imgs) if f["t"] >= end_t - 2000]
    moving = np.zeros(imgs[0].shape, dtype=bool)
    for x, y in zip(tail, tail[1:]):
        moving |= np.abs(x - y) > PIXEL_DIFF / 2
    if moving.any():  # grow by a pixel: edges of moving content shimmer
        m = moving.copy()
        m[1:, :] |= moving[:-1, :]
        m[:-1, :] |= moving[1:, :]
        m[:, 1:] |= moving[:, :-1]
        m[:, :-1] |= moving[:, 1:]
        moving = m
    keep = ~moving
    print(f"excluded as ongoing motion: {moving.mean() * 100:.1f}% of the window")

    # Change events: runs of consecutive changing steps.
    events, cur = [], None
    for k in range(1, len(seen)):
        d = np.abs(imgs[k] - imgs[k - 1])
        here = keep.copy()
        for b in (video_box(seen[k]), video_box(seen[k - 1])):
            if b:
                here[b[1]:b[3], b[0]:b[2]] = False
        dk = d[here]
        mean = float(dk.mean()) if dk.size else 0.0
        share = float((dk > PIXEL_DIFF).mean()) if dk.size else 0.0
        changed = mean > STEP_DIFF or share > PIXEL_SHARE
        if changed:
            ys, xs = np.nonzero((d > PIXEL_DIFF) & here)
            box = (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1) if len(xs) else (0, 0, 0, 0)
            if cur is None:
                cur = {"t0": seen[k - 1]["t"], "t1": seen[k]["t"], "steps": 0, "peak": 0.0, "box": box,
                       "k0": k - 1}
                events.append(cur)
            cur["t1"] = seen[k]["t"]
            cur["steps"] += 1
            cur["peak"] = max(cur["peak"], mean)
            b = cur["box"]
            cur["box"] = (min(b[0], box[0]), min(b[1], box[1]), max(b[2], box[2]), max(b[3], box[3]))
        else:
            cur = None

    def toast_like(e):
        x0, y0, x1, y1 = e["box"]
        area = (x1 - x0) * (y1 - y0) / (size[0] * size[1])
        centred = abs((x0 + x1) / 2 - size[0] / 2) < size[0] * 0.08
        return area < 0.08 and centred

    def fade_in(e):
        """None when the event is the content's one fade-in from the plain background, else why not."""
        k0, k1 = e["k0"], e["k0"] + e["steps"]
        # A fade that began with the reveal may be under way in the first captured frame.
        if k0 > 0 and not plain(imgs[k0]):
            return "it does not start from the plain background"
        took = seen[k1]["t"] - seen[k0 + 1]["t"]
        if took > FADE_MS:
            return f"it takes {took} ms"
        mask = keep.copy()
        for f in seen[k0:k1 + 1]:
            if (b := video_box(f)):
                mask[b[1]:b[3], b[0]:b[2]] = False
        s, d = imgs[k0][mask], imgs[k1][mask] - imgs[k0][mask]
        den = float((d * d).sum()) or 1.0
        prev = 0.0
        for k in range(k0 + 1, k1 + 1):
            x = imgs[k][mask] - s
            alpha = float((x * d).sum()) / den
            if alpha < prev - 0.05:
                return f"its opacity goes back at {seen[k]['t']} ms"
            if float(np.abs(x - alpha * d).mean()) > FADE_RESIDUAL:
                return f"{seen[k]['t']} ms is not a blend of the background and the final frame"
            prev = alpha
        return None

    # Windows to check: after the reveal, and after every open.
    starts = [("reveal", reveal_t)] + [("open", t) for t in opens if t >= reveal_t]
    for e in events:
        e["kind"] = "pop" if e["steps"] == 1 else f"fade/{e['steps']} steps"
    faded = set()  # frames of the content fade-in: on their way in, the video may still be faint
    for idx, (what, t0) in enumerate(starts):
        t_end = min([t0 + a.window] + [s[1] for s in starts[idx + 1:]])
        # By the frame that shows the change: the one before it may predate an open.
        mine = [e for e in events if t0 <= e["t1"] < t_end]
        toasts = 0
        for j, e in enumerate(mine):
            label = (f"{what}+{e['t0'] - t0} ms: {e['kind']}, peak {e['peak']:.1f}, "
                     f"box {e['box'][0]},{e['box'][1]}-{e['box'][2]},{e['box'][3]} of {size[0]}x{size[1]}")
            if what == "open" and j == 0 and e["steps"] <= 2:
                print(f"  ok   {label} (the switch to the opened file)")
                continue
            if what == "reveal" and j == 0:
                why = fade_in(e)
                if why is None:
                    faded.update(range(e["k0"], e["k0"] + e["steps"] + 1))
                    print(f"  ok   {label} (the content fading in)")
                    continue
                label += f" (not the content fade-in: {why})"
            if toast_like(e) and toasts == 0:
                toasts += 1
                print(f"  ok   {label} (toast)")
                continue
            print(f"  FAIL {label}" + (" (a second toast)" if toast_like(e) else ""))
            failures.append(label)
        if what == "open" and not mine:
            print(f"  note open at {t0} ms: nothing changed within {a.window} ms")
    for k, (f, im) in enumerate(zip(seen, imgs)):
        b = video_box(f)
        if k in faded or plain(im):  # the plain player background before the content comes
            continue
        if b and b[2] - b[0] > 8 and b[3] - b[1] > 8 and float(im[b[1]:b[3], b[0]:b[2]].std()) < 4:
            failures.append(f"{f['t']} ms: the video area is shown empty (no frame yet)")
            print(f"  FAIL {failures[-1]}")

    if a.sheet:
        # The first 1.2 s after the reveal and after each open: where flashes happen.
        shown = [f for f in seen if any(t0 - 60 <= f["t"] <= t0 + 1200 for _, t0 in starts)]
        cols, tw = 6, 320
        th = round(tw * size[1] / size[0])
        rows = (len(shown) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * tw, rows * (th + 14)), (40, 40, 40))
        draw = ImageDraw.Draw(sheet)
        for n, f in enumerate(shown):
            im = Image.open(os.path.join(a.dir, files[f["i"]])).convert("RGB").resize((tw, th), Image.BILINEAR)
            x, y = (n % cols) * tw, (n // cols) * (th + 14)
            sheet.paste(im, (x, y + 14))
            draw.text((x + 2, y + 1), f"{f['t']} ms", fill=(255, 255, 0))
        sheet.save(a.sheet)
        print(f"sheet: {a.sheet}")

    print("VERDICT: " + ("OK, no flash after the window appeared" if not failures else f"FAIL ({len(failures)})"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
