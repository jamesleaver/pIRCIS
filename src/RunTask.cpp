// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#include "RunTask.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>

#include "Machine.h"
#include "Platform.h"

namespace run {
namespace {

  enum class Cmd : uint8_t { Run, Pause, Step, Reset, Load, StepBack, RunToEnd,
                            SetCell, Rebuild };
  struct Command { Cmd cmd; uint32_t arg; };

  plat::Mutex        g_mutex;
  std::deque<Command> g_queue;

  volatile Speed g_speed = Speed::Medium;   // written by the UI thread, read here
  Snapshot    g_snap;
  std::string g_output;
  uint32_t    g_outputVersion = 0;
  // Bumped only when the machine is rebuilt -- a reset, a reload, an edit --
  // never when it merely prints. The UI watches this to know the grid it is
  // drawing has been replaced; watching outputVersion instead meant a full
  // repaint for every character a program wrote.
  uint32_t    g_buildVersion = 0;
  bool        g_outputTruncated = false;
  static constexpr std::size_t kMaxOutput = 16384;

  prog::Program g_loaded;
  // The seed the current run is using. It changes when a run starts from the
  // beginning, so `r` and `R` differ run to run -- and stays put for the rest
  // of that run, because step-back replays from zero and has to land in the
  // same place it left.
  uint32_t g_seed = 1;
  prog::Program g_pending;
  bool          g_hasPending = false;
  int  g_startCol = 0, g_startRow = 0;
  char g_startDir = 'E';

  // Sized for the largest loadable program. The
// interpreter's visit map is laid out with the LOADED grid's width as its
// stride, not this buffer's, so the stride has to be carried alongside.
char g_visits[prog::kMaxRows * prog::kMaxCols] = {0};
int  g_visitCols = 0;
  std::vector<GlobalVar> g_globals;
  std::vector<Chunk> g_chunks;
  bool g_replaying = false;   // silence the console while replaying for a rewind

  // Program output. Mirrored to the console as it is produced, so a long run
  // can be watched from a terminal instead of the 240 px screen.
  struct TaskSink : ircis::OutputSink {
    void write(const std::string& text) override {
      if (!g_replaying) plat::log(text.c_str());
      plat::Guard g(g_mutex);
      if (g_output.size() + text.size() <= kMaxOutput) {
        if (g_chunks.size() < 4096)
          g_chunks.push_back({(uint16_t)g_output.size(), (uint16_t)text.size()});
        g_output += text;
      }
      else g_outputTruncated = true;
      ++g_outputVersion;
    }
    void newline() override {
      if (!g_replaying) plat::logln();
      plat::Guard g(g_mutex);
      if (g_output.size() + 1 <= kMaxOutput) g_output += '\n';
      else g_outputTruncated = true;
      ++g_outputVersion;
    }
  };

  TaskSink g_sink;
  std::unique_ptr<ircis::Machine> g_machine;
  std::shared_ptr<ircis::Grid>    g_grid;
  bool     g_running = false;
  uint32_t g_startMs = 0;
  uint32_t g_runMs   = 0;

  // Caller already holds the mutex.
  void setEventLocked(const char* text) {
    std::strncpy(g_snap.lastEvent, text, sizeof(g_snap.lastEvent) - 1);
    g_snap.lastEvent[sizeof(g_snap.lastEvent) - 1] = 0;
  }

  // g_snap is copied wholesale by snapshot() on the UI thread, so every write
  // to it -- including this 56-byte string -- has to be under the mutex.
  void setEvent(const char* text) {
    plat::Guard g(g_mutex);
    setEventLocked(text);
  }

  void buildMachine(const prog::Program& s) {
    plat::Guard g(g_mutex);
    g_loaded = s;
    g_grid = std::make_shared<ircis::Grid>(s.lines());
    ircis::MachineOptions opt;
    opt.record_visits = true;
    opt.rng_seed = g_seed;
    opt.start_x = g_startCol;
    opt.start_y = g_startRow;
    opt.start_direction = g_startDir;
    g_machine.reset(new ircis::Machine(g_grid, &g_sink, opt));
    g_output.clear();
    g_outputTruncated = false;
    ++g_outputVersion;
    std::memset(g_visits, 0, sizeof(g_visits));
    g_visitCols = 0;
    g_globals.clear();
    g_chunks.clear();
    ++g_buildVersion;
    g_running = false;
    g_runMs = 0;
    g_snap = Snapshot();
    g_snap.loaded = true;
    setEventLocked("ready");   // buildMachine already holds the mutex
  }

