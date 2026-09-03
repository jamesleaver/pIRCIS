# pIRCIS 1.4.0

The emulator builds and runs on Windows, and the board can be flashed from
Windows too.

## Flash it without building it

Each release carries a ready-to-flash archive for both panel controllers,
with `flash.sh` for macOS and Linux, `flash.bat` for Windows, the offsets
written down, and a `SHA256SUMS` covering every image. All you need is
[esptool](https://pypi.org/project/esptool/):

```bash
shasum -a 256 -c SHA256SUMS
unzip pircis-1.4.0-st7796.zip && cd pircis-1.4.0-st7796
./flash.sh /dev/cu.usbserial-XXXX
```

On Windows, `flash.bat COM5`, with the COM number from Device Manager. Run
either one with no argument and it lists the ports it can see. The archives
are built by GitHub Actions from the tag rather than on a laptop, which is
what makes publishing the hashes worth anything.

The `ili9488` archive still has not been run on hardware — development is on
an ST7796, the only board I have. If you have the other one I would be glad
to hear whether it works.

## Windows

The emulator is the same program on all three systems now. On Windows it is
built with MSYS2, which supplies the compiler and SDL2, and the README walks
through it: one line to install MSYS2, one block to paste into its shell.
The build finds SDL2 by asking the machine rather than being told where it
is, so the one command works on macOS, Linux and Windows alike.

Getting there took three fixes that were as much about the build as about
Windows:

- A header used a fixed-width integer type without saying where it came
  from. clang lets that pass; GCC does not.
- LovyanGFX's manifest opts out of being archived, which put its hundred
  object files on the link line one at a time. On Windows that line is
  longer than the shell allows, and the temp file PlatformIO then goes
  through is deleted with a `cmd.exe` built-in that reads a forward-slash
  path as a switch. The emulator now archives the library: one file, a
  short line, no temp file.
- pkg-config on MSYS2 adds `-mwindows`, which would have made a program with
  no console. The emulator reads its commands from the console, so it keeps
  one.

Flashing from Windows uses the same PlatformIO, from the MSYS2 shell or from
PowerShell with Python installed; the README has both.

## Also

- **LEARN IRCIS** on the SYS page shows where the learning guide lives, with
  a QR code a phone can scan to open it. It takes the place of POWER OFF,
  which only ever put the board to sleep until its reset button was pressed.
- **Motto** is the program the device wakes up with, and it has been
  rewritten: every runner pushes numbers and prints them as base64, so the
  words only exist once the timing of the runners assembles them.
- Loading a program puts the view settings back to their defaults (speed,
  trail, follow, what a grid tap does) unless the program's own tag sets
  them. What you changed for one program no longer follows you to the next.
- On the EDIT CHARACTER page, DEL then OK writes a space, and the IRCIS
  letters `r R p v V` are shown in red as they are on the keyboards.
- A view tag's start position is now written **row first**: `~3,1N` starts
  at row 3, column 1, heading north. That is the order the editor's corner
  and the run page's "entry" line already used, and the one habit now
  serves everywhere. Four Ways and Motto, the two bundled programs that
  use one, are updated.
- A view tag written or changed in the editor takes effect when the edit
  lands, rather than only when the program is loaded again.
- The inspector works from the keyboard: the arrows move the cell, and
  `e`, `r`, `s` and `c` press its buttons.
- In the emulator, resizing the window no longer puts clicks off from
  where the pointer is.
- With a real keyboard in use, the editor draws into the three rows the
  on-screen keyboard would have taken.
- Deleting the last program in a folder removes the folder, on every
  platform, through the standard filesystem library rather than a POSIX call.
