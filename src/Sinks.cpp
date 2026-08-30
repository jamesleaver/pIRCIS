// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#include "Sinks.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "Pack.h"
#include "Platform.h"
#include "RunTask.h"

namespace sinks {

std::string report(const std::string& output, const prog::Program& grid) {
  run::Snapshot snap = run::snapshot();
  std::string r;
  // The parameter table only means anything for a packed program. Any other
  // one gets a plain IRCIS run report.
  const bool packed = grid.isPacked();
  if (!packed) {
    char head[96];
    snprintf(head, sizeof(head), "IRCIS run -- %s, %d x %d\n",
             grid.programName(), grid.rows(), grid.cols());
    r += head;
    r += "=========================================\n\n";
  }
  else {
  const std::string title = pack::str(pack::kStrReportTitle);
  r += title;
  r += "\n";
  r += std::string(title.size(), '=');
  r += "\n\n";

  r += "Parameters\n----------\n";
  for (int i = 0; i < prog::slotCount(); ++i) {
    std::string cur = grid.slotValue(i), orig = prog::slotOriginal(i);
    const std::string& label = prog::slot(i).label;
    r += "  ";
    r += label;
    r += std::string(label.size() < 24 ? 24 - label.size() : 1, ' ');
    r += cur;
    if (cur != orig) { r += "   (was "; r += orig; r += ")"; }
    r += "\n";
  }

  const std::string diffHdr = pack::str(pack::kStrReportDiffHdr);
  r += "\n";
  r += diffHdr;
  r += "\n";
  r += std::string(diffHdr.size(), '-');
  r += "\n";
  std::vector<prog::Diff> diffs = grid.diff();
  if (diffs.empty()) { r += pack::str(pack::kStrReportDiffNone); r += "\n"; }
  for (const prog::Diff& d : diffs) {
    char line[64];
    snprintf(line, sizeof(line), "  row %2d col %2d  '%c' -> '%c'\n",
             d.row, d.col, grid.baselineCell(d.row, d.col), d.ch);
    r += line;
  }
  }

  r += "\nGrid\n----\n";
  r += grid.text();

  r += "\nRun\n---\n";
  char stats[256];
  snprintf(stats, sizeof(stats),
           "  steps               %u\n"
           "  runners created     %u\n"
           "  runners died        %u\n"
           "  elapsed             %u ms\n",
           (unsigned)snap.step, (unsigned)snap.runnersCreated, (unsigned)snap.deaths,
           (unsigned)snap.elapsedMs);
  r += stats;

  r += "\nOutput\n------\n";
  r += output;
  r += "\n";
  return r;
}

bool saveRunToSd(const std::string& output, const prog::Program& grid, std::string& pathOut) {
  return plat::writeRunFile(report(output, grid), pathOut);
}

}
