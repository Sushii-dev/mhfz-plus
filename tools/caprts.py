"""Render targets of a captured frame in order of use: per run of draws into one target, how many draws, whether it
was cleared first, the blend state (ALPHABLENDENABLE, SRC/DESTBLEND, BLENDOP), z state, and the textures sampled on
stage 0. Targets drawn with blending and never cleared keep what earlier frames left: accumulation (in-between frames
replaying such passes add to it again).

    python tools/caprts.py cap_014851.bin [-frame 2]
"""
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(__file__))
from capup import load, walk, OP  # noqa: E402

RS_ZENABLE, RS_ZWRITE, RS_ALPHATEST, RS_SRCBLEND, RS_DESTBLEND, RS_ALPHABLEND, RS_BLENDOP, RS_COLORWRITE = 7, 14, 15, 19, 20, 27, 171, 168


def main():
    fi = int(sys.argv[sys.argv.index("-frame") + 1]) if "-frame" in sys.argv else 2
    frames = load(sys.argv[1])
    serial, buf, nd = frames[fi]
    rs = {}
    rt0 = ds = None
    tex0 = None
    segs = []
    cur = None
    cleared = set()
    for off, op, p in walk(buf):
        if op == OP["RS"]:
            a, b = struct.unpack_from("<II", p, 0)
            rs[a] = b
        elif op == OP["RT"]:
            i, ptr = struct.unpack_from("<II", p, 0)
            if i == 0 and ptr:
                rt0 = ptr
        elif op == OP["DS"]:
            ds = struct.unpack_from("<II", p, 0)[1]
        elif op == OP["TEX"]:
            st, ptr = struct.unpack_from("<II", p, 0)
            if st == 0:
                tex0 = ptr
        elif op == OP["CLEAR"]:
            cnt, fl = struct.unpack_from("<II", p, 0)
            cleared.add(rt0)
            if cur is None or cur["rt"] != rt0:
                cur = {"rt": rt0, "ds": ds, "draws": 0, "clear": 0, "blend": set(), "tex": set(), "first": off}
                segs.append(cur)
            cur["clear"] += 1
        elif op in (OP["DP"], OP["DIP"], OP["DPUP"], OP["DIPUP"]):
            if cur is None or cur["rt"] != rt0:
                cur = {"rt": rt0, "ds": ds, "draws": 0, "clear": 0, "blend": set(), "tex": set(), "first": off}
                segs.append(cur)
            cur["draws"] += 1
            if rs.get(RS_ALPHABLEND):
                cur["blend"].add((rs.get(RS_SRCBLEND, 2), rs.get(RS_DESTBLEND, 1), rs.get(RS_BLENDOP, 1)))
            else:
                cur["blend"].add("opaque")
            cur["tex"].add(tex0)
    agg = {}
    order = []
    for k, s in enumerate(segs):
        rt = s["rt"] or 0
        if rt not in agg:
            agg[rt] = {"draws": 0, "clears": 0, "before": 0, "blend": set(), "tex": set(), "first": k, "last": k, "ds": set()}
            order.append(rt)
        a = agg[rt]
        if s["clear"]:
            a["clears"] += s["clear"]
        elif not a["clears"]:
            a["before"] += s["draws"]
        a["draws"] += s["draws"]
        a["blend"] |= s["blend"]
        a["tex"] |= s["tex"]
        a["last"] = k
        a["ds"].add(s["ds"] or 0)
    print(f"frame {fi} serial {serial}: {nd} draws, {len(segs)} target runs, {len(order)} targets")
    for rt in order:
        a = agg[rt]
        print(f"  rt {rt:08x}: runs #{a['first']}-#{a['last']}, {a['draws']:5d} draws ({a['before']} before any clear), {a['clears']} clears, "
              f"ds {[hex(x) for x in a['ds']]}, blend {sorted(map(str, a['blend']))[:5]}, {len(a['tex'])} textures on stage 0")


main()