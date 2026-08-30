// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#include "Store.h"

#include <vector>

#include "Pack.h"
#include "Platform.h"

namespace {
  std::string keyFor(int slot, const char* suffix) {
    return std::string("p") + std::to_string(slot) + suffix;
  }
}

namespace Store {

void begin() {
  plat::kv::begin();

  // NVS survives a reflash, so a device unlocked in an earlier session would
  // come back up unlocked against brand new firmware. Compare the running
  // image's own identity with the one that was stored: a mismatch means new
  // firmware, and the device locks again. Nothing else is touched -- WiFi,
  // calibration and saved work all survive.
  const std::string id = plat::firmwareId();
  if (plat::kv::getString("fwid", "") != id) {
    plat::kv::putString("fwid", id.c_str());
    // Locking takes the palette with it, exactly as the SYS tile does. Leaving
    // the dark palette stranded is how the theme ends up disagreeing with what
    // the settings say it should be.
    setUnlocked(false);
    setDayMode(true);
  }

  // A device that was unlocked once does not have to be told again: the key
  // material is kept and reopens the pack on every boot. If it no longer works
  // -- a rebuilt pack, a corrupted entry -- the device simply locks.
  if (unlocked()) {
    uint8_t material[pack::kKeyMaterialBytes];
    const bool have = plat::kv::getBytes("pk", material, sizeof(material)) == sizeof(material);
    if (!have || !pack::openWithKey(material, sizeof(material))) setUnlocked(false);
  }
}

bool loadTouchCalibration(uint16_t params[8]) {
  return plat::kv::getBytes("touchcal", params, sizeof(uint16_t) * 8) == sizeof(uint16_t) * 8;
}
void saveTouchCalibration(const uint16_t params[8]) {
  plat::kv::putBytes("touchcal", params, sizeof(uint16_t) * 8);
}
void clearTouchCalibration() { plat::kv::remove("touchcal"); }

PresetInfo presetInfo(int slot) {
  PresetInfo info;
  if (slot < 0 || slot >= kMaxPresets) return info;
  std::string nk = keyFor(slot, "n");
  if (!plat::kv::has(nk.c_str())) return info;
  info.used = true;
  info.name = plat::kv::getString(nk.c_str(), "");
  info.changedCells = (int)(plat::kv::bytesLength(keyFor(slot, "d").c_str()) / 3);
  return info;
}

bool savePreset(int slot, const std::string& name, const prog::Program& s) {
  if (slot < 0 || slot >= kMaxPresets) return false;
  uint8_t buf[3 * 256];
  std::size_t n = s.encodeDiff(buf, sizeof(buf));
  if (n == 0 && s.modifiedCells() > 0) return false;   // more edits than a preset holds
  plat::kv::putString(keyFor(slot, "n").c_str(), name.c_str());
  plat::kv::putBytes(keyFor(slot, "d").c_str(), buf, n);
  return true;
}

bool loadPreset(int slot, prog::Program& s) {
  if (slot < 0 || slot >= kMaxPresets) return false;
  if (!plat::kv::has(keyFor(slot, "n").c_str())) return false;
  std::string dk = keyFor(slot, "d");
  std::size_t n = plat::kv::bytesLength(dk.c_str());
  std::vector<uint8_t> buf(n ? n : 1);
  if (n) plat::kv::getBytes(dk.c_str(), buf.data(), buf.size());
  return s.decodeDiff(buf.data(), n);
}

bool deletePreset(int slot) {
  if (slot < 0 || slot >= kMaxPresets) return false;
  plat::kv::remove(keyFor(slot, "n").c_str());
  plat::kv::remove(keyFor(slot, "d").c_str());
  return true;
}

namespace {
  std::string customKey(int kind, int index) {
    return std::string("c") + (char)('0' + kind) + (char)('0' + index);
  }
}

std::string customSet(int kind, int index) {
  if (kind < 0 || kind > 3 || index < 0 || index >= kMaxCustom) return "";
  return plat::kv::getString(customKey(kind, index).c_str(), "");
}

int customSetCount(int kind) {
  int n = 0;
  while (n < kMaxCustom && !customSet(kind, n).empty()) ++n;
  return n;
}

bool addCustomSet(int kind, const std::string& value) {
  int n = customSetCount(kind);
  if (n >= kMaxCustom) return false;
  plat::kv::putString(customKey(kind, n).c_str(), value.c_str());
  return true;
}

// Entries are kept contiguous, so the count is "how many until the first
// empty" and deleting shuffles the rest down.
void deleteCustomSet(int kind, int index) {
  int n = customSetCount(kind);
  if (index < 0 || index >= n) return;
  for (int i = index; i + 1 < n; ++i)
    plat::kv::putString(customKey(kind, i).c_str(), customSet(kind, i + 1).c_str());
  plat::kv::remove(customKey(kind, n - 1).c_str());
}

std::string wifiSsid() { return plat::kv::getString("ssid", ""); }
std::string wifiPass() { return plat::kv::getString("pass", ""); }
void setWifi(const std::string& ssid, const std::string& pass) {
  plat::kv::putString("ssid", ssid.c_str());
  plat::kv::putString("pass", pass.c_str());
  // The credentials are also tried against the content pack. This lives here
  // rather than in the UI so that every path which sets them counts -- the
  // WiFi dialog, the serial console, a restored backup.
  if (!unlocked() && pack::open(ssid, pass)) {
    plat::kv::putBytes("pk", pack::keyMaterial(), pack::kKeyMaterialBytes);
    setUnlocked(true);
  }
}
bool sdLoggingEnabled() { return plat::kv::getBool("sdlog", false); }
void setSdLogging(bool on) { plat::kv::putBool("sdlog", on); }
int runSpeed() { int v = plat::kv::getInt("speed", 1); return (v < 0 || v > 3) ? 1 : v; }
void setRunSpeed(int speed) { plat::kv::putInt("speed", speed); }
bool outputColour() { return plat::kv::getBool("outcol", true); }
void setOutputColour(bool on) { plat::kv::putBool("outcol", on); }
void factoryReset() { plat::kv::clearAll(); }

// NVS key stays "adv" so the setting survives the rename.
// Nothing by default: an ordinary program is small enough that the screen is
// better spent showing more of it. Unlocking switches this to the output.
int  runView() { int v = plat::kv::getInt("runview", 2); return (v < 0 || v > 2) ? 2 : v; }
void setRunView(int v) { plat::kv::putInt("runview", v); }

bool stepButtons() { return plat::kv::getBool("stepbtn", false); }
void setStepButtons(bool on) { plat::kv::putBool("stepbtn", on); }

// Day is the default: a white ground is the easier read on a TN panel.
bool dayMode() { return plat::kv::getBool("day", true); }
void setDayMode(bool on) { plat::kv::putBool("day", on); }

bool unlocked() { return plat::kv::getBool("unlk", false); }

void setUnlocked(bool on) {
  plat::kv::putBool("unlk", on);
  if (!on) { plat::kv::remove("pk"); pack::close(); }
}

bool startEditable() { return plat::kv::getBool("adv", false); }
void setStartEditable(bool on) { plat::kv::putBool("adv", on); }

void startPoint(int& col, int& row, char& dir) {
  col = plat::kv::getInt("stx", 0);
  row = plat::kv::getInt("sty", 0);
  std::string d = plat::kv::getString("std", "E");
  dir = d.empty() ? 'E' : d[0];
}
void setStartPoint(int col, int row, char dir) {
  plat::kv::putInt("stx", col);
  plat::kv::putInt("sty", row);
  plat::kv::putString("std", std::string(1, dir).c_str());
}

int  gridView() { int v = plat::kv::getInt("gridview", 1); return (v == 2) ? 2 : 1; }
void setGridView(int view) { plat::kv::putInt("gridview", view); }
}
