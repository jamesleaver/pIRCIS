# pIRCIS 1.4.6

pIRCIS has a website, pircis.fisheggs.au, with the guide, every bundled
program ready to copy, and the app's privacy and support pages. The
device points there now: LEARN IRCIS opens pircis.fisheggs.au/learn.html,
which is also what the board's code reads, and the ABOUT pages give the
website's address in place of GitHub's.

An iPad with a keyboard attached, a Magic Keyboard or a Bluetooth one,
types into the grid and into every field, as a Mac does; the drawn
keyboard goes away while a real one is attached and comes back when it is
taken off, and SYS > KEYBOARD offers the choice. The keys that act, such
as Return, Backspace and the arrows, now wait behind any character still
on its way from the keyboard, so a word and the Return after it always
arrive in the order they were typed, on the iPad and on the Mac. Held
down, f and b keep stepping, the arrows keep moving and Backspace keeps
deleting, and a finger or the mouse held on the step buttons keeps
stepping too.

The app no longer insists on the whole screen: the picture is refitted
to whatever bounds it is given, so it can share the screen or sit in a
window. ABOUT THIS DEVICE gives the screen and the panel as the app sees
them.

The Motto program has a new shape, and every picture and animation is
made again from it.

# pIRCIS 1.4.5

A review of the whole program, and what it found put right. No program
that ran before runs differently: arithmetic still wraps, as IRCIS means it
to, and the builds now say so to the compiler (`-fwrapv`) so that it is a
promise rather than a habit.

## The interpreter

A negative exponent used to hang the interpreter, and the readout evaluated
it a step early, so a five-character program could stop the board or the
app. It now gives what integer arithmetic gives: 1, plus or minus 1, or 0.
Modulo by zero ends the runner with a reason, as division by zero already
did, and INT_MIN divided or reduced by -1 comes out the same on every
machine instead of trapping on some.

A split whose outlets both loop back doubles the runners every pass, and
a push in a loop grows a stack without end; either took the board down
within a second. The interpreter now asks the device whether it is nearly
out of memory before it starts another runner or grows a stack. Refused, the
runner is not started, or dies on its next step, and the readout says
"Out of memory for a new runner" or "Out of memory for the stack". There
is no other limit on runners: a program may have as many as the device can
hold. The list of deaths kept for the readout is bounded too.

## The board's web page

A browser's form post was parsed by the web server into fields the pages
never read, so "Load onto device" always answered that the program was not
usable; and the server read the whole of any request body into memory
before the page saw it, so a large enough post took the board down. The
body is now taken as it arrives, the first 16 KB kept and the connection
dropped past that, and the form's own encoding is read as it should be.
Quotes in names are escaped on the pages.

## Loading and editing

A load that would replace a program with unsaved edits -- a listed program,
a paste, a file, NEW PROGRAM -- now asks first. UNDO is cleared by NEW
PROGRAM, by inserting or deleting a row or column, and by locking, so it
can no longer put another program's characters into the grid; the cursor
is kept inside the grid after a line is deleted. Pasted text has to be
printable characters, as an opened file already had to be. A program file
too big to be one is refused before it is read, on the board and in the
app. SETS repaints when an entry is chosen.

## Smaller things

The view follows the live runner with the lowest number, not a dead one.
A step back never shows the program at step nought on the way. The
console's `step` takes at most as many steps as a run to the end. Closing
the emulator's window at the moment of a resize no longer hangs it. Esc
leaves the second RESIZE page, the focus ring on RESIZE lists its own
buttons, and Esc on the unlock splash lets go of the pages that were to
follow. A saved-as name is made into a file name the way SAVE makes one,
and the loaded marker in PROG matches the whole path. Settings on a
computer are written whole or not at all. On a Mac, Cmd-V pastes once.
A factory reset closes the packed program as locking does. Message
dialogs on a short screen keep their buttons on it, DIAGNOSTICS puts its
third button on its own row when the dialog is narrow, and the RUN header
keeps the step count off the title at 320 pixels.

# pIRCIS 1.4.4

The iPad app runs on a Mac with Apple silicon, and this release makes it
usable there: a click lands where the pointer is, the pointer stays visible
over the window, and the Mac's keyboard types into the grid and drives the
pages as a keyboard does on the board's emulator. The system had been
swallowing clicks as game-controller input, the pointer was hidden as it is
on glass, and typed characters were never collected because the field that
collects them was asked for before the window was there.

PROG > New program has a PASTE FROM THE CLIPBOARD button wherever there is
a clipboard: the app, the emulator, a Pi. A program copied in any other app
lands in the grid and RUN shows it, the same as Ctrl/Cmd-V from a keyboard.

The program-name page repaints only what changed when a key is pressed, on
every screen. The board is otherwise unchanged.

# pIRCIS 1.4.3

The same program on the phone: pIRCIS is on the App Store for iPhone and
iPad. The board is unchanged in what it draws, and a step back no longer
repaints it.

## The app

The app is [pIRCIS on the App Store](https://apps.apple.com/app/id6809655892)
for iPhone and iPad. Nothing is rewritten for the phone: it is the one
program, and every change to pIRCIS lands on the board and the app together.
The board's 480 x 320 panel is still what everything is drawn for; the
phone lays the pages out to fit, in either orientation, with gestures, a
keyboard of its own and sharing through the phone's sheets. The app's
privacy policy and support page are `ios/PRIVACY.md` and `ios/SUPPORT.md`.

The iOS app carries no hidden program. The board and the emulator do.

## A step back repaints what changed

Stepping back rebuilds the machine and replays to the step before. That
used to show for a moment as a program at step nought, and with the trail
on it repainted the whole screen to take the tint off the cells walked past
the new step. Now the screen keeps what it shows until the step before is
ready, then the runners move, only the cells whose tint changed are put
back, and the readout repaints a character at a time, as a step forward is
drawn. A reset does the same.

The start-cell marker used to vanish once a runner had walked off it and
came back only on the next whole-screen paint; it now stays.

## Three things found building the app

Switching GRID TAP round to NOTHING put the start back at the top-left
corner, so a program whose tag starts it somewhere else set off from the
wrong cell once the setting had been tried and turned off, on every
platform. NOTHING now lets go of a start put down by tapping and keeps the
one the program asks for.

With the app's larger controls, the last row of a PROG folder ran under
the buttons that scroll it. When the list scrolls, it now shows only the
rows that end above them. The board is unchanged.

A turn of the phone showed a stretched picture for a frame or two: the
panel answered a window changing size by scaling each axis on its own and
drew that before the fit could be put right. The size change is now taken
before it is queued and the picture given its uniform fit at once.

## Small things

- A space on a runner's stack shows as `' '` in the readout rather than as
  nothing.
- README and the learning guide are revised: the readout section, a section
  on WiFi of its own, the editor's keyboards, and a note that the guide is
  under the same licence as the rest.
- A second golden screen set for the board, `tests/board_scene.txt`, covers
  the readout, GRID TAP in each mode, the trail, the dialogs and the
  keyboards, and a step back and a reset with the trail on.
