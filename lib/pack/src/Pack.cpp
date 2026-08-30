// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT

#include "Pack.h"

#include "Crypto.h"
#include "PackData.h"

#include <cstring>

namespace pack {
namespace {
  // Four bytes that mean the words were right. Chosen to say nothing.
  const uint8_t kMagic[4] = { 0x70, 0x6b, 0x01, 0x1f };

  uint8_t  g_plain[kPackBytes];
  bool     g_open = false;
  uint8_t  g_material[kKeyMaterialBytes];

  // Parsed once on open. The plaintext stays in g_plain; these hold copies so
  // callers get ordinary NUL-terminated strings.
  std::vector<Slot>        g_slots;
  int                      g_primary = 0;
  std::vector<std::string> g_strings;
  std::vector<Page>        g_info, g_device, g_splash;
  std::vector<SetGroup>    g_sets;

  const Slot        kNoSlot;
  const Page        kNoPage;
  const SetGroup    kNoSet;
  const std::string kEmpty;

  // A cursor over one section's payload. Every read is bounds-checked; the
  // first failure sets `bad` and every later read is a no-op, so a truncated
  // or wrong-keyed section falls out as empty rather than reading past the end.
  struct Cursor {
    const uint8_t* p;
    std::size_t    n, at = 0;
    bool           bad = false;

    bool need(std::size_t k) {
      if (bad || at + k > n) { bad = true; return false; }
      return true;
    }
    uint8_t u8()  { if (!need(1)) return 0; return p[at++]; }
    int8_t  i8()  { if (!need(1)) return 0; return (int8_t)p[at++]; }
    uint16_t u16() {
      if (!need(2)) return 0;
      uint16_t v = (uint16_t)p[at] | (uint16_t)(p[at+1] << 8);
      at += 2;
      return v;
    }
    std::string s8() {
      const std::size_t k = u8();
      if (!need(k)) return std::string();
      std::string s((const char*)p + at, k);
      at += k;
      return s;
    }
    std::string s16() {
      const std::size_t k = u16();
      if (!need(k)) return std::string();
      std::string s((const char*)p + at, k);
      at += k;
      return s;
    }
  };

  std::vector<Page>* groupFor(uint8_t id) {
    switch (id) {
      case kGroupInfo:   return &g_info;
      case kGroupDevice: return &g_device;
      case kGroupSplash: return &g_splash;
      default:           return nullptr;
    }
  }

  void forget() {
    g_slots.clear(); g_strings.clear();
    g_info.clear(); g_device.clear(); g_splash.clear();
    g_sets.clear();
    g_primary = 0;
  }

  void parseSlots() {
    std::size_t len = 0;
    const uint8_t* p = section(kSecSlots, len);
    if (!p) return;
    Cursor c{p, len};
    const int n = c.u8();
    g_primary = c.u8();
    for (int i = 0; i < n && !c.bad; ++i) {
      Slot s;
      s.id    = c.s8();
      s.label = c.s8();
      s.row   = c.u8();
      s.col   = c.u8();
      s.len   = c.u8();
      s.kind  = static_cast<SlotKind>(c.u8());
      s.reversed = c.u8() != 0;
      s.help  = c.s16();
      if (!c.bad) g_slots.push_back(s);
    }
    if (g_primary > (int)g_slots.size()) g_primary = (int)g_slots.size();
  }

  void parseStrings() {
    std::size_t len = 0;
    const uint8_t* p = section(kSecStrings, len);
    if (!p) return;
    Cursor c{p, len};
    const int n = c.u16();
    for (int i = 0; i < n && !c.bad; ++i) {
      std::string s = c.s16();
      if (!c.bad) g_strings.push_back(s);
    }
  }

  void parsePages() {
    std::size_t len = 0;
    const uint8_t* p = section(kSecPages, len);
    if (!p) return;
    Cursor c{p, len};
    const int groups = c.u8();
    for (int g = 0; g < groups && !c.bad; ++g) {
      std::vector<Page>* dst = groupFor(c.u8());
      const int pages = c.u8();
      for (int i = 0; i < pages && !c.bad; ++i) {
        Page pg;
        pg.tag       = c.s8();
        pg.highlight = c.i8();
        const int lines = c.u8();
        for (int l = 0; l < lines && !c.bad; ++l) {
          std::string s = c.s16();
          if (!c.bad) pg.lines.push_back(s);
        }
        if (!c.bad && dst) dst->push_back(pg);
      }
    }
  }

