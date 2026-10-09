"""tracejumps.py <trace.txt> [n]: per render target, the draws whose snapshot changes most between the two traced
renders (column 'interp' = 2nd vs 1st). A draw's own contribution = change since the previous draw on that target."""
import re, sys
from collections import defaultdict

row = re.compile(r"^\s*(\d+) ([0-9a-f]{8}) (\w+)\s+rt (\S+) ds \S+ vs (\S+) ps (\S+) \| ([\d.]+)/\d+ ([\d.]+)/\d+ \| ([\d.]+) ([\d.]+) \|")
last = {}
per_rt = defaultdict(list)
count = defaultdict(int)
for l in open(sys.argv[1], encoding="latin1"):
    m = row.match(l)
    if not m:
        continue
    k, rt, vs, ps = int(m.group(1)), m.group(4), m.group(5), m.group(6)
    v = float(m.group(9)) + float(m.group(10))
    jump = v - last.get(rt, 0.0)
    last[rt] = v
    count[rt] += 1
    per_rt[rt].append((jump, k, m.group(3), vs, ps))
n = int(sys.argv[2]) if len(sys.argv) > 2 else 6
for rt in sorted(per_rt, key=lambda r: -count[r]):
    js = sorted(per_rt[rt], reverse=True)[:n]
    print(f"rt {rt}: {count[rt]} draws; biggest own jumps: " + ", ".join(f"{k}({j:.1f} {op} vs {vs} ps {ps})" for j, k, op, vs, ps in js if j > 0.2))
