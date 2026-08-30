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
  // Where a program's printed output goes. The original wrote to std::cout and
  // an fstream; on the device this is the on-screen output pane, Serial, the
  // SD log and the web view, fanned out by a MultiSink.
  struct OutputSink {
    virtual ~OutputSink() { }
    virtual void write(const std::string& text) = 0;
    virtual void newline() = 0;
  };

  // Collects output into a std::string. Used by the host tools and tests.
  struct StringSink : OutputSink {
    std::string text;
    void write(const std::string& t) override { text += t; }
    void newline() override { text += '\n'; }
  };
}
