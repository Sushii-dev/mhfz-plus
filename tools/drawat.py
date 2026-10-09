"""drawat.py <dump.txt> <cur|prev> <hex offset> [context lines]: the draw a command offset belongs to (the next draw
line at or after it) in that frame of a dump, with the commands of its segment."""
import re, sys

lines = open(sys.argv[1], encoding="latin1").read().splitlines()
cur_at = next(i for i, l in enumerate(lines) if l.startswith("==== frame CUR"))
end = next(i for i in range(cur_at + 1, len(lines)) if lines[i].startswith("==== vertex"))
lo, hi = (cur_at + 1, end) if sys.argv[2] == "cur" else (1, cur_at)
off = int(sys.argv[3], 16)
ctx = int(sys.argv[4]) if len(sys.argv) > 4 else 0
cmd = re.compile(r"^([0-9a-f]{8}) ")
for i in range(lo, hi):
    m = cmd.match(lines[i])
    if m and int(m.group(1), 16) >= off and "[draw" in lines[i]:
        for j in range(max(lo, i - ctx), i):
            print("   ", lines[j][:140])
        print(lines[i][:200])
        break
