"""liveunpaired.py <dump.txt> <reg> [vs]: in the CUR frame, for every matched draw (optionally only with vertex shader
vs), whether the VSCF/PSCF upload of register <reg> live at that draw was paired ('*') or not. Shows how often a
draw read a value that the old pairing left un-blended."""
import re, sys
from collections import Counter

lines = open(sys.argv[1], encoding="latin1").read().splitlines()
reg = int(sys.argv[2])
want_vs = sys.argv[3] if len(sys.argv) > 3 else None
cur_at = next(i for i, l in enumerate(lines) if l.startswith("==== frame CUR"))
end = next(i for i in range(cur_at + 1, len(lines)) if lines[i].startswith("==== vertex"))
live = {}  # kind -> paired?
stats = Counter()
for l in lines[cur_at + 1:end]:
    m = re.match(r"^[0-9a-f]{8} (\*| ) (VSCF|PSCF)\s+(?:pos\d+/\d+ )?c(\d+) x(\d+)", l)
    if m:
        s, n = int(m.group(3)), int(m.group(4))
        if s <= reg < s + n:
            live[m.group(2)] = m.group(1) == "*"
        continue
    m = re.search(r"\[draw \d+ key \S+ vs (\S+)(.*)\]", l)
    if m:
        if want_vs and m.group(1) != want_vs:
            continue
        if "UNMATCHED" in m.group(2):
            stats["draw unmatched"] += 1
            continue
        for kind, paired in live.items():
            stats[f"{kind} c{reg} {'paired' if paired else 'NOT paired'}"] += 1
print(dict(stats))
