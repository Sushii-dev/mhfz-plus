"""Finds flicker in a sequence of displayed frames (retro\\NNN\\*.jpg from the rolling recorder, or captures\\*.png):
an image block whose brightness in one frame lies outside the range of both neighbouring frames by a margin - a
spike. Motion moves brightness through a block smoothly (a frame lies between its neighbours); a popping glyph, a
shadow switching for one frame or a flashing light makes a spike. Prints the frames with the most spikes (real or
in-between, from the file name) and writes flicker_heat.png (where spikes happened) and flicker_<k>.png (the worst
events: previous / frame / next, side by side, cropped around the spikes).

    python tools/flickerscan.py <folder> [-block 16] [-margin 18] [-top 6]
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def arg(name, default):
    return type(default)(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def main():
    folder = sys.argv[1]
    B = arg("-block", 16)
    margin = arg("-margin", 18.0)
    top = arg("-top", 6)
    files = sorted(glob.glob(os.path.join(folder, "*.jpg")) + glob.glob(os.path.join(folder, "*.png")))
    files = [f for f in files if not os.path.basename(f).startswith("flicker_")]
    if len(files) < 3:
        print("need at least 3 frames")
        return
    imgs = [np.asarray(Image.open(f).convert("L"), dtype=np.float32) for f in files]
    h, w = imgs[0].shape
    by, bx = h // B, w // B
    L = np.stack([im[: by * B, : bx * B].reshape(by, B, bx, B).mean(axis=(1, 3)) for im in imgs])  # frames x by x bx
    lo = np.minimum(L[:-2], L[2:])
    hi = np.maximum(L[:-2], L[2:])
    up = L[1:-1] - hi
    dn = lo - L[1:-1]
    spike = np.maximum(up, dn)  # >0: outside both neighbours
    # motion makes the neighbours differ from each other too; a pop / flash spikes against steady neighbours
    spike = spike - 0.75 * np.abs(L[2:] - L[:-2])
    hits = spike > margin
    per = hits.sum(axis=(1, 2))
    names = [os.path.basename(f) for f in files]
    kind = lambda n: "real" if "_r_" in n else "in-between"
    print(f"{len(files)} frames {w}x{h}, blocks {bx}x{by} of {B} px, margin {margin}: spikes per frame (frame 1..n-2)")
    real = [per[k] for k in range(len(per)) if kind(names[k + 1]) == "real"]
    inb = [per[k] for k in range(len(per)) if kind(names[k + 1]) != "real"]
    print(f"  real frames: {np.mean(real) if real else 0:.1f} spiking blocks on average, in-between frames: {np.mean(inb) if inb else 0:.1f}")
    order = np.argsort(-per)[:top]
    heat = hits.sum(axis=0).astype(np.float32)
    if heat.max() > 0:
        hm = (255 * heat / heat.max()).astype(np.uint8)
        Image.fromarray(hm).resize((bx * B // 2, by * B // 2), Image.NEAREST).save(os.path.join(folder, "flicker_heat.png"))
    for rank, k in enumerate(order):
        if per[k] == 0:
            break
        ys, xs = np.nonzero(hits[k])
        y0, y1 = max(0, ys.min() * B - 2 * B), min(h, (ys.max() + 1) * B + 2 * B)
        x0, x1 = max(0, xs.min() * B - 2 * B), min(w, (xs.max() + 1) * B + 2 * B)
        print(f"  #{rank}: {names[k + 1]} ({kind(names[k + 1])}): {per[k]} blocks, region x {x0}-{x1} y {y0}-{y1}, "
              f"strongest {spike[k].max():.0f}")
        crops = [Image.open(files[k + d]).convert("RGB").crop((x0, y0, x1, y1)) for d in (0, 1, 2)]
        cw, ch = crops[0].size
        out = Image.new("RGB", (cw * 3 + 8, ch), (255, 0, 255))
        for d, c in enumerate(crops):
            out.paste(c, (d * (cw + 4), 0))
        scale = min(1.0, 2400 / out.size[0])
        if scale < 1:
            out = out.resize((int(out.size[0] * scale), int(out.size[1] * scale)))
        out.save(os.path.join(folder, f"flicker_{rank}.png"))


main()
