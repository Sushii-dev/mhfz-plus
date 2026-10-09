"""For a self-check dump: list NOT-LERPED draws grouped by (vs, kind, matched/unmatched), with an example segment."""
import re, sys
from collections import Counter, defaultdict

path = sys.argv[1]
L = open(path, encoding="latin1").read().splitlines()
cs = next(i for i, l in enumerate(L) if l.startswith("==== frame CUR"))
ce = next(i for i, l in enumerate(L) if i > cs and l.startswith("===="))
cur = L[cs + 1:ce]
draw_re = re.compile(r"\[draw (\d+) key ([0-9a-f]+) vs ([0-9a-fA-Fx]+)(?: (NOT-LERPED))?(?: <- prev (\d+)| (UNMATCHED))?\]")
groups = Counter()
tot = Counter()
ex = defaultdict(list)
seg = []
rt = "?"
for l in cur:
    seg.append(l)
    parts = l.split()
    if len(parts) > 3 and parts[1] in ("RT", "*") and "RT" in parts[1:3] and parts[-2] == "0":
        rt = parts[-1]
    m = draw_re.search(l)
    if not m:
        continue
    di, key, vs, nl, prev, unm = m.groups()
    kind = l.split()[1] if l.split()[1] != "*" else l.split()[2]
    g = (vs, kind, "UNMATCHED" if unm else "matched")
    tot[(vs, kind)] += 1
    if nl:
        groups[g] += 1
        if len(ex[g]) < 1:
            ex[g].append(list(seg[-16:]))
    seg = []
print("NOT-LERPED draws:", sum(groups.values()), "of", sum(tot.values()))
for g, n in groups.most_common(15):
    print(f"  vs {g[0]} {g[1]:6s} {g[2]:9s} {n}/{tot[(g[0], g[1])]}")
for g, n in groups.most_common(4):
    print(f"--- example: vs {g[0]} {g[1]} {g[2]}")
    for l in ex[g][0]:
        print("   ", l[:175])