  void parseSets() {
    std::size_t len = 0;
    const uint8_t* p = section(kSecSets, len);
    if (!p) return;
    Cursor c{p, len};
    const int n = c.u8();
    for (int i = 0; i < n && !c.bad; ++i) {
      SetGroup g;
      g.head   = c.s8();
      g.cells  = c.u8();
      g.isCount = c.u8() != 0;
      const int slots = c.u8();
      for (int k = 0; k < slots && !c.bad; ++k) g.slots.push_back(c.s8());
      const int builtin = c.u8();
      for (int k = 0; k < builtin && !c.bad; ++k) g.builtin.push_back(c.s16());
      if (!c.bad) g_sets.push_back(g);
    }
  }

  bool decodeInto(const uint8_t key[16], const uint8_t iv[8]) {
    std::memcpy(g_plain, kPackData, kPackBytes);
    crypto::teaCbcDecrypt(key, iv, g_plain, kPackBytes);
    if (std::memcmp(g_plain, kMagic, sizeof(kMagic)) != 0) { g_open = false; return false; }
    g_open = true;
    forget();
    parseSlots();
    parseStrings();
    parsePages();
    parseSets();
    return true;
  }
}

bool open(const std::string& first, const std::string& second) {
  uint8_t key[crypto::kTeaKeyBytes], iv[crypto::kTeaBlock];
  crypto::deriveKey(first, second, key, iv);
  if (!decodeInto(key, iv)) return false;
  std::memcpy(g_material, key, sizeof(key));
  std::memcpy(g_material + sizeof(key), iv, sizeof(iv));
  return true;
}

bool openWithKey(const uint8_t* material, std::size_t len) {
  if (len != kKeyMaterialBytes) return false;
  if (!decodeInto(material, material + crypto::kTeaKeyBytes)) return false;
  std::memcpy(g_material, material, kKeyMaterialBytes);
  return true;
}

const uint8_t* keyMaterial() { return g_material; }
bool isOpen() { return g_open; }

void close() {
  g_open = false;
  forget();
  std::memset(g_plain, 0, sizeof(g_plain));
  std::memset(g_material, 0, sizeof(g_material));
}

const uint8_t* section(uint8_t id, std::size_t& lenOut) {
  lenOut = 0;
  if (!g_open) return nullptr;
  // magic(4) version(1) count(1) reserved(2), then id(1) len(2) bytes...
  const uint8_t count = g_plain[5];
  std::size_t at = 8;
  for (uint8_t i = 0; i < count; ++i) {
    if (at + 3 > kPackBytes) return nullptr;
    const uint8_t sid = g_plain[at];
    const std::size_t len = (std::size_t)g_plain[at+1] | ((std::size_t)g_plain[at+2] << 8);
    at += 3;
    if (at + len > kPackBytes) return nullptr;
    if (sid == id) { lenOut = len; return g_plain + at; }
    at += len;
  }
  return nullptr;
}

bool grid(int& rowsOut, int& colsOut, const uint8_t*& cellsOut) {
  std::size_t len = 0;
  const uint8_t* p = section(kSecGrid, len);
  if (!p || len < 2) return false;
  rowsOut = p[0];
  colsOut = p[1];
  if ((std::size_t)rowsOut * colsOut + 2 > len) return false;
  cellsOut = p + 2;
  return true;
}

const char* gridName() { return str(kStrProgName); }

int slotCount()    { return g_open ? (int)g_slots.size() : 0; }
int primarySlots() { return g_open ? g_primary : 0; }

const Slot& slot(int index) {
  if (!g_open || index < 0 || index >= (int)g_slots.size()) return kNoSlot;
  return g_slots[index];
}

const char* str(int index) {
  if (!g_open || index < 0 || index >= (int)g_strings.size()) return "";
  return g_strings[index].c_str();
}

int pageCount(uint8_t group) {
  const std::vector<Page>* g = groupFor(group);
  return (g_open && g) ? (int)g->size() : 0;
}

int setGroupCount() { return g_open ? (int)g_sets.size() : 0; }

const SetGroup& setGroup(int index) {
  if (!g_open || index < 0 || index >= (int)g_sets.size()) return kNoSet;
  return g_sets[index];
}

const Page& page(uint8_t group, int index) {
  const std::vector<Page>* g = groupFor(group);
  if (!g_open || !g || index < 0 || index >= (int)g->size()) return kNoPage;
  return (*g)[index];
}
}
