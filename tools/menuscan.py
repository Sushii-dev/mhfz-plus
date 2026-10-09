"""Every tick of a retro save: how far each in-between frame is from the blend of the real frames around it - pixels
off by more than 30 levels, and where (bounding box). In a still scene (a menu) the blend is the truth, so anything
off is something the in-between frames drew wrong.

    python tools/menuscan.py <retro\\NNN> [-top 12]
"""
import glob
import os
import sys

from PIL import Image, ImageChops

d = sys.argv[1]
top = int(sys.argv[sys.argv.index("-top") + 1]) if "-top" in sys.argv else 12
fs = sorted(f for f in glob.glob(os.path.join(d, "*.jpg")) if not os.path.basename(f).startswith("flicker_"))
names = [os.path.basename(f)[:-4] for f in fs]
ri = [i for i, n in enumerate(names) if "_r_" in n]
res = []
cache = {}


def img(i):
    if i not in cache:
        cache[i] = Image.open(fs[i]).convert("RGB")
    return cache[i]


for a, b in zip(ri, ri[1:]):
    R0, R1 = img(a), img(b)
    still = ImageChops.difference(R0, R1).convert("L").point(lambda v: 255 if v > 30 else 0)
    stillN = sum(1 for v in still.getdata() if v)
    for i in range(a + 1, b):
        t = int(names[i].split("_")[-1]) / 100.0
        dd = ImageChops.difference(img(i), Image.blend(R0, R1, t)).convert("L").point(lambda v: 255 if v > 30 else 0)
        n = sum(1 for v in dd.getdata() if v)
        res.append((n, names[i], dd.getbbox(), stillN))
    for k in list(cache):
        if k < a:
            del cache[k]
res.sort(reverse=True)
print(f"{len(res)} in-between frames; pixels off by > 30 levels from the blend of their real frames (and how much the real frames themselves changed):")
for n, nm, box, sn in res[:top]:
    print(f"  {nm}: {n} px, box {box} (real frames changed {sn} px)")
