// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#include <string>

// Serves the last run (parameters, grid diff and full output) over WiFi, so a
// long output can be copied off the device instead of read from a 240 px
// screen. Credentials are set from the console: wifi <ssid> <password>
// In the desktop emulator there is no radio, so begin() reports unavailable.
namespace web {
  // Page rendering, kept here rather than in the platform layer: HTML has
  // nothing to do with sockets, and this way the pages can be rendered and
  // checked on the host, where there is no web server at all.
  //
  // `path` is the request path, `query` the raw query string, `body` the form
  // body for a POST. The result is a complete HTML document.
  std::string renderPage(const std::string& path, const std::string& query,
                         const std::string& body, bool post);

  bool begin();
  void stop();
  void tick();
  bool running();
  bool available();
  std::string ipAddress();
}
