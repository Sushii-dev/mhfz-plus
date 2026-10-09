"""Scan captured frames for the debug-view magenta (draws that are not interpolated)."""
import glob, os, sys
import numpy as np
from PIL import Image

d = sys.argv[1] if len(sys.argv) > 1 else r"C:\Games\MezeLounge\captures"
files = sorted(glob.glob(os.path.join(d, "*.png")))
print(f"{len(files)} frames")
for f in files:
    a = np.asarray(Image.open(f).convert("RGB")).astype(np.int16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    m = (r > 180) & (b > 180) & (g < 90)
    n = int(m.sum())
    if n == 0:
        print(f"{os.path.basename(f)}  -")
        continue
    ys, xs = np.nonzero(m)
    print(f"{os.path.basename(f)}  magenta {n:8d} px ({100.0 * n / m.size:5.2f}%)  bbox x {xs.min()}-{xs.max()} y {ys.min()}-{ys.max()}")
