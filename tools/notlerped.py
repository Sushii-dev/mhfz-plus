"""From mhfz-fps-dump.txt: which draws in the CUR frame were not interpolated, grouped by render target and shader."""
import re, sys
from collections import Counter, defaultdict

path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Games\MezeLounge\mhfz-fps-dump.txt"
lines = open(path, encoding="latin1").read().splitlines()
s = next(i for i, l in enumerate(lines) if l.startswith("==== frame CUR"))
e = next(i for i, l in enumerate(lines) if i > s and l.startswith("===="))
cur = lines[s + 1:e]

rt = vs = ps = "?"
seg = []
by = Counter()
tot = Counter()
examples = defaultdict(list)
for l in cur:
    parts = l.split()
    op = parts[2] if len(parts) > 2 and parts[1] == "*" else (parts[1] if len(parts) > 1 else "")
    if op == "RT" and " RT        0 " in l:
        rt = parts[-1]
    elif op == "VS":
        vs = parts[-1]
    elif op == "PS":
        ps = parts[-1]
    seg.append(l)
    if "[draw " in l:
        kind = op
        k = (rt, vs, kind)
        tot[k] += 1
        if "NOT-LERPED" in l:
            by[k] += 1
            if len(examples[k]) < 1:
                examples[k].append(seg[-14:])
        seg = []

print(f"draws: {sum(tot.values())}, not lerped: {sum(by.values())}")
print("not-lerped by (rendertarget, vertexshader, draw kind):  notlerped/total")
for k, v in by.most_common(25):
    print(f"  rt {k[0]} vs {k[1]} {k[2]:6s}  {v}/{tot[k]}")
print()
for k, v in by.most_common(6):
    print(f"--- example segment for rt {k[0]} vs {k[1]} {k[2]}")
    for l in examples[k][0]:
        print("   ", l[:170])
