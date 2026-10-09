"""Reads build/prof/mhfz-fps-prof.bin (sampling profiler, build.ps1 -Profile) with the link map and prints where
BuildPairs spends its time: self samples per function and inclusive samples (function on the sampled stack).

    python tools/prof.py [dir] [map]   (default build/prof, d3d9.map; the offline bench: build/prof pairbench.map)
"""
import bisect
import os
import re
import struct
import sys

d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "prof")
raw = open(os.path.join(d, "mhfz-fps-prof.bin"), "rb").read()
base = struct.unpack_from("<I", raw, 0)[0]
self_h = struct.unpack_from("<65536I", raw, 4)
incl_h = struct.unpack_from("<65536I", raw, 4 + 65536 * 4)

# ld.lld PE map: "Address  Size     Align Out     In      Symbol" (addresses are RVAs)
syms = []
image_base = None
for line in open(os.path.join(d, sys.argv[2] if len(sys.argv) > 2 else "d3d9.map"), encoding="utf-8", errors="replace"):
    m = re.match(r"([0-9a-f]{8})\s+([0-9a-f]{8})\s+(\d+)\s+(.*)$", line)
    if not m:
        continue
    vma, size, rest = int(m.group(1), 16), int(m.group(2), 16), m.group(4)
    name = rest.strip()
    if name.startswith(".") or ":(" in name or name.endswith(".o") or not name:
        continue
    syms.append((vma, name))
syms.sort()
addrs = [a for a, _ in syms]
img = 0  # histogram buckets are RVAs too


def name_of(rva):
    a = img + rva
    k = bisect.bisect_right(addrs, a) - 1
    return syms[k][1] if k >= 0 else "?"


def table(h, title):
    tot = sum(self_h)  # samples with the instruction pointer in the mod's code
    agg = {}
    for b, n in enumerate(h):
        if n:
            nm = name_of(b << 4)
            agg[nm] = agg.get(nm, 0) + n
    print(f"== {title}: {tot} samples")
    for nm, n in sorted(agg.items(), key=lambda kv: -kv[1])[:30]:
        print(f"{100.0 * n / max(tot, 1):6.1f}%  {n:7d}  {nm[:230]}")


table(self_h, "self")
table(incl_h, "inclusive (counted once per sample)")
