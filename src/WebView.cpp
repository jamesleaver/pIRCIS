// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#include "WebView.h"

#include <cstdlib>
#include <vector>

#include "Pack.h"
#include "Platform.h"
#include "RunTask.h"
#include "Sinks.h"
#include "Store.h"
#include "Ui.h"

namespace web {
namespace {
  bool        g_running = false;
  std::string g_ip;

  // -- pages ---------------------------------------------------------------
  //
  // White ground, red accents, grey rules: the colours IRCIS itself uses.
  // One stylesheet, inlined, because the device serves this to a browser that
  // has no way to fetch a second file from it while a run is going.
  const char* kCss =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<style>"
    ":root{--bg:#fff;--fg:#1b1b1b;--dim:#6e6e6e;--line:#d7d7d7;--panel:#f4f4f4;"
    "--accent:#cc2200}"
    "*{box-sizing:border-box}"
    "body{background:var(--bg);color:var(--fg);margin:0;padding:20px 18px 40px;"
    "font:14px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}"
    "h1{font-size:15px;letter-spacing:.14em;text-transform:uppercase;margin:0 0 2px;"
    "color:var(--accent)}"
    "h2{font-size:13px;letter-spacing:.1em;text-transform:uppercase;color:var(--dim);"
    "margin:26px 0 8px;font-weight:600}"
    "nav{display:flex;flex-wrap:wrap;gap:16px;margin:12px 0 0}"
    "nav a{color:var(--dim);text-decoration:none;border-bottom:2px solid transparent;"
    "padding-bottom:3px}"
    "nav a:hover{color:var(--fg)}"
    "nav a.on{color:var(--accent);border-bottom-color:var(--accent)}"
    "hr{border:0;border-top:1px solid var(--line);margin:14px 0 22px}"
    "a{color:var(--accent)}"
    "pre{white-space:pre;overflow-x:auto;background:var(--panel);border:1px solid var(--line);"
    "padding:12px;margin:0}"
    "table{border-collapse:collapse;width:100%;max-width:760px}"
    "td,th{border-bottom:1px solid var(--line);padding:7px 10px;text-align:left;"
    "vertical-align:middle;white-space:nowrap}"
    "th{color:var(--dim);font-weight:600;font-size:12px;letter-spacing:.08em;"
    "text-transform:uppercase}"
    "td.n{text-align:right;color:var(--dim);font-variant-numeric:tabular-nums}"
    "tr:hover td{background:var(--panel)}"
    "textarea{width:100%;max-width:760px;height:58vh;background:var(--panel);color:var(--fg);"
    "border:1px solid var(--line);padding:10px;white-space:pre;overflow-wrap:normal;"
    "overflow-x:auto;font:inherit}"
    "button{background:var(--accent);color:#fff;border:0;padding:8px 16px;font:inherit;"
    "cursor:pointer}"
    "button:hover{background:#a81c00}"
    "button.q{background:transparent;color:var(--accent);border:1px solid var(--line);"
    "padding:5px 12px;white-space:nowrap}"
    "button.q:hover{background:var(--panel)}"
    ".msg{background:var(--panel);border-left:3px solid var(--accent);padding:9px 12px;"
    "margin:0 0 18px;max-width:760px}"
    ".bad{color:var(--accent)}"
    ".none{color:var(--dim)}"
    "form.i{display:inline;margin:0}"
    "</style>";

  std::string esc(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
      if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else if (c == '&') out += "&amp;";
      else if (c == '"') out += "&quot;";
      else out += c;
    }
    return out;
  }

  struct NavItem { const char* path; const char* label; };

  std::string chrome(const std::string& title, const std::string& here) {
    std::string p = kCss;
    p += "<title>" + esc(title) + " - pIRCIS</title>";
    p += "<h1>" + esc(title) + "</h1><nav>";
    const NavItem nav[] = {
      { "/",         "run"      },
      { "/edit",     "edit"     },
      { "/programs", "programs" },
      { "/presets",  "presets"  },
      { "/outputs",  "outputs"  },
    };
    for (const NavItem& n : nav) {
      // Presets are the unlocked device's saved slots; while it is locked they
      // are not a thing the device has, so the tab is not offered either.
      if (!Store::unlocked() && std::string(n.path) == "/presets") continue;
      p += std::string("<a href='") + n.path + "'"
         + (here == n.path ? " class=on" : "") + ">" + n.label + "</a>";
    }
    p += "</nav><hr>";
    return p;
  }

