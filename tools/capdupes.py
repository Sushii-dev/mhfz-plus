"""Identical strings in a pairing capture's frame: how many screen strings (runs of glyph quads) share their glyph
sequence (colour excluded: a label and its shadow count once per place) with another string elsewhere on the screen.
Copies of one text make the text matcher's choice of counterpart a guess.

    python tools/capdupes.py cap_*.bin
"""
import sys
from collections import defaultdict

sys.path.insert(0, __import__("os").path.dirname(__file__))
from capup import load  # noqa: E402
import capruns  # noqa: E402


def glyph(b):
    """the glyph without its colour (texcoords etc.: the vertex bytes after the 4-byte colour that follows the position)"""
    return b[4][4:] if len(b[4]) > 4 else b[4]


for path in sys.argv[1:]:
    frames = load(path)
    serial, buf, nd = frames[2]
    q = capruns.quads(buf)
    rs = capruns.runs(q)
    groups = defaultdict(list)
    for r in rs:
        key = tuple(glyph(q[d]) for d in r)
        groups[key].append((q[r[0]][0], q[r[0]][1], len(r)))
    dup_strings, dup_texts = 0, 0
    examples = []
    for key, places in groups.items():
        # places closer than 4 px are the same label (text and shadow)
        distinct = []
        for (x, y, n) in places:
            if all(abs(x - a) + abs(y - b) > 4 for (a, b, _) in distinct):
                distinct.append((x, y, n))
        if len(distinct) > 1:
            dup_texts += 1
            dup_strings += len(distinct)
            if len(examples) < 4:
                examples.append(f"{len(distinct)} x {distinct[0][2]} glyphs at " + ", ".join(f"{a:.0f},{b:.0f}" for (a, b, _) in distinct[:5]))
    print(f"{path.split(chr(92))[-1]}: {len(rs)} strings, {dup_texts} texts appear at more than one place ({dup_strings} strings)")
    for e in examples:
        print("   ", e)
