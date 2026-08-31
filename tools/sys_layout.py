#!/usr/bin/env python3
# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# The SYS page places its tiles one by one, so two can silently claim the same
# cell: the hit test finds the first and the second becomes unreachable, which
# is exactly what happened to UNDER GRID. This reads the tile definitions out
# of Ui.cpp and prints the locked and unlocked pages, failing on a collision
# or a hole.
#
# Tiles sit in one of two blocks. sysTile(row, col) is the settings grid,
# measured down from the top; sysFootTile(rowUp, col) is the About and
# erase/power block, measured UP from the bottom. The two cannot overlap.
#
#   tools/sys_layout.py
import re, sys, os

src = open(os.path.join(os.path.dirname(__file__), '..', 'src', 'Ui.cpp')).read()
block = src[src.index("Btn btnSysWifi()"):src.index("void drawSys()")]
defs = re.findall(r'Btn (btnSys\w+)\(\)\s*\{(.*?)\}', block, re.S)

UNLOCKED_ONLY = ('btnSysInfo', 'btnSysExit')
locked, unlocked = {}, {}

for name, body in defs:
    slots = ([("grid",) + tuple(map(int, t))
              for t in re.findall(r'\bsysTile\((\d+),\s*(\d+)', body)] +
             [("foot",) + tuple(map(int, t))
              for t in re.findall(r'sysFootTile\((\d+),\s*(\d+)', body)])
    if not slots:
        continue
    if 'Store::unlocked()' in body and len(slots) == 2:
        unlocked.setdefault(slots[0], []).append(name)
        locked.setdefault(slots[1], []).append(name)
    else:
        unlocked.setdefault(slots[0], []).append(name)
        if name not in UNLOCKED_ONLY:
            locked.setdefault(slots[0], []).append(name)

bad = 0
for label, grid in (("locked", locked), ("unlocked", unlocked)):
    print("%s:" % label)
    for kind, title in (("grid", "  settings, from the top"),
                        ("foot", "  foot, up from the bottom")):
        cells = {k[1:]: v for k, v in grid.items() if k[0] == kind}
        if not cells:
            continue
        print(title)
        rows = max(r for r, _ in cells) + 1
        for r in range(rows):
            for c in (0, 1):
                names = cells.get((r, c), [])
                if len(names) > 1:
                    print("    (%d,%d)  %s   <-- COLLISION" % (r, c, ", ".join(names)))
                    bad += 1
                elif not names:
                    # the row furthest from the anchor may be half empty
                    if r < rows - 1:
                        print("    (%d,%d)  -- HOLE --" % (r, c))
                        bad += 1
                else:
                    print("    (%d,%d)  %s" % (r, c, names[0]))
    print()

print("collisions and holes: %d" % bad)
sys.exit(1 if bad else 0)
