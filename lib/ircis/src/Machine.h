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

#include "Grid.h"
#include "Logger.h"
#include "Runner.h"
#include "Sink.h"

#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace ircis {

  struct MachineOptions {
    int start_x = 0;
    int start_y = 0;
    char start_direction = 'E';
    uint32_t rng_seed = 1;
    // Record, for every grid cell, whether it was executed and in which
    // direction. 1 byte per cell; lets the editor show reading direction
    // instead of guessing it.
    bool record_visits = false;
    // Memory protection, added for pIRCIS. A split in a loop doubles the
    // runners every pass, and a push in a loop grows a stack without end;
    // the reference build lets the process die of it. low_memory, when set,
    // is asked before a new runner is started or a stack grown, and answers
    // for the device it runs on; max_runners, when not 0, is a ceiling on
    // live runners for a machine with more memory than sense. A refused
    // runner is recorded as a death with its reason, so the readout says so.
    bool (*low_memory)() = nullptr;
    std::size_t max_runners = 0;
  };

  // Deaths kept for the readout and the report. The oldest go once there are
  // more than this: a loop that sheds a runner every pass would otherwise
  // keep a record of every one of them for as long as it ran.
  constexpr std::size_t kMaxDeathsKept = 256;

  struct Death {
    int runner_id;
    uint32_t step;
    unsigned long runner_steps;
    std::string error;
    // Where it was standing when it died. Added for pIRCIS: the interpreter
    // does not use it, and recording it does not change how anything runs.
    int row = -1, col = -1;
  };

  // Was Ircis::Ircis. Steps all runners; returns false once all are dead.
  class Machine {
  public:
    Machine(std::shared_ptr<Grid> grid, OutputSink* sink, MachineOptions opt = MachineOptions());

    bool update();

    uint32_t step_number() const { return step_number_; }
    bool finished() const { return finished_; }

    std::size_t runner_count() const { return runner_list_.size(); }
    const Runner& runner(std::size_t i) const { return runner_list_[i]; }

    const std::vector<Death>& deaths() const { return deaths_; }
    int runners_created() const { return runner_id_; }

    // Total steps executed across all runners (alive + dead).
    unsigned long total_runner_steps() const;

    // 0 = never executed, otherwise 'N'/'E'/'W'/'S' of the last execution.
    const std::vector<char>& visit_map() const { return visit_map_; }

    // The program's global variables, sorted by name.
    std::vector<std::pair<std::string, Data> > globals() const;

    // Diagnostics: both must be 0 for a run to be considered faithful.
    unsigned long out_of_bounds_reads() const { return grid_->out_of_bounds_reads(); }
    unsigned long stack_ub_reads() const;

    const Grid& grid() const { return *grid_; }

  private:
    int runner_id_;
    uint32_t step_number_ = 0;
    bool finished_ = false;
    unsigned long dead_runner_steps_ = 0;

    std::shared_ptr<Logger> log_;
    std::shared_ptr<Grid> grid_;
    std::shared_ptr<variable_map_t> global_var_map_;
    std::shared_ptr<std::queue<RunnerInfo> > new_runners_list_;
    std::vector<Runner> runner_list_;
    void record_death(Death d);
    std::vector<Death> deaths_;
    std::vector<char> visit_map_;
    unsigned long stack_ub_reads_ = 0;
    MachineOptions opt_;
    Rng rng_;
  };
}
