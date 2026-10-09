"""overshoot.py <session dir> [n] [out.png]: flicker detector. In a blurred in-between frame, areas brighter than both
real frames around it (or darker than both) are flicker; motion, however fast, stays between the two. Reports the
frames with the most overshoot and optionally renders the worst one (red = brighter than both, blue = darker)."""
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
        im = Image.open(os.path.join(d, f)).convert("L").resize((430, 180), Image.BILINEAR).filter(ImageFilter.GaussianBlur(2))
        cache[f] = np.asarray(im).astype(float)
    return cache[f]

out = []
for f in fs:
    m = pat.match(f)
    t = int(m.group(2))
    if m.group(3) != "i" or t - 1 not in reals or t not in reals:
        continue
    a, b, i = L(reals[t - 1]), L(reals[t]), L(f)
    over = np.maximum(i - np.maximum(a, b), 0) - np.maximum(np.minimum(a, b) - i, 0)
    over[:30, :] = 0          # HUD rows
    over[150:, 380:] = 0      # party list
    big = np.abs(over) > 6
    out.append((int(big.sum()), float(over[big].mean()) if big.any() else 0.0, f, over))
out.sort(key=lambda o: -o[0])
print(f"{len(out)} in-between frames; with >200 overshooting pixels: {sum(1 for o in out if o[0] > 200)}")
for o in out[:n]:
    ys, xs = np.nonzero(np.abs(o[3]) > 6)
    where = f"x {xs.min() * 4}-{xs.max() * 4} y {ys.min() * 4}-{ys.max() * 4}" if len(xs) else ""
    print(f"  {o[0]:5d} px  mean {o[1]:+6.1f}  {o[2]}  {where}")
if len(sys.argv) > 3 and out:
    o = out[0]
    base = np.asarray(Image.open(os.path.join(d, o[2])).convert("L").resize((430, 180))).astype(float) * 0.5
    img = np.stack([base, base, base], axis=2)
    img[..., 0] += np.clip(o[3] * 8, 0, 255)
    img[..., 2] += np.clip(-o[3] * 8, 0, 255)
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).resize((860, 360)).save(sys.argv[3])
