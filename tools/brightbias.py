"""brightbias.py <session dir> <out.png>: per pixel, how much brighter the in-between frames are than the blend of
the real frames around them, averaged over the session (motion averages out to ~0; a replay that renders something
brighter or darker shows up as a consistent red/blue area). Also prints the per-tick bias inside the worst area."""
import os, re, sys
import numpy as np
from PIL import Image, ImageFilter

sess, out = sys.argv[1], sys.argv[2]
rx = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
files = sorted(f for f in os.listdir(sess) if rx.match(f))
reals = {int(rx.match(f).group(2)): f for f in files if rx.match(f).group(3) == "r"}
W, H = 430, 180

def L(f):
    return np.asarray(Image.open(os.path.join(sess, f)).convert("L").resize((W, H), Image.BILINEAR)).astype(float)

acc = np.zeros((H, W))
n = 0
per = []
cache = {}
for f in files:
    m = rx.match(f)
    t = int(m.group(2))
    if m.group(3) != "i" or t - 1 not in reals or t not in reals:
        continue
    for k in (t - 1, t):
        if k not in cache:
            cache[k] = L(reals[k])
    d = L(f) - (cache[t - 1] + cache[t]) / 2
    acc += d
    n += 1
    per.append((f, d))
acc /= max(n, 1)
img = np.zeros((H, W, 3), np.uint8)
img[..., 0] = np.clip(acc * 12, 0, 255)
img[..., 2] = np.clip(-acc * 12, 0, 255)
Image.fromarray(img).resize((W * 2, H * 2)).save(out)
ys, xs = np.unravel_index(np.argmax(np.abs(acc)), acc.shape)
y0, y1, x0, x1 = max(0, ys - 8), ys + 8, max(0, xs - 8), xs + 8
print(f"{n} in-between frames; strongest bias {acc[ys, xs]:+.1f} at x {xs * 4} y {ys * 4} (half-res frame coords)")
for f, d in per[:: max(1, len(per) // 24)]:
    print(f"  {f}  bias in that area {d[y0:y1, x0:x1].mean():+.1f}")
