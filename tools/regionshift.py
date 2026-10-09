"""How far regions of a retro save's frames moved, frame by frame: each region of each frame against the same region of
the frame before (phase correlation, whole pixels), so a region that moves unevenly through a tick - or back and forth -
shows in the list.

    python tools/regionshift.py <retro\\NNN> <first frame> <count> <name:x0,y0,x1,y1> [...]
"""
import os
import re
import sys

import numpy as np
from PIL import Image

d, first, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
regions = []
for a in sys.argv[4:]:
    n, b = a.split(":")
    regions.append((n, tuple(int(v) for v in b.split(","))))
rx = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
files = sorted(f for f in os.listdir(d) if rx.match(f))[first:first + count]


def shift(a, b):
    fa, fb = np.fft.fft2(a - a.mean()), np.fft.fft2(b - b.mean())
    r = fa * np.conj(fb)
    r /= np.abs(r) + 1e-9
    c = np.fft.ifft2(r).real
    y, x = np.unravel_index(np.argmax(c), c.shape)
    h, w = c.shape
    return (x if x <= w // 2 else x - w), (y if y <= h // 2 else y - h)


prev = None
print("frame".ljust(26) + "".join(n.rjust(14) for n, _ in regions))
for f in files:
    im = np.asarray(Image.open(os.path.join(d, f)).convert("L"), dtype=np.float32)
    row = f[5:-4].ljust(26)
    if prev is not None:
        for n, (x0, y0, x1, y1) in regions:
            dx, dy = shift(im[y0:y1, x0:x1], prev[y0:y1, x0:x1])
            row += f"{dx:+5d},{dy:+4d}".rjust(14)
    print(row)
    prev = im
