// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#include <string>

#include "Program.h"

namespace sinks {
  // Writes the run output, the grid and its diff from the baseline to
  // /pircis/run_NNN.txt on the SD card. Returns false if no card is present or
  // the bus could not be claimed; serial and the web view are unaffected
  // either way.
  bool saveRunToSd(const std::string& output, const prog::Program& grid, std::string& pathOut);

  // Human-readable report used by both the SD file and the web view.
  std::string report(const std::string& output, const prog::Program& grid);
}
