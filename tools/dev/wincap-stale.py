"""Frames of a wrong picture per switch in a wincap.ps1 capture.

Usage: python tools/dev/wincap-stale.py <dir> [threshold]
threshold: the mean change (0-255) that starts a switch, default 6 (3 for small ones).
For each switch: frames from its first change until the window matches its settled state 15
frames later; 0 = the first changed frame is already right. Something that keeps animating after
a switch (a fading control bar, a toast) counts too: look at the frames.
"""
import sys, glob
import numpy as np
from PIL import Image
fs = sorted(glob.glob(sys.argv[1] + "/f*.png"))
ims = [np.asarray(Image.open(f).convert("L").resize((320, 200)), dtype=np.float32) for f in fs]
out, i = [], 1
while i < len(ims) - 16:
    if np.abs(ims[i] - ims[i - 1]).mean() > (float(sys.argv[2]) if len(sys.argv) > 2 else 6):
        settled = ims[i + 15]
        k = i
        while k < i + 15 and np.abs(ims[k] - settled).mean() > 4:
            k += 1
        out.append(k - i)
        i += 20
    else:
        i += 1
print("stale frames per switch:", out)
