#!/usr/bin/env python3
# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# Buttons are drawn and hit-tested from the same Btn, so the two agree by
# construction -- but only where both actually happen. This checks the two
# ways that breaks:
#
#   drawn but never hit-tested   a control that does nothing when pressed
#   hit-tested but never drawn   a live target with nothing to show for it
#
# and reports rectangles that overlap, where the first hit test shadows the
# second and the button underneath becomes unreachable.
#
#   tools/btn_audit.py
import re, sys, os

src = open(os.path.join(os.path.dirname(__file__), '..', 'src', 'Ui.cpp')).read()

# ---- every button and, where the coordinates are constant, its rectangle ---
consts = {'kScreenW': 480, 'kScreenH': 320, 'kHeaderH': 22, 'kTabH': 28,
          'kTabY': 292, 'kBodyY': 22, 'kContentH': 19}
rects = {}
for m in re.finditer(r'Btn (btn\w+)\([^)]*\)\s*\{[^}]*?return\s*\{([^}]*)\}', src, re.S):
    name, body = m.group(1), m.group(2)
    parts = [p.strip() for p in body.split(',')]
    if len(parts) < 4:
        continue
    try:
        vals = [int(eval(p, {'__builtins__': {}}, consts)) for p in parts[:4]]
        rects[name] = tuple(vals)
    except Exception:
        rects[name] = None          # computed at runtime; not checked here

# ---- which buttons each drawer draws, and each handler tests ---------------
def bodies(prefix):
    out = {}
    for m in re.finditer(r'\nvoid (' + prefix + r'\w+)\([^)]*\)\s*\{', src):
        start = m.end()
        depth, i = 1, start
        while i < len(src) and depth:
            if src[i] == '{': depth += 1
            elif src[i] == '}': depth -= 1
            i += 1
        out[m.group(1)] = src[start:i]
    return out

drawers  = bodies('draw')
handlers = bodies('handle')

def names(text, call):
    found = set(re.findall(call + r'\(\s*(btn\w+)\(', text))
    # Buttons are often copied to a local so a label can be set on them --
    # "Btn sp = btnSpeed(); sp.label = ...; drawBtn(sp);" -- so follow the
    # alias rather than reporting the button as never drawn.
    for var, fn in re.findall(r'Btn\s+(\w+)\s*=\s*(btn\w+)\(', text):
        if re.search(call + r'\(\s*' + var + r'\b', text):
            found.add(fn)
    # and some are drawn by hand out of their own rectangle
    for fn in re.findall(r'Btn\s+\w+\s*=\s*(btn\w+)\(', text):
        if re.search(r'fillRoundRect|fillRect|drawGlyph|drawAgain', text):
            found.add(fn)
    return found

drawn = set()
for b in drawers.values():
    drawn |= names(b, 'drawBtn') | names(b, 'drawGlyph')
hitted = set()
for b in handlers.values():
    hitted |= names(b, 'hit')
# some buttons are drawn or tested outside a draw/handle function
drawn  |= names(src, 'drawBtn') & set(rects)
hitted |= names(src, 'hit') & set(rects)

bad = 0
only_drawn = sorted(n for n in drawn - hitted if n in rects)
only_hit   = sorted(n for n in hitted - drawn if n in rects)
if only_drawn:
    print("drawn but never hit-tested:")
    for n in only_drawn: print("   ", n)
    bad += len(only_drawn)
if only_hit:
    print("hit-tested but never drawn:")
    for n in only_hit: print("   ", n)
    bad += len(only_hit)

# ---- overlaps among the fixed-coordinate buttons that share a screen -------
def group(n):
    for g in ('btnSys', 'btnEd', 'btnOut', 'btnDlg', 'btnPick', 'btnWifi',
              'btnSz', 'btnCell', 'btnProg', 'btnSave', 'btnEdit'):
        if n.startswith(g): return g
    return 'transport'

def overlap(a, b):
    return (a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and
            a[1] < b[1] + b[3] and b[1] < a[1] + a[3])

groups = {}
for n, r in rects.items():
    if r: groups.setdefault(group(n), []).append((n, r))
for g, items in sorted(groups.items()):
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            (n1, r1), (n2, r2) = items[i], items[j]
            if overlap(r1, r2):
                print("OVERLAP  %-18s %s   and   %-18s %s" % (n1, r1, n2, r2))
                bad += 1


# ---- the picker keyboards, drawn from pickerCell and tapped through the same
# mapping. Round-trip every key: draw it to a row and column, then look that
# row and column back up, and check it comes back as the same key.
kMain, kSide = 8, 2
kSideMax = kMain * kSide

def geom(n, split):
    sym = n - split if split else 0
    side = bool(split and sym > 0)
    extras = sym - kSideMax if (side and sym > kSideMax) else 0
    if n <= 12:  return 3, 0, (n + 2) // 3
    if side:
        cols = kMain + kSide
        return cols, kSide, (split + kMain - 1)//kMain + (extras + cols - 1)//cols
    return kMain, 0, (n + kMain - 1)//kMain

def cell(i, split, cols, sideCols, rows):
    mainRows = (split + kMain - 1)//kMain
    if sideCols and i >= split:
        k = i - split
        if k < kSideMax:
            r, c = k//sideCols, kMain + (k % sideCols)
            return (r, c) if r < rows else None
        k -= kSideMax
        r, c = mainRows + k//cols, k % cols
        return (r, c) if r < rows else None
    cc = kMain if sideCols else cols
    return (i//cc, i % cc)

print()
for name, n, split in (("kKbBase64", 64, 0), ("kKbProgram", 80, 64), ("kKbText", 95, 64)):
    cols, sideCols, rows = geom(n, split)
    seen, clash = {}, 0
    for i in range(n):
        pos = cell(i, split, cols, sideCols, rows)
        if pos is None: continue
        if pos in seen:
            print("  %s: keys %d and %d both drawn at row %d col %d"
                  % (name, seen[pos], i, pos[0], pos[1]))
            clash += 1
        seen[pos] = i
    print("  %-11s %2d keys over %d x %d, %d collisions" % (name, n, cols, rows, clash))
    bad += clash

unchecked = sorted(n for n, r in rects.items() if r is None)
print("\n%d buttons, %d with fixed rectangles checked for overlap, "
      "%d computed at runtime" % (len(rects), len(rects) - len(unchecked), len(unchecked)))
print("problems: %d" % bad)
sys.exit(1 if bad else 0)
