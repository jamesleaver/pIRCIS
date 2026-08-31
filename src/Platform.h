// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

// Thin platform layer. Everything above this line -- the interpreter core, the
// program model, the UI and the console -- is identical on the ESP32 and in
// the desktop emulator. Only the handful of primitives below have two
// implementations (src/platform/*_arduino.cpp and *_host.cpp).

#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace plat {

  uint32_t millis();
  void     delayMs(uint32_t ms);
  uint32_t freeHeap();
  uint32_t maxAllocHeap();   // largest single allocatable block -- fragmentation

  // Console. On the device this is the USB serial port; in the emulator it is
  // stdin/stdout, so the same commands work in both.
  void log(const char* text);
  void logf(const char* fmt, ...);
  void logln(const char* text = "");
  // Returns true and fills `line` when a complete command has been typed.
  bool readLine(std::string& line);

  // Cooperative primitives. The interpreter runs on a second core on the
  // device and a second thread in the emulator; the UI never blocks on it.
  class Mutex {
  public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();
  private:
    void* impl_;
  };

  struct Guard {
    explicit Guard(Mutex& m) : m_(m) { m_.lock(); }
    ~Guard() { m_.unlock(); }
    Mutex& m_;
  };

  void startTask(void (*fn)(void*), void* arg, const char* name, uint32_t stackBytes);
  void taskYield(uint32_t ms);

  // Persistent key/value store: NVS on the device, a file in the emulator.
  namespace kv {
    void begin();
    bool has(const char* key);
    void remove(const char* key);
    std::size_t getBytes(const char* key, void* buf, std::size_t cap);   // 0 if absent
    std::size_t bytesLength(const char* key);
    void putBytes(const char* key, const void* buf, std::size_t len);
    std::string getString(const char* key, const char* def = "");
    void putString(const char* key, const char* value);
    int  getInt(const char* key, int def);
    void putInt(const char* key, int value);
    bool getBool(const char* key, bool def);
    void putBool(const char* key, bool value);
    void clearAll();     // wipe every stored key
  }

  // Bulk output target: the SD card on the device, ./sdcard/ in the emulator.
  bool writeRunFile(const std::string& text, std::string& pathOut);
  bool sdPresent();      // probed once; false greys out the SD controls

  // Identifies THIS build. On the device it is the app image's own SHA,
  // so it changes whenever new firmware is flashed; on the host it is the
  // compile time. Used to notice a reflash, which NVS otherwise survives.
  std::string firmwareId();
  // When this firmware was built, as a plain date and time. firmwareId() is a
  // hash -- good for spotting that the board has been reflashed, useless for
  // telling someone which build they are on.
  std::string firmwareBuilt();

  // A different value each call. The interpreter's RNG was seeded with a
  // constant, so `r` and `R` replayed the same sequence on every run --
  // Racetrack ran an identical race every time.
  uint32_t randomSeed();

  // Screen dark and CPU asleep until the board is reset. The emulator
  // treats it as a request to quit.
  void powerOff();
  bool powerOffRequested();

  // Saved programs, as plain text files one row per line, so they can be moved
  // on and off with any computer. They live in two places and the UI lists
  // both: the board's own flash, which is always there, and an SD card, which
  // may not be. On the device that is LittleFS on the spare 896 KB data
  // partition and /pircis/programs on the card; in the emulator it is
  // ./device/programs and ./sdcard/programs. Names carry no path and no
  // extension. Every call returns false if that store is not available.
  enum class Where { Device, Card };
  bool progStoreReady(Where w);
  bool progList(Where w, std::vector<std::string>& namesOut);
  bool progRead(Where w, const std::string& name, std::string& textOut);
  bool progWrite(Where w, const std::string& name, const std::string& text);

  // Saved run reports, as written by writeRunFile. Same convention: no path,
  // no extension.
  bool runList(std::vector<std::string>& namesOut);
  bool runRead(const std::string& name, std::string& textOut);
  bool progDelete(Where w, const std::string& name);

  // Network view. Real on the device; the emulator says so and does nothing.
  // The web server runs on the loop task inside webTick(),
  // so these are called from the same thread as everything else.
  // One hook, because the split is HTTP below and content above: the platform
  // knows about sockets and nothing else, and the page it serves is built by
  // portable code that can be rendered and checked on the host.
  struct WebHooks {
    std::string (*page)(const std::string& path, const std::string& query,
                        const std::string& body, bool post) = nullptr;
  };
  void webSetHooks(const WebHooks& hooks);

  bool webBegin(const std::string& ssid, const std::string& pass, std::string& ipOut);
  void webStop();
  void webTick();
  bool webAvailable();   // false in the emulator
}
