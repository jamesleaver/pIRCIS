// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#ifndef LGFX_USE_V1        // also set in platformio.ini; keep both builds happy
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>

#include "Config.h"

#if defined(SK_HOST)

// Desktop emulator: the same LovyanGFX drawing surface, rendered into an SDL2
// window. The UI code above does not know the difference.
class CydDisplay : public lgfx::LGFX_Device {
public:
  CydDisplay();
  bool beginTouch(bool force_recalibrate = false);
  int  checkTouch() { return 0; }
  void reinitTouch() { }
private:
  lgfx::Panel_sdl panel_;
};

#else

// LovyanGFX device for the Cheap Yellow Display: panel on SPI2, resistive
// touch on SPI3.
class CydDisplay : public lgfx::LGFX_Device {
public:
  CydDisplay();
  // Loads the stored touch calibration, or runs the four-corner routine if
  // there is none.
  bool beginTouch(bool force_recalibrate = false);

  // Tap three targets and report the worst miss, in pixels. The calibration
  // itself cannot be made finer -- setTouchCalibrate stores the four corners
  // and nothing else, and the library already averages sixteen stabilised
  // readings at each -- so the useful thing is being able to see whether the
  // one you have is any good.
  int checkTouch();

  // The SD card and the touch controller share the VSPI host on this board
  // (on different pins), so anything that claims the bus for the card must
  // hand it back afterwards. See plat::writeRunFile().
  void reinitTouch() { touch_.init(); }

private:
#if defined(PANEL_ST7796)
  lgfx::Panel_ST7796 panel_;
#elif defined(PANEL_ILI9488)
  lgfx::Panel_ILI9488 panel_;
#elif defined(PANEL_ST7789)
  lgfx::Panel_ST7789 panel_;
#else
  lgfx::Panel_ILI9341 panel_;
#endif
  lgfx::Bus_SPI       bus_;
  lgfx::Light_PWM     light_;
  lgfx::Touch_XPT2046 touch_;
};

#endif

extern CydDisplay gfx;
