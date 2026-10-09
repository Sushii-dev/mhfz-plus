"""Repeated meshes in the CUR frame of a dump: how many instances, how they were matched (monotonic order or shuffled)."""
import re, sys
from collections import defaultdict

path = sys.argv[1]
L = open(path, encoding="latin1").read().splitlines()
cs = next(i for i, l in enumerate(L) if l.startswith("==== frame CUR"))
ce = next(i for i, l in enumerate(L) if i > cs and l.startswith("===="))
ps = next(i for i, l in enumerate(L) if l.startswith("==== frame PREV"))
d_re = re.compile(r"(DIP\s+t\d+ base -?\d+ min \d+ nv \d+ si \d+ pc \d+)\s+\[draw (\d+) key ([0-9a-f]+) vs (\S+?)(?: NOT-LERPED)?(?: <- prev (\d+)| UNMATCHED)?\]")
groups = defaultdict(list)
for l in L[cs + 1:ce]:
    m = d_re.search(l)
    if m:
        geo, di, key, vs, prev = m.groups()
        groups[(geo, vs)].append((int(di), int(prev) if prev else None, "UNMATCHED" in l))
prevcount = defaultdict(int)
for l in L[ps + 1:cs]:
    m = d_re.search(l)
    if m:
        prevcount[(m.group(1), m.group(4))] += 1
rows = sorted(groups.items(), key=lambda kv: -len(kv[1]))
for (geo, vs), lst in rows[:15]:
    prevs = [p for _, p, _ in lst if p is not None]
    inversions = sum(1 for a, b in zip(prevs, prevs[1:]) if b < a)
    print(f"{len(lst):4d} cur / {prevcount[(geo, vs)]:4d} prev  vs {vs}  {geo}  unmatched {sum(u for *_, u in lst)}  order-inversions {inversions}")
