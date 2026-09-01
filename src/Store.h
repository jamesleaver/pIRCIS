// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Program.h"

// Persistent state in NVS: touch calibration, saved grid presets and settings.
// A preset stores only the cells that differ from the program's baseline
// (3 bytes each, typically under 60 bytes), never a whole grid, so the
// baseline always remains the source of truth for "revert".
namespace Store {
  static constexpr int kMaxPresets = 8;

  void begin();

  bool loadTouchCalibration(uint16_t params[8]);
  void saveTouchCalibration(const uint16_t params[8]);
  void clearTouchCalibration();

  struct PresetInfo {
    bool used = false;
    std::string name;
    int changedCells = 0;
  };

  PresetInfo presetInfo(int slot);
  bool savePreset(int slot, const std::string& name, const prog::Program& s);
  bool loadPreset(int slot, prog::Program& s);
  bool deletePreset(int slot);

  // Settings
  std::string wifiSsid();
  std::string wifiPass();
  void setWifi(const std::string& ssid, const std::string& pass);
  bool sdLoggingEnabled();
  void setSdLogging(bool on);
  int runSpeed();                 // run::Speed: 0 slow, 1 medium, 2 quick, 3 full
  void setRunSpeed(int speed);
  bool outputColour();            // OUT tab: colour chunks by length/sign
  void setOutputColour(bool on);
  void factoryReset();            // clears everything the user has saved

  // Whether the content pack has been unlocked. Locked, this is an ordinary
  // IRCIS interpreter. Cleared by factoryReset().
  // Palette: night (dark) by default, day (white) for sunlight.
  // What sits under the program on RUN: 0 the runners, 1 the output as it
  // is printed, 2 nothing at all -- which is the default, because the space
  // is better spent on the program until you ask for it back.
  // What sits under the program when debug is off: 0 the output as it is
  // printed (the default), 1 nothing.
  int  runView();
  void setRunView(int v);

  // One switch for the things you want while working out what a program does:
  // the two single-step buttons on the transport, and the per-runner readout
  // under the program. Off by default -- five buttons in that strip are small
  // enough to mis-hit, and the readout costs rows the program could use.
  bool debugMode();
  void setDebugMode(bool on);

  bool dayMode();
  void setDayMode(bool on);

  bool unlocked();
  void setUnlocked(bool on);

  // Lets the character inspector move the entry point and its direction.
  // Off by default: a program is normally run as it stands.
  // What a tap on the RUN grid does. One setting, because a tap can only do
  // one of these and the old START POINT: FREE/FIXED was quietly deciding
  // both whether the start could move and whether the inspector opened.
  enum { kTapNothing = 0, kTapStart = 1, kTapInspector = 2 };
  int  gridTap();
  void setGridTap(int mode);


  // Where the runner enters the grid: column, row, and one of N/E/S/W.
  void startPoint(int& col, int& row, char& dir);
  void setStartPoint(int col, int row, char dir);

  // Show the step-back / step-forward buttons on the RUN transport. A reader's
  // setting like GRID TAP, not a property of the program, so loading one does
  // not turn it off.
  bool stepButtons();
  void setStepButtons(bool on);

  // Keep every cell a runner has stood on tinted, so the path builds up on
  // screen instead of fading. Off by default; the '~t' view tag turns it on.
  bool tracePath();
  void setTracePath(bool on);

  int  gridView();                // RUN tab view: 1 = wide, 2 = zoom
  void setGridView(int view);

  // User-defined entries on the SETS tab, one list per parameter column the
  // packed program declares. Stored as one space-separated string.
  static constexpr int kMaxCustom = 9;
  std::string customSet(int kind, int index);              // "" when absent
  int  customSetCount(int kind);
  bool addCustomSet(int kind, const std::string& value);   // false when full
  void deleteCustomSet(int kind, int index);
}
