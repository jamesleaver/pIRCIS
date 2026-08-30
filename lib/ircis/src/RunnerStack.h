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

#include "DataType.h"

#include <vector>

namespace ircis {
  // Custom stack with relative indexing -- verbatim semantics, but reads that
  // the original would have performed out of bounds (undefined behaviour on
  // the desktop build, a crash on the ESP32) are counted and answered with a
  // zero Data instead. ub_reads() must stay 0 for a faithful run.
  class RunnerStack {
  public:
    Data& operator[](int index) {
      if (index > 0) {
        if (static_cast<std::size_t>(index) >= container_.size()) return bad_read();
        return container_[index];
      }
      index -= 1;
      if (static_cast<std::size_t>(-index) > container_.size()) return bad_read();
      return container_.end()[index];
    }

    void push(const Data data) { container_.push_back(data); }
    const Data& top() const {
      if (container_.empty()) { ++ub_reads_; return fallback_; }
      return container_.back();
    }
    Data& top() {
      if (container_.empty()) return bad_read();
      return container_.back();
    }
    std::size_t size() const { return container_.size(); }
    void pop() { if (!container_.empty()) container_.pop_back(); }
    bool empty() const { return container_.empty(); }

    unsigned long ub_reads() const { return ub_reads_; }

  private:
    Data& bad_read() { ++ub_reads_; fallback_ = Data(); return fallback_; }

    std::vector<Data> container_;
    mutable Data fallback_;
    mutable unsigned long ub_reads_ = 0;
  };
}
