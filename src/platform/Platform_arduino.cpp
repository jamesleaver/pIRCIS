// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#if !defined(SK_HOST)

#include "Platform.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <SD.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "Config.h"
#include "Display.h"

namespace plat {

uint32_t millis() { return ::millis(); }
void delayMs(uint32_t ms) { ::delay(ms); }
uint32_t freeHeap() { return ESP.getFreeHeap(); }
uint32_t maxAllocHeap() { return ESP.getMaxAllocHeap(); }

// The interpreter task streams program output to the serial port while the
// loop task prints command replies; serialise them so lines stay whole.
static SemaphoreHandle_t consoleMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}
struct ConsoleLock {
  ConsoleLock()  { xSemaphoreTake(consoleMutex(), portMAX_DELAY); }
  ~ConsoleLock() { xSemaphoreGive(consoleMutex()); }
};

void log(const char* text) { ConsoleLock lk; Serial.print(text); }
void logln(const char* text) { ConsoleLock lk; Serial.println(text); }
void logf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  ConsoleLock lk;
  Serial.print(buf);
}

bool readLine(std::string& line) {
  static std::string pending;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (pending.empty()) continue;
      line = pending;
      pending.clear();
      return true;
    }
    if (pending.size() < 200) pending.push_back(c);
  }
  return false;
}

Mutex::Mutex()  { impl_ = xSemaphoreCreateMutex(); }
Mutex::~Mutex() { if (impl_) vSemaphoreDelete((SemaphoreHandle_t)impl_); }
void Mutex::lock()   { xSemaphoreTake((SemaphoreHandle_t)impl_, portMAX_DELAY); }
void Mutex::unlock() { xSemaphoreGive((SemaphoreHandle_t)impl_); }

void startTask(void (*fn)(void*), void* arg, const char* name, uint32_t stackBytes) {
  // Core 1: the interpreter. Core 0 keeps the display and touch responsive.
  xTaskCreatePinnedToCore(fn, name, stackBytes, arg, 1, nullptr, 1);
}

void taskYield(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  vTaskDelay(ticks ? ticks : 1);
}

namespace kv {
  static Preferences prefs;
  void begin() { prefs.begin("pircis", false); }
  bool has(const char* key) { return prefs.isKey(key); }
  void remove(const char* key) { prefs.remove(key); }
  std::size_t bytesLength(const char* key) { return prefs.getBytesLength(key); }
  std::size_t getBytes(const char* key, void* buf, std::size_t cap) {
    std::size_t n = prefs.getBytesLength(key);
    if (n == 0 || n > cap) return 0;
    return prefs.getBytes(key, buf, n);
  }
  void putBytes(const char* key, const void* buf, std::size_t len) { prefs.putBytes(key, buf, len); }
  std::string getString(const char* key, const char* def) {
    // Preferences logs an ERROR for a key that was simply never written, which
    // on a fresh device is every key. Ask first so the console stays clean.
    if (!prefs.isKey(key)) return def;
    return prefs.getString(key, def).c_str();
  }
  void putString(const char* key, const char* value) { prefs.putString(key, value); }
  int  getInt(const char* key, int def) { return prefs.getInt(key, def); }
  void putInt(const char* key, int value) { prefs.putInt(key, value); }
  bool getBool(const char* key, bool def) { return prefs.getBool(key, def); }
  void putBool(const char* key, bool value) { prefs.putBool(key, value); }
  void clearAll() { prefs.clear(); }
}

// Probed once at first use: mounting the card is slow and cannot be done
// while the UI is drawing.
bool sdPresent() {
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  static SPIClass probe(VSPI);
  probe.end();
  probe.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  cached = SD.begin(SD_CS, probe, 20000000) ? 1 : 0;
  if (cached) SD.end();
  probe.end();
  gfx.reinitTouch();
  return cached != 0;
}

bool writeRunFile(const std::string& text, std::string& pathOut) {
  // The card and the touch controller share the VSPI host on this board, on
  // different pins, so claim the bus, write one file, and hand it back.
  static SPIClass sdSpi(VSPI);
  bool ok = false;

  sdSpi.end();
  sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, sdSpi, 20000000)) {
    if (!SD.exists("/pircis")) SD.mkdir("/pircis");
    char path[48];
    for (int n = 1; n < 1000; ++n) {
      snprintf(path, sizeof(path), "/pircis/run_%03d.txt", n);
      if (!SD.exists(path)) break;
    }
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
      f.close();
      pathOut = path;
      ok = true;
    }
    SD.end();
  }
  sdSpi.end();
  gfx.reinitTouch();
  return ok;
}

