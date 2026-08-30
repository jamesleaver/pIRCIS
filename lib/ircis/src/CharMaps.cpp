// Ported from IRCIS -- "I Run Chars I See" -- by Arjun Nair (batman-nair):
//   https://github.com/batman-nair/IRCIS
//
// Copyright (c) 2019 Arjun Nair
// Licensed under the MIT License. See lib/ircis/LICENSE for the full text.
//
// Modified for pIRCIS by James Leaver: bounded memory, no iostream
// or filesystem, a seeded RNG and a ring-buffer trail, so the interpreter
// runs unchanged on an ESP32. Behaviour is deliberately byte-identical to
// the reference build.

#include "CharMaps.h"

namespace ircis {
  bool is_arith(char check) {
    const char arr[] = { CH_ADD, CH_SUB, CH_DIV, CH_MUL, CH_MOD,
                         CH_POW, CH_AND, CH_OR, CH_XOR, CH_BL, CH_BR };
    for (char ch : arr) { if (ch == check) return true; }
    return false;
  }
  bool is_blank(char check) {
    const char arr[] = { CH_SPC, CH_DOT };
    for (char ch : arr) { if (ch == check) return true; }
    return false;
  }
}
