"""cardpop.py <session dir> <out.png>: find where the swap scene's fog card (RGB 96,112,160) pops in between two real
frames and strip those frames; prints how far the card's right edge in the in-between frames is from the real one."""
import os, re, sys
import numpy as np
from PIL import Image, ImageDraw

d, out = sys.argv[1], sys.argv[2]
rx = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
fs = sorted(f for f in os.listdir(d) if rx.match(f))

def card(f):
    a = np.asarray(Image.open(os.path.join(d, f)).convert("RGB")).astype(int)
    return (np.abs(a[..., 0] - 96) < 14) & (np.abs(a[..., 1] - 112) < 14) & (np.abs(a[..., 2] - 160) < 14)

def right_edge(m):
    cols = np.nonzero(m.sum(axis=0) > m.shape[0] * 0.3)[0]
    return int(cols.max()) if len(cols) else -1

reals = [f for f in fs if rx.match(f).group(3) == "r"]
cover = {f: card(f).mean() for f in reals}
for a, b in zip(reals, reals[1:]):
    if cover[a] < 0.05 and cover[b] > 0.3:  # card absent, then present
        ia, ib = fs.index(a), fs.index(b)
        sel = fs[ia:ib + 1]
        er = right_edge(card(b))
        print("pop-in", a, "->", b, "real right edge", er)
        for f in sel[1:-1]:
            print("  ", f, "in-between right edge", right_edge(card(f)))
        m = Image.new("RGB", (404 * len(sel), 300))
        for k, f in enumerate(sel):
            m.paste(Image.open(os.path.join(d, f)).convert("RGB"), (k * 404, 0))
            ImageDraw.Draw(m).text((k * 404 + 4, 4), f[5:], fill=(255, 255, 0))
        m.save(out)
        break
