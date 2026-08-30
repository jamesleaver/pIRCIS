// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#include <string>

#include "Program.h"

// Touch UI. begin() after the display and RunTask are up; tick() from loop().
namespace ui {
  void begin();
  void tick();

  // The working copy the UI edits. The serial console drives the same object
  // so the screen and the terminal never disagree.
  prog::Program& editGrid();
  void markEdited();      // note unsaved edits and force a repaint
  void markLoaded();      // the working grid is now the one the machine is running
  void repaint();
  void showMessage(const std::string& title, const std::string& body);
  // Call after anything that may have changed the lock state.
  void notifyUnlocked();
  // Back to a plain IRCIS interpreter, as the SYS tile does.
  void lockDevice();

  // Emulator only: drive the UI from the console instead of the mouse.
  void injectTap(int x, int y);
  void injectDrag(int dx);   // ZOOM: pan by a pixel delta, as a finger would
  void injectDragV(int dy);  // scroll the row window, as a finger would
  unsigned long gridPaints();  // host-only instrumentation
  unsigned long bandPaints();
  // Parse a program file into the working grid. False if empty or too big.
  bool loadProgramTextPublic(const std::string& text);
  // The same, but as EDITS to the program already loaded when the shape
  // matches, so its baseline -- and therefore its diff -- survives.
  bool applyProgramTextPublic(const std::string& text);
  void injectHold(int x, int y);   // SETS: synthesise a long press
  void fontSampler();        // scratch: render one grid row in each candidate font
}
