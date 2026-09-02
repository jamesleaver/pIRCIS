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

#include "Machine.h"

#include <algorithm>
#include <utility>

namespace ircis {

  Machine::Machine(std::shared_ptr<Grid> grid, OutputSink* sink, MachineOptions opt)
    : runner_id_(0),
      log_(std::make_shared<Logger>(sink)),
      grid_(grid),
      global_var_map_(std::make_shared<variable_map_t>()),
      new_runners_list_(std::make_shared<std::queue<RunnerInfo> >()),
      opt_(opt),
      rng_(opt.rng_seed) {
    if (opt_.record_visits)
      visit_map_.assign(grid_->width() * grid_->height(), 0);
    runner_list_.emplace_back(runner_id_++,
                              DirVec(opt_.start_x, opt_.start_y, opt_.start_direction),
                              grid_, log_, global_var_map_, new_runners_list_, &rng_);
  }

  bool Machine::update() {
    if (finished_) return false;
    ++step_number_;
    bool keep_moving = false;

    // The original walked the vector with `runner_list_.erase(it--)`, which is
    // undefined when the first runner dies. Index form, same visit order.
    for (std::size_t i = 0; i < runner_list_.size(); ) {
      Runner& r = runner_list_[i];
      if (opt_.record_visits && !r.paused()) {
        const DirVec& p = r.position();
        if (grid_->is_inside(p)) {
          char& slot = visit_map_[p.get_y() * grid_->width() + p.get_x()];
          // First execution wins: a cell can be re-entered later on a different
          // heading (column 12 is both a literal and a northbound corridor),
          // but the first pass is the one that parsed it.
          if (!slot) slot = to_char(p.get_direction());
        }
      }
      if (r.step()) {
        keep_moving = true;
        ++i;
      }
      else {
        Logger::log_line("Runner ", r.get_id(), " died.");
        deaths_.push_back({r.get_id(), step_number_, r.steps_taken(), r.error(),
                           static_cast<int>(r.position().get_y()),
                           static_cast<int>(r.position().get_x())});
        dead_runner_steps_ += r.steps_taken();
        stack_ub_reads_ += r.stack().ub_reads();
        runner_list_.erase(runner_list_.begin() + i);
      }
    }

    while (!new_runners_list_->empty()) {
      Logger::log_line("Adding new Runner");
      keep_moving = true;
      RunnerInfo info = new_runners_list_->front();
      runner_list_.emplace_back(runner_id_++, info.position, grid_, log_, global_var_map_,
                                new_runners_list_, &rng_, info.st, info.var_map, info.trail);
      new_runners_list_->pop();
    }

    if (!keep_moving) {
      // Upstream prints a space and a newline here, which on a terminal puts
      // a blank line after the run. Here the output is a buffer that the
      // screen, the saved file and the web page all show, and for a program
      // whose output already ended in a newline it became a second, blank
      // line that appeared the moment the run finished and moved everything
      // above it. The program's own output is left exactly as it printed it.
      Logger::log_line_dbg("Ircis has finished running!");
      finished_ = true;
    }
    return keep_moving;
  }

  unsigned long Machine::total_runner_steps() const {
    unsigned long total = dead_runner_steps_;
    for (const auto& r : runner_list_) total += r.steps_taken();
    return total;
  }

  std::vector<std::pair<std::string, Data> > Machine::globals() const {
    std::vector<std::pair<std::string, Data> > out;
    out.reserve(global_var_map_->size());
    for (const auto& kv : *global_var_map_) out.push_back(kv);
    for (std::size_t i = 1; i < out.size(); ++i)          // insertion sort by name
      for (std::size_t j = i; j > 0 && out[j].first < out[j - 1].first; --j)
        std::swap(out[j], out[j - 1]);
    return out;
  }

  unsigned long Machine::stack_ub_reads() const {
    unsigned long total = stack_ub_reads_;
    for (const auto& r : runner_list_) total += r.stack().ub_reads();
    return total;
  }
}
