#!/usr/bin/env python3
# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# Bakes the bundled example programs into flash, from programs/.
#
# These replace the examples that used to come from IRCIS-master/examples.
# Those are fine programs but they are up to 28 x 73, which on a 480 x 320
# panel means the small font and scrolling before you have understood
# anything. Everything bundled here fits 34 x 11, so the whole program is on
# screen at the large font while it runs, which is the point of the device.
#
# All of programs/ is bundled. dice never finishes -- it keeps its runners
# orbiting so the roll stays countable -- and the golden test knows that.
import os, sys

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "programs")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(__file__), "..", "lib", "program", "src", "Examples.h")

# name shown on the device -> source file
WANTED = [
    ("Hello World",   "hello.txt"),
    ("Countdown",     "countdown.txt"),
    ("Count to 20",   "count.txt"),
    ("Odd Numbers",   "odds.txt"),
    ("Even Numbers",  "evens.txt"),
    ("Threes",        "threes.txt"),
    ("Seven Times",   "times7.txt"),
    ("Squares",       "squares.txt"),
    ("Cubes",         "cubes.txt"),
    ("Doubling",      "powers2.txt"),
    ("Halving",       "halving.txt"),
    ("Running Total", "triangle.txt"),
    ("Fibonacci",     "fib.txt"),
    ("Binary",        "binary.txt"),
    ("Dice Roll",     "dice.txt"),
    ("Dumb Clock",    "clock.txt"),
    ("Coin Flip",     "coin.txt"),
    ("Lottery",       "lotto.txt"),
    ("Race",          "race.txt"),
    ("Spiral",        "spiral.txt"),
    ("Snake",         "snake.txt"),
    # Arjun Nair's own example, from IRCIS. Three runners round a track, the
    # output being the order they finish in -- and the order is random.
    ("Racetrack",     "racetrack.txt"),

    # Added later: tables and conversions, one-of-N answers, printed
    # reference cards, and a few that are only worth watching.
    ("Nine Times",          "nines.txt"),
    ("Eleven Times",        "elevens.txt"),
    ("Oblong Numbers",      "oblong.txt"),
    ("Fahrenheit",          "fahrenheit.txt"),
    ("Backwards",           "backwards.txt"),
    ("Divide 720",          "divisors.txt"),
    ("Leftovers",           "leftover.txt"),
    ("Down By Three",       "minusthree.txt"),
    ("Doubles",             "doubles.txt"),
    ("Quarters",            "quarters.txt"),
    ("Sevens Plus One",     "gaps.txt"),
    ("Halving 1024",        "halved.txt"),
    ("True Or False",       "truefalse.txt"),
    ("Morse A to M",        "morse1.txt"),
    ("Morse N to Z",        "morse2.txt"),
    ("SOS",                 "sos.txt"),
    ("Greeting",            "greeting.txt"),
    ("Advice",              "advice.txt"),
    ("Excuse",              "excuse.txt"),
    ("Motto",               "motto.txt"),
    ("Warning",             "warning.txt"),
    ("Quick Brown Fox",     "lorem.txt"),
    ("Liquor Jugs",         "pangram.txt"),
    ("Two Dimensions",      "esolang.txt"),
    ("Magic Eight Ball",    "eightball.txt"),
    ("Fortune",             "fortune.txt"),
    ("Excuse Machine",      "excuses.txt"),
    ("Weather",             "weather.txt"),
    ("Horoscope",           "horoscope.txt"),
    ("Compliment",          "compliment.txt"),
    ("Mood",                "mood.txt"),
    ("Lunch",               "lunch.txt"),
    ("Dog Name",            "dogname.txt"),
    ("Band Name",           "band.txt"),
    ("Verdict",             "verdict.txt"),
    ("More Advice",         "advice2.txt"),
    ("Staircase",           "staircase.txt"),
    ("Circuit",             "circuit.txt"),
    ("Comb",                "comb.txt"),
    ("Four Ways",           "fourways.txt"),
    ("Serpent",             "serpent.txt"),
    ("Bounce",              "bounce.txt"),
    ("Pi",                   "pi.txt"),
    ("Dumb Pi",              "dumbpi.txt"),
    ("Insult Machine",       "insult.txt"),
    ("Morse Decoder",        "morsedecode.txt"),
]

