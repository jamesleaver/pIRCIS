// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#include <cstdint>

// RGB565. Two palettes: DAY is the default -- a white ground, which a TN panel
// needs more than most for reading in sunlight -- and NIGHT inverts it to a
// dark ground with amber program text.
//
// These are inline variables rather than constants so the whole UI can be
// re-themed by assigning to them; every call site stays `theme::bg`.
namespace theme {
  inline uint16_t bg        = 0x0000;
  inline uint16_t panel     = 0x18E3;
  inline uint16_t line      = 0x39E7;
  inline uint16_t text      = 0xEF7D;
  inline uint16_t dim       = 0x8410;
  inline uint16_t blank     = 0x39E7;   // the '.' padding that fills the grid
  inline uint16_t accent    = 0xFD20;   // amber
  inline uint16_t good      = 0x2FEB;
  inline uint16_t warn      = 0xFBE0;
  inline uint16_t bad       = 0xF9A6;
  inline uint16_t edited    = 0x07FF;   // cyan: a cell that differs from the original
  inline uint16_t selected  = 0xFFFF;

  // One colour per runner, in the order they are spawned.
  inline uint16_t runner[6] = { 0xFD20, 0x07E0, 0xF81F, 0x05FF, 0xFFE0, 0xFC9F };

  inline bool dayMode = false;

  // Swap the palette. Day is not a mechanical inversion: amber on white is
  // barely legible, and the runner colours have to be darkened rather than
  // flipped or they vanish into the background.
  inline void setDay(bool day) {
    dayMode = day;
    if (day) {
      bg       = 0xFFFF;   // white
      panel    = 0xE71C;   // light grey card
      line     = 0xAD55;   // border
      text     = 0x0000;   // black
      dim      = 0x6B4D;
      blank    = 0xC618;   // the '.' padding, faint but visible
      accent   = 0xB960;   // dark amber, readable on white
      good     = 0x0480;   // dark green
      warn     = 0xC300;   // dark orange
      bad      = 0xA000;   // dark red
      edited   = 0x0219;   // dark blue
      selected = 0x0000;
      const uint16_t day6[6] = { 0xB960, 0x0480, 0x9013, 0x021F, 0x8400, 0xA80D };
      for (int i = 0; i < 6; ++i) runner[i] = day6[i];
    }
    else {
      bg       = 0x0000;
      panel    = 0x18E3;
      line     = 0x39E7;
      text     = 0xEF7D;
      dim      = 0x8410;
      blank    = 0x39E7;
      accent   = 0xFD20;
      good     = 0x2FEB;
      warn     = 0xFBE0;
      bad      = 0xF9A6;
      edited   = 0x07FF;
      selected = 0xFFFF;
      const uint16_t night6[6] = { 0xFD20, 0x07E0, 0xF81F, 0x05FF, 0xFFE0, 0xFC9F };
      for (int i = 0; i < 6; ++i) runner[i] = night6[i];
    }
  }
}
