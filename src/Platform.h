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
  // A character typed on a real keyboard, or 0 if none is waiting.
  //
  // The emulator has one: the machine it runs on. The board does not, unless
  // somebody plugs one in, so this returns 0 there. Special keys arrive as
  // control codes: \b backspace, \r enter, and 0x11-0x14 for the arrows.
  char pollKey();
  static constexpr char kKeyUp = 0x11, kKeyDown = 0x12,
                        kKeyLeft = 0x13, kKeyRight = 0x14;
  // Navigation and the usual desktop chords, so the emulator can be driven
  // without the mouse. Ctrl and Cmd are both accepted for the chords, since
  // the emulator runs on machines that expect one or the other.
  static constexpr char kKeyTab   = 0x15,  // move to the next tab
                        kKeyBack  = 0x16,  // shift-tab, the previous tab
                        kKeyEsc   = 0x17,  // close whatever is open
                        kKeySave  = 0x18,  // ctrl/cmd S
                        kKeyUndo  = 0x19,  // ctrl/cmd Z
                        kKeyRedo  = 0x1a,  // ctrl/cmd Y, or shift ctrl/cmd Z
                        kKeyRun   = 0x1b,  // ctrl/cmd R, play or pause
                        kKeyHelp  = 0x1c,  // F1, the shortcut list
                        kKeyPaste = 0x1f,  // ctrl/cmd V, the clipboard
                        kKeyName  = 0x1d,  // ctrl/cmd N, rename
                        kKeyZoom  = 0x1e;  // ctrl/cmd G, the grid view
  // Push a key into the queue as though it had been typed. The emulator's
  // console uses it for the same reason it has `tap`: so a scene can drive the
  // keyboard paths without a human at the keys. Does nothing on the board.
  void injectKey(char c);
  // What the machine has on its clipboard, empty if nothing or if this build
  // has no clipboard to ask.
  std::string clipboard();
  // Whether this build can receive typed keys at all -- what SYS uses to
  // decide whether offering the setting makes any sense.
  bool haveKeyboard();

  uint32_t randomSeed();


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
