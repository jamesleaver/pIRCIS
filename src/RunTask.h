// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IrcisConfig.h"   // IRCIS_TRAIL_LEN

#include "Program.h"

// The interpreter runs in its own FreeRTOS task pinned to core 1; the UI and
// touch polling stay on core 0. They communicate through a command queue and a
// mutex-guarded snapshot, so a full-speed run never blocks the display.
namespace run {

  // There is no single-step mode: the transport's forward and back buttons do
  // that, whatever speed is selected.
  // Rates, not budgets. SLOW and FAST were once tuned for a program of a
  // couple of hundred thousand steps and were far too quick for an ordinary
  // one, which is a few hundred: hello_world finished in 142 steps before you
  // could see a runner move. SLOW and FAST are now slow enough to follow a
  // single runner by eye; QUICK is for the long ones.
  enum class Speed : uint8_t {
    Slow  = 0,   // ~3 steps/sec    -- watch each move land
    Medium = 1,   // ~25 steps/sec   -- follow a runner without losing it
    Quick = 2,   // ~2000 steps/sec -- a long program in a minute or two
    Full  = 3    // unthrottled
  };

  static constexpr int kMaxRunners = 8;
  // How much of each runner's tail the UI is given. Kept equal to
  // IRCIS_TRAIL_LEN so the interpreter records exactly what is drawn.
  static constexpr int kTrailView = IRCIS_TRAIL_LEN;

  // Indexed by runner id, not by position in the live list, so a runner that
  // has died keeps its row instead of vanishing from the readout.
  struct RunnerView {
    int8_t  id;
    bool    used;        // this runner was created at some point
    bool    alive;
    uint32_t diedStep;
    uint8_t x, y;
    char    dir;
    bool    paused;
    uint8_t trailLen;                 // 0 = none; [0] is the most recent
    uint8_t trailX[kTrailView];
    uint8_t trailY[kTrailView];
  };

  // One value printed by the program. The interpreter emits output a chunk at
  // a time, so the boundaries are known exactly rather than guessed from the
  // string.
  struct Chunk {
    uint16_t start;
    uint16_t len;
  };

  // A global variable of the running program.
  struct GlobalVar {
    char name[8];
    int  value;
    bool isInt;
  };

  // Dying is ordinary in IRCIS: a runner reaches '!' or walks off the grid,
  // and both are how programs are meant to end. A death for any other reason
  // is the only diagnostic the language offers, so those are the ones carried
  // -- a handful of them, because this is a diagnostic and not a log.
  static constexpr int kMaxDeathNotes = 4;
  struct DeathNote {
    int8_t   id;
    uint32_t step;
    char     why[40];
  };

  struct Snapshot {
    uint32_t step = 0;
    bool     running = false;
    bool     finished = false;
    bool     loaded = false;
    uint8_t  runnerCount = 0;
    uint8_t  runnersCreated = 0;
    RunnerView runners[kMaxRunners];
    uint8_t  deaths = 0;
    uint32_t elapsedMs = 0;
    unsigned long oobReads = 0;
    unsigned long ubReads = 0;
    char     lastEvent[56] = {0};
    uint8_t   deathNoteCount = 0;
    DeathNote deathNotes[kMaxDeathNotes] = {};
  };

  void begin();

  // Reset the machine onto a copy of this grid. Stops any run in progress.
  // IRCIS lets a program start from any cell heading any direction; the
  // default entry point is (0,0) heading east.
  void load(const prog::Program& s);
  void setStart(int col, int row, char dir);
  int  startCol();
  int  startRow();
  char startDir();

  // One cell of the loaded grid, without copying the grid to get at it. The
  // machine is deliberately NOT rebuilt: the editor sends a burst of these
  // while you type and then asks for a single rebuild, which is the expensive
  // half. Out-of-range coordinates are ignored.
  void setCell(int row, int col, char ch);
  // Rebuild the machine from the loaded grid as it now stands.
  void rebuild();

  void cmdRun();
  void cmdPause();
  void cmdStep(uint32_t steps);
  void cmdReset();
  // Rewind one step. The interpreter has no undo, so this rebuilds the machine
  // and replays to step-1 with the console silenced. Cheap early in a run,
  // proportional to the step count later on.
  void cmdStepBack();
  // Run to completion as fast as the machine will go, ignoring the speed.
  void cmdRunToEnd();
  void setSpeed(Speed s);
  Speed speed();

  Snapshot snapshot();

  // Program output produced so far. `version` increments on every append, so
  // the UI can tell whether it needs to redraw without copying the string.
  std::string output();
  uint32_t outputVersion();
  // Changes when the machine is rebuilt, not when it prints.
  uint32_t buildVersion();

  // A copy of the grid the machine is actually running (taken at load time).
  // Copies the grid the interpreter is running INTO the caller's object.
  // It does not return one: prog::Program is over 3 KB, and a by-value
  // return put that on the caller's stack. On the board that overflowed the
  // 8 KB Arduino loop task and panicked on the first touch.
  void loadedGridInto(prog::Program& out);

  // 0 = never executed, else 'N'/'E'/'W'/'S' of the first execution.
  char visitAt(int row, int col);
  // The whole map at once, into a caller-owned buffer. Returns the column
  // stride, or 0 if nothing has run. visitAt() takes the mutex per cell, which
  // is a thousand locks per repaint -- far too many to do while drawing.
  int visitsInto(char* out, unsigned long n);

  // The program's globals as of the last publish, sorted by name.
  std::vector<GlobalVar> globals();
  std::vector<Chunk> chunks();
}
