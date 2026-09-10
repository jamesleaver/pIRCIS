# pIRCIS 1.4.3

The same program on the phone: the iPhone and iPad build joins the
repository. The board is unchanged in what it draws, and a step back no
longer repaints it.

## The app

`ios/` builds the program as an iPhone and iPad app: `ios/setup.sh` fetches
what it needs and generates the Xcode project. Nothing is rewritten for the
phone: every change to pIRCIS lands on the board and the app in one commit.
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
