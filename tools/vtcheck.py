"""Inspect the IDirect3DDevice9 vtable inside a running 32-bit game: which module each hooked slot points to."""
import ctypes, ctypes.wintypes as W, struct, sys

k = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi")
pid = int(sys.argv[1])
dev = int(sys.argv[2], 16)
h = k.OpenProcess(0x0410, False, pid)

def read(a, n=4):
    b = ctypes.create_string_buffer(n)
    r = ctypes.c_size_t()
    return b.raw if k.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(r)) else None

def u32(a):
    return struct.unpack("<I", read(a))[0]

mods = (ctypes.c_void_p * 1024)()
need = W.DWORD()
psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 1)
names = {}
for m in mods[: need.value // ctypes.sizeof(ctypes.c_void_p)]:
    if not m:
        continue
    nm = ctypes.create_unicode_buffer(260)
    psapi.GetModuleFileNameExW(h, ctypes.c_void_p(m), nm, 260)
    mi = (ctypes.c_uint32 * 3)()
    psapi.GetModuleInformation(h, ctypes.c_void_p(m), mi, 12)
    names[nm.value] = (m, mi[1])
for n, (b, s) in names.items():
    if any(x in n.lower() for x in ["d3d9", "rtss", "hook", "overlay", "windhawk", "nvd3d", "steam", "discord", "reshade", "lossless"]):
        print(hex(b), hex(s), n)

def owner(a):
    for n, (b, s) in names.items():
        if b <= a < b + s:
            return n.split("\\")[-1]
    return "?"

vt = u32(dev)
print("vtable", hex(vt), owner(vt))
for i, nm in [(16, "Reset"), (17, "Present"), (41, "BeginScene"), (42, "EndScene"), (57, "SetRenderState"),
              (82, "DrawIndexedPrimitive"), (83, "DrawPrimitiveUP"), (94, "SetVertexShaderConstantF")]:
    a = u32(vt + 4 * i)
    print(f"{nm:26s} {a:#010x} {owner(a)}  first bytes {read(a, 8).hex()}")
