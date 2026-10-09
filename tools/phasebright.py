"""Mean brightness of a screen region per frame of a retro save, grouped by the frame's place in its tick (the
in-between frames' t, the real frame): a lighting error tied to one in-between frame shows as one phase brighter
or darker than the others. Region defaults to the player character (the screen's lower middle).

    python tools/phasebright.py <retro\\NNN> [x0 y0 x1 y1]
"""
import glob
import os
import sys
from collections import defaultdict

from PIL import Image, ImageStat

d = sys.argv[1]
box = tuple(int(v) for v in sys.argv[2:6]) if len(sys.argv) >= 6 else (740, 330, 980, 580)
fs = sorted(f for f in glob.glob(os.path.join(d, "*.jpg")) if not os.path.basename(f).startswith("flicker_"))
rows = []
for f in fs:
    p = os.path.basename(f)[:-4].split("_")
    rows.append((p[1], p[2], int(p[3]), ImageStat.Stat(Image.open(f).convert("L").crop(box)).mean[0]))
# per tick: each frame's brightness relative to the tick's real frame and the one before (their mean)
by = defaultdict(list)
ticks = defaultdict(dict)
order = []
for k, (tick, kind, t, L) in enumerate(rows):
    if tick not in ticks:
        order.append(tick)
    ticks[tick][(kind, t)] = L
prev_real = None
for tick in order:
    fr = ticks[tick]
    real = [L for (kind, t), L in fr.items() if kind == "r"]
    if prev_real is not None and real:
        base = 0.5 * (prev_real + real[0])
        rank = sorted((t, L) for (kind, t), L in fr.items() if kind == "i")
        for n, (t, L) in enumerate(rank):
            by[f"in-between #{n + 1}"].append(L - base)
        by["real"].append(real[0] - base)
    if real:
        prev_real = real[0]
print(f"{os.path.basename(d)}: region {box}, brightness relative to the real frames around it (mean, and the count over +3 / under -3):")
for key in sorted(by):
    v = by[key]
    print(f"  {key:15s} {sum(v) / len(v):+6.2f}   over +3: {sum(x > 3 for x in v):3d}   under -3: {sum(x < -3 for x in v):3d}   of {len(v)}")
