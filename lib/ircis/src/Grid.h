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

#include "CharMaps.h"
#include "DirVec.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ircis {
  // The program source. Unlike the original this never touches the filesystem
  // and never throws: reads outside the grid return CH_DOT (a blank). Runner
  // already bounds-checks before every access, so this is unreachable in
  // practice -- out_of_bounds_reads() is there to prove it.
  class Grid {
  public:
    Grid() : height_(0), width_(0) { }
    explicit Grid(const std::vector<std::string>& lines) { set_lines(lines); }

    // Splits on '\n', pads short lines with '.' (as equalize_lines() did).
    static Grid from_text(const std::string& text);

    void set_lines(const std::vector<std::string>& lines);

    char get(std::size_t xx, std::size_t yy) const {
      if (!is_inside(xx, yy)) { ++out_of_bounds_reads_; return CH_DOT; }
      return lines_[yy][xx];
    }
    char get(const DirVec& pos) const { return get(pos.get_x(), pos.get_y()); }

    bool set(std::size_t xx, std::size_t yy, char ch) {
      if (!is_inside(xx, yy)) return false;
      lines_[yy][xx] = ch;
      return true;
    }

    bool is_inside(std::size_t xx, std::size_t yy) const {
      return xx < width_ && yy < height_;   // unsigned, so always >= 0
    }
    bool is_inside(const DirVec& pos) const {
      return is_inside(pos.get_x(), pos.get_y());
    }

    const std::vector<std::string>& get_lines() const { return lines_; }
    std::size_t width() const { return width_; }
    std::size_t height() const { return height_; }

    unsigned long out_of_bounds_reads() const { return out_of_bounds_reads_; }

  private:
    void equalize_lines();

    std::vector<std::string> lines_;
    std::size_t height_, width_;
    mutable unsigned long out_of_bounds_reads_ = 0;
  };
}
