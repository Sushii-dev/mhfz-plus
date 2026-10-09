"""Where the crowd scene's labels are in captured frames (tools\\labelstrips.ps1 leaves them in build\\cap-<name>\\captures),
against where they should be: each label row's left edge in every in-between frame, compared with the linear blend
of the real frames around it at that frame's t. Test-app scene 9/10: titles at y = 250 + 6k (k = 0..5), names at
y = 210 and 320.

    python tools/labeltrack.py build/cap-crowd-new [-v]
"""
import glob
import os
import sys

from PIL import Image

ROWS = [250 + 6 * k for k in range(6)] + [210, 320]
if "-rows12" in sys.argv:  # (scene 12: four labels 20 px apart)
    ROWS = [230 + 20 * k for k in range(4)]


def labels(img):
    """per label row: the left edges of the label blocks on it (text colour pixels, gaps > 20 px split labels)"""
    px = img.load()
    W = img.width
    out = []
    for y0 in ROWS:
        y = y0 + 8  # (the middle of a glyph)
        xs = []
        for x in range(W):
            r, g, b = px[x, y][:3]
            if (r > 200 and g > 180 and b < 170) or (r > 200 and g > 200 and b > 200):  # yellow titles, white names
                xs.append(x)
        blocks = []
        for x in xs:
            if not blocks or x - blocks[-1][1] > 20:
                blocks.append([x, x])
            else:
                blocks[-1][1] = x
        out.append([b[0] for b in blocks if b[1] - b[0] > 30])
    return out


def main():
    d = sys.argv[1]
    fs = sorted(glob.glob(os.path.join(d, "captures", "*.png")))
    frames = []
    for f in fs:
        n = os.path.basename(f)[:-4]
        kind, t = n.split("_")[-2], int(n.split("_")[-1]) / 100.0
        frames.append((kind, t, labels(Image.open(f).convert("RGB")), n))
    reals = [i for i, f in enumerate(frames) if f[0] == "r"]
    errs, worst = [], (0, "")
    for a, b in zip(reals, reals[1:]):
        A, B = frames[a][2], frames[b][2]
        for i in range(a + 1, b):
            kind, t, L, name = frames[i]
            for row in range(len(ROWS)):
                # pair the labels of the two real frames by order on the row (they keep their order; same count only)
                if len(A[row]) != len(B[row]) or len(L[row]) != len(A[row]):
                    continue
                for x0, x1, x in zip(A[row], B[row], L[row]):
                    e = abs(x - (x0 + t * (x1 - x0)))
                    errs.append(e)
                    if e > worst[0]:
                        worst = (e, f"{name} row y={ROWS[row]}: at {x}, expected {x0 + t * (x1 - x0):.0f} ({x0} -> {x1})")
    if not errs:
        print("no comparable frames")
        return
    errs.sort()
    print(f"{len(errs)} label positions in in-between frames: mean error {sum(errs) / len(errs):.1f} px, 90% {errs[int(0.9 * len(errs))]:.0f} px, "
          f"max {errs[-1]:.0f} px, over 20 px: {sum(e > 20 for e in errs)}")
    print("  worst:", worst[1])


main()
