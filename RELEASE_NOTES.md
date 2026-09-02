# pIRCIS 1.3.0

The screen stopped flickering, the two program pages became one program,
and a learning guide arrived.

## Flash it without building it

Each release carries a ready-to-flash archive for both panel controllers,
with `flash.sh`, the offsets written down, and a `SHA256SUMS` covering every
image. All you need is [esptool](https://pypi.org/project/esptool/):

```bash
shasum -a 256 -c SHA256SUMS
unzip pircis-1.3.0-st7796.zip && cd pircis-1.3.0-st7796
./flash.sh /dev/cu.usbserial-XXXX
```

Run `./flash.sh` with no argument and it lists the ports it can see. The
archives are built by GitHub Actions from the tag rather than on a laptop,
which is what makes publishing the hashes worth anything.

The `ili9488` archive still has not been run on hardware — development is on
an ST7796, the only board I have. If you have the other one I would be glad
to hear whether it works.

## A guide to IRCIS

[LEARN.md](LEARN.md) teaches the language from the first runner to a worked
reading of a whole program, in twenty short sections. Every example in it is
run against the interpreter before a release, so what it says a program
prints is what it prints. It names the bundled programs the way the device
does and says which folder each is in.

## RUN and EDIT show the same program

They used to be two layouts of the same thing, a few pixels and a row apart,
with their own zoom. Now there is one: switch between them and nothing moves,
including the ZOOM button. ZOOM is the same size on both, small arrows on the
grid's edges scroll a program larger than the window, and FOLLOW RUNNER works
in both views. Opening EDIT pauses the run, and nothing in a running program
can be edited until it is paused.

## Faster, and without the flicker

The program is drawn a row at a time from memory rather than a cell at a
time to the panel, and each frame is one transaction on the bus, so a page
arrives instead of crawling in. What changes is what gets repainted: a new
character on OUT redraws one line, a toggle on SYS redraws one tile, a
keystroke in a dialog redraws the value, a page turn redraws the page inside
its frame. The rule between the program and its output no longer blinks on
every character printed, and the output no longer jumps up a line when a run
finishes.

## The programs

Sixty-one now, in five folders named for what you are there to do:
Counting, Deciding, Decoding, Talking and Watching. Showing-off has gone, its
contents sorted into the others, and nine programs that were the same idea
as another have gone too.

- **Two Dimensions**, **Greeting** and **Advice** read their strings along a
  snake, a square wave and a spiral, to show that a runner reads a program
  along whatever path it is given.
- **Bounce** bounces, **Comb** has teeth, **Four Ways** splits four ways, and
  **Serpent** is no longer Snake again. Each checked against the interpreter's
  visit map.
- **Decoding** is new: Insult Machine, Base 64, Morse Decoder and Binary.

## Everything else

- The WIFI tile says when a browser last asked for a page.
- CONFIRM reads WORKING while a slow action runs, so deleting a program no
  longer looks like a hang.
- The calibration prompt gives way to "Loading" once the corners are tapped.
- The SETS page holds still while a run is going.

## Fixed

- The first cell of every run was never shown: the runner appeared at its
  second step.
- A program that asks to start somewhere other than the top-left corner
  sometimes started there anyway.
- The scroll arrows' touch targets reached off the edge of the panel, where
  no finger is, so a tap on one often landed on the character underneath.
- The inspector's buttons could not be pressed at all on a program shorter
  than six rows.
- Programs that moved folder left their old copies behind on the device.
- The trailing space and newline the interpreter printed at the end of a run
  no longer go into the output.