  // One value out of a form-encoded or query string. Returns "" when absent.
  std::string field(const std::string& src, const std::string& key) {
    const std::string want = key + "=";
    std::size_t at = 0;
    while (at < src.size()) {
      std::size_t amp = src.find('&', at);
      if (amp == std::string::npos) amp = src.size();
      if (src.compare(at, want.size(), want) == 0) {
        std::string raw = src.substr(at + want.size(), amp - at - want.size());
        // Percent-decoding, and '+' for space as a form post writes it.
        std::string out;
        for (std::size_t i = 0; i < raw.size(); ++i) {
          if (raw[i] == '+') out += ' ';
          else if (raw[i] == '%' && i + 2 < raw.size()) {
            auto hex = [](char c) -> int {
              if (c >= '0' && c <= '9') return c - '0';
              if (c >= 'a' && c <= 'f') return c - 'a' + 10;
              if (c >= 'A' && c <= 'F') return c - 'A' + 10;
              return -1;
            };
            const int hi = hex(raw[i + 1]), lo = hex(raw[i + 2]);
            if (hi < 0 || lo < 0) { out += raw[i]; continue; }
            out += (char)(hi * 16 + lo);
            i += 2;
          }
          else out += raw[i];
        }
        return out;
      }
      at = amp + 1;
    }
    return std::string();
  }

  // A one-button form, so anything with a side effect is a POST rather than a
  // link something could follow by accident.
  std::string action(const std::string& path, const std::string& name,
                     const std::string& value, const std::string& label) {
    return "<form class=i method=post action='" + path + "'>"
           "<input type=hidden name='" + name + "' value='" + esc(value) + "'>"
           "<button class=q type=submit>" + esc(label) + "</button></form>";
  }

  std::string reportText() {
    static prog::Program ranGrid;      // 3 KB; must not live on the stack
    run::loadedGridInto(ranGrid);
    return sinks::report(run::output(), ranGrid);
  }

  std::string pageRun() {
    std::string p = chrome("Last run", "/");
    p += "<pre>" + esc(reportText()) + "</pre>";
    return p;
  }

  std::string pageEdit(const std::string& body, bool post) {
    std::string p = chrome("Edit program", "/edit");
    if (post) {
      // The body is already buffered by the time we are called, so the guard
      // is on what we copy out of it: a 32 x 96 program with newlines is under
      // 3.2 KB, and anything far past that is a mistake or a prank.
      const std::string raw = field(body, "prog");
      if (raw.size() > 8192) {
        p += "<p class='msg bad'>That is too large to be a program.</p>";
      }
      else {
        std::string clean;                       // browsers submit CRLF
        for (char c : raw) if (c != '\r') clean += c;
        const bool ok = ui::applyProgramTextPublic(clean);
        p += ok ? "<p class=msg>Loaded onto the device.</p>"
                : "<p class='msg bad'>Not a usable program: it must be at least "
                  "one row and fit 32 x 96.</p>";
      }
    }
    p += "<p class=none>" + esc(ui::editGrid().programName()) + "</p>";
    p += "<form method=post action='/edit'><textarea name=prog spellcheck=false>";
    p += esc(ui::editGrid().text());
    p += "</textarea><p><button type=submit>Load onto device</button></p></form>";
    return p;
  }

  // Programs live in two stores. The name alone is ambiguous, so every link
  // and form carries the store with it.
  plat::Where whereFrom(const std::string& v) {
    return v == "card" ? plat::Where::Card : plat::Where::Device;
  }
  const char* whereTag(plat::Where w) {
    return w == plat::Where::Device ? "device" : "card";
  }

  void listStore(std::string& p, plat::Where w, const char* heading) {
    std::vector<std::string> names;
    if (!plat::progList(w, names) || names.empty()) return;
    p += "<h2>" + std::string(heading) + "</h2>";
    p += "<table><tr><th>Program</th><th></th></tr>";
    for (const std::string& n : names)
      p += "<tr><td><a href='/program?where=" + std::string(whereTag(w))
             + "&name=" + esc(n) + "'>" + esc(n) + ".txt</a></td>"
           "<td>" + action("/programs", "load",
                           std::string(whereTag(w)) + ":" + n, "load") + "</td></tr>";
    p += "</table>";
  }

  std::string pagePrograms(const std::string& body, bool post) {
    std::string p = chrome("Saved programs", "/programs");
    if (post) {
      std::string sel = field(body, "load");
      const std::size_t colon = sel.find(':');
      plat::Where w = plat::Where::Device;
      if (colon != std::string::npos) { w = whereFrom(sel.substr(0, colon)); sel = sel.substr(colon + 1); }
      const std::string name = sel;
      std::string text;
      if (name.empty() || !plat::progRead(w, name, text))
        p += "<p class='msg bad'>Could not read that file.</p>";
      else if (!ui::loadProgramTextPublic(text))
        p += "<p class='msg bad'>That file is not a usable program.</p>";
      else {
        ui::editGrid().setProgramName(name);
        run::load(ui::editGrid());
        ui::markLoaded();
        p += "<p class=msg>Loaded " + esc(name) + " onto the device.</p>";
      }
    }
    const std::size_t before = p.size();
    listStore(p, plat::Where::Device, "On this device");
    if (plat::sdPresent()) listStore(p, plat::Where::Card, "On the SD card");
    if (p.size() == before) p += "<p class=none>Nothing saved yet.</p>";
    return p;
  }

