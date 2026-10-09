"""ghostview.py <session dir> <interp file> <out.png> [dilate]: real-before | interp with ghost pixels red | real-after."""
import os, re, sys
import numpy as np
from PIL import Image, ImageFilter, ImageDraw

sess, f, out = sys.argv[1], sys.argv[2], sys.argv[3]
DIL = int(sys.argv[4]) if len(sys.argv) > 4 else 21
tick = int(re.search(r"tick(\d+)", f).group(1))
files = os.listdir(sess)
before = next(x for x in files if f"tick{tick - 1:06d}_r" in x)
after = next(x for x in files if f"tick{tick:06d}_r" in x)

def load(n):
    return np.asarray(Image.open(os.path.join(sess, n)).convert("RGB")).astype(np.int16)

def leaf(a):
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    return (g > 60) & (g > r + 12) & (g > b + 12)

def dil(m):
    return np.asarray(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(DIL))) > 0

A, I, B = load(before), load(f), load(after)
ghost = leaf(I) & ~(dil(leaf(A)) | dil(leaf(B)))
mark = I.copy()
mark[ghost] = [255, 0, 0]
h, w = I.shape[:2]
m = Image.new("RGB", (w, h * 3 + 20))
for k, (arr, name) in enumerate([(A, before), (mark, f + " (red = ghost)"), (B, after)]):
    m.paste(Image.fromarray(arr.astype(np.uint8)), (0, k * (h + 10)))
    ImageDraw.Draw(m).text((6, k * (h + 10) + 6), name, fill=(255, 255, 0))
m = m.resize((w * 2 // 3, (h * 3 + 20) * 2 // 3))
m.save(out)
