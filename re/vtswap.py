"""Find code that writes the D3D device's vtable pointer (anti-hook vtable copy)."""
import re, struct
from mh import *

DEV = 0x1e811a3c
hits = []
for r in find_all(struct.pack("<I", DEV)):
    ins = list(md.disasm(img.read(r - 2, 0x80), r - 2))
    regs = set()
    for i in ins:
        if i.address < r - 1:
            continue
        m = re.match(r"(e\w\w), dword ptr \[0x1e811a3c\]", i.op_str)
        if i.mnemonic == "mov" and m:
            regs.add(m.group(1))
            continue
        m = re.match(r"dword ptr \[(e\w\w)\], (e\w\w)", i.op_str)
        if i.mnemonic == "mov" and m and m.group(1) in regs:
            hits.append((r, i.address, i.op_str))
        if i.mnemonic in ("ret", "call") and i.address > r + 0x40:
            break
for h in hits:
    print(hex(h[0]), hex(h[1]), h[2], "func", hex(func_start(h[1]) or 0))
# also: rep movsd with ecx=0x77 (119 entries) or memcpy size 0x1dc
for pat in (b"\xb9\x77\x00\x00\x00", b"\x68\xdc\x01\x00\x00", b"\xb9\xdc\x01\x00\x00"):
    for a in find_all(pat):
        print("size-pattern", pat.hex(), hex(a), "func", hex(func_start(a) or 0))
