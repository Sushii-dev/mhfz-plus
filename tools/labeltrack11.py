"""Test-app scene 11 (identical titles over the flick scene's posts): label positions in captured frames
(tools\\labelstrips.ps1 -> build\\cap-<name>\\captures) against the truth. The scene's camera is recomputed here; each
real frame's tick is found by fitting, and every in-between frame's labels are compared with the blend of the true
positions of the ticks around it at its t (each detected label against the nearest ideal one).

    python tools/labeltrack11.py build/cap-<name> [width height]
"""
import glob
import math
import os
import sys

from PIL import Image


def truth(tick, SW, SH):
    yaw = 1.1 * math.sin(tick * 0.35)
    cy, sy = math.cos(yaw), math.sin(yaw)
    dist, tx, tz = 700.0, 0.0, 0.0
    cx, cz, cyy = tx - sy * dist, tz - cy * dist, 260.0
    R = [[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]]
    Vw = [[R[i][0], R[i][1], R[i][2], -(R[i][0] * cx + R[i][1] * cyy + R[i][2] * cz)] for i in range(3)] + [[0, 0, 0, 1]]
    Pm = [[1.0, 0, 0, 0], [0, 1.333, 0, 0], [0, 0, 1.0, -1.0], [0, 0, 1, 0]]
    out = {}
    for p in range(16):
        pa, pr, r = p * 0.3927, 300.0 + (p % 4) * 220.0, 40.0 + (p % 5) * 8.0
        wx, wy, wz = tx + math.sin(pa) * pr, 60.0 + (p % 3) * 70.0 + r + 25.0, tz + math.cos(pa) * pr
        cv = [Vw[i][0] * wx + Vw[i][1] * wy + Vw[i][2] * wz + Vw[i][3] for i in range(4)]
        c = [sum(Pm[i][k] * cv[k] for k in range(4)) for i in range(4)]
        if c[3] < 50:
            continue
        sx = (c[0] / c[3] + 1) * 0.5 * SW - 36.0
        sy_ = (1 - c[1] / c[3]) * 0.5 * SH - 8.0
        x0 = math.floor(sx + 0.5) - 0.5
        if x0 < 0 or x0 + 72 > SW or sy_ < 0 or sy_ + 16 > SH:
            continue
        out[p] = (x0, math.floor(sy_ + 0.5) - 0.5)
    return out


def blocks(img):
    """left/top of the yellow title blocks (text colour pixels; a label is 6 glyphs of 9 px every 12 px)"""
    px = img.load()
    W, H = img.size
    hits = set()
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y][:3]
            if r > 230 and g > 200 and 90 < b < 170:
                hits.add((x, y))
    seen, out = set(), []
    for (x, y) in sorted(hits, key=lambda q: (q[1], q[0])):
        if (x, y) in seen:
            continue
        # a label: hits within 72 x 16 from its top-left
        mem = [(a, b) for a in range(x, x + 72) for b in range(y, y + 16) if (a, b) in hits]
        if len(mem) < 6 * 9 * 10:
            continue
        for q in mem:
            seen.add(q)
        out.append((x, y))
    return out


def main():
    d = sys.argv[1]
    SW, SH = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (784, 561)
    fs = sorted(glob.glob(os.path.join(d, "captures", "*.png")))
    frames = []
    for f in fs:
        n = os.path.basename(f)[:-4]
        frames.append((n.split("_")[-2], int(n.split("_")[-1]) / 100.0, blocks(Image.open(f).convert("RGB")), n))
    # each real frame's tick: the best fit of the true positions to its labels
    def fit(bl):
        best = (1e9, -1)
        for T in range(0, 400):
            tr = list(truth(T, SW, SH).values())
            if not tr or not bl:
                continue
            e = sum(min(abs(bx - x) + abs(by - y) for (x, y) in tr) for (bx, by) in bl) / len(bl)
            e += sum(min(abs(bx - x) + abs(by - y) for (bx, by) in bl) for (x, y) in tr) / len(tr)
            if e < best[0]:
                best = (e, T)
        return best
    reals = [i for i, f in enumerate(frames) if f[0] == "r"]
    ticks = {i: fit(frames[i][2]) for i in reals}
    errs, worst = [], (0, "")
    for a, b in zip(reals, reals[1:]):
        ta, tb = ticks[a][1], ticks[b][1]
        if tb != ta + 1:
            continue
        A, B = truth(ta, SW, SH), truth(tb, SW, SH)
        ideal = [(A[p][0] + 0, A[p][1], B[p][0], B[p][1]) for p in A if p in B]
        for i in range(a + 1, b):
            kind, t, bl, name = frames[i]
            pts = [(x0 + t * (x1 - x0), y0 + t * (y1 - y0)) for (x0, y0, x1, y1) in ideal]
            for (bx, by) in bl:
                if not pts:
                    break
                e = min(math.hypot(bx - x, by - y) for (x, y) in pts)
                errs.append(e)
                if e > worst[0]:
                    worst = (e, f"{name}: label at {bx},{by}")
    print("real frames fitted to ticks:", " ".join(f"{ticks[i][1]}({ticks[i][0]:.1f}px)" for i in reals))
    if not errs:
        print("no comparable frames")
        return
    errs.sort()
    print(f"{len(errs)} labels in in-between frames: mean error {sum(errs) / len(errs):.1f} px, 90% {errs[int(0.9 * len(errs))]:.0f} px, "
          f"max {errs[-1]:.0f} px, over 20 px: {sum(e > 20 for e in errs)} | worst: {worst[1]}")


main()
