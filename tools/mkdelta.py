"""mkdelta.py <originals dir> <patched dir> <out dir>: writes the English patch as differences to the original game
files - out\manifest.txt plus one .delta per file - so the package holds no game files and only applies to the
exact originals it was made from. Every delta is decoded again and checked before it is written.

manifest.txt: rel|origSize|origSha256|newSize|newSha256|deltaFile
delta: "MHFD" u32 version(1) u32 origSize u32 newSize, then ops: 1 COPY u32 offset u32 length | 2 ADD u32 length
bytes | 0 END
"""
import hashlib, os, struct, sys

B = 32      # match block
STEP = 8    # original indexed every STEP bytes

def make_delta(orig, new):
    index = {}
    for i in range(0, len(orig) - B + 1, STEP):
        index.setdefault(orig[i:i + B], i)
    ops = []
    lit = bytearray()
    j, n, no = 0, len(new), len(orig)

    def flush():
        if lit:
            ops.append((2, bytes(lit)))
            lit.clear()

    while j < n:
        i = index.get(new[j:j + B]) if j + B <= n else None
        if i is None:
            lit.append(new[j])
            j += 1
            continue
        # extend the match backwards into pending literal bytes
        back = 0
        while back < len(lit) and i - back - 1 >= 0 and new[j - back - 1] == orig[i - back - 1]:
            back += 1
        if back:
            del lit[len(lit) - back:]
            i -= back
            j -= back
        # extend forwards, in chunks first
        k = 0
        while True:
            c = 4096
            if j + k + c <= n and i + k + c <= no and new[j + k:j + k + c] == orig[i + k:i + k + c]:
                k += c
                continue
            break
        while j + k < n and i + k < no and new[j + k] == orig[i + k]:
            k += 1
        flush()
        ops.append((1, i, k))
        j += k
    flush()
    out = bytearray(b"MHFD" + struct.pack("<III", 1, len(orig), len(new)))
    for op in ops:
        if op[0] == 1:
            out += struct.pack("<BII", 1, op[1], op[2])
        else:
            out += struct.pack("<BI", 2, len(op[1])) + op[1]
    out += b"\0"
    return bytes(out)

def apply_delta(orig, delta):
    assert delta[:4] == b"MHFD"
    ver, osz, nsz = struct.unpack_from("<III", delta, 4)
    assert ver == 1 and osz == len(orig)
    p, out = 16, bytearray()
    while p < len(delta):
        op = delta[p]
        p += 1
        if op == 0:
            break
        if op == 1:
            a, b = struct.unpack_from("<II", delta, p)
            p += 8
            out += orig[a:a + b]
        elif op == 2:
            (a,) = struct.unpack_from("<I", delta, p)
            p += 4
            out += delta[p:p + a]
            p += a
        else:
            raise ValueError("bad op")
    assert len(out) == nsz
    return bytes(out)

def main():
    src, pat, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    lines = ["# rel|origSize|origSha256|newSize|newSha256|deltaFile"]
    total_new = total_delta = 0
    for root, _, files in os.walk(pat):
        for name in sorted(files):
            p = os.path.join(root, name)
            rel = os.path.relpath(p, pat)
            o = os.path.join(src, rel)
            if not os.path.exists(o):
                print("NO ORIGINAL for", rel)
                sys.exit(1)
            orig, new = open(o, "rb").read(), open(p, "rb").read()
            d = make_delta(orig, new)
            assert apply_delta(orig, d) == new, rel
            dname = rel.replace("\\", "_").replace("/", "_") + ".delta"
            open(os.path.join(out, dname), "wb").write(d)
            lines.append("|".join([rel, str(len(orig)), hashlib.sha256(orig).hexdigest(), str(len(new)),
                                   hashlib.sha256(new).hexdigest(), dname]))
            total_new += len(new)
            total_delta += len(d)
            print(f"{rel:28s} {len(new):>10,d} -> delta {len(d):>9,d}")
    open(os.path.join(out, "manifest.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print(f"total {total_new:,d} bytes -> {total_delta:,d} bytes of deltas")

if __name__ == "__main__":
    main()
