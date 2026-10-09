"""Minimal PE inspector: arch, timestamp, sections (+entropy), imports, exports, protector hints, timing APIs."""
import collections, datetime, math, re, struct, sys

def entropy(b):
    if not b:
        return 0.0
    c = collections.Counter(b)
    return -sum(v / len(b) * math.log2(v / len(b)) for v in c.values())

def cstr(d, o):
    e = d.find(b"\0", o)
    return d[o:e].decode("latin1")

for path in sys.argv[1:]:
    d = open(path, "rb").read()
    if d[:2] != b"MZ":
        print(f"== {path}: not a PE (first bytes {d[:8].hex()}, entropy {entropy(d[:1<<20]):.2f})")
        continue
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        print(f"== {path}: MZ but no PE signature"); continue
    machine, nsec, ts, _, _, optsz, chars = struct.unpack_from("<HHIIIHH", d, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    is64 = magic == 0x20B
    entry = struct.unpack_from("<I", d, opt + 16)[0]
    base = struct.unpack_from("<Q" if is64 else "<I", d, opt + (24 if is64 else 28))[0]
    sub = struct.unpack_from("<H", d, opt + 68)[0]
    dllch = struct.unpack_from("<H", d, opt + 70)[0]
    ddir = opt + (112 if is64 else 96)
    dirs = [struct.unpack_from("<II", d, ddir + 8 * i) for i in range(16)]
    secs = []
    so = opt + optsz
    for i in range(nsec):
        name, vs, va, rs, ra = struct.unpack_from("<8sIIII", d, so + 40 * i)
        secs.append((name.rstrip(b"\0").decode("latin1"), va, vs, ra, rs))
    def rva2off(r):
        for n, va, vs, ra, rs in secs:
            if va <= r < va + max(vs, rs):
                return r - va + ra
        return None
    print(f"== {path}")
    print(f"   {'x64' if is64 else 'x86 (32-bit)'} machine={machine:#x} built={datetime.datetime.utcfromtimestamp(ts):%Y-%m-%d} "
          f"{'DLL' if chars & 0x2000 else 'EXE'} base={base:#x} entry={entry:#x} subsystem={sub} "
          f"ASLR={'yes' if dllch & 0x40 else 'no'} signed={'yes' if dirs[4][1] else 'no'} TLS={'yes' if dirs[9][1] else 'no'}")
    for n, va, vs, ra, rs in secs:
        print(f"   section {n:8} va={va:#09x} vsize={vs:>9} raw={rs:>9} entropy={entropy(d[ra:ra + rs]):.2f}")
    imp = rva2off(dirs[1][0]) if dirs[1][0] else None
    if imp:
        dlls = []
        i = imp
        while True:
            oft, _, _, nm, ft = struct.unpack_from("<IIIII", d, i)
            if nm == 0:
                break
            name = cstr(d, rva2off(nm))
            funcs = []
            t = rva2off(oft or ft)
            while t is not None:
                v = struct.unpack_from("<Q" if is64 else "<I", d, t)[0]
                if v == 0:
                    break
                if not (v >> (63 if is64 else 31)):
                    funcs.append(cstr(d, rva2off(v & 0x7FFFFFFF) + 2))
                t += 8 if is64 else 4
            dlls.append((name, funcs))
            i += 20
        print("   imports:", ", ".join(f"{n}({len(f)})" for n, f in dlls))
        interesting = re.compile(r"Sleep|timeBeginPeriod|timeGetTime|QueryPerformance|GetTickCount|Direct3DCreate9|"
                                 r"DirectInput|CreateThread|IsDebuggerPresent|VirtualProtect|LoadLibrary|GetProcAddress|zmq_|EVP_|ECDH")
        hits = sorted({f for _, fs in dlls for f in fs if interesting.search(f)})
        print("   notable imports:", ", ".join(hits[:40]))
    if dirs[0][0]:
        eo = rva2off(dirs[0][0])
        nfun, nnames = struct.unpack_from("<II", d, eo + 20)
        names_rva = struct.unpack_from("<I", d, eo + 32)[0]
        no = rva2off(names_rva)
        names = [cstr(d, rva2off(struct.unpack_from("<I", d, no + 4 * k)[0])) for k in range(min(nnames, 15))]
        print(f"   exports: {nfun} ({', '.join(names)})")
    for marker in (b"VMProtect", b".vmp0", b"Themida", b"WinLicense", b"Enigma", b"ASProtect", b"UPX", b"PECompact",
                   b"MPRESS", b"Obsidium", b"Denuvo", b"GameGuard", b"nProtect", b"XIGNCODE", b"HackShield"):
        if marker in d:
            print("   marker found:", marker.decode())
