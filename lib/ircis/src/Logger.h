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
#include "DataType.h"
#include "DirVec.h"
#include "Sink.h"

#include <string>

namespace ircis {

  // -- string conversion used by the trace/error machinery --
  inline std::string to_str(const char* v)        { return v ? std::string(v) : std::string(); }
  inline std::string to_str(char v)               { return std::string(1, v); }
  inline std::string to_str(const std::string& v) { return v; }
  inline std::string to_str(int v)                { return std::to_string(v); }
  inline std::string to_str(unsigned int v)       { return std::to_string(v); }
  inline std::string to_str(long v)               { return std::to_string(v); }
  inline std::string to_str(unsigned long v)      { return std::to_string(v); }
  inline std::string to_str(bool v)               { return v ? "1" : "0"; }

  inline std::string cat() { return std::string(); }
  template <typename T, typename... Rest>
  std::string cat(const T& val, const Rest&... rest) {
    return to_str(val) + cat(rest...);
  }

  // Program output + (optionally) the interpreter's internal trace.
  class Logger {
  public:
    explicit Logger(OutputSink* sink) : sink_(sink) { }

    void set_sink(OutputSink* sink) { sink_ = sink; }

    void print(const std::string& val) { if (sink_) sink_->write(val); }
    void print_line()                  { if (sink_) sink_->newline(); }
    void print_line(const std::string& val) { print(val); print_line(); }

  private:
    OutputSink* sink_;

    // -- internal trace --
  public:
#if IRCIS_DEBUG_LOG
    typedef void (*TraceFn)(const char* line);
    static void set_trace(TraceFn fn) { trace_ = fn; }
    template <typename... Types> static void log_line(const Types&... vars) {
      if (trace_) trace_(("DEBUG: " + cat(vars...)).c_str());
    }
    template <typename... Types> static void err_line(const Types&... vars) {
      if (trace_) trace_(("ERROR: " + cat(vars...)).c_str());
    }
  private:
    static TraceFn trace_;
  public:
#else
    template <typename... Types> static void log_line(const Types&...) { }
    template <typename... Types> static void err_line(const Types&...) { }
#endif
    template <typename... Types> static void log_line_dbg(const Types&... vars) { log_line(vars...); }
    template <typename... Types> static void err_line_dbg(const Types&... vars) { err_line(vars...); }
  };
}
