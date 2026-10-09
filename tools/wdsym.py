# Resolves the mod offsets in log lines ("d3d9.dll+0x39e57": watchdog, camera probe, exceptions) to function names with the link map of the
# same build (build\d3d9.map, or the d3d9.<name>.map kept next to a deployed build).
#   python tools\wdsym.py <log or map...>      e.g.  python tools\wdsym.py mhfz-fps.log build\d3d9.map
import re
import sys

logs = [a for a in sys.argv[1:] if not a.lower().endswith(".map")]
maps = [a for a in sys.argv[1:] if a.lower().endswith(".map")] or [__file__.rsplit("tools", 1)[0] + r"build\d3d9.map"]
syms = []
for line in open(maps[0], encoding="utf-8", errors="replace"):
    m = re.match(r"\s*([0-9a-f]{8})\s+[0-9a-f]{8}\s+\d+\s+(\S.*)$", line)
    if m and not m.group(2).startswith("<"):
        syms.append((int(m.group(1), 16), m.group(2).strip()))
syms.sort()


def name(off):
    lo, hi, best = 0, len(syms) - 1, None
    while lo <= hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= off:
            best, lo = syms[mid], mid + 1
        else:
            hi = mid - 1
    return f"{best[1]}+{off - best[0]:#x}" if best else "?"


for path in logs:
    for line in open(path, encoding="utf-8", errors="replace"):
        if "d3d9.dll+0x" in line:  # (watchdog, camera probe, exception lines; the backend's frames say "d3d9.dll[backend]")
            print(line.rstrip())
            for off in re.findall(r"d3d9\.dll\+0x([0-9a-f]+)", line):
                print(f"    d3d9.dll+0x{off} = {name(int(off, 16))}")
