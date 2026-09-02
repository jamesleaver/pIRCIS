// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#include "Program.h"

#include <algorithm>

#include "Runner.h"     // base64_decode_int / base64_encode_int

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace prog {

  int slotCount()    { return pack::slotCount(); }
  int primarySlots() { return pack::primarySlots(); }

  const Slot& slot(int index) { return pack::slot(index); }

  int slotIndex(const char* id) {
    for (int i = 0; i < pack::slotCount(); ++i)
      if (pack::slot(i).id == id) return i;
    return -1;
  }

  // The value as the pack has it, before any edit.
  std::string slotOriginal(int index) {
    int rows = 0, cols = 0;
    const uint8_t* cells = nullptr;
    if (!pack::grid(rows, cols, cells)) return std::string();
    const Slot& s = pack::slot(index);
    if (s.len == 0 || s.row >= rows || s.col + s.len > cols) return std::string();
    return std::string((const char*)cells + (std::size_t)s.row * cols + s.col, s.len);
  }

  // Index 0 comes from the pack; the rest are the bundled IRCIS examples.
  namespace {
    // Row pointers into the pack's own grid. The rows are fixed width and are
    // not NUL-terminated, which is why every read of them is a memcpy of a
    // known length.
    const char* g_packedRows[kMaxRows];

    ProgramDef packedDef() {
      int rows = 0, cols = 0;
      const uint8_t* cells = nullptr;
      if (!pack::grid(rows, cols, cells)) return { "", "", nullptr, 0, 0, false };
      if (rows > kMaxRows) rows = kMaxRows;
      for (int r = 0; r < rows; ++r)
        g_packedRows[r] = (const char*)cells + (std::size_t)r * cols;
      return { pack::gridName(), "", g_packedRows, rows, cols, true };
    }

    ProgramDef makeTable(int i) {
      if (i == kPackedIndex) return packedDef();
      const ExampleDef& e = kExamples[i - 1];
      return { e.name, e.folder, e.rows, e.rows_n, e.cols_n, false };
    }
  }

  const ProgramDef& programAt(int index) {
    static ProgramDef cache[1 + kExampleCount];
    static bool built = false;
    static bool builtOpen = false;
    // Index 0 only exists while the pack is open, so the cache is rebuilt
    // whenever that changes.
    if (!built || builtOpen != pack::isOpen()) {
      for (int i = 0; i < 1 + kExampleCount; ++i) cache[i] = makeTable(i);
      built = true;
      builtOpen = pack::isOpen();
    }
    if (index < 0 || index >= 1 + kExampleCount) index = kOpeningExample;
    return cache[index];
  }

  int programCount() { return 1 + kExampleCount; }

  void Program::loadProgram(int index) {
    if (index < 0 || index >= programCount()) index = kOpeningExample;
    if (index == kPackedIndex && !pack::isOpen()) index = kOpeningExample;
    prog_ = index;
    name_.clear();                       // back to the table's own name
    const ProgramDef& d = programAt(prog_);
    rows_ = d.rows_n < kMaxRows ? d.rows_n : kMaxRows;
    cols_ = d.cols_n < kMaxCols ? d.cols_n : kMaxCols;
    revertAll();
  }

  void Program::newProgram(int rows, int cols) {
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    if (rows > kMaxRows) rows = kMaxRows;
    if (cols > kMaxCols) cols = kMaxCols;
    prog_ = kScratchIndex;
    name_.clear();
    rows_ = rows;
    cols_ = cols;
    revertAll();
  }

  void Program::adoptBaseline() {
    for (int r = 0; r < kMaxRows; ++r) std::memcpy(base_[r], cells_[r], kMaxCols);
  }

  void Program::revertAll() {
    for (int r = 0; r < kMaxRows; ++r) std::memset(base_[r], '.', kMaxCols);
    if (!isScratch()) {
      const ProgramDef& d = programAt(prog_);
      for (int r = 0; r < rows_; ++r)
        std::memcpy(base_[r], d.rows[r], cols_);
    }
    for (int r = 0; r < kMaxRows; ++r) std::memcpy(cells_[r], base_[r], kMaxCols);
  }

  void Program::revertSlot(int slot) {
    if (slot < 0 || slot >= prog::slotCount()) return;
    const Slot& s = prog::slot(slot);
    const std::string orig = slotOriginal(slot);
    if ((int)orig.size() != s.len) return;
    for (int i = 0; i < s.len; ++i)
      cells_[s.row][s.col + i] = orig[i];
  }

  char Program::cell(int row, int col) const {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return '.';
    return cells_[row][col];
  }

  char Program::baselineCell(int row, int col) const {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return '.';
    return base_[row][col];
  }


  bool Program::setCell(int row, int col, char ch) {
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return false;
    cells_[row][col] = ch;
    return true;
  }

  bool Program::cellModified(int row, int col) const {
    return cell(row, col) != baselineCell(row, col);
  }

  int Program::modifiedCells() const {
    int n = 0;
    for (int r = 0; r < rows_; ++r)
      for (int c = 0; c < cols_; ++c)
        if (cells_[r][c] != baselineCell(r, c)) ++n;
    return n;
  }

  std::string Program::slotValue(int slot) const {
    if (slot < 0 || slot >= prog::slotCount()) return std::string();
    const Slot& s = prog::slot(slot);
    std::string v(&cells_[s.row][s.col], s.len);
    // A runner reading east-to-west meets these characters in the opposite
    // order to the way they sit in the grid, so that is the order the value
    // is in. The flag was carried on every slot and acted on nowhere; it
    // costs nothing today, because the only reversed slot is one character
    // wide, and would have been silently wrong for the next one.
    if (s.reversed) std::reverse(v.begin(), v.end());
    return v;
  }

  bool Program::slotModified(int slot) const {
    return slotValue(slot) != slotOriginal(slot);
  }

  bool Program::setSlotValue(int slot, const std::string& value) {
    if (slot < 0 || slot >= prog::slotCount()) return false;
    const Slot& s = prog::slot(slot);
    if (static_cast<int>(value.size()) > s.len) return false;

    std::string padded;
    switch (s.kind) {
    case SlotKind::Base64:
      // Left-aligned, padded on the right with '.' -- a blank, which ends the
      // literal. Shorter values simply have fewer base64 digits; padding on the
      // left with 'A' would have been value-preserving too, but reads as though
      // the value were something else.
      padded = value;
      padded.append(s.len - value.size(), '.');
      break;
    case SlotKind::Count:
      padded = value;
      padded.append(s.len - value.size(), '.');   // '.' is blank; ends the mode
      break;
    case SlotKind::Raw:
      padded = value;
      padded.append(s.len - value.size(), '.');
      break;
    }
    if (s.reversed) std::reverse(padded.begin(), padded.end());
    for (int i = 0; i < s.len; ++i)
      cells_[s.row][s.col + i] = padded[i];
    return true;
  }

  int Program::slotAsInt(int slot) const {
    // Mirror IRCIS exactly (Runner::process_integer_buffer): read up to the
    // first blank, and if every character is a digit the literal is DECIMAL;
    // any other base64 character makes it base64. Decoding unconditionally as
    // base64 misreads '2024' as 14,372,280.
    std::string v = slotValue(slot);
    std::size_t n = 0;
    while (n < v.size() && ircis::isbase64(v[n])) ++n;
    std::string lit = v.substr(0, n);
    if (lit.empty()) return 0;
    for (char c : lit)
      if (!std::isdigit(static_cast<unsigned char>(c)))
        return ircis::base64_decode_int(lit);
    return std::atoi(lit.c_str());
  }

  bool Program::setSlotFromInt(int slot, int value) {
    if (slot < 0 || slot >= prog::slotCount()) return false;
    if (value < 0) return false;
    // Prefer the decimal spelling when it fits. IRCIS reads an all-digit
    // literal as decimal, and '2024' is a great deal easier to read back off
    // the grid than 'fo'.
    std::string dec = std::to_string(value);
    if (static_cast<int>(dec.size()) <= prog::slot(slot).len) return setSlotValue(slot, dec);
    std::string enc = ircis::base64_encode_int(value);
    if (static_cast<int>(enc.size()) > prog::slot(slot).len) return false;
    return setSlotValue(slot, enc);
  }

  // The count parameter is the one the pack marks as such, rather than one
  // with a particular name.
  int countSlot() {
    for (int i = 0; i < slotCount(); ++i)
      if (slot(i).kind == SlotKind::Count) return i;
    return -1;
  }

  int Program::countValue() const {
    const int idx = countSlot();
    if (idx < 0) return 0;
    std::string v = slotValue(idx);
    int n = 0;
    for (char ch : v) {
      if (ch < '0' || ch > '9') break;
      n = n * 10 + (ch - '0');
    }
    return n;
  }

  bool Program::setCountValue(int n) {
    const int idx = countSlot();
    if (idx < 0 || n < 0 || n > 99) return false;
    return setSlotValue(idx, std::to_string(n));
  }

  std::vector<std::string> Program::lines() const {
    std::vector<std::string> out;
    out.reserve(rows_);
    for (int r = 0; r < rows_; ++r)
      out.emplace_back(cells_[r], cols_);
    return out;
  }

  std::string Program::text() const {
    std::string out;
    for (int r = 0; r < rows_; ++r) {
      out.append(cells_[r], cols_);
      out.push_back('\n');
    }
    return out;
  }

  std::vector<Diff> Program::diff() const {
    std::vector<Diff> out;
    for (int r = 0; r < rows_; ++r)
      for (int c = 0; c < cols_; ++c)
        if (cells_[r][c] != baselineCell(r, c))
          out.push_back({static_cast<uint8_t>(r), static_cast<uint8_t>(c), cells_[r][c]});
    return out;
  }

  bool Program::applyDiff(const std::vector<Diff>& diffs) {
    revertAll();
    for (const Diff& d : diffs) {
      if (d.row >= rows_ || d.col >= cols_) return false;
      cells_[d.row][d.col] = d.ch;
    }
    return true;
  }

  std::size_t Program::encodeDiff(uint8_t* out, std::size_t cap) const {
    std::vector<Diff> d = diff();
    std::size_t need = d.size() * 3;
    if (need > cap) return 0;
    std::size_t i = 0;
    for (const Diff& e : d) {
      out[i++] = e.row;
      out[i++] = e.col;
      out[i++] = static_cast<uint8_t>(e.ch);
    }
    return need;
  }

  bool Program::decodeDiff(const uint8_t* in, std::size_t len) {
    if (len % 3 != 0) return false;
    std::vector<Diff> d;
    for (std::size_t i = 0; i < len; i += 3)
      d.push_back({in[i], in[i + 1], static_cast<char>(in[i + 2])});
    return applyDiff(d);
  }

  // Shape edits. Refused on a packed program, whose behaviour can depend on
  // step counts: adding or removing a cell changes the answer (see Slot).
  bool Program::resize(int rows, int cols) {
    if (isPacked()) return false;
    if (rows < 1 || rows > kMaxRows || cols < 1 || cols > kMaxCols) return false;
    for (int r = rows_; r < rows; ++r) std::memset(cells_[r], '.', kMaxCols);
    for (int r = 0; r < rows; ++r)
      for (int c = cols_; c < cols; ++c) cells_[r][c] = '.';
    rows_ = rows; cols_ = cols;
    return true;
  }

  bool Program::insertRow(int at) {
    if (isPacked() || rows_ >= kMaxRows || at < 0 || at > rows_) return false;
    // The baseline moves with the cells, or "what this was before you edited
    // it" starts naming a different cell after every insertion.
    for (int r = rows_; r > at; --r) {
      std::memcpy(cells_[r], cells_[r - 1], kMaxCols);
      std::memcpy(base_[r],  base_[r - 1],  kMaxCols);
    }
    std::memset(cells_[at], '.', kMaxCols);
    std::memset(base_[at],  '.', kMaxCols);
    ++rows_;
    return true;
  }

  bool Program::deleteRow(int at) {
    if (isPacked() || rows_ <= 1 || at < 0 || at >= rows_) return false;
    for (int r = at; r < rows_ - 1; ++r) {
      std::memcpy(cells_[r], cells_[r + 1], kMaxCols);
      std::memcpy(base_[r],  base_[r + 1],  kMaxCols);
    }
    std::memset(cells_[rows_ - 1], '.', kMaxCols);
    std::memset(base_[rows_ - 1],  '.', kMaxCols);
    --rows_;
    return true;
  }

  bool Program::insertCol(int at) {
    if (isPacked() || cols_ >= kMaxCols || at < 0 || at > cols_) return false;
    for (int r = 0; r < rows_; ++r) {
      for (int c = cols_; c > at; --c) {
        cells_[r][c] = cells_[r][c - 1];
        base_[r][c]  = base_[r][c - 1];
      }
      cells_[r][at] = '.';
      base_[r][at]  = '.';
    }
    ++cols_;
    return true;
  }

  bool Program::deleteCol(int at) {
    if (isPacked() || cols_ <= 1 || at < 0 || at >= cols_) return false;
    for (int r = 0; r < rows_; ++r) {
      for (int c = at; c < cols_ - 1; ++c) {
        cells_[r][c] = cells_[r][c + 1];
        base_[r][c]  = base_[r][c + 1];
      }
      cells_[r][cols_ - 1] = '.';
      base_[r][cols_ - 1]  = '.';
    }
    --cols_;
    return true;
  }
}
