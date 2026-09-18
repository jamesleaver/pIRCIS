// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#if defined(SK_HOST)

#include "Platform.h"
#include "Display.h"

#include <SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <cstdlib>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif
#endif
#if !defined(TARGET_OS_IPHONE)
#define TARGET_OS_IPHONE 0
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <iostream>
#include <random>
#include <istream>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace plat {
namespace {
  const auto g_start = std::chrono::steady_clock::now();
  // Settings sit beside the programs on the desktop. A phone shows its
  // Documents folder to the person, so there they go in the app's own
  // Library, which it does not.
  std::string storePath() {
    if (!TARGET_OS_IPHONE) return "emulator_nvs.txt";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/Library/settings.txt";
  }
  const char* kSdDir = "sdcard";
}

uint32_t millis() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - g_start).count();
}
void delayMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
uint32_t freeHeap() { return 300000; }   // the ESP32 figure this design targets
// A phone says how much it will give before it kills the app; a desktop is
// not asked, and the machine's runner ceiling is what protects it.
bool lowMemory() {
#if TARGET_OS_IPHONE
  return os_proc_available_memory() < 32u * 1024 * 1024;
#else
  return false;
#endif
}
uint32_t maxAllocHeap() { return 300000; }

// The interpreter thread streams output here while the UI thread prints
// command replies; without a lock the two interleave mid-line.
static std::mutex& consoleMutex() { static std::mutex m; return m; }

void log(const char* text) {
  std::lock_guard<std::mutex> lk(consoleMutex());
  std::fputs(text, stdout); std::fflush(stdout);
}
void logln(const char* text) {
  std::lock_guard<std::mutex> lk(consoleMutex());
  std::fputs(text, stdout); std::fputc('\n', stdout); std::fflush(stdout);
}
void logf(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  log(buf);            // takes the console lock
}

// The console has to behave like the serial port: asked for a line, and never
// waiting for one. Setting stdin non-blocking is the POSIX way and has no
// Windows equivalent, so a thread sits on it instead and the caller takes
// whatever has arrived. Detached, because it outlives nothing that matters and
// a blocked read cannot be cancelled portably anyway.
namespace {
  std::mutex               g_lineMx;
  std::vector<std::string> g_lines;

  void pushLine(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    if (s.empty()) return;
    std::lock_guard<std::mutex> g(g_lineMx);
    g_lines.push_back(std::move(s));
  }

  // The console is the terminal, or a file or named pipe given by
  // PIRCIS_CONSOLE where there is no terminal to type into, as in the phone
  // simulator. A pipe ends whenever its writer goes away, so it is opened
  // again for the next one.
  void pipeReader(const char* path) {
    std::string carry;
    for (;;) {
      const int fd = ::open(path, O_RDONLY);
      if (fd < 0) return;
      char buf[256];
      for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        carry.append(buf, (std::size_t)n);
        std::size_t nl;
        while ((nl = carry.find('\n')) != std::string::npos) {
          pushLine(carry.substr(0, nl));
          carry.erase(0, nl + 1);
        }
      }
      ::close(fd);
    }
  }

  void stdinReader() {
    if (const char* path = std::getenv("PIRCIS_CONSOLE")) { pipeReader(path); return; }
    std::string s;
    while (std::getline(std::cin, s)) pushLine(s);
  }
}

bool readLine(std::string& line) {
  static bool started = false;
#if defined(__EMSCRIPTEN__)
  started = true;        // a page has no terminal; pircis_console() below is the way in
#endif
  if (!started) { started = true; std::thread(stdinReader).detach(); }
  std::lock_guard<std::mutex> g(g_lineMx);
  if (g_lines.empty()) return false;
  line = g_lines.front();
  g_lines.erase(g_lines.begin());
  return true;
}

// Deliberately NOT recursive: the device's FreeRTOS mutex is not recursive
// either, so a nested lock must deadlock here too. A recursive mutex would let
// the emulator sail past a bug that hangs the board.
Mutex::Mutex()  { impl_ = new std::mutex(); }
Mutex::~Mutex() { delete static_cast<std::mutex*>(impl_); }
void Mutex::lock()   { static_cast<std::mutex*>(impl_)->lock(); }
void Mutex::unlock() { static_cast<std::mutex*>(impl_)->unlock(); }

