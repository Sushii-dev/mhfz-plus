"""textghosts.py <session dir> [dilate] [min px per block]: white label text in in-between frames that is far from any
label text in the real frames around it, reported per 24x24 block (clusters = a letter in the wrong place)."""
import os, re, sys
import numpy as np
from PIL import Image, ImageFilter

sess = sys.argv[1]
DIL = int(sys.argv[2]) if len(sys.argv) > 2 else 9
MINPX = int(sys.argv[3]) if len(sys.argv) > 3 else 10
B = 24
rx = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")

def mask(f):
    a = np.asarray(Image.open(os.path.join(sess, f)).convert("RGB")).astype(np.int16)
    return (a.min(axis=2) > 185) & (a.max(axis=2) - a.min(axis=2) < 40)

def dil(m):
    return np.asarray(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(DIL))) > 0

files = sorted(f for f in os.listdir(sess) if rx.match(f))
reals = {int(rx.match(f).group(2)): f for f in files if rx.match(f).group(3) == "r"}
cache = {}
hits = []
for f in files:
    m = rx.match(f)
    tick = int(m.group(2))
    if m.group(3) != "i" or tick - 1 not in reals or tick not in reals:
        continue
    for k in (tick - 1, tick):
        if k not in cache:
            cache[k] = dil(mask(reals[k]))
    g = mask(f) & ~(cache[tick - 1] | cache[tick])
    h, w = g.shape
    bl = g[: h // B * B, : w // B * B].reshape(h // B, B, w // B, B).sum(axis=(1, 3))
    for by, bx in zip(*np.nonzero(bl >= MINPX)):
        hits.append((int(bl[by, bx]), f, bx * B, by * B))
hits.sort(reverse=True)
print(f"{len(hits)} blocks with >= {MINPX} stray text pixels")
for n, f, x, y in hits[:25]:
    print(f"  {n:4d} px  {f}  at x {x} y {y}")
