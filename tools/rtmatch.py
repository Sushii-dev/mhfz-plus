"""rtmatch.py <dump.txt> [rt]: which render target each draw of the CUR frame renders to, and the render target of the
PREV-frame draw it was matched with. Lists RTs with draws matched to draws on a different RT (cross-pass pairing)."""
import re, sys
from collections import Counter, defaultdict

lines = open(sys.argv[1], encoding="latin1").read().splitlines()
cur_at = next(i for i, l in enumerate(lines) if l.startswith("==== frame CUR"))
end = next(i for i in range(cur_at + 1, len(lines)) if lines[i].startswith("==== vertex"))

def draws(lo, hi):
    rt, out = None, {}
    for i in range(lo, hi):
        l = lines[i]
        m = re.match(r"^[0-9a-f]{8} . RT\s+0 (\S+)", l)
        if m:
            rt = m.group(1)
        m = re.search(r"\[draw (\d+) key (\S+) vs (\S+)(.*)\]", l)
        if m:
            pm = re.search(r"<- prev (\d+)", m.group(4))
            out[int(m.group(1))] = (rt, m.group(2), m.group(3), int(pm.group(1)) if pm else None, "UNMATCHED" in m.group(4), l.split()[1] if l[9] != "*" else l.split()[2])
    return out

prev, cur = draws(1, cur_at), draws(cur_at + 1, end)
per = defaultdict(Counter)
for k, (rt, key, vs, pm, unm, op) in cur.items():
    if pm is None:
        per[rt]["unmatched"] += 1
    elif prev.get(pm, (None,))[0] == rt:
        per[rt]["same rt"] += 1
    else:
        per[rt]["OTHER rt " + str(prev.get(pm, (None,))[0])] += 1
first = {}
for k, (rt, *_r) in sorted(cur.items()):
    first.setdefault(rt, k)
for rt in sorted(per, key=lambda r: first[r]):
    print(f"rt {rt} (first draw {first[rt]}): {dict(per[rt])}")
if len(sys.argv) > 2:
    for k, v in sorted(cur.items()):
        if v[0] == sys.argv[2]:
            pm = v[3]
            print(f"  cur {k} vs {v[2]} key {v[1]} -> prev {pm} (rt {prev[pm][0] if pm is not None and pm in prev else '-'}, vs {prev[pm][2] if pm is not None and pm in prev else '-'})")