void startTask(void (*fn)(void*), void* arg, const char*, uint32_t) {
  std::thread(fn, arg).detach();
}
void taskYield(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms ? ms : 1)); }

// -- key/value store, persisted as hex lines so it survives a restart --
namespace kv {
namespace {
  std::map<std::string, std::vector<uint8_t>> g_map;
  bool g_loaded = false;

  std::string toHex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) { s += d[b >> 4]; s += d[b & 15]; }
    return s;
  }
  std::vector<uint8_t> fromHex(const std::string& s) {
    std::vector<uint8_t> v;
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    };
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
      int hi = nib(s[i]), lo = nib(s[i + 1]);
      if (hi < 0 || lo < 0) break;
      v.push_back((uint8_t)((hi << 4) | lo));
    }
    return v;
  }
  void save() {
    // Written beside the file and renamed over it, so an exit mid-write
    // leaves the old settings rather than half of the new.
    const std::string path = storePath(), tmp = path + ".tmp";
    {
      std::ofstream f(tmp, std::ios::trunc);
      for (const auto& kvp : g_map) f << kvp.first << ' ' << toHex(kvp.second) << '\n';
    }
    std::rename(tmp.c_str(), path.c_str());
  }
}

void begin() {
  if (g_loaded) return;
  g_loaded = true;
  std::ifstream f(storePath());
  std::string line;
  while (std::getline(f, line)) {
    auto sp = line.find(' ');
    if (sp == std::string::npos) continue;
    g_map[line.substr(0, sp)] = fromHex(line.substr(sp + 1));
  }
}

bool has(const char* key) { return g_map.count(key) != 0; }
void remove(const char* key) { g_map.erase(key); save(); }
std::size_t bytesLength(const char* key) {
  auto it = g_map.find(key);
  return it == g_map.end() ? 0 : it->second.size();
}
std::size_t getBytes(const char* key, void* buf, std::size_t cap) {
  auto it = g_map.find(key);
  if (it == g_map.end() || it->second.size() > cap) return 0;
  std::memcpy(buf, it->second.data(), it->second.size());
  return it->second.size();
}
void putBytes(const char* key, const void* buf, std::size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(buf);
  g_map[key] = std::vector<uint8_t>(p, p + len);
  save();
}
std::string getString(const char* key, const char* def) {
  auto it = g_map.find(key);
  if (it == g_map.end()) return def;
  return std::string(it->second.begin(), it->second.end());
}
void putString(const char* key, const char* value) { putBytes(key, value, std::strlen(value)); }
int getInt(const char* key, int def) {
  std::string s = getString(key, "");
  return s.empty() ? def : std::atoi(s.c_str());
}
void putInt(const char* key, int value) { putString(key, std::to_string(value).c_str()); }
bool getBool(const char* key, bool def) { return getInt(key, def ? 1 : 0) != 0; }
void putBool(const char* key, bool value) { putInt(key, value ? 1 : 0); }
void clearAll() { g_map.clear(); save(); }
}

// A page in a browser has no card either: what it keeps is the browser's.
#if defined(__EMSCRIPTEN__)
#define SK_NO_CARD 1
#else
#define SK_NO_CARD TARGET_OS_IPHONE
#endif
bool sdPresent() { return !SK_NO_CARD; }   // the emulator writes to ./sdcard/; a phone has no card

