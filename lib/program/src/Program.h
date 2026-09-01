// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#include "Examples.h"
#include "Pack.h"

#include <cstdint>
#include <string>
#include <vector>

namespace prog {

  // Any IRCIS program can be loaded, so the working copy is sized at runtime
  // and these are the bounds it must fit inside. The largest bundled example
  // is 28 x 73; the rest is headroom for the editor.
  constexpr int kMaxRows = 32;
  constexpr int kMaxCols = 96;

  // A program that can be loaded: one from the content pack, or one of the
  // bundled IRCIS examples.
  struct ProgramDef {
    const char* name;
    // Which folder it is written into on the device -- one level, or empty
    // for the top of the list. The packed program has none: it is never
    // written to a filesystem at all.
    const char* folder;
    const char* const* rows;
    int rows_n;
    int cols_n;
    bool packed;
  };

  // Index 0 comes from the content pack. While the pack is closed there is no
  // such program: it is absent from the list and cannot be loaded.
  constexpr int kPackedIndex = 0;
  // The first program that is always available.
  constexpr int kFirstExample = 1;
  // A program the user built here rather than one from the table. It has no
  // baseline to revert to, so every cell starts and reverts to '.' (blank).
  constexpr int kScratchIndex = -1;
  const ProgramDef& programAt(int index);
  int programCount();

  // Named runs of cells in the packed grid that the editor exposes as
  // parameters. They are described by the pack, so there are none while it is
  // closed.
  //
  // Every edit through a slot is LENGTH-PRESERVING, and that is not a UI
  // convenience: a packed program's behaviour can depend on how many steps it
  // takes, so inserting or deleting a single cell changes what it computes.
  // The editor therefore overwrites in place and never shifts.
  using Slot     = pack::Slot;
  using SlotKind = pack::SlotKind;

  int slotCount();
  int primarySlots();                 // how many appear on the first page
  const Slot& slot(int index);
  int slotIndex(const char* id);      // -1 if unknown
  int countSlot();                    // the Count-kind slot, or -1
  std::string slotOriginal(int slot); // the value as the pack has it

  // A single-cell override, used to persist edits compactly.
  struct Diff {
    uint8_t row;
    uint8_t col;
    char ch;
  };

  // The working copy of the grid: baseline in flash, edits in RAM, always
  // revertible because the baseline can never be overwritten.
  class Program {
  public:
    Program() { loadProgram(pack::isOpen() ? kPackedIndex : kFirstExample); }

    // Swap in a different program. Discards any edits: they are expressed as
    // offsets into a specific baseline and mean nothing against another one.
    void loadProgram(int index);
    // A blank grid of the given size, every cell '.'. Clamped to kMaxRows/Cols.
    void newProgram(int rows, int cols);
    // Take the current cells as the baseline. A program arriving as text --
    // from the SD card, or pasted into the web editor -- has no baseline of
    // its own, and diffing it against blanks would mark every character as
    // edited. Called once, right after the text is written in.
    void adoptBaseline();
    bool isScratch() const { return prog_ < 0; }
    int  programIndex() const { return prog_; }
    // A name the user gave it, if any: scratch programs and anything loaded
    // from a file start out named after where they came from.
    const char* programName() const {
      if (!name_.empty()) return name_.c_str();
      return isScratch() ? "Untitled" : programAt(prog_).name;
    }
    void setProgramName(const std::string& n) { name_ = n; }
    bool isPacked() const { return !isScratch() && programAt(prog_).packed; }

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    // Editor shape operations. All refuse on a packed program, whose edits
    // must stay length-preserving (see Slot above).
    bool resize(int rows, int cols);
    // Add or take away a row or a column at a given edge, moving the cells
    // that are already there. resize() only ever grows or trims the bottom
    // right corner, so it cannot make room at the top or on the left. The
    // baseline moves with the cells: "what this was before you edited it"
    // would otherwise start naming a different cell after every insertion.
    bool insertRow(int at);      // at in [0, rows()]
    bool deleteRow(int at);      // at in [0, rows())
    bool insertCol(int at);
    bool deleteCol(int at);

    void revertAll();
    void revertSlot(int slot);

    char cell(int row, int col) const;
    bool setCell(int row, int col, char ch);
    char baselineCell(int row, int col) const;
    bool cellModified(int row, int col) const;
    int modifiedCells() const;

    std::string slotValue(int slot) const;          // as it appears in the grid
    bool setSlotValue(int slot, const std::string& value);
    bool slotModified(int slot) const;

    // Convenience for the numeric slots.
    int slotAsInt(int slot) const;                  // base64 -> int
    bool setSlotFromInt(int slot, int value);       // int -> base64, padded
    // The parameter the pack marks as a count, if it has one.
    int countValue() const;
    bool setCountValue(int n);                      // 0..99

    std::vector<std::string> lines() const;
    std::string text() const;

    std::vector<Diff> diff() const;
    bool applyDiff(const std::vector<Diff>& diffs);  // replaces current edits

    // Compact serialisation for NVS: 3 bytes per changed cell.
    std::size_t encodeDiff(uint8_t* out, std::size_t cap) const;
    bool decodeDiff(const uint8_t* in, std::size_t len);

  private:
    std::string name_;
    int  prog_ = kFirstExample;
    int  rows_ = 1;
    int  cols_ = 1;
    char cells_[kMaxRows][kMaxCols];
    // What "unedited" means for this program: the table's rows for a bundled
    // one, blanks for a new one, the text itself for one that was loaded.
    char base_[kMaxRows][kMaxCols];
  };
}
