# pIRCIS 1.2.0

Sixty-eight programs instead of twenty-two, folders to find them in, and a
long list of small fixes to things that were quietly annoying.

## Flash it without building it

Each release carries a ready-to-flash archive for both panel controllers,
with `flash.sh`, the offsets written down, and a `SHA256SUMS` covering every
image. All you need is [esptool](https://pypi.org/project/esptool/):

```bash
shasum -a 256 -c SHA256SUMS
unzip pircis-1.2.0-st7796.zip && cd pircis-1.2.0-st7796
./flash.sh /dev/cu.usbserial-XXXX
```

Run `./flash.sh` with no argument and it lists the ports it can see. The
archives are built by GitHub Actions from the tag rather than on a laptop,
which is what makes publishing the hashes worth anything.

The `ili9488` archive still has not been run on hardware — development is on
an ST7796, the only board I have. If you have the other one I would be glad
to hear whether it works.

## Programs are in folders now

**PROG** opens on Counting, Talking, Deciding, Watching and Showing-off, each
saying how many are inside. Sixty-eight in one alphabetical list was twelve
screens of scrolling. They are real directories on both the device and the
card, so the folders you see on the board are the folders you see on a
computer. Your own saved programs are left where they are.

## Forty-six new programs

Worth a look:

- **Morse Decoder** — type dits and dahs, get a word back. It walks a binary
  tree for each letter.
- **Pi** — ten correct digits out of Machin's formula, in a language with
  32-bit integers and no arrays.
- **Motto** — draws IRCIS on the screen by walking the shape of the letters,
  then prints what it stands for.
- **Insult Machine**, **Dog Name** and **Warning** give nothing away until you
  run them: there is not a readable word in any of their grids.

**Racetrack** is rebuilt to fill the screen, and the race is now actually
fair — the lanes are identical and the handicaps cancel out.

## Everything else

- The last ten runs are kept. Step back through them with `<` and `>` on OUT.
- A view tag, `~t`, keeps the runners' path on screen instead of letting it
  fade. That is what makes Motto legible, and it suits the watchers too.
- Step buttons have a switch of their own in SYS, and stay on when you load
  another program.
- **CHECK TOUCH** measures your calibration and then offers to redo it. It
  used to only measure one you had already thrown away.
- **PROG** will write the current speed and view into the program as a tag.
- **DIAGNOSTICS** stays live while you look at it, and now says why a runner
  died if it died of anything other than reaching `!` or leaving the grid.

## Fixed

- You could not scroll to the last line of a tall program in ZOOM. The
  readout was drawn over the rows that were missing.
- **RUN TO END** gives up after five million steps, and said nothing about
  it — indistinguishable from a program that finished.
- An empty PROG page said nothing at all, not even that RESTORE BUILT-INS
  exists.
- Typing in the editor rebuilt the whole program on every keystroke.
- The RUN page now says when the cell your program starts on is blank, which
  is the commonest reason a first program does nothing.