// -- web view --
namespace {
  WebServer   g_server(80);
  bool        g_webRunning = false;

  WebHooks g_hooks;

  // HTTP only: what a page contains is the web layer's business, and it
  // builds them where they can be rendered and checked without a radio.
  void serve() {
    if (!g_hooks.page) { g_server.send(503, "text/plain", "no handler"); return; }
    const bool post = g_server.method() == HTTP_POST;
    std::string body;
    if (post) {
      // WebServer has already buffered the request; cap what we copy out of
      // it, and let the page decide whether what is left is usable.
      String raw = g_server.arg("plain");
      if (raw.length() > 16384) raw = raw.substring(0, 16384);
      body = raw.c_str();
    }
    std::string query;
    for (int i = 0; i < g_server.args(); ++i) {
      if (g_server.argName(i) == "plain") continue;
      if (!query.empty()) query += '&';
      query += g_server.argName(i).c_str();
      query += '=';
      query += g_server.arg(i).c_str();
    }
    const std::string page =
        g_hooks.page(g_server.uri().c_str(), query, body, post);
    g_server.send(200, "text/html", page.c_str());
  }
}


bool webBegin(const std::string& ssid, const std::string& pass, std::string& ipOut) {
  if (g_webRunning) { ipOut = WiFi.localIP().toString().c_str(); return true; }
  if (ssid.empty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = ::millis();
  while (WiFi.status() != WL_CONNECTED && ::millis() - start < 15000) ::delay(200);
  if (WiFi.status() != WL_CONNECTED) { WiFi.disconnect(true); return false; }
  ipOut = WiFi.localIP().toString().c_str();
  g_server.onNotFound(serve);
  g_server.begin();
  g_webRunning = true;
  return true;
}

void webStop() {
  if (!g_webRunning) return;
  g_server.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  g_webRunning = false;
}

void webSetHooks(const WebHooks& hooks) { g_hooks = hooks; }
void webTick() { if (g_webRunning) g_server.handleClient(); }
bool webAvailable() { return true; }


// --- saved programs --------------------------------------------------------
namespace {
  const char* kSkDir   = "/pircis";
  const char* kProgDir = "/pircis/programs";

  // FAT-safe and short. Anything else is refused rather than mangled.
  bool safeLeaf(const std::string& n) {
    if (n.empty() || n.size() > 24) return false;
    for (char c : n)
      if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
  }

  // A name may carry one folder in front of it -- "Counting/odds" -- and no
  // more. One level is what the PROG list can show without a breadcrumb, and
  // deeper nesting on seventy-odd files buys nothing but taps.
  bool safeName(const std::string& n) {
    const std::size_t slash = n.find('/');
    if (slash == std::string::npos) return safeLeaf(n);
    return n.find('/', slash + 1) == std::string::npos
        && safeLeaf(n.substr(0, slash)) && safeLeaf(n.substr(slash + 1));
  }

  // Entries come back from LittleFS as full paths and from SD as bare names,
  // so take the last component either way.
  std::string leafOf(const std::string& p) {
    const std::size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
  }

  // Claim the card's bus, do one job, hand it back. Kept as a single helper so
  // every card access releases the bus the same way; a missed release leaves
  // the display driver talking to a reconfigured SPI host.
  template <typename F>
  bool withSd(F&& job) {
    static SPIClass sdSpi(VSPI);
    bool ok = false;
    sdSpi.end();
    sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (SD.begin(SD_CS, sdSpi, 20000000)) {
      if (!SD.exists(kSkDir))   SD.mkdir(kSkDir);
      if (!SD.exists(kProgDir)) SD.mkdir(kProgDir);
      ok = job();
      SD.end();
    }
    sdSpi.end();
    gfx.reinitTouch();
    return ok;
  }

  std::string progPath(const char* dir, const std::string& name) {
    return std::string(dir) + "/" + name + ".txt";
  }

  // The board's own flash: LittleFS on the data partition the build already
  // reserves and nothing else uses. Unlike the card it shares no bus with the
  // display, so it is mounted once and left mounted -- there is nothing to
  // hand back. begin(true) formats it the first time.
  const char* kDevProgDir = "/programs";
  bool devFsReady() {
    static bool tried = false, ok = false;
    if (!tried) {
      tried = true;
      ok = LittleFS.begin(true);
      if (ok && !LittleFS.exists(kDevProgDir)) LittleFS.mkdir(kDevProgDir);
    }
    return ok;
  }

  // One job against whichever store was asked for, so the four operations
  // below are written once rather than twice.
  template <typename F>
  bool withStore(Where w, F&& job) {
    if (w == Where::Device)
      return devFsReady() && job((fs::FS&)LittleFS, kDevProgDir);
    return withSd([&] { return job((fs::FS&)SD, kProgDir); });
  }
}

bool progStoreReady(Where w) { return w == Where::Device ? devFsReady() : sdPresent(); }

bool runList(std::vector<std::string>& namesOut) {
  namesOut.clear();
  return withSd([&] {
    File dir = SD.open(kSkDir);
    if (!dir) return false;
    while (File e = dir.openNextFile()) {
      std::string n = e.name();
      std::size_t slash = n.find_last_of('/');
      if (slash != std::string::npos) n = n.substr(slash + 1);
      if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0)
        namesOut.push_back(n.substr(0, n.size() - 4));
      e.close();
    }
    dir.close();
    std::sort(namesOut.begin(), namesOut.end());
    return true;
  });
}

