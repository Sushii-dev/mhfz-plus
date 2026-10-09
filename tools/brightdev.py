"""Per in-between frame of a retro save: a region's mean brightness against the blend of the real frames around it (by
the frame's t). Lists the frames that deviate most - a lighting flash shows as one frame off the line.

    python tools/brightdev.py <retro\\NNN> [x0 y0 x1 y1] [top]
"""
import os
import re
import sys

from PIL import Image, ImageStat

d = sys.argv[1]
box = tuple(int(v) for v in sys.argv[2:6]) if len(sys.argv) >= 6 else None
top = int(sys.argv[6]) if len(sys.argv) > 6 else 8
rx = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
files = sorted(f for f in os.listdir(d) if rx.match(f))
vals = []
for f in files:
    m = rx.match(f)
    im = Image.open(os.path.join(d, f)).convert("L")
    if box is None:
        w, h = im.size
        box = (w * 2 // 5, h // 3, w * 3 // 5, h * 5 // 6)
    vals.append((f, m.group(3), int(m.group(4)) / 100.0, ImageStat.Stat(im.crop(box)).mean[0]))
devs = []
for k, (f, kind, t, v) in enumerate(vals):
    if kind != "i":
        continue
    a = next((j for j in range(k - 1, -1, -1) if vals[j][1] == "r"), None)
    b = next((j for j in range(k + 1, len(vals)) if vals[j][1] == "r"), None)
    if a is None or b is None:
        continue
    exp = vals[a][3] + (vals[b][3] - vals[a][3]) * t
    devs.append((v - exp, f, vals[a][3], vals[b][3], v))
devs.sort(key=lambda x: -abs(x[0]))
print(f"{os.path.basename(d)} region {box}: {len(devs)} in-between frames, mean |dev| {sum(abs(x[0]) for x in devs) / max(len(devs), 1):.2f}")
for dv, f, ra, rb, v in devs[:top]:
    print(f"  {f}: {v:6.1f} vs blend of reals {ra:6.1f} -> {rb:6.1f}: {dv:+.1f}")
