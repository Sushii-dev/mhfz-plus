"""Screen-space (pre-transformed, XYZRHW) UP draws in pairing captures (paircap\\cap_*.bin): per texture / pixel shader
group, how many draws and vertices, and the range of their depth (z) and 1/w (rhw). HUD quads sit at z = 0, rhw = 1;
world-anchored ones (labels over characters, particles, fog sprites) carry real depth.

    python tools/capup.py cap_014851.bin [-frame 2] [-all]
"""
import struct
import sys
from collections import defaultdict

OPS = ["none", "RS", "SS", "TSS", "TEX", "VS", "PS", "DECL", "FVF", "VSCF", "VSCI", "VSCB", "PSCF", "PSCI", "PSCB", "SSRC",
       "SSFREQ", "IDX", "RT", "DS", "VP", "SCISSOR", "XFORM", "MULXFORM", "MAT", "LIGHT", "LENABLE", "CLIP", "CLEAR", "DP",
       "DIP", "DPUP", "DIPUP", "STRETCH", "COLORFILL", "BEGIN", "END", "SWVP", "SBAPPLY", "SBCAPTURE", "NPATCH", "TEXSUB"]
OP = {n: i for i, n in enumerate(OPS)}


def load(path):
    b = open(path, "rb").read()
    o = 0

    def u32():
        nonlocal o
        v = struct.unpack_from("<I", b, o)[0]
        o += 4
        return v
    assert b[:4] == b"MHPC"
    o = 4
    u32()
    cs = u32()
    o += cs  # config
    n, sz = u32(), u32()
    o += n * (4 + sz)
    n, sz = u32(), u32()
    o += n * (4 + sz)
    drsz = u32()
    frames = []
    for _ in range(3):
        serial, valid, nvscf, svs, sps, bl = u32(), u32(), u32(), u32(), u32(), u32()
        buf = b[o:o + bl]
        o += bl
        nd = u32()
        o += nd * drsz
        frames.append((serial, buf, nd))
    return frames


def walk(buf):
    o = 0
    while o + 8 <= len(buf):
        op, fl, size = struct.unpack_from("<HHI", buf, o)
        if size == 0:
            break
        yield o, op, buf[o + 8:o + size]
        o += size


def upverts(op, p):
    """(stride, posOff, rhw, vertex bytes, primitive type, prims, nverts)"""
    if op == OP["DPUP"]:
        t, pc, stride, vbytes, posOff, posN, rhw = struct.unpack_from("<IIIIhBB", p, 0)
        return stride, posOff, rhw, p[20:20 + vbytes], t, pc, vbytes // max(stride, 1)
    t, mi, nv, pc, fmt, stride, ib, vbytes, posOff, posN, rhw = struct.unpack_from("<IIIIIIIIhBB", p, 0)
    iba = (ib + 3) & ~3
    return stride, posOff, rhw, p[36 + iba:36 + iba + vbytes], t, pc, vbytes // max(stride, 1)


def main():
    path = sys.argv[1]
    fi = 2
    if "-frame" in sys.argv:
        fi = int(sys.argv[sys.argv.index("-frame") + 1])
    frames = load(path)
    serial, buf, nd = frames[fi]
    tex0 = ps = rt0 = 0
    groups = defaultdict(lambda: {"draws": 0, "verts": 0, "z": [1e9, -1e9], "rhw": [1e9, -1e9], "x": [1e9, -1e9], "y": [1e9, -1e9],
                                  "first": None, "last": None})
    nup = nrhw = 0
    di = 0
    for off, op, p in walk(buf):
        if op == OP["TEX"]:
            st, ptr = struct.unpack_from("<II", p, 0)
            if st == 0:
                tex0 = ptr
        elif op == OP["PS"]:
            ps = struct.unpack_from("<II", p, 0)[1]
        elif op == OP["RT"]:
            i, ptr = struct.unpack_from("<II", p, 0)
            if i == 0 and ptr:
                rt0 = ptr
        if op in (OP["DP"], OP["DIP"], OP["DPUP"], OP["DIPUP"]):
            di += 1
        if op not in (OP["DPUP"], OP["DIPUP"]):
            continue
        nup += 1
        stride, posOff, rhw, vb, t, pc, nv = upverts(op, p)
        if not rhw or posOff < 0:
            continue
        nrhw += 1
        g = groups[(rt0, tex0, ps)]
        g["draws"] += 1
        g["verts"] += nv
        g["first"] = g["first"] if g["first"] is not None else di
        g["last"] = di
        for k in range(nv):
            x, y, z, w = struct.unpack_from("<ffff", vb, k * stride + posOff)
            g["x"] = [min(g["x"][0], x), max(g["x"][1], x)]
            g["y"] = [min(g["y"][0], y), max(g["y"][1], y)]
            g["z"] = [min(g["z"][0], z), max(g["z"][1], z)]
            g["rhw"] = [min(g["rhw"][0], w), max(g["rhw"][1], w)]
    print(f"{path} frame {fi} (serial {serial}): {nd} draws, {nup} UP draws, {nrhw} screen-space (XYZRHW)")
    rows = sorted(groups.items(), key=lambda kv: -kv[1]["draws"])
    for (rt, tx, p), g in rows[: (999 if "-all" in sys.argv else 40)]:
        depth = "flat (HUD-like)" if g["z"][1] - g["z"][0] < 1e-6 and abs(g["rhw"][0] - 1) < 1e-6 and abs(g["rhw"][1] - 1) < 1e-6 else "DEPTH"
        print(f"  rt {rt:08x} tex {tx:08x} ps {p:08x}: {g['draws']:4d} draws {g['verts']:6d} verts, draws #{g['first']}-#{g['last']}, "
              f"x {g['x'][0]:7.1f}..{g['x'][1]:7.1f} y {g['y'][0]:7.1f}..{g['y'][1]:7.1f} z {g['z'][0]:.4f}..{g['z'][1]:.4f} "
              f"rhw {g['rhw'][0]:.5f}..{g['rhw'][1]:.5f}  {depth}")


if __name__ == "__main__":
    main()