# Which folder each one is written into on the device. PROG shows these as
# folders, so they are named for whoever is scrolling, not for the filesystem.
# Anything not named here lands at the top level.
FOLDER = {}
for _fn in ("hello countdown count odds evens threes times7 nines elevens doubles "
            "quarters gaps minusthree backwards squares cubes oblong triangle "
            "powers2 halving halved divisors leftover fib fahrenheit binary").split():
    FOLDER[_fn + ".txt"] = "Counting"
for _fn in ("sos greeting lorem pangram esolang advice excuse motto warning "
            "morse1 morse2").split():
    FOLDER[_fn + ".txt"] = "Talking"
for _fn in ("coin truefalse lotto clock eightball fortune excuses weather "
            "horoscope compliment mood lunch verdict advice2 dogname band").split():
    FOLDER[_fn + ".txt"] = "Deciding"
for _fn in ("spiral bounce snake serpent staircase circuit comb fourways").split():
    FOLDER[_fn + ".txt"] = "Watching"
for _fn in ("insult pi racetrack dumbpi dice race morsedecode").split():
    FOLDER[_fn + ".txt"] = "Showing-off"


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')

progs = []
for name, fn in WANTED:
    p = os.path.join(SRC, fn)
    if not os.path.exists(p):
        print("  missing, skipped: %s" % p); continue
    rows = [l.rstrip("\n").rstrip("\r") for l in open(p, encoding="utf-8")]
    while rows and not rows[-1].strip():
        rows.pop()
    w = max(len(r) for r in rows)
    rows = [r.ljust(w, ".") for r in rows]
    progs.append((name, fn, rows, w, FOLDER.get(fn, "")))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("// GENERATED by tools/gen_examples.py -- do not edit by hand.\n")
    f.write("//\n")
    f.write("// The bundled programs, baked in from programs/. Nearly all are this\n")
    f.write("// project's; binary.txt is Arjun Nair's, MIT licensed and copyright\n")
    f.write("// (c) 2019 Arjun Nair -- see lib/ircis/LICENSE.\n")
    f.write("//\n")
    f.write("// Rows are padded with '.' (IRCIS blank) to a rectangle, which is what\n")
    f.write("// the interpreter's Grid expects.\n\n")
    f.write("namespace prog {\n")
    for i, (name, fn, rows, w, _d) in enumerate(progs):
        f.write("  // %s -- %d x %d\n" % (fn, len(rows), w))
        f.write("  inline const char* const kExample%d[] = {\n" % i)
        for r in rows:
            f.write('    "%s",\n' % esc(r))
        f.write("  };\n")
    f.write("\n  // folder is where the program is written on the device -- one\n")
    f.write("  // level, empty for the top of the list.\n")
    f.write("  struct ExampleDef { const char* name; const char* folder; const char* const* rows; int rows_n; int cols_n; };\n")
    f.write("  inline const ExampleDef kExamples[] = {\n")
    for i, (name, fn, rows, w, _d) in enumerate(progs):
        f.write('    { "%s", "%s", kExample%d, %d, %d },\n' % (esc(name), esc(_d), i, len(rows), w))
    f.write("  };\n")
    f.write("  inline constexpr int kExampleCount = %d;\n" % len(progs))
    f.write("  inline constexpr int kExampleMaxRows = %d;\n" % max(len(r) for _,_,r,_,_d in progs))
    f.write("  inline constexpr int kExampleMaxCols = %d;\n" % max(w for _,_,_,w,_d in progs))
    f.write("}\n")

print("wrote %s: %d programs, largest %d x %d" % (
    OUT, len(progs), max(len(r) for _,_,r,_,_d in progs), max(w for _,_,_,w,_d in progs)))
