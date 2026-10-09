"""hotpix.py <session dir> [n] [out.png]: pixels of an in-between frame clearly brighter (or darker) than in BOTH real
frames around it, at a 5 px tolerance for motion (each real frame is max/min-filtered first). Small moving
features stay inside that envelope; a character drawn brighter or darker than it really is does not."""
import os, re, sys
import numpy as np
from PIL import Image, ImageFilter, ImageDraw

d = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 10
pat = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
fs = sorted(f for f in os.listdir(d) if pat.match(f))
reals = {int(pat.match(f).group(2)): f for f in fs if pat.match(f).group(3) == "r"}
env = {}

def envelope(f):
    if f not in env:
        g = Image.open(os.path.join(d, f)).convert("L")
        env[f] = (np.asarray(g.filter(ImageFilter.MaxFilter(11))).astype(int), np.asarray(g.filter(ImageFilter.MinFilter(11))).astype(int))
    return env[f]

res = []
for f in fs:
    m = pat.match(f)
    t = int(m.group(2))
    if m.group(3) != "i" or t - 1 not in reals or t not in reals:
        continue
    (amax, amin), (bmax, bmin) = envelope(reals[t - 1]), envelope(reals[t])
    i = np.asarray(Image.open(os.path.join(d, f)).convert("L")).astype(int)
    hi = i - np.maximum(amax, bmax)
    lo = np.minimum(amin, bmin) - i
    mask = (hi > 20) | (lo > 20)
    mask[:70, :] = False          # HUD
    mask[560:, 1500:] = False     # party list
    res.append((int(mask.sum()), int((hi > 20).sum()), int((lo > 20).sum()), f, mask))
res.sort(key=lambda r: -r[0])
print(f"{len(res)} in-between frames; with >150 such pixels: {sum(1 for r in res if r[0] > 150)}")
for r in res[:n]:
    ys, xs = np.nonzero(r[4])
    where = f"x {xs.min()}-{xs.max()} y {ys.min()}-{ys.max()}" if len(xs) else ""
    print(f"  {r[0]:6d} px (brighter {r[1]}, darker {r[2]})  {r[3]}  {where}")
if len(sys.argv) > 3 and res:
    r = res[0]
    img = np.asarray(Image.open(os.path.join(d, r[3])).convert("RGB")).copy()
    img[r[4]] = [255, 0, 0]
    Image.fromarray(img).resize((860, 360)).save(sys.argv[3])
