"""statictext.py <session dir> x0 y0 x1 y1: the region holds static text, so every in-between frame must match a
real frame there. Reports, per in-between frame, the pixels that differ from both neighbouring real frames."""
import os, re, sys
import numpy as np
from PIL import Image

sess = sys.argv[1]
box = tuple(int(v) for v in sys.argv[2:6])
rx = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
files = sorted(f for f in os.listdir(sess) if rx.match(f))
reals = {int(rx.match(f).group(2)): f for f in files if rx.match(f).group(3) == "r"}

def load(f):
    return np.asarray(Image.open(os.path.join(sess, f)).convert("RGB").crop(box)).astype(np.int16)

total, worst = 0, []
for f in files:
    m = rx.match(f)
    t = int(m.group(2))
    if m.group(3) != "i" or t - 1 not in reals or t not in reals:
        continue
    i, a, b = load(f), load(reals[t - 1]), load(reals[t])
    bad = (np.abs(i - a).max(axis=2) > 60) & (np.abs(i - b).max(axis=2) > 60)
    n = int(bad.sum())
    total += n
    if n:
        worst.append((n, f))
worst.sort(reverse=True)
print(f"stray pixels in all in-between frames: {total}; frames affected: {len(worst)}; worst: {worst[:4]}")
