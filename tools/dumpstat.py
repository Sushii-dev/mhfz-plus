"""Analyse mhfz-fps-dump.txt: for positional VSCF packets in the CUR frame, how many draws read them and whether they were paired."""
import re, sys
from collections import Counter

path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Games\MezeLounge\mhfz-fps-dump.txt"
lines = open(path, encoding="latin1").read().splitlines()
start = next(i for i, l in enumerate(lines) if l.startswith("==== frame CUR"))
end = next(i for i, l in enumerate(lines) if i > start and l.startswith("===="))
cur = lines[start + 1:end]

pkt_re = re.compile(r"^([0-9a-f]{8}) (\*| ) VSCF\s+pos(\d+)/(\d+) c(\d+) x(\d+)")
owner = {}            # register -> packet id
pkts = []             # dicts
draws_unmatched_seg = 0
for l in cur:
    m = pkt_re.match(l)
    if m:
        off, star, npos, cnt, s, c = m.groups()
        p = dict(off=off, paired=star == "*", npos=int(npos), start=int(s), count=int(c), readers=0, line=l)
        pkts.append(p)
        for r in range(p["start"], p["start"] + p["count"]):
            owner[r] = len(pkts) - 1
        continue
    if " DIP " in l or " DP " in l or " DPUP " in l or " DIPUP " in l:
        for pid in set(owner.values()):
            pkts[pid]["readers"] += 1  # rough: every live packet counts as read by this draw
pos = [p for p in pkts if p["npos"] > 0]
print("positional packets:", len(pos), " paired:", sum(p["paired"] for p in pos))
buckets = Counter()
for p in pos:
    k = ("paired" if p["paired"] else "unpaired", "1 draw" if p["readers"] <= 1 else "2-9 draws" if p["readers"] < 10 else "10+ draws")
    buckets[k] += 1
for k, v in sorted(buckets.items()):
    print(f"  {k[0]:9s} {k[1]:10s} {v}")
print("\nunpaired positional packets read by 10+ draws (first 25):")
n = 0
for p in pos:
    if not p["paired"] and p["readers"] >= 10:
        print(f"  readers={p['readers']:5d}  {p['line'][:150]}")
        n += 1
        if n >= 25:
            break
