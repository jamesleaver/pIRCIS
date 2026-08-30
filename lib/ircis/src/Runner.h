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

#include "IrcisConfig.h"
#include "CharMaps.h"
#include "DataType.h"
#include "RunnerStack.h"
#include "DirVec.h"
#include "Logger.h"
#include "Grid.h"

#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace ircis {
  enum Mode {
    NONE = 0,
    STACK,
    STACK_POP,
    STACK_PUSH
  };

  inline std::string to_str(Mode m) { return std::to_string(static_cast<int>(m)); }

  // Characters that end each mode (was a static std::unordered_map).
  const char* mode_end_chars(Mode mode);
  bool is_mode_end_char(Mode mode, char current_char);

  extern const char kBase64Chars[];   // A-Z a-z 0-9 + /
  bool isbase64(char current_char);
  int base64_decode_int(std::string input);
  std::string base64_encode_int(int value);

  typedef std::unordered_map<std::string, Data> variable_map_t;

  // Fixed-size ring of recent positions, for drawing the runner's tail.
  // The original kept the full path in a std::vector<DirVec>, which for a
  // long run is hundreds of thousands of positions and megabytes of RAM.
  class Trail {
  public:
    void push(const DirVec& p) {
      buf_[head_] = p;
      head_ = static_cast<uint16_t>((head_ + 1) % IRCIS_TRAIL_LEN);
      if (count_ < IRCIS_TRAIL_LEN) ++count_;
    }
    uint16_t size() const { return count_; }
    // 0 == most recent
    const DirVec& at(uint16_t age) const {
      int idx = static_cast<int>(head_) - 1 - static_cast<int>(age);
      while (idx < 0) idx += IRCIS_TRAIL_LEN;
      return buf_[idx];
    }
  private:
    DirVec buf_[IRCIS_TRAIL_LEN];
    uint16_t count_ = 0;
    uint16_t head_ = 0;
  };

  struct RunnerInfo {
    DirVec position;
    RunnerStack st;
    variable_map_t var_map;
    Trail trail;
  };

  // Deterministic replacement for std::random_device/default_random_engine.
  // Reachable only through the 'r' and 'R' opcodes. The golden programs never
  // execute them, which the golden test proves: their step counts are
  // identical run to run.
  class Rng {
  public:
    explicit Rng(uint32_t seed = 1u) : state_(seed ? seed : 1u) { }
    uint32_t next() {
      state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
      return state_;
    }
  private:
    uint32_t state_;
  };

  class Runner {
  public:
    Runner(int id, DirVec init_pos, std::shared_ptr<Grid> grid, std::shared_ptr<Logger> log,
           std::shared_ptr<variable_map_t> global_var_map,
           std::shared_ptr<std::queue<RunnerInfo> > new_runners_list,
           Rng* rng)
      : id_(id), position_(init_pos), grid_(grid), log_(log),
        global_var_map_(global_var_map), new_runners_list_(new_runners_list), rng_(rng) {
      Logger::log_line("Created Runner at position ", position_);
      mode_ = Mode::NONE;
      stack_mode_ = false;
      integer_mode_ = false;
      base64_mode = false;
    }
    Runner(int id, DirVec init_pos, std::shared_ptr<Grid> grid, std::shared_ptr<Logger> log,
           std::shared_ptr<variable_map_t> global_var_map,
           std::shared_ptr<std::queue<RunnerInfo> > new_runners_list, Rng* rng,
           RunnerStack st, variable_map_t var_map, Trail trail)
      : Runner(id, init_pos, grid, log, global_var_map, new_runners_list, rng) {
      st_ = st;
      var_map_ = var_map;
      trail_ = trail;
    }

    // Update the Runner movement. Returns true if the Runner is still alive.
    bool step();

    int get_id() const { return id_; }
    const DirVec& position() const { return position_; }
    const Trail& trail() const { return trail_; }
    const RunnerStack& stack() const { return st_; }
    const std::string& error() const { return err_str_; }
    bool paused() const { return pause_time_ != 0; }
    int pause_remaining() const { return pause_time_; }
    unsigned long steps_taken() const { return steps_taken_; }

  private:
    bool process_char(char current_char);
    bool process_integer_buffer();
    bool process_mode_buffer();
    bool process_split();
    bool process_stack_pop();
    bool process_stack_push();
    bool process_global_var_fetch();
    bool process_global_var_insert();
    bool process_local_var_fetch();
    bool process_local_var_insert();
    void push_random_number_to_stack(int limit);

    int id_ = 0;
    DirVec position_;
    RunnerStack st_;
    variable_map_t var_map_;

    Mode mode_;
    bool stack_mode_;
    bool integer_mode_;
    bool base64_mode;
    std::string mode_buffer_;
    std::string integer_mode_buffer_;
    int pause_time_ = 0;
    unsigned long steps_taken_ = 0;

    std::shared_ptr<Grid> grid_;
    std::shared_ptr<Logger> log_;
    std::shared_ptr<variable_map_t> global_var_map_;
    std::shared_ptr<std::queue<RunnerInfo> > new_runners_list_;
    Rng* rng_;

    Trail trail_;

    template <typename... Types>
    void set_error(const Types&... vars) { err_str_ = cat(vars...); }

    template <typename... Types>
    void log_line(const Types&... vars) { Logger::log_line("Runner ", get_id(), ": ", vars...); }
    template <typename... Types>
    void err_line(const Types&... vars) { Logger::err_line("Runner ", get_id(), ": ", vars...); }

    std::string err_str_;
#if IRCIS_TRACK_PROCESSED_CHARS
    std::string processed_chars_;
#endif
  };
}
