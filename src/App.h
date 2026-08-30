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
  bool quitRequested();   // set by the emulator's `quit` console command
}
