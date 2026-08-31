# pIRCIS 1.1.0

Programs now live on the device itself, the play control moved to where your
thumb already is, and there are prebuilt, hashed binaries so you no longer need
a toolchain to put this on a board.

## Flash it without building it

Releases carry a ready-to-flash archive for each of the two panel controllers,
with `flash.sh`, the offsets written down, and a `SHA256SUMS` covering every
image. All you need is [esptool](https://pypi.org/project/esptool/):

```bash
shasum -a 256 -c SHA256SUMS
unzip pircis-1.1.0-st7796.zip && cd pircis-1.1.0-st7796
./flash.sh /dev/cu.usbserial-XXXX
```

They are built by GitHub Actions from the tag rather than on a laptop, which is
what makes publishing the hashes worth anything — `tools/release.sh` reproduces
them locally if you want to check.

The device now tells you what is on it: **SYS > ABOUT THIS DEVICE** ends on a
page giving the version, the build stamp, the image id and which panel the
binary drives. The 4" boards ship with either an ST7796 or an ILI9488, and the
wrong build shows a dark screen — now you can read which one you flashed
instead of guessing.

## Programs live on the device

The board has 896 KB of flash that nothing was using. It is a filesystem now,
so **a program can be saved with no SD card in the slot** — previously you
could edit a program on a card-less device and have no way to keep it.

The twenty-one bundled programs are copied into it at first start, which makes
them ordinary files: edit one, save over it, rename it, delete it. The
originals stay in the firmware, and **SYS > RESTORE BUILT-INS** puts them back
without touching anything you made yourself.

**PROG** and **SAVE** were two tabs splitting one question — which program do I
want to run? They are one **PROG** list: every program on the device and the
card together, in one alphabetical order, with a small chip or notched-card
mark saying where each lives. Both stores hold the same `<name>.txt`, one grid
row per line, so a program is the same file on either side.

That drops the plain-mode tab bar from six tabs to five, and each tab from 80
to 96 pixels — which matters on a resistive panel.

## Play is the RUN tab

Tapping the RUN tab while you are on the RUN page has always started and paused
a run, but nothing said so. The tab now draws `▶`, or `||` once a run is going,
and it is by far the biggest target on the screen. The separate play button is
gone, and the two transport buttons that remain — restart and run-to-end — grew
from 44 to 68 pixels.

## The editor

**UNDO and REDO**, over the last 128 cell edits, in the status bar. A resistive
panel occasionally reads a tap one key sideways, which makes taking an edit
back more useful than it sounds.

**Resizing asks which side.** Top, bottom, left or right, each adding or
removing, applied together when you accept — "eleven rows" never said whether
the new one lands above the program or below it, and for a program whose
runner starts at 0,0 that is the whole question.

**Discard changes** on PROG puts a program back to the last version saved.

The cursor arrows are gone from the edit grid and the character inspector. They
did exactly what tapping the cell already did, and on the edit page the cell had
been inflated to 30×32 purely to leave them whitespace. At 24×26 — the same tap
target as a keyboard key — you see 20×7 cells instead of 16×5.

The keys are set in the same face and size as the grid, so a key looks like what
lands in the cell; they were the 5×7 pixel font at double size, matching nothing
else on the device.

The character inspector, which shows what a cell used to be and reverts it one
cell at a time, is no longer exclusive to a packed program. It opens on any
program when **SYS > START POINT** is `FREE` — the setting that already governed
tapping the grid, where moving the entry point was an unlabelled two-tap gesture
and is now a button that says `START`.

## Settings that agree with themselves

`RUN VIEW` and `DEBUG` were two toggles producing three states and contradicting
each other: `DEBUG` silently won, so `RUN VIEW` would read `OUTPUT` while the
runners were on screen. They are one **UNDER GRID** setting cycling `OUTPUT` /
`RUNNERS` / `NOTHING`.

The rest of SYS is reordered: a settings grid at the top, then the About pages
and the two things you cannot undo anchored to the foot under a rule. Two
different tiles were both called `DEBUG` — the one that opens the internals
panel is now `DIAGNOSTICS`, and **DUMP GRID** moved inside it, next to the
console output it writes to. `OFF` reads `POWER OFF`.

The welcome dialog shows once rather than on every power-on. A factory reset
brings it back.

## Aiming, not guessing

Zoomed out a cell is six pixels across, so tapping the grid now **zooms to
what you touched** and the second tap — on a cell four times the area — is the
one that selects. The character inspector and the parameter editor both work
that way.

Touch readings are taken five times and the median used. A resistive panel's
first sample is its worst, taken while pressure is still rising, and on a 43 px
key a few pixels is one character sideways.

**Recalibrating now tells you how it went**: three taps on rings, and the worst
miss reported in pixels against what a key actually is. The calibration itself
cannot be made finer — it is the four screen corners and nothing else — so
knowing whether the one you have is good is the useful part.

## Fixes

- **The character inspector's taps landed on the wrong cell.** It mapped a tap
  as though the cursor were always centred, but the window pins to the edges of
  the program, so near any edge a tap selected a character up to four columns
  from the one touched. The cursor arrows were masking it.
- **Random numbers were slightly biased.** `r` and `R` used a plain modulo,
  which favours the low residues whenever 2³² does not divide the range —
  invisible for a die, real for a large one. They are rejection-sampled now,
  the same guarantee upstream IRCIS gets from `uniform_int_distribution`.
- **Programs a device had not been shown were invisible to everything else.**
  The built-ins were copied to storage only when PROG was first opened, so the
  web view, the serial console and anything else reading programs found an
  empty store until someone visited that tab.
- **Programs that print nothing no longer end on a blank page.** A run that
  emitted only whitespace still jumped to OUT; the ones you watch rather than
  read now stay on the grid.
- **SAVE no longer asks for an SD card.** It writes back to whichever store the
  program came from, falling back to the device if that was a card since
  removed.
- **Fifteen keys on the text keyboard typed the wrong character.** That
  keyboard names programs and presets and takes the WiFi password; space gave
  `!` and `(` gave `"`. Its layout has three cases and the tap mapping only
  knew two.
- **The zoomed grid was drawn over its own readout.** A ten-row program did not
  fit beside the output, so it was clamped to the top and ran straight through
  it — the runner showing through the text. The same assumption hid the scroll
  chevrons on exactly those programs.
- **A finished run stays on the grid** instead of jumping to the output page.
- `SAVE SD` on the output page appears only when there is a card to save to,
  rather than sitting permanently greyed, and it moved into the status bar with
  every other tab's controls.

## For contributors

- `tools/release.sh` — build and hash the release artifacts
- `tools/make_shots.sh` — regenerate every screenshot and GIF from the emulator
- `tools/sys_layout.py` — fail the SYS page on a tile collision or a hole
  (written after two tiles silently claimed the same cell and one became
  unreachable)
