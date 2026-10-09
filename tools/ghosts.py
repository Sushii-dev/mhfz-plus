"""Find "ghost" foliage in rolling-recorder frames: leaf-green pixels in an in-between frame that are far from any
leaf-green pixel of the real frames before and after it."""
import glob, os, re, sys
import numpy as np
from PIL import Image, ImageFilter

root = sys.argv[1]
DIL = int(sys.argv[2]) if len(sys.argv) > 2 else 21  # allowed motion in pixels (half resolution)
rx = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")

MODE = sys.argv[3] if len(sys.argv) > 3 else "leaf"

def leafmask(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    if MODE == "text":  # white label text: bright and unsaturated
        return (a.min(axis=2) > 185) & (a.max(axis=2) - a.min(axis=2) < 40)
    return (g > 60) & (g > r + 12) & (g > b + 12)

def dilate(m, k):
    im = Image.fromarray((m * 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.MaxFilter(k))) > 0

for sess in sorted(glob.glob(os.path.join(root, "*"))):
    if not os.path.isdir(sess):
        continue
    files = []
    for f in sorted(os.listdir(sess)):
        m = rx.match(f)
        if m:
            files.append((int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4)), f))
    reals = {tick: f for _, tick, kind, _, f in files if kind == "r"}
    cache = {}
    worst = []
    for idx, tick, kind, t, f in files:
        if kind != "i" or (tick - 1) not in reals or tick not in reals:
            continue
        for key in (tick - 1, tick):
            if key not in cache:
                cache[key] = dilate(leafmask(os.path.join(sess, reals[key])), DIL)
        allowed = cache[tick - 1] | cache[tick]
        m = leafmask(os.path.join(sess, f))
        ghost = m & ~allowed
        n = int(ghost.sum())
        if n:
            ys, xs = np.nonzero(ghost)
            worst.append((n, f, xs.min(), xs.max(), ys.min(), ys.max()))
    worst.sort(reverse=True)
    print(f"== {os.path.basename(sess)}: {len(files)} frames, in-between frames with ghost pixels: {len(worst)}")
    for w in worst[:6]:
        print(f"   {w[0]:6d} px  {w[1]}  x {w[2]}-{w[3]} y {w[4]}-{w[5]}")

