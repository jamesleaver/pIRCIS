// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

// Everything the firmware does, independent of how it was started. The ESP32
// entry point (src/main.cpp) and the desktop emulator (emulator/main.cpp) both
// just call setup() once and loop() forever.
namespace app {
  void setup();
  void loop();
  bool quitRequested();
  // A screen that changed size (the phone turned). The platform asks; the
  // program's loop rebuilds its side at its next turn and says when it is
  // done; the platform finishes its own side and clears the request.
  bool takeSizeRequest(int& w, int& h);   // the console asked for another screen size
  bool requestResize(int w, int h);   // false while one is already in flight
  bool resizeBusy();
  bool resizeParked();              // the program is parked; the display may be rebuilt
  void resizeSize(int& w, int& h);
  void clearResize();               // done: the program goes on at the new size   // set by the emulator's `quit` console command
}
