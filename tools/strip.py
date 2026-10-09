"""strip.py <session dir> <first index> <count> <x0> <y0> <x1> <y1> <out.png> [cols]: crop one region from consecutive
rolling-recorder frames (real 'r' and in-between 'i') into a labelled grid, to see flicker frame by frame."""
import os, re, sys
from PIL import Image, ImageDraw

sess, first, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
box = tuple(int(v) for v in sys.argv[4:8])
out = sys.argv[8]
cols = int(sys.argv[9]) if len(sys.argv) > 9 else 6
rx = re.compile(r"(\d+)_tick(\d+)_([ri])_(\d+)\.jpg")
files = sorted(f for f in os.listdir(sess) if rx.match(f))
sel = [f for f in files if first <= int(rx.match(f).group(1)) < first + count]
w, h = box[2] - box[0], box[3] - box[1]
rows = (len(sel) + cols - 1) // cols
m = Image.new("RGB", (cols * (w + 4), rows * (h + 4)), (255, 255, 255))
for k, f in enumerate(sel):
    im = Image.open(os.path.join(sess, f)).convert("RGB").crop(box)
    x, y = (k % cols) * (w + 4), (k // cols) * (h + 4)
    m.paste(im, (x, y))
    g = rx.match(f)
    ImageDraw.Draw(m).text((x + 3, y + 2), f"{g.group(2)[-4:]} {g.group(3)}{g.group(4)}", fill=(255, 255, 0) if g.group(3) == "r" else (0, 255, 255))
m.save(out)
print(len(sel), "frames", m.size)
