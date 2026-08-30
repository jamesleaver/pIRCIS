// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
//
// The pack pipeline, end to end: the builder wrote it, this opens it, and what
// comes out has to be internally consistent -- a grid of the shape it claims,
// parameters that land inside it, and a string table the firmware can index.
//
// The words are not in this file. Pass them in:
//     PACK_WORDS="one two" make test
// Without them the mechanism is still checked -- that a wrong guess is refused
// and leaves the pack shut -- and the content checks report themselves skipped.
#include "Pack.h"
#include "Program.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int fails = 0;
static void ck(bool ok, const char* what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++fails;
}

int main() {
  std::printf("content pack\n");

  ck(!pack::isOpen(), "starts shut");
  ck(!pack::open("not", "these"), "a wrong guess is refused");
  ck(!pack::isOpen(), "and leaves it shut");
  std::size_t len = 0;
  ck(pack::section(pack::kSecGrid, len) == nullptr, "a shut pack yields nothing");
  ck(prog::slotCount() == 0, "and offers no parameters");
  ck(pack::str(pack::kStrRunTitle)[0] == '\0', "and no strings");

  // Index 0 belongs to the pack, so while it is shut a default Program must
  // land on a bundled example instead of an empty grid.
  {
    prog::Program p;
    ck(p.programIndex() != prog::kPackedIndex, "a shut pack leaves index 0 unloadable");
    ck(p.rows() > 0 && p.cols() > 0, "and the default program is a real one");
  }

  const char* words = std::getenv("PACK_WORDS");
  if (!words || !*words) {
    std::printf("  skip  content checks (set PACK_WORDS=\"<word> <word>\" to run them)\n");
    std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails==1?"":"s");
    return fails ? 1 : 0;
  }

  std::string w(words);
  const std::size_t sp = w.find(' ');
  ck(sp != std::string::npos, "PACK_WORDS holds two words");
  if (sp == std::string::npos) return 1;
  const std::string a = w.substr(0, sp), b = w.substr(sp + 1);

  ck(pack::open(a, b), "the right words open it");
  ck(pack::isOpen(), "and it stays open");

  int rows = 0, cols = 0;
  const uint8_t* cells = nullptr;
  ck(pack::grid(rows, cols, cells), "it carries a grid");
  ck(rows > 0 && cols > 0 && rows <= prog::kMaxRows && cols <= prog::kMaxCols,
     "of a shape the editor can hold");

  bool printable = true;
  for (int i = 0; i < rows * cols && printable; ++i)
    if (cells[i] < 0x20 || cells[i] > 0x7e) printable = false;
  ck(printable, "and every cell is a printable character");

  // Parameters: each has to name a run of cells that is actually inside the
  // grid, or an edit through it would write out of bounds.
  const int n = prog::slotCount();
  ck(n > 0, "it describes parameters");
  ck(prog::primarySlots() > 0 && prog::primarySlots() <= n, "with a sane first page");
  bool inBounds = true, lengths = true, ids = true;
  for (int i = 0; i < n; ++i) {
    const prog::Slot& s = prog::slot(i);
    if (s.row >= rows || s.col + s.len > cols || s.len == 0) inBounds = false;
    if ((int)prog::slotOriginal(i).size() != s.len) lengths = false;
    if (s.id.empty() || prog::slotIndex(s.id.c_str()) != i) ids = false;
  }
  ck(inBounds, "every parameter lies inside the grid");
  ck(lengths, "and reads back at its own length");
  ck(ids, "and is findable by its id");

  // The string table has to be at least as long as the enum the firmware
  // indexes it with, or a lookup returns "" and a label silently goes blank.
  ck(pack::str(pack::kStrProgName)[0] != '\0', "the string table reaches its last entry");

  ck(pack::pageCount(pack::kGroupInfo)   > 0, "it carries the info pages");
  ck(pack::pageCount(pack::kGroupDevice) > 0, "the device pages");
  ck(pack::pageCount(pack::kGroupSplash) > 0, "and the splash text");

  // The packed program loads and matches the pack's own grid.
  {
    prog::Program p;
    p.loadProgram(prog::kPackedIndex);
    bool same = p.isPacked() && p.rows() == rows && p.cols() == cols;
    for (int r = 0; r < rows && same; ++r)
      for (int c = 0; c < cols && same; ++c)
        if (p.cell(r, c) != (char)cells[r * cols + c]) same = false;
    ck(same, "the packed program loads byte for byte from the pack");
  }

  // Reopening from stored key material, the way a boot does it.
  uint8_t keep[pack::kKeyMaterialBytes];
  std::memcpy(keep, pack::keyMaterial(), sizeof(keep));
  pack::close();
  ck(!pack::isOpen(), "close shuts it");
  ck(prog::slotCount() == 0, "and takes the parameters with it");
  ck(pack::openWithKey(keep, sizeof(keep)), "kept key material reopens it without the words");
  keep[0] ^= 0xFF;
  pack::close();
  ck(!pack::openWithKey(keep, sizeof(keep)), "one bad byte of key material does not");

  std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails==1?"":"s");
  return fails ? 1 : 0;
}
