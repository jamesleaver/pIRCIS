# pIRCIS 1.4.2

The readout under the grid now says what each runner is about to do, and
shows its stack; a tap on a cell can open the editor on it; a program's
display tag no longer changes your settings; and a handful of fixes.

## The runner readout, one step ahead

With `SYS > UNDER GRID` set to `RUNNERS`, each runner gets a line: its row
and column and which way it faces, the character it is standing on, what
that character will do when the runner steps, and the top of its stack,
newest value last. `turn south`, `split`, `int mode on`, then `int 2` and
`int 20` as a number is read and `push 20` on the blank that ends it,
`pop mode`, `&N`, `save N=20`, `print 3`, `check false`, `pause 5`. It is
the interpreter's own debug log, one step ahead of it. Before the first
step the runner's line sits under the "press play" prompt, so a program
can be read one cell at a time before it runs; turn on `STEP BUTTONS` and
step through it, reading the line as you go. README and the learning
guide show how to use it on a program you are writing.

The band repaints a character at a time rather than a line at a time, so
a step no longer blinks the whole readout. The pair of arrows that scroll
a list longer than the band sit stacked at its left edge, where they used
to share the program's scroll row with the first line. While a run is
going the runners still alive are listed first, and each runner's name is
in the colour it is drawn in on the grid.

## GRID TAP: EDIT

A fourth setting for the tile, after the inspector: a tap on a cell of a
running program opens the editor with the grid exactly as it was on RUN,
same view and same scroll, and the cursor on the cell that was tapped.
There is no zoom first in this mode. GRID TAP is your setting and stays
across program loads, as STEP BUTTONS and the theme do.

## A program's tag is laid over your settings, not written into them

TRAIL and UNDER GRID are set by hand on SYS. A program whose tag asks for
the trail, or for a readout, gets it while that program is loaded, and the
setting comes back when another one is loaded; a program that asked for
the trail no longer leaves it on for every program after it. The SYS
tiles show what is in force and, tapped, take over. The speed, the follow
setting and the start cell are still set by every load.

## Fixes

- Reverting a program loaded from a file emptied the grid. Discard changes
  on PROG, REVERT in the inspector and the console's `revert` all go back
  to the last version saved.
- Every route that loads a program settles it the same way, so the start
  cell of the program that ran last no longer carries over.
- Play and pause while running, and a step back, repaint what changed
  rather than the whole screen.
- Single dashes in the dialogs.
- In the desktop emulator, the mouse wheel scrolls the grid on RUN and in
  the editor; `wheel dy dx x y` on the console turns one for a script.

## Programs

Binary, Circuit, Comb, Spiral and Staircase are revised; Comb, Spiral and
Staircase now keep their trail, so the figure each one draws stays on the
screen. Spiral's GIF in the README is remade.
