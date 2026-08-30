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

#include "Grid.h"
#include "Logger.h"

namespace ircis {

  Grid Grid::from_text(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    for (char ch : text) {
      if (ch == '\n') { lines.push_back(line); line.clear(); }
      else if (ch != '\r') { line.push_back(ch); }
    }
    if (!line.empty()) lines.push_back(line);
    return Grid(lines);
  }

  void Grid::set_lines(const std::vector<std::string>& lines) {
    lines_ = lines;
    width_ = 0;
    for (const auto& l : lines_) {
      if (l.length() > width_) width_ = l.length();
    }
    height_ = lines_.size();
    equalize_lines();
  }

  void Grid::equalize_lines() {
    for (auto& line : lines_) {
      if (line.length() < width_) line.resize(width_, CH_DOT);
    }
  }
}
