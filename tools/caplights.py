"""Point lights per draw in a pairing capture (pixel shader c21-c23 positions, c24-c26 parameters, c162/c163 a
fourth one): for the draws of the main target, in order, whether each draw's light slots hold the same lights in the
previous tick - same order, the same lights reordered, or other lights. Blending a slot whose light changed slides
that light across the scene for the in-between frames.

    python tools/caplights.py cap_004462.bin [-rt 0579f7a0] [-v]
"""
import struct
import sys
from collections import Counter

sys.path.insert(0, __import__("os").path.dirname(__file__))
from capup import load, walk, OP  # noqa: E402


def draws(buf, want_rt):
    ps = {}
    vsobj = None
    rt0 = None
    out = []
    for off, op, p in walk(buf):
        if op == OP["RT"]:
            i, ptr = struct.unpack_from("<II", p, 0)
            if i == 0:
                rt0 = ptr
        elif op == OP["PSCF"]:
            start, count, ordn = struct.unpack_from("<III", p, 0)
            vals = struct.unpack_from("<%df" % (count * 4), p, 12)
            for r in range(count):
                ps[start + r] = vals[r * 4:r * 4 + 4]
            if start <= 21 < start + count:
                ps["fresh"] = True
        elif op == OP["VSCF"]:
            start, count, ordn = struct.unpack_from("<III", p, 0)
            if start == 38 and count == 4:
                vsobj = struct.unpack_from("<16f", p, 12)
        elif op in (OP["DP"], OP["DIP"]) and rt0 == want_rt and ps.pop("fresh", False):
            lights = tuple(tuple(round(x, 1) for x in ps.get(r, (0, 0, 0, 0))[:3]) for r in (21, 22, 23, 162))
            parms = tuple(ps.get(r, (0, 0, 0, 0)) for r in (24, 25, 26, 163))
            col = ps.get(158)
            obj = (round(vsobj[3], 0), round(vsobj[7], 0), round(vsobj[11], 0)) if vsobj else None
            out.append((obj, lights, parms, col))
    return out


def main():
    frames = load(sys.argv[1])
    rt = int(sys.argv[sys.argv.index("-rt") + 1], 16) if "-rt" in sys.argv else 0x0579f7a0
    P, N = draws(frames[1][1], rt), draws(frames[2][1], rt)
    print(f"draws with fresh lights: previous {len(P)}, now {len(N)}")
    # pair by object position (the draw's c38 matrix translation is in view space, close between ticks): nearest
    used = set()
    cat = Counter()
    for k, (o, l, pa, col) in enumerate(N):
        best, bd = None, 1e18
        for j, (o2, l2, pa2, col2) in enumerate(P):
            if j in used or o is None or o2 is None:
                continue
            d = sum((a - b) ** 2 for a, b in zip(o, o2))
            if d < bd:
                best, bd = j, d
        if best is None or bd > 400.0 ** 2:
            cat["no counterpart"] += 1
            continue
        used.add(best)
        l2 = P[best][1]
        if l == l2:
            cat["same lights, same slots"] += 1
        elif sorted(l) == sorted(l2):
            cat["same lights, other slots"] += 1
            if "-v" in sys.argv:
                print(f"  reordered: {l2} -> {l}")
        else:
            moved = sum(1 for a, b in zip(l, l2) if a != b)
            far = max((sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5 for a, b in zip(l, l2) if a != b), default=0)
            cat[f"{moved} slot(s) hold another light"] += 1
            if "-v" in sys.argv:
                print(f"  changed ({far:.0f} units): {l2} -> {l}")
    for k, v in cat.most_common():
        print(f"  {k}: {v}")


main()