bool writeRunFile(const std::string& text, std::string& pathOut) {
  std::error_code ec;
  std::filesystem::create_directories(kSdDir, ec);
  char path[64];
  for (int n = 1; n < 1000; ++n) {
    snprintf(path, sizeof(path), "%s/run_%03d.txt", kSdDir, n);
    if (!std::filesystem::exists(path, ec)) break;
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(text.data(), (std::streamsize)text.size());
  pathOut = path;
  return true;
}

bool webBegin(const std::string&, const std::string&, std::string&) { return false; }
void webStop() { }
void webTick() { }
bool webAvailable() { return false; }

// The desktop keeps every tile so the whole page can be tried here; a phone
// has no radio, no card slot and no panel to calibrate, and both have a
// browser to hand an address to.
// A screen of its own has no radio this program drives and a touch panel
// the system calibrates, not us; the card is a directory and stays.
bool hasWifi()       { return !TARGET_OS_IPHONE && !deviceMode(); }
bool hasSdSlot()     { return !SK_NO_CARD; }
bool hasTouchCheck() { return !TARGET_OS_IPHONE && !deviceMode(); }
bool canOpenUrl()    { return true; }
#if defined(__EMSCRIPTEN__)
// The page opens it: a browser only lets a page open another from its own
// thread, and may still refuse, in which case the page shows the link.
bool openUrl(const char* url) {
  MAIN_THREAD_EM_ASM({ if (window.pircisOpen) window.pircisOpen(UTF8ToString($0)); }, url);
  return true;
}
#else
bool openUrl(const char* url) { return SDL_OpenURL(url) == 0; }
#endif

// A phone's glass, or a screen this program is the whole of: a mouse is
// one finger that drags but cannot pinch.
namespace {
  bool g_device = false;                 // --kiosk: the program is the screen
  bool g_touch = false, g_keyboard = true, g_mouse = true;
  std::atomic<int>  g_scaleAsk{0};
  std::atomic<bool> g_quitAsk{false};
}
void setDeviceMode(bool on, bool touch, bool keyboard, bool mouse) {
  g_device = on; g_touch = touch; g_keyboard = keyboard; g_mouse = mouse;
}
bool deviceMode() { return g_device; }
int  takeScaleRequest() { return g_scaleAsk.exchange(0); }
bool quitAsked() { return g_quitAsk.exchange(false); }
bool hideCursor()  { return g_device && g_touch && !g_mouse; }
bool hasGestures() { return TARGET_OS_IPHONE || g_device; }
bool hasPinch()    { return (TARGET_OS_IPHONE && !onMac()) || g_touch; }
bool takePinch(int& dir, int& x, int& y) { return gfx.sdl().takePinch(dir, x, y); }
bool takeWheel(int& dy, int& dx, int& x, int& y) { return gfx.sdl().takeWheel(dy, dx, x, y); }
// The desktop emulator keeps the board's default, the on-screen keys, so
// the pages can be tried as the board shows them; a screen with a keyboard
// plugged in starts with that keyboard.
bool preferHardwareKeys() { return (g_device && g_keyboard) || (TARGET_OS_IPHONE && hardwareKeyboard()); }
#if !TARGET_OS_IPHONE
bool onMac() { return false; }
void startTextInputOnMain() {}
void keepTextInput() {}
bool hardwareKeyboard() { return true; }
bool takeKeyboardChange() { return false; }
std::string keyboardNote() { return std::string(); }
void alignLayerScale() {}
#endif

// What is plugged in, as Linux lists it: a device whose handlers include
// kbd and whose event bits are a keyboard's (EV=120013, or the same with a
// LED bit dropped) is a keyboard; one with a mouse handler is a mouse or a
// touchpad. Elsewhere both are assumed present.
void probeInput(bool& keyboard, bool& mouse) {
  keyboard = true; mouse = true;
#if defined(__linux__)
  std::ifstream f("/proc/bus/input/devices");
  if (!f) return;
  keyboard = false; mouse = false;
  std::string line, handlers, ev;
  auto flush = [&]() {
    if (handlers.find("kbd") != std::string::npos && (ev == "120013" || ev == "100013" || ev == "120003")) keyboard = true;
    if (handlers.find("mouse") != std::string::npos) mouse = true;
    handlers.clear(); ev.clear();
  };
  while (std::getline(f, line)) {
    if (line.empty()) { flush(); continue; }
    if (line.rfind("H: Handlers=", 0) == 0) handlers = line.substr(12);
    else if (line.rfind("B: EV=", 0) == 0) ev = line.substr(6);
  }
  flush();
#endif
}

#if !TARGET_OS_IPHONE
// A screen that this program is the whole of is an app in the pages'
// sense too: no board to speak of, and the editor a tap away.
bool isApp() { return g_device; }
bool screenArea(ScreenArea&) { return false; }
bool screenChanged() { return false; }
bool isActive() { return true; }
bool hasNativeKeys() { return false; }
void nativeKeys(const NativeKeys&) {}
void nativeKeysRelayout() {}
// The desktop has the file system itself; the phone's sheets are in
// Platform_ios.mm.
bool canShareFiles() { return false; }
bool shareText(const std::string&, const std::string&) { return false; }
bool canPickFiles() { return false; }
void pickFile() {}
bool takePickedFile(std::string&, std::string&) { return false; }
#endif


// --- saved programs --------------------------------------------------------
namespace {
  // Two stores, mirroring the device: its own flash and the card.
  const char* kDevDir = "device";
  std::string storeRoot(Where w) { return w == Where::Device ? kDevDir : kSdDir; }
  std::string progDir(Where w)   { return storeRoot(w) + "/programs"; }
  // Keep names to something every filesystem and the SD FAT driver accept.
  bool safeLeaf(const std::string& n) {
    if (n.empty() || n.size() > 24) return false;
    for (char c : n)
      if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
  }

  // One folder in front of the name and no more, matching the device.
  bool safeName(const std::string& n) {
    const std::size_t slash = n.find('/');
    if (slash == std::string::npos) return safeLeaf(n);
    return n.find('/', slash + 1) == std::string::npos
        && safeLeaf(n.substr(0, slash)) && safeLeaf(n.substr(slash + 1));
  }

  bool isTxt(const std::string& n) {
    return n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0;
  }
}

bool progStoreReady(Where w) { return w == Where::Device || sdPresent(); }

// Bare "odds" at the top, "Counting/odds" one folder down. One level only,
// the same as the board.
bool progList(Where w, std::vector<std::string>& namesOut) {
  namesOut.clear();
  if (!progStoreReady(w)) return false;
  const std::string root = progDir(w);
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) return true;   // none yet is fine
  for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
    const std::string n = e.path().filename().string();
    if (e.is_directory(ec)) {
      if (!safeLeaf(n)) continue;
      for (const auto& f : std::filesystem::directory_iterator(e.path(), ec)) {
        const std::string fn = f.path().filename().string();
        if (isTxt(fn)) namesOut.push_back(n + "/" + fn.substr(0, fn.size() - 4));
      }
    }
    else if (isTxt(n)) namesOut.push_back(n.substr(0, n.size() - 4));
  }
  std::sort(namesOut.begin(), namesOut.end());
  return true;
}

