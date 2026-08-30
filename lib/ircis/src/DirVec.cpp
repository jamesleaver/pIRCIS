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

#include "DirVec.h"
#include "Logger.h"

namespace ircis {
  Direction get_right(const Direction& dir) {
    switch (dir) {
    case NORTH:  return EAST;
    case EAST:   return SOUTH;
    case SOUTH:  return WEST;
    case WEST:   return NORTH;
    default:     return NORTH;
    }
  }
  Direction get_left(const Direction& dir) {
    switch (dir) {
    case NORTH:  return WEST;
    case EAST:   return NORTH;
    case SOUTH:  return EAST;
    case WEST:   return SOUTH;
    default:     return NORTH;
    }
  }
  Direction from_char(char start_direction_char) {
    switch (start_direction_char) {
    case 'N':  return NORTH;
    case 'E':  return EAST;
    case 'S':  return SOUTH;
    case 'W':  return WEST;
    default:
      Logger::err_line_dbg("Got invalid direction value: ", start_direction_char,
                           ", using default EAST");
      return EAST;
    }
  }
  char to_char(const Direction& dir) {
    switch (dir) {
    case NORTH: return 'N';
    case EAST:  return 'E';
    case WEST:  return 'W';
    case SOUTH: return 'S';
    }
    return 'X';
  }

  void DirVec::update() {
    int dx = 0, dy = 0;
    switch (direction) {
    case NORTH: dy = -1;    break;
    case WEST:  dx = -1;    break;
    case EAST:  dx = 1;     break;
    case SOUTH: dy = 1;     break;
    }
    xx += dx;
    yy += dy;
  }

  std::string DirVec::to_string() const {
    return "(" + std::to_string(xx) + "," + std::to_string(yy) + "," +
           std::string(1, to_char(direction)) + ")";
  }
}
