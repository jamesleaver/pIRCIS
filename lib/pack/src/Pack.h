// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// An encrypted content pack.
//
// The firmware ships one blob. Two words open it: the key is
// SHA256(SHA256(a) || SHA256(b)) and the cipher is TEA-CBC. Nothing derived
// from the words is stored, so there is no digest to compare a guess against;
// the only test is whether the plaintext starts with the expected four bytes,
// which is why those bytes sit inside the ciphertext rather than in front of
// it.
//
// This is obfuscation, not security. TEA is 1994 vintage and the point here is
// small, exact code that costs an ESP32 nothing.
namespace pack {

  // Section identifiers.
  enum : uint8_t {
    kSecGrid    = 1,
    kSecSlots   = 2,
    kSecStrings = 3,
    kSecPages   = 4,
    kSecSets    = 5,
  };

  // String table indices, in the order the pack stores them.
  enum : int {
    kStrModeName = 0, kStrRunTitle, kStrSplashTitle, kStrInfoTitle, kStrInfoTile,
    kStrExitTile, kStrExitTitle, kStrEditTitle, kStrResizeBody, kStrOpenTitle,
    kStrOpenBody, kStrRevertBody, kStrDumpDiff, kStrResetBody, kStrDebugEmpty,
    kStrGvRole0, kStrGvRole1, kStrGvRole2, kStrGvRole3, kStrGvRole4, kStrGvRole5,
    kStrGvRole6, kStrReportTitle, kStrReportDiffHdr, kStrReportDiffNone,
    kStrConsoleBanner, kStrProgName, kStrCount
  };

  // Page groups.
  enum : uint8_t { kGroupInfo = 1, kGroupDevice = 2, kGroupSplash = 3 };

  // How a parameter's value is written back into the grid.
  enum class SlotKind : uint8_t {
    // Base64 integer or string. Right-aligned, left-padded with 'A'. 'A' is
    // base64 zero and IRCIS strips leading 'A's before decoding, so the
    // padding is value-preserving.
    Base64 = 0,
    // A small decimal count. Left-aligned and right-padded with '.' (blank),
    // because IRCIS's stack-pop mode reads digits until the first blank.
    Count = 1,
    // Anything else: written verbatim, length enforced.
    Raw    = 2,
  };

  // A fixed run of cells in the packed grid that the editor exposes as a
  // named parameter.
  struct Slot {
    std::string id, label, help;
    uint8_t  row = 0, col = 0, len = 0;
    SlotKind kind = SlotKind::Base64;
    bool     reversed = false;
  };

  // One column of the parameter-sets page: a heading, the parameters it
  // writes, and the entries offered for them.
  struct SetGroup {
    std::string head;
    uint8_t cells   = 0;      // column width, in character cells
    bool    isCount = false;  // writes a count rather than named slots
    std::vector<std::string> slots;
    std::vector<std::string> builtin;
  };

  // A page of text in a paged dialog. `highlight` picks out one line in the
  // accent colour; -1 for none.
  struct Page {
    std::string tag;
    int highlight = -1;
    std::vector<std::string> lines;
  };

  // Try the words. False leaves everything closed and unchanged.
  bool open(const std::string& first, const std::string& second);

  // Reopen from key material kept by the caller (see keyMaterial), so a device
  // that was opened once does not have to be told again on every boot.
  bool openWithKey(const uint8_t* material, std::size_t len);
  constexpr std::size_t kKeyMaterialBytes = 24;   // TEA key + CBC IV
  const uint8_t* keyMaterial();

  bool isOpen();
  void close();

  // Null when the pack is closed or has no such section.
  const uint8_t* section(uint8_t id, std::size_t& lenOut);

  // Section accessors. All are empty or false while the pack is closed.
  bool grid(int& rowsOut, int& colsOut, const uint8_t*& cellsOut);
  const char* gridName();                 // "" when closed

  int slotCount();
  int primarySlots();                     // how many appear on the first page
  const Slot& slot(int index);            // an empty slot when out of range

  const char* str(int index);             // "" when closed or out of range

  int pageCount(uint8_t group);
  const Page& page(uint8_t group, int index);

  int setGroupCount();
  const SetGroup& setGroup(int index);
}