bool progRead(Where w, const std::string& name, std::string& textOut) {
  if (!safeName(name) || !progStoreReady(w)) return false;
  std::ifstream f(progDir(w) + "/" + name + ".txt", std::ios::binary);
  if (!f) return false;
  textOut.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

// Saved run reports live in the card's top directory rather than in programs/.
bool runList(std::vector<std::string>& namesOut) {
  namesOut.clear();
  std::error_code ec;
  if (!std::filesystem::is_directory(kSdDir, ec)) return true; // none yet is fine
  for (const auto& e : std::filesystem::directory_iterator(kSdDir, ec)) {
    const std::string n = e.path().filename().string();
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0)
      namesOut.push_back(n.substr(0, n.size() - 4));
  }
  std::sort(namesOut.begin(), namesOut.end());
  return true;
}

bool runRead(const std::string& name, std::string& textOut) {
  if (!safeName(name)) return false;
  std::ifstream f(std::string(kSdDir) + "/" + name + ".txt", std::ios::binary);
  if (!f) return false;
  textOut.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool progWrite(Where w, const std::string& name, const std::string& text) {
  if (!safeName(name) || !progStoreReady(w)) return false;
  std::error_code ec;
  std::filesystem::create_directories(progDir(w), ec);
  const std::size_t slash = name.find('/');
  if (slash != std::string::npos)
    std::filesystem::create_directories(progDir(w) + "/" + name.substr(0, slash), ec);
  std::ofstream f(progDir(w) + "/" + name + ".txt", std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(text.data(), (std::streamsize)text.size());
  return (bool)f;
}

bool progDelete(Where w, const std::string& name) {
  if (!safeName(name) || !progStoreReady(w)) return false;
  std::error_code ec;
  const bool ok = std::filesystem::remove(progDir(w) + "/" + name + ".txt", ec) && !ec;
  // The folder goes with its last program. remove() on a directory only
  // succeeds when it is empty, which is exactly the test wanted here.
  const std::size_t slash = name.find('/');
  if (ok && slash != std::string::npos)
    std::filesystem::remove(progDir(w) + "/" + name.substr(0, slash), ec);
  return ok;
}


// --- the keyboard the emulator is sitting on ------------------------------
//
// LovyanGFX's SDL panel pumps the event queue itself and only maps keys to
// GPIO pins, so text never reaches us. SDL_AddEventWatch sees each event as it
// is posted without consuming it, which leaves the panel's own handling alone.
namespace {
  std::mutex        g_keyMx;
  std::vector<char> g_keys;          // small enough that a vector is a queue
  Uint32            g_dropTextUntil = 0;   // text events are ignored until this tick

  void pushKey(char c) {
    std::lock_guard<std::mutex> g(g_keyMx);
    if (g_keys.size() < 64) g_keys.push_back(c);
  }

  // On a phone or a Mac the keys come by two roads. A press reaches SDL at
  // once through the keyboard framework, and the character it types comes
  // a moment later through the text field, so Return pressed straight
  // after a word could arrive before the word. The keys that act -- Return,
  // Backspace, Tab, the arrows, the chords -- therefore wait behind any
  // character still on its way: a printable press counts one due, its
  // text pays it off, and while any is due the acting keys are held. A
  // press whose text never comes -- a dead key -- is let go after a while.
  int               g_textDue   = 0;
  Uint32            g_textDueAt = 0;
  std::vector<char> g_held;

  // A key held down. A desktop's system repeats it and says so (the press
  // comes again marked as a repeat); a phone's keyboard framework and a
  // Mac's, on the app, send one press and one release and nothing between.
  // So the app repeats the key itself while it is down, unless the system
  // has been seen to: f and b step, the arrows move, Backspace deletes, and
  // holding any of them keeps it going.
  SDL_Keycode g_heldSym   = 0;
  char        g_heldKey   = 0;
  Uint32      g_heldSince = 0, g_heldLast = 0;
  bool        g_sysRepeats = false;
  constexpr Uint32 kRepeatAfter = 350, kRepeatEvery = 90;
  bool repeating() { return g_heldSym && !g_sysRepeats && SDL_GetTicks() - g_heldSince > kRepeatAfter; }
  void flushHeldLocked() {
    for (char c : g_held) if (g_keys.size() < 64) g_keys.push_back(c);
    g_held.clear();
  }
  void pushActing(char c) {
    std::lock_guard<std::mutex> g(g_keyMx);
    if (TARGET_OS_IPHONE && g_textDue > 0) g_held.push_back(c);
    else if (g_keys.size() < 64) g_keys.push_back(c);
  }
  void textArrived() {
    std::lock_guard<std::mutex> g(g_keyMx);
    if (g_textDue > 0) --g_textDue;
    if (g_textDue == 0) flushHeldLocked();
  }
  void pressDue() {
    std::lock_guard<std::mutex> g(g_keyMx);
    if (g_textDue++ == 0) g_textDueAt = SDL_GetTicks();
  }

  // The arrows by where they are on the keyboard, not by what the system
  // calls them: Safari reports a Mac's arrow keys as the number pad's, and
  // SDL then names them keypad 2, 4, 6 and 8.
  SDL_Keycode symOf(const SDL_Event* e) {
    switch (e->key.keysym.scancode) {
      case SDL_SCANCODE_UP:    return SDLK_UP;
      case SDL_SCANCODE_DOWN:  return SDLK_DOWN;
      case SDL_SCANCODE_LEFT:  return SDLK_LEFT;
      case SDL_SCANCODE_RIGHT: return SDLK_RIGHT;
      default:                 return e->key.keysym.sym;
    }
  }
  int keyWatch(void*, SDL_Event* e) {
    // On a screen of its own, Alt with a digit picks the picture scale and
    // Alt-Q leaves; the panel's own resize keys are switched off there,
    // since the window is the screen.
    if (e->type == SDL_KEYDOWN && g_device && (e->key.keysym.mod & KMOD_ALT)) {
      const SDL_Keycode k = e->key.keysym.sym;
      if (k >= SDLK_1 && k <= SDLK_4) { g_scaleAsk = (int)(k - SDLK_1) + 1; return 1; }
      if (k == SDLK_q) { g_quitAsk = true; return 1; }
    }
    if (e->type == SDL_TEXTINPUT) {
      // On a Mac the hidden field that collects typed characters also
      // answers Cmd-V with the clipboard, as any field would; that text is
      // the paste already handled below, not typing.
      if ((Sint32)(g_dropTextUntil - SDL_GetTicks()) > 0) return 1;
      for (const char* p = e->text.text; *p; ++p) {
        // While the app is repeating a held key itself, the text the
        // system may also repeat for it is not typed a second time.
        if (repeating() && *p == g_heldKey) continue;
        if (*p >= 0x20 && *p < 0x7f) pushKey(*p);
      }
      textArrived();
    }
    else if (e->type == SDL_KEYUP) {
      if (symOf(e) == g_heldSym) g_heldSym = 0;
    }
    else if (e->type == SDL_KEYDOWN) {
      const SDL_Keymod m = (SDL_Keymod)e->key.keysym.mod;
      const bool chord = (m & (KMOD_CTRL | KMOD_GUI)) != 0;   // ctrl or cmd
      if (chord && TARGET_OS_IPHONE) g_dropTextUntil = SDL_GetTicks() + 150;   // the hidden field answers the chord too
      const bool shift = (m & KMOD_SHIFT) != 0;
      if (chord) {
        // A chord never produces SDL_TEXTINPUT, so these cannot collide with
        // a character being typed into the grid.
        switch (e->key.keysym.sym) {
          case SDLK_s: pushActing(kKeySave); break;
          case SDLK_z: pushActing(shift ? kKeyRedo : kKeyUndo); break;
          case SDLK_y: pushActing(kKeyRedo); break;
          case SDLK_r: pushActing(kKeyRun);  break;
          case SDLK_n: pushActing(kKeyName); break;
          case SDLK_g: pushActing(kKeyZoom); break;
#if !defined(__EMSCRIPTEN__)
          case SDLK_v: pushActing(kKeyPaste); break;   // in a browser the page's paste event does this
#endif
          // Cmd-Shift-? is the question mark, which is the same symbol as the
          // editor's help button. Reaches the shortcut list from the one page
          // where a bare key cannot, because there they go into the program.
          case SDLK_SLASH:
          case SDLK_QUESTION: if (shift) pushActing(kKeyHelp); break;
          default: break;
        }
        return 1;
      }
      const SDL_Keycode sym = symOf(e);
      if (e->key.repeat) g_sysRepeats = true;      // the system repeats: the app need not
      else if (sym == SDLK_f || sym == SDLK_b || sym == SDLK_BACKSPACE ||
               sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT || sym == SDLK_RIGHT) {
        g_heldSym = sym; g_heldSince = SDL_GetTicks(); g_heldLast = g_heldSince; g_sysRepeats = false;
        g_heldKey = sym == SDLK_f ? 'f' : sym == SDLK_b ? 'b' : sym == SDLK_BACKSPACE ? '\b'
                  : sym == SDLK_UP ? kKeyUp : sym == SDLK_DOWN ? kKeyDown : sym == SDLK_LEFT ? kKeyLeft : kKeyRight;
      }
      switch (sym) {
        case SDLK_BACKSPACE: pushActing('\b'); break;
        case SDLK_RETURN:    pushActing('\r'); break;
        case SDLK_UP:        pushActing(kKeyUp); break;
        case SDLK_DOWN:      pushActing(kKeyDown); break;
        case SDLK_LEFT:      pushActing(kKeyLeft); break;
        case SDLK_RIGHT:     pushActing(kKeyRight); break;
        case SDLK_TAB:       pushActing(shift ? kKeyBack : kKeyTab); break;
        case SDLK_ESCAPE:    pushActing(kKeyEsc); break;
        case SDLK_F1:        pushActing(kKeyHelp); break;
        // Space is left to SDL_TEXTINPUT, which delivers it as an ordinary
        // 0x20. Pushing it here as well would insert it twice. The UI decides
        // what a space means: a blank in the grid, a press anywhere else.
        default:
          // A printable press: its character is on its way through the
          // text field, and the keys that act wait for it.
          if (TARGET_OS_IPHONE && !e->key.repeat && sym >= 0x20 && sym < 0x7f) pressDue();
          break;
      }
    }
    return 1;                        // 1 keeps the event in the queue
  }
}

char pollKey() {
  static bool armed = false;
  {
    // A press whose text never came: after a moment the held keys go
    // through anyway, in the order they were pressed.
    std::lock_guard<std::mutex> g(g_keyMx);
    if (g_textDue > 0 && SDL_GetTicks() - g_textDueAt > 120) { g_textDue = 0; flushHeldLocked(); }
  }
  if (!armed) {
    armed = true;
    SDL_AddEventWatch(keyWatch, nullptr);
    // Asking SDL for text input is how a desktop gets typed characters. On a
    // phone the same call raises the system keyboard, from this thread,
    // which UIKit forbids; the phone types on the drawn keyboard instead.
    if (!TARGET_OS_IPHONE) SDL_StartTextInput();
    else if (onMac()) startTextInputOnMain();   // a Mac has the keyboard a phone lacks
  }
  std::lock_guard<std::mutex> g(g_keyMx);
  if (!g_keys.empty()) {
    const char c = g_keys.front();
    g_keys.erase(g_keys.begin());
    return c;
  }
  // Nothing typed: a key still held repeats, so long as it really is still
  // down -- a release lost to a change of focus must not repeat for ever.
  if (g_heldSym) {
    const Uint8* down = SDL_GetKeyboardState(nullptr);
    if (!down[SDL_GetScancodeFromKey(g_heldSym)]) g_heldSym = 0;
  }
  if (repeating() && SDL_GetTicks() - g_heldLast >= kRepeatEvery) {
    g_heldLast = SDL_GetTicks();
    return g_heldKey;
  }
  return 0;
}

void injectKey(char c) { pushKey(c); }

#if defined(__EMSCRIPTEN__)
// A page may not read the clipboard when it likes, only be handed what was
// pasted into it. The page passes that on here, and it is the clipboard.
namespace { std::mutex g_clipMx; std::string g_webClip; }
bool hasClipboard() { std::lock_guard<std::mutex> g(g_clipMx); return !g_webClip.empty(); }
std::string clipboard() { std::lock_guard<std::mutex> g(g_clipMx); return g_webClip; }
extern "C" EMSCRIPTEN_KEEPALIVE void pircis_paste(const char* text) {
  { std::lock_guard<std::mutex> g(g_clipMx); g_webClip = text ? text : ""; }
  pushKey(kKeyPaste);
}
extern "C" EMSCRIPTEN_KEEPALIVE void pircis_console(const char* line) { pushLine(line ? line : ""); }
#else
bool hasClipboard() { return true; }
std::string clipboard() {
  if (!SDL_HasClipboardText()) return std::string();
  char* p = SDL_GetClipboardText();
  if (!p) return std::string();
  std::string s(p);
  SDL_free(p);
  return s;
}
#endif

// A phone has no keyboard of its own and, like the board, types on the one
// the program draws; a tablet with a keyboard attached, or a Mac, has one.
bool haveKeyboard() { return !TARGET_OS_IPHONE || hardwareKeyboard(); }

// No radio on the desktop, so the hooks are stored and never used.
void webSetHooks(const WebHooks&) {}


// The board reports the first eight bytes of its app image hash. There is no
// app image here, so hash the build stamp to the same shape -- it still
// changes on every build, which is all Store uses it for.
std::string firmwareId() {
  const char* stamp = __DATE__ " " __TIME__;
  uint64_t h = 1469598103934665603ull;              // FNV-1a
  for (const char* c = stamp; *c; ++c) { h ^= (unsigned char)*c; h *= 1099511628211ull; }
  char buf[17];
  snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
  return buf;
}
std::string firmwareBuilt() { return __DATE__ " " __TIME__; }


uint32_t randomSeed() {
  static std::mt19937 gen(std::random_device{}());
  return (uint32_t)gen();
}

}
#endif
