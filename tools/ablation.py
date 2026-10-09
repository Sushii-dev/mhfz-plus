"""ablation.py <mhfz-fps.log>: summarise the self-check ablation lines. For each mechanism switched off: how the mean
error, the brightness bias and the counts of too-bright / too-dark blocks change against the base prediction."""
import re, sys
from collections import defaultdict

pat = re.compile(r"selfcheck #(\d+) ablation \(mean error, bias, blocks brighter/darker\): (.*)$")
part = re.compile(r"([\w-]+) ([\d.]+) ([+-][\d.]+) (\d+)/(\d+)")
rows = []
for line in open(sys.argv[1], encoding="latin1"):
    m = pat.search(line)
    if not m:
        continue
    vals = {}
    for seg in m.group(2).split(" | "):
        p = part.search(seg)
        if p:
            vals[p.group(1)] = tuple(float(p.group(i)) for i in range(2, 6))
    if "base" in vals:
        rows.append((int(m.group(1)), vals))
print(f"{len(rows)} ablation runs")
sel = [r for r in rows if r[1]["base"][2] >= int(sys.argv[2] if len(sys.argv) > 2 else 3)]
print(f"{len(sel)} with >= {sys.argv[2] if len(sys.argv) > 2 else 3} too-bright blocks in the base prediction")
agg = defaultdict(lambda: [0.0, 0.0, 0.0, 0.0, 0, 0])
for _, v in sel:
    b = v["base"]
    for k, x in v.items():
        if k == "base":
            continue
        a = agg[k]
        a[0] += x[0] - b[0]
        a[1] += x[1] - b[1]
        a[2] += x[2] - b[2]
        a[3] += x[3] - b[3]
        a[4] += 1
        a[5] += x[2] < b[2] * 0.5
print(f"{'switched off':20s} {'d mean':>8s} {'d bias':>8s} {'d bright':>9s} {'d dark':>8s}  bright halved in")
for k, a in sorted(agg.items(), key=lambda kv: kv[1][2] / max(kv[1][4], 1)):
    n = max(a[4], 1)
    print(f"{k:20s} {a[0] / n:+8.2f} {a[1] / n:+8.2f} {a[2] / n:+9.1f} {a[3] / n:+8.1f}  {a[5]}/{a[4]}")
print("examples (base bright/dark):", [(i, v["base"][2], v["base"][3]) for i, v in sel[:10]])
