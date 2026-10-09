"""Shared helpers for static analysis of mhfo-hd.dll (x86, base 0x10000000, no ASLR).

Usage:  from mh import *   then  img.read(va, n), dis(va, n), iat['timeGetTime'], xrefs_call_iat('Sleep') ...
"""
import os, re, struct, functools
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

DLL = os.environ.get("MHF_DLL", r"C:\Games\MezeLounge\mhfo-hd.dll")

class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=False)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = open(path, "rb").read()
        self.secs = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\0").decode("latin1")
            self.secs.append((name, self.base + s.VirtualAddress, s.Misc_VirtualSize,
                              s.PointerToRawData, s.SizeOfRawData, s.Characteristics))

    def off(self, va):
        for name, sva, vsz, raw, rsz, ch in self.secs:
            if sva <= va < sva + max(vsz, rsz):
                d = va - sva
                return raw + d if d < rsz else None
        return None

    def read(self, va, n):
        o = self.off(va)
        if o is None:
            return b"\0" * n
        return self.data[o:o + n]

    def u32(self, va): return struct.unpack("<I", self.read(va, 4))[0]
    def f32(self, va): return struct.unpack("<f", self.read(va, 4))[0]
    def f64(self, va): return struct.unpack("<d", self.read(va, 8))[0]

    def sec(self, name):
        for s in self.secs:
            if s[0] == name:
                return s
        raise KeyError(name)

    def text(self):
        name, sva, vsz, raw, rsz, ch = self.sec(".text")
        return sva, self.data[raw:raw + rsz]

img = Image(DLL)
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = False

# import address table: name -> IAT slot VA
iat = {}
for entry in img.pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        nm = imp.name.decode() if imp.name else f"{entry.dll.decode()}#{imp.ordinal}"
        iat[nm] = imp.address
iat_rev = {v: k for k, v in iat.items()}

TEXT_VA, TEXT = img.text()

def find_all(pat: bytes, data=None, base=None):
    data = TEXT if data is None else data
    base = TEXT_VA if base is None else base
    i = data.find(pat)
    while i != -1:
        yield base + i
        i = data.find(pat, i + 1)

def xrefs_call_iat(name):
    """call dword ptr [IAT] (FF 15) and mov reg,[IAT] (8B 05/0D/15/1D/35/3D) sites."""
    slot = struct.pack("<I", iat[name])
    out = []
    for va in find_all(slot):
        b = img.read(va - 2, 2)
        if b == b"\xff\x15":
            out.append(("call", va - 2))
        elif b[0] == 0x8B and (b[1] & 0xC7) == 0x05:
            out.append(("mov", va - 2))
        elif b[1] == 0xA1:
            out.append(("mov eax", va - 1))
        else:
            out.append(("ref", va))
    return out

def fmt_ins(i):
    s = f"{i.address:08x}: {i.bytes.hex():<20} {i.mnemonic} {i.op_str}"
    m = re.search(r"\[0x([0-9a-f]+)\]", i.op_str)
    if m and int(m.group(1), 16) in iat_rev:
        s += f"    ; {iat_rev[int(m.group(1), 16)]}"
    return s

def dis(va, n=0x80, show=True):
    out = []
    for i in md.disasm(img.read(va, n), va):
        out.append(fmt_ins(i))
    if show:
        print("\n".join(out))
    return out

def func_start(va, maxback=0x4000):
    """Heuristic: walk back to the nearest preceding int3/padding boundary followed by a plausible prologue."""
    lo = max(TEXT_VA, va - maxback)
    blob = img.read(lo, va - lo)
    for k in range(len(blob) - 1, 0, -1):
        if blob[k - 1] in (0xCC, 0xC3) and blob[k] in (0x55, 0x83, 0x81, 0x53, 0x56, 0x57, 0x6A, 0x8B, 0x51, 0xA1, 0x64, 0x68, 0xB8, 0xD9):
            if blob[k - 1] == 0xCC or (k >= 2 and blob[k - 2] == 0xCC):
                return lo + k
            if blob[k - 1] == 0xC3:
                return lo + k
    return None

@functools.lru_cache(None)
def call_targets():
    """Map target -> list of call sites for direct E8 calls in .text (rel32)."""
    from collections import defaultdict
    d = defaultdict(list)
    t = TEXT
    i = t.find(b"\xe8")
    n = len(t)
    end = TEXT_VA + n
    while i != -1:
        if i + 5 <= n:
            rel = struct.unpack_from("<i", t, i + 1)[0]
            tgt = TEXT_VA + i + 5 + rel
            if TEXT_VA <= tgt < end:
                d[tgt].append(TEXT_VA + i)
        i = t.find(b"\xe8", i + 1)
    return d

def callers(va):
    return call_targets().get(va, [])

def func_dis(va, maxlen=0x3000, show=True):
    """Linear disassembly from va until a ret followed by int3 padding (rough)."""
    out = []
    buf = img.read(va, maxlen)
    for i in md.disasm(buf, va):
        out.append(fmt_ins(i))
        if i.mnemonic in ("ret", "jmp") and i.address + i.size - va < maxlen:
            nxt = img.read(i.address + i.size, 2)
            if nxt == b"\xcc\xcc" or (i.mnemonic == "ret" and nxt[:1] == b"\xcc"):
                break
    if show:
        print("\n".join(out))
    return out
