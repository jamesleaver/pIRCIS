# pIRCIS

A pocket computer for writing and watching **IRCIS** programs, running on a
cheap 4" ESP32 touchscreen. Pick a program, hit play, and watch the runners
walk across the grid one step at a time. Edit it on the device with a stylus.
No computer needed once it is flashed.

The `p` is for pocket.

IRCIS — *"I Run Chars I See"* — is a two-dimensional esolang by
[Arjun Nair (batman-nair)](https://github.com/batman-nair/IRCIS). A program is
a grid of characters, one instruction per cell. A *runner* walks it in a
straight line until something tells it to turn, and it can split into several
runners going different ways at once.

<p align="center">
  <img src="shots/board.jpg" alt="pIRCIS running on the board" width="520">
</p>

<p align="center">
  <img src="shots/motto.gif" alt="the motto program drawing IRCIS" width="480">
</p>

That second one is a real program running. Five runners walk the shape of the
letters and spell out **IRCIS** as they go, then the program prints what it
stands for. The letters are not in the grid — they only exist as the paths the
runners take.

## What you need

- A 4.0" 320x480 ESP32 touch display — a Freenove FNK0114S, or any of the
  "cheap yellow display" boards of that size. Driven landscape at 480x320.
- A USB cable.
- Optionally a microSD card, for saving programs and run logs.

These boards ship with one of two display controllers. Everything here was
built against an **ST7796**, which is the one I have. There is an `ili9488`
build too, but **I have never run it on hardware** — if you have one of those
boards I would love to know whether it works.

## Getting it on the board

The easiest way is to build it yourself:

```bash
git clone https://github.com/jamesleaver/pIRCIS.git
cd pIRCIS
brew install platformio sdl2
pio run -e st7796 -t upload -t monitor
```

SDL2 is only for the desktop emulator — skip it if you just want to flash a
board. On Linux, `pip install platformio` and `apt install libsdl2-dev`.

If the screen stays dark or the colours look wrong, you have the other panel:
use `-e ili9488` instead. If `upload` cannot find the board, check
`pio device list` — if the port is not there at all you are missing the
USB-serial driver, not anything to do with PlatformIO. And if it sits on
`Connecting.....` forever, hold the **BOOT** button, tap **EN/RST**, then let go
of BOOT.

There are also prebuilt archives on
[Releases](https://github.com/jamesleaver/pIRCIS/releases) if you would rather
not build. Grab the zip for your panel plus `SHA256SUMS`, check it, unzip it,
and run `./flash.sh` — with no arguments it lists the ports it can see.
Flashing that way needs [esptool](https://pypi.org/project/esptool/), and on
recent macOS you will need it in a virtual environment:

```bash
python3 -m venv ~/.venvs/esptool
source ~/.venvs/esptool/bin/activate
pip install esptool
```

## Using it

Five tabs along the bottom: **RUN**, **OUT**, **EDIT**, **PROG**, **SYS**.

### PROG — pick a program

Sixty-seven programs come with it, sorted into folders. Tap a folder to go in,
**Back** to come out.

| | |
|---|---|
| ![the folders](shots/programs.png) | ![inside a folder](shots/folder.png) |

Programs live in two places — the board's own flash and the SD card — and the
list merges them. A chip mark means it is on the device, a notched card means
it is on the SD card. Tap one to load it. `X` deletes it.

The two **Save** rows write the current program to either store, into whichever
folder you are looking at. **New program...** gives you an empty grid at any
size up to 32 x 96.

Nothing here is precious: edit a built-in program, save over it, rename it,
delete it. `SYS > RESTORE BUILT-INS` puts the originals back and leaves
anything you made alone.

### RUN — watch it go

The **RUN** tab is also the play button. While you are on that page it shows
`▶`, or `⏸` once something is going, and tapping it starts or pauses. It is the
biggest target on the screen, which is the point.

<p align="center">
  <img src="shots/hello.gif" alt="hello world running" width="420">
</p>

The rest of the transport sits in the status bar: reset back to the start,
step, and run-to-the-end. Turn on **SYS > STEP BUTTONS** and you get two more —
step back and step forward — so you can walk a program one tick at a time and
back again. Stepping back rebuilds and replays, so it is instant early in a run
and slow once you are thousands of steps in.

Four speeds: **SLOW** (about 3 steps a second), **MED** (25), **QUICK** (2000)
and **FULL**. At SLOW each runner leaves a short fading tail, which is how you
follow what it is doing. However fast you set it, a run always starts slowly
for a second so you can see the runners set off — except at FULL, where you are
asking for the answer rather than the show.

A program too wide to read gets a **ZOOM** button.

### OUT — read what it printed

<p align="center">
  <img src="shots/hello_out.png" alt="the output page" width="420">
</p>

When a run finishes the **OUT** tab gets a dot and the RUN tab turns into a
go-again arrow.

You can watch output being generated live on the **OUT** tab while a program is
running.

It also keeps the **last ten runs**. The `<` and `>` buttons at the bottom step
back through them, each with what it printed and how many steps it took.

<p align="center">
  <img src="shots/history.png" alt="stepping back through previous runs" width="420">
</p>

**SAVE SD** writes whichever run you are looking at to the card. It only
appears when there is a card in the slot.

### EDIT — change it with your finger

Tap a cell to put the cursor there, then type. `.` is the blank, so it doubles
as delete.

| | |
|---|---|
| ![the editor](shots/editor.png) | ![the letters](shots/editor_abc.png) |

The main keyboard is the thirty-three characters you actually write IRCIS with.
Tapping **EDIT** while you are already on it cycles to the capitals and then the
lower case, laid out QWERTY. The tab tells you which keyboard you are looking
at.

- **UNDO / REDO** go back through the last 128 cell edits.
- The size button asks which **side** to add to or take from — top, bottom,
  left or right — because "eleven rows" never said whether the new one lands
  above your program or below it.
- **PROG** grows a **Discard changes** row whenever there is something to throw
  away.

### SYS — settings

<p align="center">
  <img src="shots/system.png" alt="the settings page" width="420">
</p>

WiFi, what sits under the grid, what tapping the grid does, SD logging, a
day/night palette, and restoring the built-ins.

**CHECK TOUCH** asks you to tap three rings and tells you how far out the worst
one was, then offers to recalibrate. A key is about 43 px wide, so within six
pixels is fine and much past fourteen is worth redoing.

**DIAGNOSTICS** is live while you watch it: free heap, steps per second, and
whether the run read anything out of bounds. If a runner died of anything other
than reaching `!` or walking off the edge, the reason is listed there.

## The programs

Sixty-eight of them. The full list is in [`programs/`](programs/).

| | |
|---|---|
| ![dice](shots/gifs/dice.gif) | ![dumb clock](shots/gifs/clock.gif) |
| **Dice Roll** — rolls, prints the number, then puts exactly that many runners into a ring, so you can count the answer going round. | **Dumb Clock** — invents a plausible time and reads it out in words. |
| ![racetrack](shots/gifs/racetrack.gif) | ![spiral](shots/spiral.gif) |
| **Racetrack** — three runners, five random pit stops each. The finishing order determines the winner. | **Spiral** — one runner winding inward over every cell. Eight programs print nothing at all and are just worth watching. |

A few are hiding what they do until you run them:

<p align="center">
  <img src="shots/insult.png" alt="a grid of nothing but numbers" width="420">
</p>

That is the whole of **Insult Machine**. There is not a letter in it — every
word is carried as a number, because `%` prints an integer as base64 characters.
**Dog Name** does the same but picks its numbers with two coin flips, and
**Warning** does not even write the numbers down: each one is a quotient and a
remainder, multiplied back out as it runs.

**Morse Decoder** turns morse back into a word. The whole top row is yours: put
one code per letter after the `'0.`, left to right — `1` for a dit, `2` for a
dah — so `'1111.'1.'1211.'1211.'222.` gives HELLO. The row underneath says so on
the device, with arrows pointing at where to type. It walks a binary tree for
each letter. Five letters is the ceiling: the answer is built up in a single
integer, and an int32 holds exactly five base64 characters.

The rest are counting loops, times tables, one-of-four answer machines, the
Morse alphabet on two screens, and ten correct digits of π out of Machin's
formula — in a language with 32-bit integers and no arrays.

## Telling a program how to show itself

A program can say how it wants to be displayed, in one short tag written
anywhere in the grid. A tilde, then single letters:

```
under the grid:  n  nothing    d  runner readout
speed:           s  slow   m  med   q  quick   f  full
path:            t  keep every cell a runner has crossed tinted
start position:  <col>,<row> and one of N E S W
```

So `~nm3,1N` means: nothing underneath, medium speed, start at column 3 row 1
heading north. Order does not matter. Anything you leave out goes back to the
default, so most programs need no tag at all.

`t` is the interesting one. Normally a runner shows a short tail and the cells
behind it go back to normal. With `t`, every cell any runner has stood on stays
tinted — so the whole path builds up and stays on screen. That is what makes
the banner at the top of this page work. Add it to `spiral` or `snake` and you
get the same effect.

None of the tag characters is an IRCIS command, so a runner that crosses one
just steps over it — a tag can sit on any blank cell in the middle of a
program. And you do not have to type it: set the speed and view you want, then
tap **PROG > Save this view in the program** and it writes the tag for you.

## Writing your own

| Character | What it does |
|:---:|---|
| `< > ^ v` | Move the runner |
| `+ - * / %` | Arithmetic, in *integer* mode |
| `#` | Print the top of the stack |
| `%` | Print the top of the stack as base64 characters |
| `$` | Newline |
| `!` | This runner stops |
| `.` and space | Blank — the runner walks over it |
| `"` | Toggle *stack* mode: characters get pushed as they are |
| `'` | Start a number; a blank ends it |
| `?` | If the top of the stack is non-zero carry on, otherwise turn |
| `*` | Split into more runners |
| `@n` / `&n` | Push the n'th item / pop n items |
| `@name` / `&name` | Push a variable / set one |
| `r` / `R` | Random 0 or 1 / random up to a limit |
| `p` | Pause this runner for n ticks |

Four things catch everyone out, me included:

- **Arithmetic takes the top of the stack as the left operand.** Push 10 then 3
  and `'-.` gives -7, not 7.
- **`?` does not pop.** It looks at the top and leaves it there, so a branch has
  to clear up after itself.
- **Text is pushed, so it comes out backwards.** Write it reversed in the grid.
- **A cell holds one instruction.** A path that crosses one of its own turns
  will take that turn again and loop for ever.

The full command list and the language rules are in
[Arjun Nair's README](https://github.com/batman-nair/IRCIS), which is where I
learned all of this.

## Over serial and over WiFi

The serial console (115200) drives the same grid as the screen:

```
grid          print the loaded program
cell 3 12 v   set one cell
run
out           print what it produced
report        the whole run, with statistics
```

`help` lists the rest. Output is mirrored to serial as it appears, so you can
watch a long run from a laptop.

`SYS > WIFI` serves the device on your network: the last run, an editable copy
of the loaded program, both program stores, and the saved outputs. You can
paste a program in from a browser and it runs on the device.

**There is no password on any of it.** Anyone who can reach the board can read
what is on the card and write a program onto it. That is fine for something on
your own desk, but it should be your choice — turn WiFi off on a network you
share.

## On your Mac, without hardware

```bash
pio run -e emulator -t exec
```

That builds the *actual firmware* — same interpreter, same UI, same console —
against an SDL2 window standing in for the panel. The mouse is the touchscreen
and stdin is the serial port. Every screenshot on this page was taken from it.

Three extra commands make it scriptable: `tap <x> <y>`, `shot run.ppm` and
`page /outputs`. There is a headless runner too:

```bash
cd host && make
./sk_emu --grid myprogram.txt --stats
./sk_emu --visits        # which cells ran, and which way the runner was going
```

## What's inside

```
lib/ircis/    the interpreter core
lib/program/  the program model -- variable-size grid, revertible edits
lib/pack/     SHA-256, TEA-CBC, and the content pack they open
src/          firmware: display, touch UI, run task, NVS, SD, web view
host/         command-line runner and tests
```

`lib/ircis/` is a **port, not a reimplementation** — batman-nair's IRCIS with
the filesystem, iostreams and unbounded allocations taken out, and nothing else
changed. A program's behaviour can depend on how many steps it takes, so
"functionally equivalent" would not be good enough.

`cd host && make test` builds the same core for macOS and checks that every
bundled program loads at the size its table claims, runs to completion, and
reads nothing out of bounds. `tools/ui_regression.sh` drives the emulator
through a fixed set of taps and compares the screens byte for byte.
`tools/make_shots.sh` regenerates every picture on this page.

Build output goes to `~/.cache/pircis/build`, deliberately outside any
cloud-synced folder — a file provider can quietly hand a stale object to a
running build.

## One more program

There is an IRCIS program on this device that the list does not show.

It is encrypted, and two words open it. Enter them correctly and you get an
easter egg.

## Copyright

The firmware, UI, host tools and tests are **Copyright (c) 2026 James Leaver**,
released under the [MIT License](LICENSE).

Two things here are not mine. `lib/ircis/`, the interpreter core, is Arjun
Nair's ([batman-nair/IRCIS](https://github.com/batman-nair/IRCIS), MIT — see
[`lib/ircis/LICENSE`](lib/ircis/LICENSE)). LovyanGFX, fetched at build time
rather than vendored, is lovyan03's (FreeBSD).
