// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

// pIRCIS -- an IRCIS interpreter for the ESP32-2432S028R.
//
// IRCIS is a 2D esolang: the program is a grid, and runners walk it. This
// firmware runs one on hardware -- the same interpreter core that is diffed
// against the reference desktop build (see host/test_golden.cpp), a touch UI
// for editing, and a revert path that cannot be lost because every program
// keeps its baseline.
//
// Everything of substance is in app::setup()/app::loop(), which the desktop
// emulator (emulator/main.cpp) drives identically.

#if !defined(SK_HOST)

#include <Arduino.h>

#include "App.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  app::setup();
}

void loop() {
  app::loop();
}

#endif
