// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

// Host build of the same core that runs on the ESP32.
// Lets you drive the emulator from the command line while the board is en route,
// and is the reference the on-device build is diffed against.
#include "Machine.h"
#include "Program.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ircis;

static void usage() {
  std::printf(
    "usage: sk_emu [options]\n"
    "  --slot ID=VALUE   set a named parameter (see --slots)\n"

    "  --cell R,C=CH     overwrite a single grid cell\n"
    "  --slots           list slots with current and original values, then exit\n"
    "  --grid FILE       run an IRCIS program from a text file\n"
    "  --max-steps N     abort after N global steps (default 5000000)\n"
    "  --stats           print run statistics to stderr\n"
    "  --visits          print the execution/direction map to stderr\n"
    "  --globals         print the program's global variables after the run\n");
}

int main(int argc, char** argv) {
  prog::Program program;
  std::string grid_file;
  long max_steps = 5000000;
  bool stats = false, visits = false, list_slots = false, globals = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) { usage(); std::exit(2); }
      return argv[++i];
    };
    if (a == "--slot") {
      std::string kv = next();
      auto eq = kv.find('=');
      if (eq == std::string::npos) { usage(); return 2; }
      std::string id = kv.substr(0, eq), val = kv.substr(eq + 1);
      int idx = prog::slotIndex(id.c_str());
      if (idx < 0) { std::fprintf(stderr, "unknown slot: %s\n", id.c_str()); return 2; }
      if (!program.setSlotValue(idx, val)) {
        std::fprintf(stderr, "value '%s' does not fit slot %s (%d cells)\n",
                     val.c_str(), id.c_str(), prog::slot(idx).len);
        return 2;
      }
    }
    else if (a == "--cell") {
      std::string kv = next();
      int r, c; char ch;
      if (std::sscanf(kv.c_str(), "%d,%d=%c", &r, &c, &ch) != 3) { usage(); return 2; }
      if (!program.setCell(r, c, ch)) { std::fprintf(stderr, "cell out of range\n"); return 2; }
    }
    else if (a == "--slots") { list_slots = true; }
    else if (a == "--grid") { grid_file = next(); }
    else if (a == "--max-steps") { max_steps = std::atol(next().c_str()); }
    else if (a == "--stats") { stats = true; }
    else if (a == "--visits") { visits = true; }
    else if (a == "--globals") { globals = true; }
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); usage(); return 2; }
  }

  if (list_slots) {
    std::printf("%-8s %-22s %-4s %-4s %-4s %-8s %-8s\n",
                "ID", "LABEL", "ROW", "COL", "LEN", "CURRENT", "ORIGINAL");
    for (int i = 0; i < prog::slotCount(); ++i) {
      const prog::Slot& s = prog::slot(i);
      std::printf("%-8s %-22s %-4d %-4d %-4d %-8s %-8s%s\n",
                  s.id.c_str(), s.label.c_str(), s.row, s.col, s.len,
                  program.slotValue(i).c_str(), prog::slotOriginal(i).c_str(),
                  program.slotModified(i) ? "  *modified" : "");
    }
    return 0;
  }

  auto grid = std::make_shared<Grid>();
  if (!grid_file.empty()) {
    FILE* f = std::fopen(grid_file.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", grid_file.c_str()); return 1; }
    std::string text;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    *grid = Grid::from_text(text);
  }
  else {
    *grid = Grid(program.lines());
  }

  StringSink sink;
  MachineOptions opt;
  opt.record_visits = visits;
  Machine machine(grid, &sink, opt);

  long guard = 0;
  while (machine.update()) {
    if (++guard > max_steps) { std::fprintf(stderr, "guard tripped at %ld steps\n", guard); break; }
  }

  std::fputs(sink.text.c_str(), stdout);

  if (stats) {
    std::fprintf(stderr, "\n--- run statistics ---\n");
    std::fprintf(stderr, "global steps        : %u\n", machine.step_number());
    std::fprintf(stderr, "runners created     : %d\n", machine.runners_created());
    std::fprintf(stderr, "total runner steps  : %lu\n", machine.total_runner_steps());
    std::fprintf(stderr, "grid cells modified : %d\n", program.modifiedCells());
    std::fprintf(stderr, "out-of-bounds reads : %lu\n", machine.out_of_bounds_reads());
    std::fprintf(stderr, "stack UB reads      : %lu\n", machine.stack_ub_reads());
    for (const auto& d : machine.deaths())
      std::fprintf(stderr, "runner %d died at step %u after %lu steps: %s\n",
                   d.runner_id, d.step, d.runner_steps, d.error.c_str());
  }

  if (globals) {
    std::fprintf(stderr, "\n--- global variables ---\n");
    for (const auto& g : machine.globals())
      std::fprintf(stderr, "  &%-4s %12d  %s\n", g.first.c_str(), g.second.value,
                   g.second.is_integer ? base64_encode_int(g.second.value).c_str() : "(char)");
  }

  if (visits) {
    const auto& vm = machine.visit_map();
    std::fprintf(stderr, "\n--- execution map (. = never executed) ---\n");
    for (std::size_t y = 0; y < grid->height(); ++y) {
      for (std::size_t x = 0; x < grid->width(); ++x) {
        char c = vm[y * grid->width() + x];
        std::fputc(c ? c : '.', stderr);
      }
      std::fputc('\n', stderr);
    }
  }
  return 0;
}
