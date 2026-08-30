// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

// Golden test: every bundled program loads at the shape the table declares,
// runs to completion without reading out of bounds, and survives the editing
// model -- shape edits, cell edits, and a diff that round-trips.
#include "Machine.h"
#include "Program.h"

#include <cstdio>
#include <algorithm>
#include <memory>
#include <string>

using namespace ircis;

static int failures = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
  if (!ok) ++failures;
}

int main() {
  // The bundled IRCIS examples exercise the variable-size program model:
  // they run from 4 x 25 up to 28 x 73.
  std::printf("bundled example programs\n");
  check(prog::programCount() == 1 + prog::kExampleCount,
        "program table holds index 0 plus the examples");
  check(prog::programAt(prog::kFirstExample).name[0] != '\0',
        "the first always-available program has a name");

  for (int i = 1; i < prog::programCount(); ++i) {
    const prog::ProgramDef& def = prog::programAt(i);
    prog::Program p;
    p.loadProgram(i);

    char what[160];
    std::snprintf(what, sizeof(what), "%-12s loads and runs at %2d x %2d",
                  def.name, def.rows_n, def.cols_n);
    bool shape = p.rows() == def.rows_n && p.cols() == def.cols_n
              && !p.isPacked() && p.modifiedCells() == 0;

    auto g = std::make_shared<Grid>(p.lines());
    shape = shape && (int)g->height() == def.rows_n && (int)g->width() == def.cols_n;

    StringSink es;
    MachineOptions eo;
    eo.record_visits = true;
    Machine em(g, &es, eo);
    long eguard = 0;
    bool ran = true;
    while (em.update()) if (++eguard > 2000000) { ran = false; break; }

    check(shape && ran && em.out_of_bounds_reads() == 0, what);
  }

  {   // hello_world is the one example whose output is worth pinning
    prog::Program hw;
    for (int i = 1; i < prog::programCount(); ++i)
      if (std::string(prog::programAt(i).name) == "Hello World") hw.loadProgram(i);
    auto g = std::make_shared<Grid>(hw.lines());
    StringSink hs; MachineOptions ho; Machine hm(g, &hs, ho);
    long gd = 0;
    while (hm.update()) if (++gd > 200000) break;
    check(hs.text.find("Hello World!") != std::string::npos,
          "Hello World prints \"Hello World!\"");
  }

  {   // shape edits, allowed on any program that is not packed
    prog::Program ex; ex.loadProgram(prog::kFirstExample);
    int r0 = ex.rows(), c0 = ex.cols();
    check(ex.insertRow(0) && ex.rows() == r0 + 1 && ex.deleteRow(0) && ex.rows() == r0,
          "an example accepts a row being inserted and removed");
    check(ex.insertCol(0) && ex.cols() == c0 + 1 && ex.deleteCol(0) && ex.cols() == c0,
          "an example accepts a column being inserted and removed");
  }

  // Cell edits, the diff they produce, and the round trip through the
  // compact encoding NVS uses.
  std::printf("\nediting model\n");
  {
    prog::Program p;
    p.loadProgram(prog::kFirstExample);
    check(p.modifiedCells() == 0, "a freshly loaded program matches its baseline");

    const char was = p.cell(0, 0);
    const char now = was == 'x' ? 'y' : 'x';
    check(p.setCell(0, 0, now), "a cell accepts an edit");
    check(p.cell(0, 0) == now, "and holds it");
    check(p.cellModified(0, 0) && p.modifiedCells() == 1, "exactly one cell is modified");
    check(p.baselineCell(0, 0) == was, "the baseline still remembers what was there");

    std::vector<prog::Diff> d = p.diff();
    check(d.size() == 1, "the diff covers it");

    uint8_t buf[512];
    std::size_t n = p.encodeDiff(buf, sizeof(buf));
    check(n == d.size() * 3, "diff encodes to 3 bytes per cell");

    prog::Program restored;
    restored.loadProgram(prog::kFirstExample);
    check(restored.decodeDiff(buf, n), "diff decodes");
    check(restored.text() == p.text(), "decoded diff reproduces the edited grid");

    p.revertAll();
    check(p.modifiedCells() == 0 && p.cell(0, 0) == was, "revertAll puts it back");

    // An out-of-range cell must be refused rather than written past the grid.
    check(!p.setCell(-1, 0, 'x') && !p.setCell(0, -1, 'x')
          && !p.setCell(p.rows(), 0, 'x') && !p.setCell(0, p.cols(), 'x'),
          "cells outside the grid are refused");
  }

  // With no pack open there are no parameters, and every accessor has to say
  // so rather than reach into an empty table.
  {
    prog::Program p;
    check(prog::slotCount() == 0, "a closed pack describes no parameters");
    check(prog::slotIndex("ANY") == -1, "and finds none by id");
    check(prog::slotOriginal(0).empty(), "and has no original to offer");
    check(p.slotValue(0).empty() && !p.setSlotValue(0, "x") && !p.slotModified(0),
          "so the slot accessors refuse cleanly");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
              failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
