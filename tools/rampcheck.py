"""rampcheck.py <session dir> x0 y0 x1 y1: for a region whose brightness ramps over ticks, how far each in-between
frame is from the linear blend of the real frames around it (0 = smoothly interpolated)."""
import os, re, sys
import numpy as np
from PIL import Image

d = sys.argv[1]
box = tuple(int(v) for v in sys.argv[2:6])
pat = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
fs = sorted(f for f in os.listdir(d) if pat.match(f))
val = {f: np.asarray(Image.open(os.path.join(d, f)).convert("L").crop(box)).mean() for f in fs}
reals = {int(pat.match(f).group(2)): val[f] for f in fs if pat.match(f).group(3) == "r"}
dev = []
for f in fs:
    m = pat.match(f)
    t = int(m.group(2))
    if m.group(3) != "i" or t - 1 not in reals or t not in reals:
        continue
    a, b, tt = reals[t - 1], reals[t], int(m.group(4)) / 100
    if b < a:  # sawtooth reset: no smooth expectation
        continue
    dev.append(abs(val[f] - (a + (b - a) * tt)))
print(f"{len(dev)} in-between frames: mean deviation {np.mean(dev):.2f}, max {np.max(dev):.2f} (levels)")