  std::string pageProgram(const std::string& query) {
    const std::string name = field(query, "name");
    const plat::Where w = whereFrom(field(query, "where"));
    std::string p = chrome("Saved program", "/programs");
    std::string text;
    p += "<p class=none>" + esc(name) + ".txt on the " + whereTag(w) + "</p>";
    if (name.empty() || !plat::progRead(w, name, text) || text.empty())
      p += "<p class=none>Not found, or empty.</p>";
    else {
      p += "<pre>" + esc(text) + "</pre>";
      p += "<p>" + action("/programs", "load",
                          std::string(whereTag(w)) + ":" + name,
                          "load onto device") + "</p>";
    }
    return p;
  }

  std::string pagePresets(const std::string& body, bool post) {
    std::string p = chrome("Presets", "/presets");
    if (!Store::unlocked()) {
      p += "<p class=none>This device has no presets.</p>";
      return p;
    }
    if (post) {
      const int slot = std::atoi(field(body, "load").c_str());
      if (slot < 0 || slot >= Store::kMaxPresets || !Store::loadPreset(slot, ui::editGrid()))
        p += "<p class='msg bad'>That preset could not be loaded.</p>";
      else {
        run::load(ui::editGrid());
        ui::markLoaded();
        p += "<p class=msg>Preset " + std::to_string(slot + 1) + " loaded onto the device.</p>";
      }
    }
    bool any = false;
    std::string rows;
    for (int i = 0; i < Store::kMaxPresets; ++i) {
      Store::PresetInfo info = Store::presetInfo(i);
      if (!info.used) continue;
      any = true;
      rows += "<tr><td class=n>" + std::to_string(i + 1) + "</td>"
              "<td>" + esc(info.name) + "</td>"
              "<td class=n>" + std::to_string(info.changedCells) + " cells</td>"
              "<td>" + action("/presets", "load", std::to_string(i), "load") +
              "</td></tr>";
    }
    if (!any) { p += "<p class=none>Nothing saved yet.</p>"; return p; }
    p += "<table><tr><th>Slot</th><th>Name</th><th>Edits</th><th></th></tr>" + rows + "</table>";
    return p;
  }

  std::string pageOutputs() {
    std::string p = chrome("Saved outputs", "/outputs");
    std::vector<std::string> names;
    if (!plat::runList(names) || names.empty()) {
      p += "<p class=none>No card, or no runs saved to it yet.</p>";
      p += "<p class=none>OUT &gt; SAVE SD writes one, and SYS &gt; SD LOG writes "
           "every completed run.</p>";
      return p;
    }
    p += "<table><tr><th>File</th></tr>";
    for (const std::string& n : names)
      p += "<tr><td><a href='/output?name=" + esc(n) + "'>" + esc(n) + ".txt</a></td></tr>";
    p += "</table>";
    return p;
  }

  std::string pageOutput(const std::string& query) {
    const std::string name = field(query, "name");
    std::string p = chrome("Saved output", "/outputs");
    std::string text;
    p += "<p class=none>" + esc(name) + ".txt</p>";
    if (name.empty() || !plat::runRead(name, text) || text.empty())
      p += "<p class=none>Not found, or empty.</p>";
    else p += "<pre>" + esc(text) + "</pre>";
    return p;
  }

  std::string pageMissing() {
    std::string p = chrome("Not here", "");
    p += "<p class=none>No such page.</p>";
    return p;
  }
}

std::string renderPage(const std::string& path, const std::string& query,
                       const std::string& body, bool post) {
  if (path == "/")          return pageRun();
  if (path == "/edit")      return pageEdit(body, post);
  if (path == "/programs")  return pagePrograms(body, post);
  if (path == "/program")   return pageProgram(query);
  if (path == "/presets")   return pagePresets(body, post);
  if (path == "/outputs")   return pageOutputs();
  if (path == "/output")    return pageOutput(query);
  return pageMissing();
}

namespace {
  std::string hookPage(const std::string& path, const std::string& query,
                       const std::string& body, bool post) {
    return renderPage(path, query, body, post);
  }
}

bool begin() {
  if (!plat::webAvailable()) return false;
  if (g_running) return true;
  if (!plat::webBegin(Store::wifiSsid(), Store::wifiPass(), g_ip)) return false;

  plat::WebHooks hooks;
  hooks.page = hookPage;
  plat::webSetHooks(hooks);

  g_running = true;
  plat::logf("[web] http://%s/\n", g_ip.c_str());
  return true;
}

void stop() {
  if (!g_running) return;
  plat::webStop();
  g_running = false;
  g_ip.clear();
}

void tick() {
  if (!g_running) return;
  plat::webTick();
}

bool running() { return g_running; }
bool available() { return plat::webAvailable(); }
std::string ipAddress() { return g_ip; }
}
