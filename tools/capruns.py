"""Text strings (runs of consecutive screen-space glyph quads, as MatchTextRuns builds them) in a pairing capture:
matched between frames P and N by glyph sequence, and how each moved - translation, scale, the largest per-glyph
deviation from that. Shows whether labels move rigidly, scale, or get clipped.

    python tools/capruns.py cap_014851.bin [-v]
"""
import struct
import sys
from collections import defaultdict

sys.path.insert(0, __import__("os").path.dirname(__file__))
from capup import load, walk, upverts, OP  # noqa: E402


def quads(buf):
    """per draw (in draw order): None, or (x0, y0, x1, y1, keybytes) for a screen-space UP draw"""
    out = []
    for off, op, p in walk(buf):
        if op in (OP["DP"], OP["DIP"]):
            out.append(None)
            continue
        if op not in (OP["DPUP"], OP["DIPUP"]):
            continue
        stride, posOff, rhw, vb, t, pc, nv = upverts(op, p)
        if not rhw or posOff < 0 or nv == 0:
            out.append(None)
            continue
        xs, ys, rest = [], [], []
        for k in range(nv):
            x, y = struct.unpack_from("<ff", vb, k * stride + posOff)
            xs.append(x)
            ys.append(y)
            v = vb[k * stride:(k + 1) * stride]
            rest.append(v[:posOff] + v[posOff + 16:])  # everything but the position: colour, texcoords (the glyph)
        out.append((min(xs), min(ys), max(xs), max(ys), b"".join(rest)))
    return out


def runs(q):
    rs, cur, prev = [], [], None
    for d, b in enumerate(q):
        if b is None:
            if len(cur) >= 2:
                rs.append(cur)
            cur, prev = [], None
            continue
        joins = False
        if prev is not None:
            h, ph = b[3] - b[1], prev[3] - prev[1]
            H = max(h, ph, 1.0)
            joins = max(h, ph) <= 2 * max(min(h, ph), 1.0) and abs(b[1] - prev[1]) <= 0.5 * H and max(b[0], prev[0]) - min(b[2], prev[2]) <= 1.5 * H
        if not joins:
            if len(cur) >= 2:
                rs.append(cur)
            cur = []
        cur.append(d)
        prev = b
    if len(cur) >= 2:
        rs.append(cur)
    return rs


def main():
    frames = load(sys.argv[1])
    qp, qn = quads(frames[1][1]), quads(frames[2][1])
    rp, rn = runs(qp), runs(qn)
    sig = lambda q, r: tuple(q[d][4] for d in r)
    byp = defaultdict(list)
    for r in rp:
        byp[sig(qp, r)].append(r)
    exact = rigid = scaled = other = nomatch = 0
    for r in rn:
        s = sig(qn, r)
        cands = byp.get(s)
        if not cands:
            nomatch += 1
            if "-v" in sys.argv:
                b = qn[r[0]]
                print(f"  no counterpart: {len(r)} quads at {b[0]:.1f},{b[1]:.1f} h {b[3]-b[1]:.1f}")
            continue
        best = min(cands, key=lambda c: abs(qp[c[0]][0] - qn[r[0]][0]) + abs(qp[c[0]][1] - qn[r[0]][1]))
        a = [qn[d] for d in r]
        b = [qp[d] for d in best]
        dx, dy = a[0][0] - b[0][0], a[0][1] - b[0][1]
        dev = max(max(abs(x[0] - y[0] - dx), abs(x[1] - y[1] - dy), abs(x[2] - y[2] - dx), abs(x[3] - y[3] - dy)) for x, y in zip(a, b))
        wa, wb = a[-1][2] - a[0][0], b[-1][2] - b[0][0]
        scale = wa / wb if wb else 1
        if dev == 0 and dx == 0 and dy == 0:
            exact += 1
        elif dev <= 0.75:
            rigid += 1
        elif abs(scale - 1) > 0.005:
            scaled += 1
        else:
            other += 1
        if "-v" in sys.argv and (dev > 0 or dx or dy):
            print(f"  run of {len(r)} at {a[0][0]:.1f},{a[0][1]:.1f} h {a[0][3]-a[0][1]:.1f}: moved {dx:+.2f},{dy:+.2f}, deviation {dev:.2f}, scale {scale:.4f}")
    print(f"{sys.argv[1]}: N {len(rn)} runs, P {len(rp)} runs | unchanged {exact}, moved rigidly {rigid}, scaled {scaled}, "
          f"moved non-rigidly {other}, no counterpart {nomatch}")


main()
