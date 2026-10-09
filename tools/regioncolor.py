"""regioncolor.py <session dir> x0 y0 x1 y1: mean RGB of a region per frame; prints the spread of real frames and of
in-between frames (a static region must look the same in all of them)."""
import os, re, sys
import numpy as np
from PIL import Image

d = sys.argv[1]
box = tuple(int(v) for v in sys.argv[2:6])
pat = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
rs, iis = [], []
for f in sorted(os.listdir(d)):
    m = pat.match(f)
    if not m:
        continue
    c = np.asarray(Image.open(os.path.join(d, f)).convert("RGB").crop(box)).reshape(-1, 3).mean(axis=0)
    (rs if m.group(3) == "r" else iis).append(c)
rs, iis = np.array(rs), np.array(iis)
print("real   mean RGB", np.round(rs.mean(axis=0), 1), " max deviation", np.round(np.abs(rs - rs.mean(axis=0)).max(), 1))
print("interp mean RGB", np.round(iis.mean(axis=0), 1), " max deviation from real mean", np.round(np.abs(iis - rs.mean(axis=0)).max(), 1))
