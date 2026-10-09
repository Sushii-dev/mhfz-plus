"""In-between frames of a retro save against the blend of the real frames around them (static scenes: menus): mean and
peak difference per frame, and an amplified difference image of the in-between frames after one real frame.

    python tools/realdiff.py <retro\\NNN> [which-real-from-end=3] [out.png]
"""
import glob
import os
import sys

from PIL import Image, ImageChops, ImageStat

d = sys.argv[1]
back = int(sys.argv[2]) if len(sys.argv) > 2 else 3
out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(d, "..", os.path.basename(d) + "_diff.png")
fs = sorted(f for f in glob.glob(os.path.join(d, "*.jpg")) if not os.path.basename(f).startswith("flicker_"))
names = [os.path.basename(f) for f in fs]
ri = [i for i, n in enumerate(names) if "_r_" in n]
a, b = ri[-back], ri[-back + 1]
R0, R1 = Image.open(fs[a]).convert("RGB"), Image.open(fs[b]).convert("RGB")
print("real", names[a], "-> real", names[b], ": mean diff", round(ImageStat.Stat(ImageChops.difference(R0, R1).convert("L")).mean[0], 2))
rows = []
for i in range(a + 1, b):
    im = Image.open(fs[i]).convert("RGB")
    t = int(names[i][:-4].split("_")[-1]) / 100.0
    dd = ImageChops.difference(im, Image.blend(R0, R1, t)).convert("L")
    print(" ", names[i], "vs the blend: mean", round(ImageStat.Stat(dd).mean[0], 2), "peak", dd.getextrema()[1])
    rows.append(dd.point(lambda v: min(255, v * 6)))
w, h = rows[0].size
o = Image.new("L", (w, h * len(rows)))
for k, r in enumerate(rows):
    o.paste(r, (0, h * k))
o.resize((w // 2, h * len(rows) // 2)).save(out)
print("->", out)
