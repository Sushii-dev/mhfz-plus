"""brightcount.py <session dir> [x0 y0 x1 y1]: per frame, count of near-white pixels (all channels > 200) inside the
box (default: whole frame); prints real vs in-between averages and the per-tick series."""
import os, re, sys
import numpy as np
from PIL import Image

d = sys.argv[1]
box = tuple(int(v) for v in sys.argv[2:6]) if len(sys.argv) >= 6 else None
pat = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
fs = sorted(f for f in os.listdir(d) if pat.match(f))
real, inter, rows = [], [], []
for f in fs:
    im = Image.open(os.path.join(d, f)).convert("RGB")
    if box:
        im = im.crop(box)
    a = np.asarray(im).astype(int)
    v = int((a.min(axis=2) > 200).sum())
    m = pat.match(f)
    (real if m.group(3) == "r" else inter).append(v)
    rows.append((m.group(2), m.group(3), m.group(4), v))
print(f"real frames {len(real)}: mean {np.mean(real):.0f} | in-between {len(inter)}: mean {np.mean(inter) if inter else 0:.0f}")
for r in rows[: int(sys.argv[6]) if len(sys.argv) > 6 else 0]:
    print("  ", *r)
