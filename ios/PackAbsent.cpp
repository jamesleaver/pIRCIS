// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The pack interface for builds that carry no packed program: nothing ever
// opens, and every query answers as it would for a closed pack. A build
// compiles this in place of Pack.cpp, so nothing of the packed content --
// not even its encrypted bytes -- is in the binary.
#include "Pack.h"

namespace pack {
  namespace {
    const Slot     kNoSlot;
    const SetGroup kNoGroup;
    const Page     kNoPage;
    uint8_t        kNoKey[kKeyMaterialBytes] = {};
  }

  bool open(const std::string&, const std::string&) { return false; }
  bool openWithKey(const uint8_t*, std::size_t) { return false; }
  const uint8_t* keyMaterial() { return kNoKey; }
  bool isOpen() { return false; }
  void close() {}

  const uint8_t* section(uint8_t, std::size_t& lenOut) { lenOut = 0; return nullptr; }
  bool grid(int& rowsOut, int& colsOut, const uint8_t*& cellsOut) {
    rowsOut = colsOut = 0; cellsOut = nullptr; return false;
  }
  const char* gridName() { return ""; }
  int slotCount() { return 0; }
  int primarySlots() { return 0; }
  const Slot& slot(int) { return kNoSlot; }
  const char* str(int) { return ""; }
  int pageCount(uint8_t) { return 0; }
  const Page& page(uint8_t, int) { return kNoPage; }
  int setGroupCount() { return 0; }
  const SetGroup& setGroup(int) { return kNoGroup; }
}
