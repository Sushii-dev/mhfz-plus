"""Constant uploads of a pairing capture by register range (VS and PS): how often each (start, count) is uploaded per
tick, which targets it is drawn into, and a sample of values - to find light, fog and colour parameters.

    python tools/capconsts.py cap_004462.bin [-rt 0579f7a0] [-ps] [-min 4]
"""
import struct
import sys
from collections import defaultdict

sys.path.insert(0, __import__("os").path.dirname(__file__))
from capup import load, walk, OP  # noqa: E402


def main():
    frames = load(sys.argv[1])
    want_rt = int(sys.argv[sys.argv.index("-rt") + 1], 16) if "-rt" in sys.argv else None
    kind = OP["PSCF"] if "-ps" in sys.argv else OP["VSCF"]
    mn = int(sys.argv[sys.argv.index("-min") + 1]) if "-min" in sys.argv else 4
    for fi in (1, 2):
        rt0 = None
        ups = defaultdict(list)
        for off, op, p in walk(frames[fi][1]):
            if op == OP["RT"]:
                i, ptr = struct.unpack_from("<II", p, 0)
                if i == 0:
                    rt0 = ptr
            elif op == kind and (want_rt is None or rt0 == want_rt):
                start, count, ordn = struct.unpack_from("<III", p, 0)
                vals = struct.unpack_from("<%df" % (count * 4), p, 12)
                ups[(start, count)].append(vals)
        print(f"frame {fi}:")
        for (s, c), lst in sorted(ups.items()):
            if len(lst) < mn:
                continue
            distinct = len(set(tuple(round(x, 4) for x in v) for v in lst))
            sample = " ".join(f"{x:.4g}" for x in lst[0][:8])
            print(f"  c{s} x{c}: {len(lst)} uploads, {distinct} distinct | first: {sample}")


main()
