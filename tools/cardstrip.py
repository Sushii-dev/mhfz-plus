"""Crop the billboard area from captures[8:13] into one strip (test app)."""
import glob, sys
from PIL import Image, ImageDraw

out = sys.argv[1]
fs = sorted(glob.glob("captures/*.png"))[8:13]
ims = [Image.open(f).convert("RGB").crop((100, 100, 700, 500)).resize((400, 266)) for f in fs]
m = Image.new("RGB", (len(ims) * 404, 260))
for i, im in enumerate(ims):
    m.paste(im, (i * 404, 0))
    ImageDraw.Draw(m).text((i * 404 + 4, 4), fs[i][-14:], fill=(255, 255, 0))
m.save(out)
