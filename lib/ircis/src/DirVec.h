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

#include <string>

namespace ircis {
  enum Direction {
    NORTH,
    EAST,
    WEST,
    SOUTH
  };
  Direction get_right(const Direction& dir);
  Direction get_left(const Direction& dir);
  Direction from_char(char start_direction_char);
  char to_char(const Direction& dir);

  class DirVec {
  public:
    DirVec() : xx(0), yy(0), direction(EAST) { }
    DirVec(int start_x, int start_y, Direction start_dir)
      : xx(start_x), yy(start_y), direction(start_dir) { }
    DirVec(int start_x, int start_y, char start_direction_char)
      : xx(start_x), yy(start_y), direction(from_char(start_direction_char)) { }

    void update();

    void set_position(int new_x, int new_y) { xx = new_x; yy = new_y; }
    void change_dir(const Direction new_dir) { direction = new_dir; }
    Direction get_direction() const { return direction; }
    unsigned int get_x() const { return xx; }
    unsigned int get_y() const { return yy; }

    Direction get_left() const { return ircis::get_left(direction); }
    Direction get_right() const { return ircis::get_right(direction); }

    void move(const Direction& dir) { change_dir(dir); update(); }

    std::string to_string() const;

  private:
    unsigned int xx, yy;
    Direction direction;
  };

  inline std::string to_str(const DirVec& p) { return p.to_string(); }
  inline std::string to_str(Direction d) { return std::string(1, to_char(d)); }
}