  void publish() {
    plat::Guard g(g_mutex);
    if (!g_machine) return;
    g_snap.step = g_machine->step_number();
    g_snap.running = g_running;
    g_snap.finished = g_machine->finished();
    g_snap.runnersCreated = (uint8_t)g_machine->runners_created();
    g_snap.deaths = (uint8_t)g_machine->deaths().size();
    g_snap.oobReads = g_machine->out_of_bounds_reads();
    g_snap.ubReads = g_machine->stack_ub_reads();
    g_snap.elapsedMs = g_runMs + (g_running ? (plat::millis() - g_startMs) : 0);

    // Slots are addressed by runner id so a death leaves its row in place.
    for (int i = 0; i < kMaxRunners; ++i) {
      g_snap.runners[i].id = (int8_t)i;
      g_snap.runners[i].used = false;
      g_snap.runners[i].alive = false;
      g_snap.runners[i].trailLen = 0;
    }
    for (const auto& d : g_machine->deaths()) {
      if (d.runner_id < 0 || d.runner_id >= kMaxRunners) continue;
      RunnerView& v = g_snap.runners[d.runner_id];
      v.used = true;
      v.alive = false;
      v.diedStep = d.step;
    }
    // '!' and walking off the edge are how a program is supposed to stop, so
    // neither is worth a line. Anything else is a real fault and is the only
    // explanation the interpreter will ever give for it.
    g_snap.deathNoteCount = 0;
    for (const auto& d : g_machine->deaths()) {
      if (g_snap.deathNoteCount >= kMaxDeathNotes) break;
      if (d.error.empty() ||
          d.error == "End character reached" ||
          d.error == "Runner went outside grid") continue;
      DeathNote& note = g_snap.deathNotes[g_snap.deathNoteCount++];
      note.id   = (int8_t)d.runner_id;
      note.step = d.step;
      std::strncpy(note.why, d.error.c_str(), sizeof(note.why) - 1);
      note.why[sizeof(note.why) - 1] = 0;
    }

    std::size_t n = g_machine->runner_count();
    g_snap.runnerCount = (uint8_t)(n > kMaxRunners ? kMaxRunners : n);
    for (std::size_t i = 0; i < n; ++i) {
      const ircis::Runner& r = g_machine->runner(i);
      int id = r.get_id();
      if (id < 0 || id >= kMaxRunners) continue;
      RunnerView& v = g_snap.runners[id];
      v.used = true;
      v.alive = true;
      v.x = (uint8_t)r.position().get_x();
      v.y = (uint8_t)r.position().get_y();
      v.dir = ircis::to_char(r.position().get_direction());
      v.paused = r.paused();

      const ircis::Trail& t = r.trail();
      uint8_t want = (uint8_t)(t.size() < kTrailView ? t.size() : kTrailView);
      v.trailLen = want;
      for (uint8_t a = 0; a < want; ++a) {
        const ircis::DirVec& p = t.at(a);
        v.trailX[a] = (uint8_t)p.get_x();
        v.trailY[a] = (uint8_t)p.get_y();
      }
    }

    g_globals.clear();
    for (const auto& kv : g_machine->globals()) {
      GlobalVar gv{};
      std::strncpy(gv.name, kv.first.c_str(), sizeof(gv.name) - 1);
      gv.value = kv.second.value;
      gv.isInt = kv.second.is_integer;
      g_globals.push_back(gv);
    }
    const auto& vm = g_machine->visit_map();
    if (!vm.empty() && vm.size() <= sizeof(g_visits)) {
      std::memcpy(g_visits, vm.data(), vm.size());
      g_visitCols = g_grid ? (int)g_grid->width() : 0;
    }
  }

  // Steps executed per scheduling slice, and the pause between slices.
  // FAST is deliberately watchable -- about 6k steps/sec, so a full run takes
  // half a minute and you can still see where the runners are.
  // Steps per slice, and how long the run task sleeps between slices. One step
  // per slice is what makes SLOW and FAST watchable: the pause is the point.
  uint32_t budgetFor(Speed s) {
    switch (s) {
      case Speed::Slow:  return 1;
      case Speed::Medium:  return 1;
      case Speed::Quick: return 40;
      case Speed::Full:  return 20000;
    }
    return 1;
  }
  uint32_t delayFor(Speed s) {
    switch (s) {
      case Speed::Slow:  return 320;   // ~3 steps/sec
      case Speed::Medium:  return 40;    // ~25 steps/sec
      case Speed::Quick: return 20;    // ~2000 steps/sec
      case Speed::Full:  return 0;
    }
    return 40;
  }

