"""pairjumps.py <dump.txt> [n]: constant packets that are blended (paired) between the two dumped frames although their
values differ a lot (relative to their magnitude). Alternating per-tick values (double-buffered shadow maps,
atlas halves) show up here; positional packets are marked pos."""
import re, sys
from collections import Counter

lines = open(sys.argv[1], encoding="latin1").read().splitlines()
cur_at = next(i for i, l in enumerate(lines) if l.startswith("==== frame CUR"))
pairs_at = next(i for i, l in enumerate(lines) if l.startswith("==== pairs"))
cmd = re.compile(r"^([0-9a-f]{8}) (\*| ) (\w+)\s+(.*)$")

def parse(rng):
    out = {}
    for i in rng:
        m = cmd.match(lines[i])
        if m:
            out[int(m.group(1), 16)] = (m.group(3), m.group(4))
    return out

prev = parse(range(1, cur_at))
end_cur = next(i for i in range(cur_at + 1, len(lines)) if lines[i].startswith("===="))
cur = parse(range(cur_at + 1, end_cur))
vals = re.compile(r"(pos\d+/\d+ )?c(\d+) x(\d+) :((?: \S+)*)")
THR = float(sys.argv[3]) if len(sys.argv) > 3 else 0.2
hits = []
for l in lines[pairs_at + 1:]:
    m = re.match(r"([0-9a-f]{8}) <- ([0-9a-f]{8})", l)
    if not m:
        continue
    c, p = int(m.group(1), 16), int(m.group(2), 16)
    if c not in cur or p not in prev or cur[c][0] not in ("VSCF", "PSCF"):
        continue
    a, b = vals.search(cur[c][1]), vals.search(prev[p][1])
    if not a or not b:
        continue
    va = [float(x) for x in a.group(4).split()]
    vb = [float(x) for x in b.group(4).split()]
    mag = max(1e-6, max(abs(x) for x in va + vb))
    d = max(abs(x - y) for x, y in zip(va, vb))
    if d / mag > THR and not (cur[c][0] == "VSCF" and int(a.group(2)) == 5):
        hits.append((d / mag, cur[c][0], a.group(1) or "", int(a.group(2)), int(a.group(3)), c, p, va[:8], vb[:8]))
hits.sort(reverse=True)
print(f"{len(hits)} blended packets change by more than 20% of their magnitude")
print("by register:", Counter((h[1], h[3], h[4], h[2].strip()) for h in hits).most_common(15))
for h in hits[: int(sys.argv[2]) if len(sys.argv) > 2 else 15]:
    print(f"{h[0]:.2f} {h[1]} {h[2]}c{h[3]} x{h[4]} cur {h[5]:08x} prev {h[6]:08x}\n   cur  {h[7]}\n   prev {h[8]}")

