#!/usr/bin/env python3
# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# Runs every example in LEARN.md and checks it prints what the guide says it
# prints. A tutorial whose examples do not work is worse than no tutorial, and
# these are easy to break by editing the prose around them.
#
#   tools/check_guide.py            check LEARN.md
#   tools/check_guide.py --spare    also report arrows that do nothing
#
# The convention: a fenced block, then a line beginning "prints". If that line
# contains `backticked` values the output must match one of them exactly --
# several, for the programs that pick at random -- and otherwise the example
# only has to run without hanging.
import os, re, subprocess, sys, tempfile

ROOT = os.path.join(os.path.dirname(__file__), '..')
EMU = os.path.join(ROOT, 'host', 'sk_emu')
ARGS = [a for a in sys.argv[1:] if not a.startswith('-')]
DOC = os.path.join(ROOT, ARGS[0] if ARGS else 'LEARN.md')

def examples(text):
    # A fenced block, optionally followed by a "prints ..." line. Where the
    # output runs to more than one line it cannot sit in backticks, so
    # "prints:" on its own means the next fenced block IS the expected output.
    parts = re.split(r'```\n(.*?)```', text, flags=re.S)
    blocks, after = parts[1::2], parts[2::2]
    used = set()
    for i, body in enumerate(blocks):
        if i in used or not body.strip(): continue
        tail = after[i] if i < len(after) else ''
        if tail.strip() == 'prints:' and i + 1 < len(blocks):
            used.add(i + 1)
            yield body.rstrip('\n'), 'BLOCK:' + blocks[i + 1].rstrip('\n')
        else:
            m = re.match(r'\s*(prints[^\n]*)', tail)
            yield body.rstrip('\n'), (m.group(1).strip() if m else '')

def run(program):
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        f.write(program + '\n'); path = f.name
    try:
        p = subprocess.run([EMU, '--grid', path, '--max-steps', '200000'],
                           capture_output=True, text=True, timeout=30)
        return p.stdout.strip(), p.returncode
    except subprocess.TimeoutExpired:
        return '<timed out>', -1
    finally:
        os.unlink(path)

def visits(program):
    """Output plus the map of which cells ran. A program that prints nothing
    still has a signature, which output alone does not give you."""
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
        f.write(program + '\n'); path = f.name
    try:
        p = subprocess.run([EMU, '--grid', path, '--visits', '--max-steps', '100000'],
                           capture_output=True, text=True, timeout=30)
        return p.stdout + '||' + p.stderr
    except subprocess.TimeoutExpired:
        return '<timed out>'
    finally:
        os.unlink(path)

def spare_arrows(program):
    """Arrows that can be blanked without changing anything. An arrow is only
    doing work where the runner arrives facing some other way, so a redundant
    one in a teaching example is just noise for the reader."""
    # sk_emu seeds its RNG the same way every run, so a program that picks at
    # random still gives one answer here -- and every arm that answer does not
    # take then looks spare. Rerunning cannot detect it. Look for the random
    # commands in the source instead, ignoring any inside a quoted string.
    for row in program.split('\n'):
        inq = False
        for ch in row:
            if ch == '"': inq = not inq
            elif not inq and ch in 'rR': return None
    a = visits(program)
    rows, out = program.split('\n'), []
    for ri, row in enumerate(rows):
        for ci, ch in enumerate(row):
            if ch not in '<>^v': continue
            alt = rows[:]; alt[ri] = row[:ci] + '.' + row[ci + 1:]
            if visits('\n'.join(alt)) == a: out.append((ri, ci, ch))
    return out

def main():
    if not os.path.exists(EMU):
        print('build the runner first:  cd host && make sk_emu'); return 2
    text = open(DOC).read()
    checked = pinned = failed = 0
    for program, says in examples(text):
        # Skip the tables and tag listings -- they are not programs. Anything
        # the guide makes a claim about is one, whatever it starts with: a
        # program hidden inside prose does not begin with an arrow.
        if not says and not program.lstrip().startswith(('>', 'v', '<', '^', '.', '*')):
            continue
        checked += 1
        out, rc = run(program)
        if says.startswith('BLOCK:'):
            want = [says[6:].strip()]
        else:
            want = re.findall(r'`([^`]*)`', says) if says else []
        # "prints nothing" is a claim worth checking too: a program that dies
        # before it reaches its output is exactly the sort of thing to pin.
        if says and re.match(r'prints nothing\b', says) and not want: want = ['']
        first = program.split('\n')[0][:46]
        if want:
            pinned += 1
            if out not in want:
                failed += 1
                print('FAIL  %-46s\n      wanted %s\n      got    %r'
                      % (first, ' or '.join(repr(w) for w in want), out))
        elif rc == -1:
            failed += 1
            print('FAIL  %-46s  did not terminate' % first)
    print('%d examples, %d with a pinned output, %d failed' % (checked, pinned, failed))

    if '--spare' in sys.argv:
        print()
        for program, _ in examples(text):
            if not program.lstrip().startswith(('>', 'v', '<', '^', '.')): continue
            # reference tables are laid out with runs of spaces; programs are not
            if '   ' in program.split('\n')[0]: continue
            sp = spare_arrows(program)
            first = program.split('\n')[0][:40]
            if sp is None:
                print('  %-42s random, not compared' % first)
            else:
                # the opening '>' is a convention, not a mistake: the runner
                # already starts facing east and every bundled program has one
                sp = [x for x in sp if x[:2] != (0, 0)]
                print('  %-42s %s' % (first, 'no spare arrows' if not sp
                      else 'SPARE: ' + ' '.join('r%dc%d(%s)' % x for x in sp)))
    return 1 if failed else 0

sys.exit(main())
