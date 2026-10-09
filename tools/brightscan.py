"""Brightness over a retro save's frames: each frame's mean luminance (whole frame and a 4x3 grid of regions), real
frames vs in-between frames. A flicker of the lighting shows as in-between frames brighter or darker than the
real frames around them (where the blend of the two neighbours would be). Prints the frames that deviate most.

    python tools/brightscan.py <retro\\NNN> [-grid]
"""
import glob
import os
import sys

from PIL import Image, ImageStat


def lum(im):
    return ImageStat.Stat(im.convert("L")).mean[0]


def main():
    d = sys.argv[1]
    fs = sorted(f for f in glob.glob(os.path.join(d, "*.jpg")) if not os.path.basename(f).startswith("flicker_"))
    rows = []
    for f in fs:
        n = os.path.basename(f)[:-4]
        parts = n.split("_")  # NNNN_tickTTTTTT_r|i_TTT
        kind, t = parts[2], int(parts[3]) / 100.0
        im = Image.open(f).convert("RGB")
        W, H = im.size
        regs = []
        for gy in range(3):
            for gx in range(4):
                regs.append(lum(im.crop((gx * W // 4, gy * H // 3, (gx + 1) * W // 4, (gy + 1) * H // 3))))
        rows.append((n, kind, t, lum(im), regs))
    # deviation of each in-between frame from the blend of the real frames around it (per region, the largest)
    reals = [i for i, r in enumerate(rows) if r[1] == "r"]
    devs = []
    for a, b in zip(reals, reals[1:]):
        for i in range(a + 1, b):
            n, kind, t, L, regs = rows[i]
            exp = [rows[a][4][k] + t * (rows[b][4][k] - rows[a][4][k]) for k in range(12)]
            dk = [regs[k] - exp[k] for k in range(12)]
            k = max(range(12), key=lambda q: abs(dk[q]))
            whole = L - (rows[a][3] + t * (rows[b][3] - rows[a][3]))
            devs.append((abs(dk[k]), n, k, dk[k], whole))
    # the real frames' own change from tick to tick, for scale
    rch = [max(abs(rows[b][4][k] - rows[a][4][k]) for k in range(12)) for a, b in zip(reals, reals[1:])]
    devs.sort(reverse=True)
    print(f"{len(rows)} frames ({len(reals)} real): in-between frames vs the blend of their real neighbours, largest region deviation")
    s = sorted(d[0] for d in devs)
    if s:
        print(f"  median {s[len(s) // 2]:.2f}, 90% {s[int(0.9 * len(s))]:.2f}, max {s[-1]:.2f} (luminance 0-255) | real frames' own tick-to-tick "
              f"change (largest region): median {sorted(rch)[len(rch) // 2]:.2f}, max {max(rch):.2f}")
    for dv in devs[:10]:
        print(f"  {dv[1]}: region {dv[2] % 4},{dv[2] // 4} {dv[3]:+.2f} (whole frame {dv[4]:+.2f})")
    if "-series" in sys.argv:
        for n, kind, t, L, regs in rows:
            print(f"  {n} {L:6.2f} " + " ".join(f"{v:6.1f}" for v in regs))


main()
