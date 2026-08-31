// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#if defined(SK_HOST)

#include "Platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <random>
#include <istream>
#include <cctype>
#include <algorithm>
#include <dirent.h>
#include <map>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace plat {
namespace {
  const auto g_start = std::chrono::steady_clock::now();
  const char* kStorePath = "emulator_nvs.txt";
  const char* kSdDir = "sdcard";
}

uint32_t millis() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - g_start).count();
}
void delayMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
uint32_t freeHeap() { return 300000; }   // the ESP32 figure this design targets
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

// Non-blocking stdin, so the emulator's console behaves like the serial port.
// A single read can carry several commands, so everything after the newline is
// kept for the next call rather than dropped.
bool readLine(std::string& line) {
  static bool configured = false;
  static std::string buffer;
  if (!configured) {
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    configured = true;
  }
  char buf[512];
  ssize_t n;
  while ((n = ::read(STDIN_FILENO, buf, sizeof(buf))) > 0)
    buffer.append(buf, (std::size_t)n);

  while (!buffer.empty()) {
    std::size_t nl = buffer.find_first_of("\r\n");
    if (nl == std::string::npos) return false;
    std::string candidate = buffer.substr(0, nl);
    buffer.erase(0, nl + 1);
    if (!candidate.empty()) { line = candidate; return true; }
  }
  return false;
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
    std::ofstream f(kStorePath, std::ios::trunc);
    for (const auto& kvp : g_map) f << kvp.first << ' ' << toHex(kvp.second) << '\n';
  }
}

void begin() {
  if (g_loaded) return;
  g_loaded = true;
  std::ifstream f(kStorePath);
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

bool sdPresent() { return true; }   // the emulator writes to ./sdcard/

bool writeRunFile(const std::string& text, std::string& pathOut) {
  ::mkdir(kSdDir, 0755);
  char path[64];
  for (int n = 1; n < 1000; ++n) {
    snprintf(path, sizeof(path), "%s/run_%03d.txt", kSdDir, n);
    struct stat st;
    if (::stat(path, &st) != 0) break;
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


// --- saved programs --------------------------------------------------------
namespace {
  // Two stores, mirroring the device: its own flash and the card.
  const char* kDevDir = "device";
  std::string storeRoot(Where w) { return w == Where::Device ? kDevDir : kSdDir; }
  std::string progDir(Where w)   { return storeRoot(w) + "/programs"; }
  // Keep names to something every filesystem and the SD FAT driver accept.
  bool safeName(const std::string& n) {
    if (n.empty() || n.size() > 24) return false;
    for (char c : n)
      if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
  }
}

bool progStoreReady(Where w) { return w == Where::Device || sdPresent(); }

bool progList(Where w, std::vector<std::string>& namesOut) {
  namesOut.clear();
  if (!progStoreReady(w)) return false;
  DIR* d = ::opendir(progDir(w).c_str());
  if (!d) return true;                    // no directory yet is not an error
  while (struct dirent* e = ::readdir(d)) {
    std::string n = e->d_name;
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0)
      namesOut.push_back(n.substr(0, n.size() - 4));
  }
  ::closedir(d);
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
  DIR* d = ::opendir(kSdDir);
  if (!d) return true;                    // no directory yet is not an error
  while (struct dirent* e = ::readdir(d)) {
    std::string n = e->d_name;
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0)
      namesOut.push_back(n.substr(0, n.size() - 4));
  }
  ::closedir(d);
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
  ::mkdir(storeRoot(w).c_str(), 0755);
  ::mkdir(progDir(w).c_str(), 0755);
  std::ofstream f(progDir(w) + "/" + name + ".txt", std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(text.data(), (std::streamsize)text.size());
  return (bool)f;
}

bool progDelete(Where w, const std::string& name) {
  if (!safeName(name) || !progStoreReady(w)) return false;
  return ::remove((progDir(w) + "/" + name + ".txt").c_str()) == 0;
}


namespace { bool g_powerOff = false; }
void powerOff() { g_powerOff = true; }
bool powerOffRequested() { return g_powerOff; }


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