  // At QUICK a program is gone before the eye finds the first runner, so a run
  // opens slow enough to see the runners set off and then gets out of the way.
  // About a second in total: easing all the way down from SLOW took five,
  // which reads as the device being broken rather than considerate.
  //
  // FULL is exempt. Asking for FULL is asking for the answer, not for the
  // performance, and a second of ceremony in front of it is a second spent
  // watching something you said you did not want to watch.
  constexpr uint32_t kEaseSteps   = 16;
  constexpr uint32_t kEaseStartMs = 120;
  uint32_t g_sinceStart = 0;     // steps since play was last pressed

  bool easing(Speed s) { return s != Speed::Full && g_sinceStart < kEaseSteps; }

  uint32_t easedBudget(Speed s) {
    return easing(s) ? 1u : budgetFor(s);
  }
  uint32_t easedDelay(Speed s) {
    const uint32_t want = delayFor(s);
    if (!easing(s)) return want;
    const uint32_t from = kEaseStartMs;
    if (want >= from) return want;                 // already slow enough
    return from - (from - want) * g_sinceStart / kEaseSteps;
  }

  void runSteps(uint32_t n) {
    if (!g_machine) return;
    for (uint32_t i = 0; i < n; ++i) {
      if (!g_machine->update()) {
        g_running = false;
        g_runMs += plat::millis() - g_startMs;
        const auto& d = g_machine->deaths();
        char buf[56];
        snprintf(buf, sizeof(buf), "finished: %u steps, %u runners",
                 (unsigned)g_machine->step_number(), (unsigned)d.size());
        setEvent(buf);
        plat::logf("\n[run] finished at step %u\n", (unsigned)g_machine->step_number());
        for (const auto& e : d)
          plat::logf("[run] runner %d died at step %u: %s\n",
                     e.runner_id, (unsigned)e.step, e.error.c_str());
        return;
      }
    }
  }

  bool popCommand(Command& out) {
    plat::Guard g(g_mutex);
    if (g_queue.empty()) return false;
    out = g_queue.front();
    g_queue.pop_front();
    return true;
  }

  void send(Cmd cmd, uint32_t arg = 0) {
    plat::Guard g(g_mutex);
    g_queue.push_back({cmd, arg});
  }

  void taskFn(void*) {
    buildMachine(prog::Program());
    for (;;) {
      Command c;
      while (popCommand(c)) {
        switch (c.cmd) {
          case Cmd::Load: {
            bool pending;
            prog::Program next;
            { plat::Guard g(g_mutex); pending = g_hasPending; next = g_pending; g_hasPending = false; }
            if (pending) { g_seed = plat::randomSeed(); buildMachine(next); }
            break;
          }
          // Row, column and character packed into the one word the queue
          // carries. The grid is at most 32 x 96, so a byte each is plenty.
          case Cmd::SetCell: {
            plat::Guard g(g_mutex);
            g_loaded.setCell((int)((c.arg >> 16) & 0xFF),
                             (int)((c.arg >> 8) & 0xFF),
                             (char)(c.arg & 0xFF));
            break;
          }

          case Cmd::Rebuild:
            g_seed = plat::randomSeed();
            buildMachine(g_loaded);
            break;

          case Cmd::Reset:
            g_seed = plat::randomSeed();   // back to the top means a new race
            buildMachine(g_loaded);
            break;
          case Cmd::Run:
            // Play on a finished run means "go again". Deciding that here
            // rather than in the UI is what makes it reliable: the UI works
            // from a published snapshot, which is not necessarily current at
            // the moment the button is pressed, and every route to Run --
            // the button, the tab shortcut, the console -- gets it.
            if (g_machine && g_machine->finished()) {
              g_seed = plat::randomSeed();
              buildMachine(g_loaded);
              g_runMs = 0;
            }
            if (g_machine && !g_machine->finished()) {
              g_running = true; g_sinceStart = 0;   // ease in from here
              g_startMs = plat::millis(); setEvent("running");
            }
            break;
          case Cmd::Pause:
            if (g_running) { g_running = false; g_runMs += plat::millis() - g_startMs; }
            setEvent("paused");
            break;
          case Cmd::Step:
            if (g_running) { g_running = false; g_runMs += plat::millis() - g_startMs; }
            g_startMs = plat::millis();
            runSteps(c.arg);
            g_runMs += plat::millis() - g_startMs;
            setEvent("stepped");
            break;
          case Cmd::StepBack: {
            uint32_t target = g_machine ? g_machine->step_number() : 0;
            if (target == 0) break;
            --target;
            // No local copy: prog::Program is over 3 KB and this task has
            // 12 KB of stack. buildMachine() reads g_loaded and writes it
            // straight back, so passing it directly is both correct and free.
            g_replaying = true;
            buildMachine(g_loaded);
            for (uint32_t i = 0; i < target; ++i) {
              if (!g_machine->update()) break;
              // Replaying 150,000 steps must not starve the watchdog.
              if ((i & 0x3FFF) == 0x3FFF) plat::taskYield(0);
            }
            g_replaying = false;
            publish();
            setEvent("stepped back");
            break;
          }
          case Cmd::RunToEnd: {
            if (g_running) { g_running = false; g_runMs += plat::millis() - g_startMs; }
            g_startMs = plat::millis();
            // A program the user wrote may never terminate, and an infinite
            // loop is an ordinary thing to write while learning a 2D language.
            // Without the yield this starves the watchdog and resets the
            // board; without the ceiling it never comes back at all.
            constexpr uint32_t kSlice = 20000;
            constexpr uint32_t kCeiling = 5000000;   // as the golden test uses
            uint32_t done = 0;
            while (g_machine && !g_machine->finished() && done < kCeiling) {
              runSteps(kSlice);
              done += kSlice;
              plat::taskYield(0);
            }
            g_runMs += plat::millis() - g_startMs;
            setEvent(g_machine && !g_machine->finished() ? "stopped: no end in sight"
                                                         : "ran to end");
            break;
          }
        }
      }

      Speed sp = g_speed;   // single byte; volatile read is enough on both targets
      if (g_running) {
        const uint32_t n = easedBudget(sp);
        runSteps(n);
        if (g_sinceStart < kEaseSteps) g_sinceStart += n;
      }
      publish();
      plat::taskYield(g_running ? easedDelay(sp) : 20);
    }
  }
}

