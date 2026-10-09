"""Hypothesis test on a dump: positional VSCF packets read by several draws (shared, e.g. camera) that were NOT
paired although the previous frame has a same-slot packet with different values (so they needed lerping)."""
import re, sys
from collections import defaultdict

path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Games\MezeLounge\mhfz-fps-dump.txt"
L = open(path, encoding="latin1").read().splitlines()
ps = next(i for i, l in enumerate(L) if l.startswith("==== frame PREV"))
cs = next(i for i, l in enumerate(L) if l.startswith("==== frame CUR"))
ce = next(i for i, l in enumerate(L) if i > cs and l.startswith("===="))

pkt = re.compile(r"^([0-9a-f]{8}) (\*| ) VSCF\s+(?:pos(\d+)/(\d+) )?c(\d+) x(\d+) :(.*)$")

def parse(lines, with_pos):
    out, owner = [], {}
    for l in lines:
        m = pkt.match(l)
        if m:
            off, star, npos, cnt, s, c, vals = m.groups()
            p = dict(star=star == "*", npos=int(npos or 0), s=int(s), c=int(c), vals=[float(v) for v in vals.split()], readers=0, line=l)
            out.append(p)
            for r in range(p["s"], p["s"] + p["c"]):
                owner[r] = p
        elif "[draw " in l:
            for p in set(id(x) for x in owner.values()):
                pass
            seen = set()
            for p in owner.values():
                if id(p) not in seen:
                    seen.add(id(p))
                    p["readers"] += 1
    return out

prev = parse(L[ps + 1:cs], False)
cur = parse(L[cs + 1:ce], True)
byslot = defaultdict(list)
for p in prev:
    byslot[(p["s"], p["c"])].append(p["vals"])

bad = []
for p in cur:
    if p["npos"] == 0 or p["star"] or p["readers"] < 2:
        continue
    cands = byslot.get((p["s"], p["c"]), [])
    if not cands:
        continue
    if any(v == p["vals"] for v in cands):
        continue  # identical packet existed: nothing to lerp
    d = min(sum(abs(a - b) for a, b in zip(v, p["vals"])) for v in cands)
    bad.append((p["readers"], d, p["line"]))
print(f"shared positional packets in CUR: {sum(1 for p in cur if p['npos'] and p['readers'] >= 2)}")
print(f"unpaired but changed (needed lerp): {len(bad)}, covering {sum(b[0] for b in bad)} draw-reads")
for r, d, l in sorted(bad, reverse=True)[:20]:
    print(f"  readers {r:5d}  nearest-diff {d:10.3f}  {l[:140]}")
