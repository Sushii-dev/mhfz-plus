"""Matrices shared by the draws of each off-screen target (light view-projections of shadow maps, reflection cameras)
in a pairing capture, and how they change from the previous tick to this one. A shadow map whose projection is
snapped to whole texels moves in exact texel steps; blending such a matrix puts the in-between frames' shadow map
between texels, and its edges shimmer.

    python tools/capshadow.py cap_004462.bin [-all]
"""
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __import__("os").path.dirname(__file__))
from capup import load, walk, OP  # noqa: E402


def scan(buf):
    """per target: Counter of (start, 16 floats) of the 4-register uploads in effect at its draws"""
    regs = {}
    rt0 = None
    per = defaultdict(Counter)
    draws = Counter()
    for off, op, p in walk(buf):
        if op == OP["VSCF"]:
            start, count, ordn = struct.unpack_from("<III", p, 0)
            vals = struct.unpack_from("<%df" % (count * 4), p, 12)
            for r in range(count):
                regs[start + r] = vals[r * 4:r * 4 + 4]
            if count == 4:
                regs[("m", start)] = vals
        elif op == OP["RT"]:
            i, ptr = struct.unpack_from("<II", p, 0)
            if i == 0:
                rt0 = ptr
        elif op in (OP["DP"], OP["DIP"], OP["DPUP"], OP["DIPUP"]):
            draws[rt0] += 1
            for k, v in regs.items():
                if isinstance(k, tuple):
                    per[rt0][(k[1], tuple(round(x, 6) for x in v))] += 1
    return per, draws


def main():
    frames = load(sys.argv[1])
    show_all = "-all" in sys.argv
    (pp, pd), (np_, nd) = scan(frames[1][1]), scan(frames[2][1])
    for rt in nd:
        if not rt or nd[rt] < 20:
            continue
        # matrices in effect for most of this target's draws, in both ticks
        common = [(k, c) for k, c in np_[rt].items() if c >= 0.5 * nd[rt]]
        if not common:
            continue
        print(f"rt {rt:08x}: {nd[rt]} draws ({pd.get(rt, 0)} in the previous tick)")
        for (start, m), c in sorted(common)[: (99 if show_all else 4)]:
            prev = [(k2, c2) for k2, c2 in pp[rt].items() if k2[0] == start and c2 >= 0.5 * max(pd.get(rt, 1), 1)]
            print(f"  c{start} x4 in {c}/{nd[rt]} draws:")
            print("    now  " + " ".join(f"{x:10.5f}" for x in m))
            for (s2, m2), c2 in prev[:1]:
                d = [a - b for a, b in zip(m, m2)]
                print("    prev " + " ".join(f"{x:10.5f}" for x in m2))
                print("    diff " + " ".join(f"{x:10.6f}" for x in d))


main()