  void begin() { plat::startTask(taskFn, nullptr, "ircis", 12288); }

  void load(const prog::Program& s) {
    { plat::Guard g(g_mutex); g_pending = s; g_hasPending = true; }
    send(Cmd::Load);
  }

  void setCell(int row, int col, char ch) {
    if (row < 0 || row > 0xFF || col < 0 || col > 0xFF) return;
    send(Cmd::SetCell, ((uint32_t)row << 16) | ((uint32_t)col << 8) | (uint8_t)ch);
  }
  void rebuild() { send(Cmd::Rebuild); }

  void cmdRun()   { send(Cmd::Run); }
  void cmdPause() { send(Cmd::Pause); }
  void cmdStep(uint32_t steps) { send(Cmd::Step, steps); }
  void cmdReset() { send(Cmd::Reset); }
  void setStart(int col, int row, char dir) {
    plat::Guard g(g_mutex);
    g_startCol = col; g_startRow = row; g_startDir = dir;
  }
  int  startCol() { plat::Guard g(g_mutex); return g_startCol; }
  int  startRow() { plat::Guard g(g_mutex); return g_startRow; }
  char startDir() { plat::Guard g(g_mutex); return g_startDir; }

  void cmdStepBack() { send(Cmd::StepBack); }
  void cmdRunToEnd() { send(Cmd::RunToEnd); }

  void setSpeed(Speed s) { g_speed = s; }
  Speed speed() { return g_speed; }

  Snapshot snapshot() { plat::Guard g(g_mutex); return g_snap; }

  std::string output() {
    plat::Guard g(g_mutex);
    if (g_outputTruncated) return g_output + "\n[output truncated -- see the console or SD]";
    return g_output;
  }
  uint32_t outputVersion() { plat::Guard g(g_mutex); return g_outputVersion; }
  uint32_t buildVersion()  { plat::Guard g(g_mutex); return g_buildVersion; }

  void loadedGridInto(prog::Program& out) { plat::Guard g(g_mutex); out = g_loaded; }
  // Copied under the mutex rather than handed out as a raw pointer: the
  // interpreter thread memcpy's this whole array in publish().
  char visitAt(int row, int col) {
    if (g_visitCols <= 0) return 0;
    if (row < 0 || col < 0 || col >= g_visitCols) return 0;
    std::size_t i = (std::size_t)row * g_visitCols + col;
    if (i >= sizeof(g_visits)) return 0;
    plat::Guard g(g_mutex);
    return g_visits[i];
  }
  int visitsInto(char* out, unsigned long n) {
    plat::Guard g(g_mutex);
    if (g_visitCols <= 0) return 0;
    unsigned long have = sizeof(g_visits);
    std::memcpy(out, g_visits, n < have ? n : have);
    return g_visitCols;
  }

  std::vector<GlobalVar> globals() { plat::Guard g(g_mutex); return g_globals; }
  std::vector<Chunk> chunks() { plat::Guard g(g_mutex); return g_chunks; }
}
