"""peek.py <pid> <hexaddr> [count]: read dwords from a running 32-bit process."""
import ctypes, struct, sys

k = ctypes.WinDLL("kernel32", use_last_error=True)
h = k.OpenProcess(0x0410, False, int(sys.argv[1]))
a = int(sys.argv[2], 16)
n = int(sys.argv[3]) if len(sys.argv) > 3 else 4
b = ctypes.create_string_buffer(4 * n)
r = ctypes.c_size_t()
if not k.ReadProcessMemory(h, ctypes.c_void_p(a), b, 4 * n, ctypes.byref(r)):
    sys.exit(f"read failed {ctypes.get_last_error()}")
for i in range(n):
    print(f"{a + 4 * i:08x}: {struct.unpack_from('<I', b.raw, 4 * i)[0]:08x}")