bool runRead(const std::string& name, std::string& textOut) {
  if (!safeName(name)) return false;
  return withSd([&] {
    File f = SD.open((std::string(kSkDir) + "/" + name + ".txt").c_str(), FILE_READ);
    if (!f) return false;
    textOut.clear();
    while (f.available()) textOut += (char)f.read();
    f.close();
    return true;
  });
}

// Names come back the way they are addressed: "odds" at the top, and
// "Counting/odds" one folder down. Folders are not descended past the first
// level, so a stray deep directory is ignored rather than half-listed.
bool progList(Where w, std::vector<std::string>& namesOut) {
  namesOut.clear();
  return withStore(w, [&](fs::FS& fs, const char* dir) {
    File d = fs.open(dir);
    if (!d) return false;
    auto addTxt = [&](const std::string& raw, const std::string& prefix) {
      const std::string n = leafOf(raw);
      if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0)
        namesOut.push_back(prefix + n.substr(0, n.size() - 4));
    };
    while (File e = d.openNextFile()) {
      if (e.isDirectory()) {
        const std::string sub = leafOf(e.name());
        if (safeLeaf(sub)) {
          File s = fs.open((std::string(dir) + "/" + sub).c_str());
          if (s) {
            while (File f = s.openNextFile()) {
              if (!f.isDirectory()) addTxt(f.name(), sub + "/");
              f.close();
            }
            s.close();
          }
        }
      }
      else addTxt(e.name(), "");
      e.close();
    }
    d.close();
    std::sort(namesOut.begin(), namesOut.end());
    return true;
  });
}
bool progRead(Where w, const std::string& name, std::string& textOut) {
  if (!safeName(name)) return false;
  return withStore(w, [&](fs::FS& fs, const char* dir) {
    File f = fs.open(progPath(dir, name).c_str(), FILE_READ);
    if (!f) return false;
    textOut.clear();
    textOut.reserve(f.size());
    while (f.available()) textOut.push_back((char)f.read());
    f.close();
    return true;
  });
}
bool progWrite(Where w, const std::string& name, const std::string& text) {
  if (!safeName(name)) return false;
  return withStore(w, [&](fs::FS& fs, const char* dir) {
    const std::size_t slash = name.find('/');
    if (slash != std::string::npos) {
      const std::string sub = std::string(dir) + "/" + name.substr(0, slash);
      if (!fs.exists(sub.c_str())) fs.mkdir(sub.c_str());
    }
    File f = fs.open(progPath(dir, name).c_str(), FILE_WRITE);
    if (!f) return false;
    f.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    f.close();
    return true;
  });
}
bool progDelete(Where w, const std::string& name) {
  if (!safeName(name)) return false;
  return withStore(w, [&](fs::FS& fs, const char* dir) {
    return fs.remove(progPath(dir, name).c_str());
  });
}


void powerOff() {
  gfx.setBrightness(0);
  gfx.sleep();
  // No wake source is configured: the board comes back on reset, which is what
  // an "off" button on a device with no power switch can honestly offer.
  esp_deep_sleep_start();
}
bool powerOffRequested() { return false; }


std::string firmwareId() {
  const esp_app_desc_t* d = esp_ota_get_app_description();
  char buf[17];
  for (int i = 0; i < 8; ++i) snprintf(buf + i * 2, 3, "%02x", d->app_elf_sha256[i]);
  buf[16] = 0;
  return buf;
}


std::string firmwareBuilt() {
  const esp_app_desc_t* d = esp_ota_get_app_description();
  return std::string(d->date) + " " + d->time;
}

uint32_t randomSeed() { return esp_random(); }   // hardware RNG

}
#endif
