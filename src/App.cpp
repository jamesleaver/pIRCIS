// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#include "App.h"

#include <cstdlib>
#include <string>
#include <vector>

#if defined(SK_HOST)
#include <fstream>
#endif

#include "Config.h"
#include "Display.h"
#include "Pack.h"
#include "Platform.h"
#include "RunTask.h"
#include "Program.h"
#include "Sinks.h"
#include "Store.h"
#include "Ui.h"
#include "WebView.h"

#if !defined(SK_HOST)
#include <Arduino.h>
#endif

namespace app {
namespace {

void setLed(bool r, bool g, bool b) {
#if !defined(SK_HOST)
  digitalWrite(LED_R, r ? LOW : HIGH);   // active low
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
#else
  (void)r; (void)g; (void)b;
#endif
}

void printHelp() {
  plat::logln(
    "\ncommands:\n"
    "  run | pause | reset            transport\n"
    "  step [n]                       advance n steps (default 1)\n"
    "  speed slow|medium|quick|full     run speed\n"
    "  cell <row> <col> <char>        overwrite a single cell\n"
    "  revert                         undo every edit\n"
    "  load                           push edits to the interpreter and reset\n"
    "  grid | out | report            dump state\n"
    "  progsave <name> | progload <name> | proglist\n"
    "                                 saved programs on the SD card\n"
    "  wifi <ssid> <password>         store credentials\n"
    "  web on|off                     start/stop the web view\n"
    "  sd                             write the last run to a file"
#if defined(SK_HOST)
    "\n  shot [file.ppm]                (emulator) dump the panel framebuffer\n"
    "  tap <x> <y>                    (emulator) synthetic touch\n"
    "  drag <dx>                      (emulator) pan the ZOOM view by dx px\n"
    "  quit                           (emulator) close the window"
#endif
    );
  // The parameter commands are listed only when there is a packed program to
  // point them at.
  if (Store::unlocked())
    plat::logln(
      "\n  slots                          list parameters, current vs original\n"
      "  set <ID>=<VALUE>               set one parameter\n"
      "  revert <ID>                    revert one parameter\n"
      "  save <n> <name> | preset <n>   store / recall a preset (1-8)");
}

void listSlots() {
  prog::Program& s = ui::editGrid();
  plat::logln("\nID       LABEL                  R   C   LEN CURRENT  ORIGINAL");
  for (int i = 0; i < prog::slotCount(); ++i) {
    const prog::Slot& sl = prog::slot(i);
    plat::logf("%-8s %-22s %-3d %-3d %-3d %-8s %-8s%s\n",
               sl.id.c_str(), sl.label.c_str(), sl.row, sl.col, sl.len,
               s.slotValue(i).c_str(), prog::slotOriginal(i).c_str(),
               s.slotModified(i) ? "  *" : "");
  }
}

std::string trim(std::string v) {
  std::size_t a = v.find_first_not_of(" \t");
  if (a == std::string::npos) return "";
  std::size_t b = v.find_last_not_of(" \t");
  return v.substr(a, b - a + 1);
}

#if defined(SK_HOST)
// Console names may name their store: "card:spiral" or "device:spiral".
// A bare name means the device, which is the one that is always there.
const char* whereTag(plat::Where w) {
  return w == plat::Where::Device ? "device" : "card";
}
plat::Where splitWhere(std::string& name) {
  const std::size_t c = name.find(':');
  if (c == std::string::npos) return plat::Where::Device;
  const std::string tag = name.substr(0, c);
  if (tag != "card" && tag != "device") return plat::Where::Device;
  name = name.substr(c + 1);
  return tag == "card" ? plat::Where::Card : plat::Where::Device;
}
#endif

std::string nextToken(std::string& line) {
  line = trim(line);
  std::size_t sp = line.find(' ');
  std::string tok = (sp == std::string::npos) ? line : line.substr(0, sp);
  line = (sp == std::string::npos) ? std::string() : trim(line.substr(sp + 1));
  return tok;
}

bool g_quit = false;
// Scratch for run::loadedGridInto(). File scope: see RunTask.h.
prog::Program g_ranGrid;

void handleCommand(std::string line) {
  line = trim(line);
  if (line.empty()) return;
  std::string cmd = nextToken(line);
  prog::Program& s = ui::editGrid();

  // The parameter commands are refused while the pack is closed, not merely
  // left out of the help: answering a command that is not listed would say as
  // much as listing it. `revert` is shared -- bare it undoes every edit, which
  // any program wants; `revert <ID>` names a parameter, which only a packed
  // program has.
  if (!Store::unlocked()) {
    const bool packedOnly = cmd == "slots" || cmd == "set"  || cmd == "save" ||
                            cmd == "preset" ||
                            (cmd == "revert" && !line.empty());
    if (packedOnly) { plat::logf("unknown command '%s' -- try help\n", cmd.c_str()); return; }
  }

  if (cmd == "help" || cmd == "?") printHelp();
  else if (cmd == "quit" || cmd == "exit") g_quit = true;
  else if (cmd == "run")   run::cmdRun();
  else if (cmd == "pause") run::cmdPause();
  else if (cmd == "reset") { run::load(s); ui::markLoaded(); }
  else if (cmd == "step")  run::cmdStep(line.empty() ? 1 : (uint32_t)std::atol(line.c_str()));
  else if (cmd == "speed") {
    std::string v = nextToken(line);
    if (v == "slow") run::setSpeed(run::Speed::Slow);
    else if (v == "medium") run::setSpeed(run::Speed::Medium);
    else if (v == "quick") run::setSpeed(run::Speed::Quick);
    else if (v == "full") run::setSpeed(run::Speed::Full);
    else { plat::logln("speed: slow|medium|quick|full"); return; }
    Store::setRunSpeed((int)run::speed());
    ui::repaint();
  }
  else if (cmd == "slots") listSlots();
  else if (cmd == "set") {
    std::size_t eq = line.find('=');
    if (eq == std::string::npos) { plat::logln("usage: set ID=VALUE"); return; }
    std::string id = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    int idx = prog::slotIndex(id.c_str());
    if (idx < 0) { plat::logf("unknown slot %s\n", id.c_str()); return; }
    if (!s.setSlotValue(idx, val))
      plat::logf("'%s' does not fit %s (%d cells)\n", val.c_str(), id.c_str(), prog::slot(idx).len);
    else { ui::markEdited(); plat::logf("%s = %s\n", id.c_str(), s.slotValue(idx).c_str()); }
  }
  else if (cmd == "cell") {
    int r = std::atoi(nextToken(line).c_str());
    int c = std::atoi(nextToken(line).c_str());
    std::string ch = nextToken(line);
    if (ch.size() != 1 || !s.setCell(r, c, ch[0])) plat::logln("usage: cell <row> <col> <char>");
    else { ui::markEdited(); plat::logf("(%d,%d) = %c\n", r, c, ch[0]); }
  }
  else if (cmd == "revert") {
    std::string id = nextToken(line);
    if (id.empty()) { s.revertAll(); plat::logln("reverted every edit"); }
    else {
      int idx = prog::slotIndex(id.c_str());
      if (idx < 0) { plat::logf("unknown slot %s\n", id.c_str()); return; }
      s.revertSlot(idx);
      plat::logf("%s reverted to %s\n", id.c_str(), prog::slotOriginal(idx).c_str());
    }
    ui::markEdited();
  }
  else if (cmd == "load") { run::load(s); ui::markLoaded(); plat::logln("loaded; machine reset"); }
  else if (cmd == "grid") plat::log(s.text().c_str());
  else if (cmd == "out")  plat::logln(run::output().c_str());
  else if (cmd == "report") {
    run::loadedGridInto(g_ranGrid);
    plat::log(sinks::report(run::output(), g_ranGrid).c_str());
  }
  else if (cmd == "save") {
    int n = std::atoi(nextToken(line).c_str());
    if (n < 1 || n > Store::kMaxPresets) { plat::logln("preset must be 1-8"); return; }
    std::string name = line.empty() ? "unnamed" : line;
    if (Store::savePreset(n - 1, name, s)) plat::logf("saved preset %d '%s'\n", n, name.c_str());
    else plat::logln("preset save failed (too many edits)");
  }
  else if (cmd == "preset") {
    int n = std::atoi(nextToken(line).c_str());
    if (n < 1 || n > Store::kMaxPresets) { plat::logln("preset must be 1-8"); return; }
    if (Store::loadPreset(n - 1, s)) { ui::markEdited(); plat::logf("loaded preset %d\n", n); }
    else plat::logln("preset is empty");
  }
  else if (cmd == "wifi") {
    std::string ssid = nextToken(line);
    Store::setWifi(ssid, line);
    plat::logf("stored ssid '%s'\n", ssid.c_str());
    ui::notifyUnlocked();
  }
  else if (cmd == "web") {
    std::string v = nextToken(line);
    if (!web::available()) { plat::logln("no radio in the emulator"); return; }
    if (v == "off") { web::stop(); plat::logln("web view stopped"); }
    else if (web::begin()) plat::logf("http://%s/\n", web::ipAddress().c_str());
    else plat::logln("could not join wifi (set it with: wifi <ssid> <pass>)");
  }
  else if (cmd == "sd") {
    std::string path;
    run::loadedGridInto(g_ranGrid);
    if (sinks::saveRunToSd(run::output(), g_ranGrid, path))
      plat::logf("wrote %s\n", path.c_str());
    else plat::logln("write failed");
  }
#if defined(SK_HOST)

  else if (cmd == "page") {
    // Emulator only: render a web page and print it. There is no radio here,
    // so this is the only way to see what the device would serve -- and it is
    // the same code that serves it.
    std::string path = nextToken(line);
    if (path.empty()) path = "/";
    std::string query;
    const std::size_t q = path.find('?');
    if (q != std::string::npos) { query = path.substr(q + 1); path = path.substr(0, q); }
    plat::log(web::renderPage(path, query, line, !line.empty()).c_str());
    plat::logln("");
  }
  else if (cmd == "shot") {
    // Emulator only: dump the panel framebuffer so the UI can be reviewed and
    // documented without a camera pointed at the board.
    std::string name = line.empty() ? std::string("screenshot.ppm") : line;
    std::vector<uint16_t> px((std::size_t)kScreenW * kScreenH);
    gfx.readRect(0, 0, kScreenW, kScreenH, px.data());
    std::ofstream f(name, std::ios::binary);
    if (!f) { plat::logln("could not write the file"); return; }
    f << "P6\n" << kScreenW << " " << kScreenH << "\n255\n";
    for (uint16_t raw : px) {
      // readRect() into a uint16_t buffer yields swap565 (the byte order SPI
      // panels want), not rgb565 -- unswap before splitting the channels.
      uint16_t p = (uint16_t)((raw >> 8) | (raw << 8));
      f.put((char)(((p >> 11) & 0x1F) * 255 / 31));
      f.put((char)(((p >> 5) & 0x3F) * 255 / 63));
      f.put((char)((p & 0x1F) * 255 / 31));
    }
    plat::logf("wrote %s\n", name.c_str());
  }
  else if (cmd == "fonts") { ui::fontSampler(); plat::logln("font sampler drawn"); }
  else if (cmd == "hold") {
    int hx = std::atoi(nextToken(line).c_str());
    int hy = std::atoi(nextToken(line).c_str());
    ui::injectHold(hx, hy);
    plat::logf("held (%d,%d)\n", hx, hy);
  }
  else if (cmd == "drag") {
    int dx = std::atoi(line.c_str());
    ui::injectDrag(dx);
    plat::logf("dragged %d px\n", dx);
  }
  else if (cmd == "progsave") {
    std::string name = nextToken(line);
    if (name.empty()) { plat::logln("usage: progsave [card:]<name>"); return; }
    plat::Where w = splitWhere(name);
    if (plat::progWrite(w, name, s.text()))
      plat::logf("saved %s.txt on the %s\n", name.c_str(), whereTag(w));
    else plat::logf("could not write to the %s\n", whereTag(w));
  }
  else if (cmd == "progload") {
    std::string name = nextToken(line);
    plat::Where w = splitWhere(name);
    std::string text;
    if (!plat::progRead(w, name, text)) { plat::logln("no such program"); return; }
    if (!ui::loadProgramTextPublic(text, name.c_str())) plat::logln("not a usable program");
    else {
      // The name goes in with the program, so one load does it.
      plat::logf("loaded %s (%d x %d), %d edited cell%s\n", name.c_str(),
                  s.rows(), s.cols(), s.modifiedCells(),
                  s.modifiedCells() == 1 ? "" : "s");
    }
  }
  else if (cmd == "webedit") {
    // Exactly what the web editor's POST does, so the path can be exercised
    // without a radio: apply this text as edits to the loaded program.
    std::string name = nextToken(line);
    plat::Where w = splitWhere(name);
    std::string text;
    if (!plat::progRead(w, name, text)) { plat::logln("no such program"); return; }
    if (!ui::applyProgramTextPublic(text)) plat::logln("not a usable program");
    else plat::logf("applied %s; %d edited cell%s\n", name.c_str(),
                    s.modifiedCells(), s.modifiedCells() == 1 ? "" : "s");
  }
  else if (cmd == "proglist") {
    for (plat::Where w : { plat::Where::Device, plat::Where::Card }) {
      std::vector<std::string> names;
      if (!plat::progList(w, names)) continue;
      plat::logf("%s:\n", whereTag(w));
      if (names.empty()) plat::logln("  (nothing)");
      for (const std::string& n : names) plat::logf("  %s\n", n.c_str());
    }
  }
  else if (cmd == "lock") {
    // The mirror of setting the credentials. Flashing new firmware does not
    // touch NVS, so a device unlocked in an earlier session stays unlocked;
    // this is how to put it back without hunting for the SYS tile.
    ui::lockDevice();
    plat::logln("locked: plain IRCIS interpreter");
  }
  else if (cmd == "settings") {
    int sc, sr; char sd;
    Store::startPoint(sc, sr, sd);
    const char* sp[] = { "slow", "medium", "quick", "full" };
    plat::logf("view=%s debug=%s speed=%s start=%d,%d%c%s\n",
               Store::runView() == 0 ? "output" : "none",
               Store::debugMode() ? "on" : "off",
               sp[Store::runSpeed() & 3],
               sc, sr, sd, Store::startEditable() ? "" : " (fixed)");
  }
  else if (cmd == "gridpaints") plat::logf("full repaints: %lu, grid repaints: %lu, band repaints: %lu, machine rebuilds: %u\n", ui::fullPaints(), ui::gridPaints(), ui::bandPaints(), (unsigned)run::buildVersion());
  else if (cmd == "dragv") {
    int dy = std::atoi(line.c_str());
    ui::injectDragV(dy);
    plat::logf("dragged %d px vertically\n", dy);
  }
  else if (cmd == "tap") {
    // Emulator only: synthesise a touch, so screens can be driven from a script.
    int x = std::atoi(nextToken(line).c_str());
    int y = std::atoi(nextToken(line).c_str());
    ui::injectTap(x, y);
    plat::logf("tapped (%d,%d)\n", x, y);
  }
#endif
  else plat::logf("unknown command '%s' -- try help\n", cmd.c_str());
}

} // namespace

bool quitRequested() { return g_quit || plat::powerOffRequested(); }

void setup() {
#if !defined(SK_HOST)
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
#endif
  setLed(false, false, false);

  Store::begin();

  // After Store::begin(), so the banner can tell which device this is.
  if (Store::unlocked()) {
    plat::logf("\n%s\n", pack::str(pack::kStrConsoleBanner));
    plat::logf("%d programs, %d editable parameters\n",
               prog::programCount(), prog::slotCount());
  }
  else {
    plat::logln("\npIRCIS -- an IRCIS interpreter");
    plat::logf("%d programs\n", prog::programCount() - 1);
  }

  gfx.init();
#if defined(SK_HOST)
  gfx.setRotation(0);           // the emulated panel is already 320x240
#else
  gfx.setRotation(TFT_ROTATION);
  gfx.setBrightness(200);
#endif
  gfx.fillScreen(0x0000);
  gfx.beginTouch();

  run::begin();
  ui::begin();
  printHelp();
}

void loop() {
  ui::tick();
  web::tick();

  std::string line;
  if (plat::readLine(line)) handleCommand(line);

  static bool ledState = false;
  run::Snapshot snap = run::snapshot();
  if (snap.running != ledState) { setLed(false, snap.running, false); ledState = snap.running; }

  // Auto-save on the rising edge of "finished", when SD logging is enabled.
  // One write per run, after the interpreter has stopped, so it never contends
  // with the touch controller for the shared bus mid-run.
  static bool wasFinished = false;
  if (snap.finished && !wasFinished) {
    wasFinished = true;
    if (Store::sdLoggingEnabled()) {
      std::string path;
      run::loadedGridInto(g_ranGrid);
    if (sinks::saveRunToSd(run::output(), g_ranGrid, path))
        plat::logf("[sd] wrote %s\n", path.c_str());
      else
        plat::logln("[sd] auto-save failed (no card?)");
    }
  }
  else if (!snap.finished) wasFinished = false;

  plat::delayMs(2);
}

} // namespace app
