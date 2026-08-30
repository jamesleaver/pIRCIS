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

#pragma once

namespace ircis {
  // Character defines -- verbatim from IRCIS src/CharMaps.h
#define CH_NORTH '^'
#define CH_SOUTH 'v'
#define CH_EAST '>'
#define CH_WEST '<'

#define CH_ADD '+'
#define CH_SUB '-'
#define CH_MUL '*'
#define CH_DIV '/'
#define CH_MOD '%'
#define CH_POW '^'
#define CH_AND '&'
#define CH_OR  '|'
#define CH_XOR 'V'
#define CH_BL  '<'
#define CH_BR  '>'

#define CH_PRINT '#'
#define CH_PRINT_BASE64 '%'
#define CH_ENDL '$'

#define CH_PUSH '@'
#define CH_POP '&'

#define CH_STACK '"'
#define CH_INT '\''

#define CH_SPLIT '*'
#define CH_CHECK '?'

#define CH_RAND 'r'
#define CH_RAND_INT 'R'
#define CH_PAUSE 'p'

#define CH_DOT '.'
#define CH_SPC ' '

#define CH_END '!'

  bool is_arith(char check);
  bool is_blank(char check);
}
