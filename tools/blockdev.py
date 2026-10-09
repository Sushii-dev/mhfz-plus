"""blockdev.py <session dir> [n]: per in-between frame, how far blurred 16x16 blocks deviate from the linear blend of
the real frames around it (scene area only, HUD rows excluded). Large-area deviations = shimmer / flicker."""
import os, re, sys
import numpy as np
from PIL import Image, ImageFilter

d = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 10
pat = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
fs = sorted(f for f in os.listdir(d) if pat.match(f))
reals = {int(pat.match(f).group(2)): f for f in fs if pat.match(f).group(3) == "r"}
cache = {}

def L(f):
    if f not in cache:
        cache[f] = np.asarray(Image.open(os.path.join(d, f)).convert("L").filter(ImageFilter.GaussianBlur(4)).resize((430, 180))).astype(float)
    return cache[f]

out = []
for f in fs:
    m = pat.match(f)
    t = int(m.group(2))
    if m.group(3) != "i" or t - 1 not in reals or t not in reals:
        continue
    tt = int(m.group(4)) / 100
    e = (L(f) - (L(reals[t - 1]) * (1 - tt) + L(reals[t]) * tt))[32:160, 0:400]
    bl = e.reshape(8, 16, 25, 16).mean(axis=(1, 3))
    out.append((float(np.abs(bl).max()), int((np.abs(bl) > 4).sum()), float(e.mean()), f))
arr = np.array([o[0] for o in out])
print(f"{len(out)} in-between frames: median worst block {np.median(arr):.1f}, frames with a block > 8: {(arr > 8).sum()}")
for o in sorted(out, reverse=True)[:n]:
    print("  worst block %5.1f  blocks>4: %3d  mean %+5.2f  %s" % o)
