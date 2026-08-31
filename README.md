# pIRCIS

An IRCIS interpreter for a Freenove FNK0114S — a 4.0" 320x480 ST7796 touch
display, driven landscape at 480x320. Choose a program, watch the runners walk
it a step at a time, and edit it on the device.

The `p` is for pocket.

IRCIS — "I Run Chars I See" — is a 2D esolang by
[Arjun Nair (batman-nair)](https://github.com/batman-nair/IRCIS). The program is
a grid, one instruction per cell; a runner walks it in a straight line until
told to turn, and can split into several runners at once.

| | |
|---|---|
| ![hello running](shots/hello.gif) | ![dumb-clock running](shots/gifs/clock.gif) |
| Hello World, whole on screen at the large font. | Dumb Clock, which pretends to tell the time. |
| ![hello output](shots/hello_out.png) | ![programs](shots/programs.png) |
| What it printed, centred, with how the run went. | Every program on the device: a chip mark for its own storage, a notched card for the SD slot. |
| ![editor](shots/editor.png) | ![letters](shots/editor_abc.png) |
| The editor. Everything you write IRCIS with. | Tapping `EDIT` again cycles to the capitals and then the lower case. |
| ![system](shots/system.png) | |
| Settings, with the About pages. `UNDER GRID` chooses what the RUN page shows beneath the program. | |

## Getting started

Grab a 4.0" 320x480 ESP32 board, also known as a CYD (cheap yellow display).

There are two ways to get started: flash a build from
[Releases](https://github.com/jamesleaver/pIRCIS/releases), or build it
yourself (which may be the easiest option).

The 4" Freenove boards ship with one of two display controllers. Everything
here was developed against an ST7796 board, which is the only one I have. **The
`ili9488` build has never been run on hardware.**  If you have an ILI9488 board,
I would be glad to hear whether it works.

### From a release

Download both the archive for your panel and `SHA256SUMS` from the release,
then:

```bash
shasum -a 256 -c SHA256SUMS          # or sha256sum -c on Linux
unzip pircis-1.1.0-st7796.zip
cd pircis-1.1.0-st7796
```

Flashing needs [esptool](https://pypi.org/project/esptool/). On a recent macOS
`pip install` refuses to touch the system Python, so put it in a virtual
environment — and remember it is only available while that environment is
active, which means running the first line again in a new terminal:

```bash
python3 -m venv ~/.venvs/esptool
source ~/.venvs/esptool/bin/activate
pip install esptool
```

Then flash. Run it with no argument first and it lists the serial ports it can
see; the board is usually the `usbserial` one:

```bash
./flash.sh
./flash.sh /dev/cu.usbserial-240
```

### From source (easiest)

```bash
git clone https://github.com/jamesleaver/pIRCIS.git
cd pIRCIS
brew install platformio sdl2
```

SDL2 is only for the desktop emulator; leave it out if you are only flashing a
board. On Linux use your own package manager (`pip install platformio`,
`apt install libsdl2-dev`) — nothing else differs.

Then pick one:

```bash
pio run -e st7796 -t upload -t monitor   # flash a board and watch the console
pio run -e emulator -t exec              # run it on your Mac, no hardware
cd host && make test                     # the tests: no PlatformIO, no board
```

If the display stays dark or the colours look inverted, that board has the
other panel controller — use `-e ili9488`, bearing in mind that build is
untested on real hardware (see above). If `upload` cannot find the board,
check `pio device list`: if the port is not there at all, the USB-serial driver
is missing rather than anything being wrong with PlatformIO.

## Using it

Five tabs along the bottom: **RUN**, **OUT**, **EDIT**, **PROG**, **SYS**.

### Load a program

**PROG** lists every program on the device, and scrolls. Twenty-two are
bundled, including Arjun Nair's own `racetrack` from IRCIS: three runners go
round a track and the output is the order they finish in, which is different
every time.

There are counting and arithmetic ones, four that use the random numbers -- a
die roll, an invented clock time, a coin, six lottery numbers -- and four worth
watching rather than reading. The same files are in [`programs/`](programs/),
along with a few more GIFs of them in [`shots/gifs/`](shots/gifs/). Tap one and it
loads; its name then appears as the title of the RUN page. **New program...**
at the bottom gives you a blank grid at any size up to 32 x 96, every cell a `.`
(the blank).

### Run it

**RUN** shows the grid. Play is the **RUN** tab itself: while you are on that
page the tab draws `▶`, or `⏸️` once a run is going, and tapping it starts or
pauses. It is the biggest target on the screen, which is the point — from any
other tab the same tab still says RUN and still navigates.

The rest of the transport is right-aligned in the status bar:

| | |
|---|---|
| `\|◀` | reset: back to the top, output and timer cleared |
| `▶\|` | run to the end as fast as it will go |

Two more — step back and step forward — appear when **SYS > UNDER GRID** is set
to `RUNNERS`.

What sits under the grid is **SYS > UNDER GRID**: the output as it is printed,
the per-runner readout, or nothing at all if you would rather have the rows.

However fast you set it, a run always opens slowly enough to see the runners
set off, then gets out of the way after a second or so. `FULL` skips the lead-in
entirely — asking for FULL is asking for the answer, not for the performance.

The speed button cycles **SLOW** (about 3 steps a second), **MED** (about 25),
**QUICK** (about 2000) and **FULL**. At SLOW each runner leaves a fading trail.

A program too wide to read gets a **ZOOM** button.

Scroll buttons (`^` and `v`) appear when there is more than fits.

### Read the output

When a run finishes, the **OUT** tab gets a dot to say there is something to
look at. This tab will also show live output while a program is running.

**SAVE SD**, in the status bar, writes the whole run to the card;
it only appears when there is a card in the slot.

### Edit it

**EDIT** is a text editor for the loaded program. Tap a cell to put the cursor
there, then type. `.` is the blank, so it doubles as delete.

The keyboard is the thirty-three characters you actually write IRCIS with —
movement, the blank, arithmetic, the digits, and the commands.

Tapping the **EDIT** tab while you are already on it cycles to the capitals
and then the lower case, the way tapping **RUN** plays and pauses; the tab
names the keyboard you are looking at. Commands are in the accent colour,
so `V`, `R`, `v`, `r` and `p` read as commands on the letter pages too — they
are base64 digits that also do a job.

The status bar carries the program's name, its size, a `ZOOM` toggle, **SAVE**
and **UNDO**/**REDO**. The name and size are buttons — one renames the program,
the other reshapes it — and SAVE writes it back where it came from under that
name, so renaming and saving sit next to each other. Saving over the file the
program came from just saves; saving over a different one asks first, and once
saved nothing in the program counts as an unsaved edit any more.

UNDO and REDO go back through the last 128 cell edits.

The size button asks which **side** to add to or take from — top, bottom, left
or right.

**PROG** grows a **Discard changes** row whenever there is something to
discard, which puts the program back to the last version saved.

### Ask for a view

A program can say how it wants to be shown, in one short tag written anywhere
in the grid. A tilde (`~`), then single letters:

```
under the grid:  n  nothing    d  runner readout
speed:           s  slow   m  med   q  quick   f  full
start position:  <col>,<row> and one of N E S W
```

So `~nm3,1N` is: nothing underneath, medium, start at column 3 row 1 heading
north. Order does not matter -- `~3,1Nmn` is the same tag.

Without a compass letter a start heads east. The comma is what marks a coordinate,
and either side of it can be left off, so `~,2` starts at column 0 row 2.

Anything a tag leaves out takes the default: the output under the grid, med,
no runner readout, and starting at 0,0 heading east. **A program with no tag
gets all of those**, so only a program wanting something unusual needs a tag.

None of the tag characters is an IRCIS command, so a runner that crosses one
steps straight over it. It can sit on any blank cell in the middle of a program.

### Keep it

Programs live in two places, and **PROG** lists both together in one
alphabetical list. A chip is for programs on the device's own storage,
a notched card for those on an SD card.

Tap a program to load it, `X` to delete it, or one of the two **Save** rows at the top
to write the current program to either store under the name in the status bar.

The device's own storage is a LittleFS filesystem on the board's spare flash,
so it works with no card in the slot. The programs that ship with pIRCIS are
copied into it the first time it starts, which makes them ordinary files:
edit one, save over it, rename it, delete it. The originals stay in the
firmware — `SYS > RESTORE BUILT-INS` writes them back, leaving anything you
made yourself alone.

Both stores hold plain `.txt`, one grid row per line.

### Settings

**SYS** is a page of settings — WiFi, what sits under the grid, the entry
point, SD logging (when on, every completed run is written to the card), touch
recalibration, a day/night palette, and restoring the built-in programs.

Recalibrating touch ends by asking you to tap three rings and telling you how
far out the worst one landed. A key is about 43 px wide, so within six pixels
is comfortable and much beyond fourteen is worth doing again.

**DIAGNOSTICS** is a live panel: free heap, largest allocatable block, steps
per second, and whether the run read anything out of bounds. **DUMP GRID**,
inside it, writes the loaded program and every edit to the serial console.

**ABOUT THIS DEVICE** ends on a page saying which firmware is on the board —
version, build stamp, image id and panel.

**RESET ALL DATA** returns the device to how it flashed, built-in programs
included.

**START POINT** decides where execution begins. On `FIXED` it is always row 0,
column 0, heading east. Set it `FREE` and tapping a character on RUN starts the
program there; tapping it again turns it, and a chevron shows which way the
runner will leave.

## Over serial and over WiFi

The serial console (115200) drives the same working grid as the screen:

```
grid          print the loaded program
cell 3 12 v   set one cell
load          push the edits into the interpreter
run
out           print what it produced
report        the whole run: program, output and statistics
```

`help` lists the rest. Output is mirrored to serial as it is produced, so a
long run can be watched from a laptop.

`SYS > WIFI` serves the device over the local network: the last run, an
editable copy of the loaded program (paste one in and it runs on the device),
the programs in both stores, the saved outputs, and the preset slots. Programs
and presets can be loaded onto the device from there as well as read.

## On your Mac, without hardware

`pio run -e emulator -t exec` builds the **actual firmware** — the same
interpreter, the same UI, the same console — against an SDL2 window standing in
for the panel. The mouse is the touch screen; stdin is the serial port. Only a
thin platform layer differs between the two builds, so what you exercise on the
Mac is what runs on the board.

Three commands exist only there, and make the UI scriptable:

```
tap <x> <y>        synthesise a touch
shot run.ppm       dump the panel framebuffer
page /outputs      render a web page and print it
```

There is also a command-line runner with no display at all:

```bash
cd host && make
./sk_emu --grid myprogram.txt --stats
./sk_emu --visits        # the execution/direction map of every cell
```

## Under the hood

The interpreter is a **port, not a reimplementation**: `lib/ircis/` is
batman-nair's IRCIS with the filesystem, iostreams and unbounded allocations
taken out and nothing else changed. That matters because a program's behaviour
can depend on how many steps it takes, so an interpreter that is merely
functionally equivalent would not be safe to trust with one.

So the port is checked rather than assumed. `cd host && make test` builds the
same core for macOS and asserts that every bundled program loads at the shape
its table declares, runs to completion, reads nothing out of bounds, and
survives the editing model. `tools/ui_regression.sh` drives the emulator
through a fixed tap sequence and compares ten screens byte for byte.

```
lib/ircis/    the interpreter core -- no filesystem, no iostream, bounded memory
lib/program/  the program model -- variable-size grid, revertible
lib/pack/     SHA-256, TEA-CBC, and the content pack they open
src/          firmware: display, touch UI, run task, NVS, SD, web view
host/         command-line runner and tests (plain Makefile)
```

Build output goes to `~/.cache/pircis/build`, deliberately outside any
cloud-synced folder: a file provider can re-materialise a stale object
underneath a running build.

*(Some macOS Command Line Tools installs ship libc++ only inside the SDK, where
clang does not look. Both builds detect that and work around it.)*

## One more program

There is an IRCIS program on this device that the list does not show.

It is encrypted and hidden, but can be decrypted and revealed with two
words. Entered correctly into the device, the two words reveal an easter egg.

## Copyright

The firmware, UI, host tools and tests are **Copyright (c) 2026 James Leaver**,
released under the [MIT License](LICENSE).

Two things here are not mine: `lib/ircis/`, the interpreter core, is Arjun
Nair's ([batman-nair/IRCIS](https://github.com/batman-nair/IRCIS), MIT — see
[`lib/ircis/LICENSE`](lib/ircis/LICENSE)); and LovyanGFX, fetched at build time
rather than vendored, is lovyan03's (FreeBSD).
