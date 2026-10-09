"""Disassembles one function of a profiling build with the sampling profiler's counts (self samples per 16 bytes), to
find the hot loop inside a function that inlines a lot.

    python tools/profasm.py build/prof pairbench.map pairbench.exe "SegmentA(" [min_samples]
"""
import bisect
import os
import re
import struct
import sys

import capstone

d, mapname, exename, want = sys.argv[1:5]
minS = int(sys.argv[5]) if len(sys.argv) > 5 else 1
raw = open(os.path.join(d, "mhfz-fps-prof.bin"), "rb").read()
self_h = struct.unpack_from("<65536I", raw, 4)
syms = []
for line in open(os.path.join(d, mapname), encoding="utf-8", errors="replace"):
    m = re.match(r"([0-9a-f]{8})\s+([0-9a-f]{8})\s+(\d+)\s+(.*)$", line)
    if not m:
        continue
    name = m.group(4).strip()
    if name.startswith(".") or ":(" in name or name.endswith(".o") or not name:
        continue
    syms.append((int(m.group(1), 16), name))
syms.sort()
starts = [a for a, _ in syms]
hit = [k for k, (a, n) in enumerate(syms) if n.startswith(want) or want in n]
if not hit:
    sys.exit("no symbol matches " + want)
k = hit[0]
beg = syms[k][0]
end = next((a for a, _ in syms[k + 1:] if a > beg), beg + 0x1000)
print(f"{syms[k][1][:120]}: rva {beg:#x}-{end:#x} ({end - beg} bytes)")

pe = open(os.path.join(d, exename), "rb").read()
lfanew = struct.unpack_from("<I", pe, 0x3C)[0]
nsec = struct.unpack_from("<H", pe, lfanew + 6)[0]
optsz = struct.unpack_from("<H", pe, lfanew + 20)[0]
image_base = struct.unpack_from("<I", pe, lfanew + 24 + 28)[0]
sec = lfanew + 24 + optsz
code = None
for s in range(nsec):
    o = sec + s * 40
    va, vsz, rawp = struct.unpack_from("<I", pe, o + 12)[0], struct.unpack_from("<I", pe, o + 8)[0], struct.unpack_from("<I", pe, o + 20)[0]
    if va <= beg < va + vsz:
        code = pe[rawp + (beg - va):rawp + (end - va)]
if code is None:
    sys.exit("section not found")
tot = sum(self_h[(beg >> 4) + i] for i in range((end - beg + 15) >> 4))
print(f"{tot} self samples")
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
lastb = -1
for ins in md.disasm(code, image_base + beg):
    rva = ins.address - image_base
    b = rva >> 4
    n = self_h[b & 0xFFFF] if b != lastb else 0
    lastb = b
    tag = ""
    if ins.mnemonic == "call":
        try:
            t = int(ins.op_str, 16) - image_base
            j = bisect.bisect_right(starts, t) - 1
            tag = "  ; " + syms[j][1][:80]
        except ValueError:
            pass
    if n >= minS or ins.mnemonic in ("call",) or tag:
        print(f"{n:6d}  {rva:08x}  {ins.mnemonic:8s} {ins.op_str}{tag}")
