// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#if defined(SK_HOST)

#include "Display.h"
#include "Store.h"
#include "Theme.h"

CydDisplay gfx;

CydDisplay::CydDisplay() {
  auto cfg = panel_.config();
  cfg.memory_width  = kScreenW;
  cfg.memory_height = kScreenH;
  cfg.panel_width   = kScreenW;
  cfg.panel_height  = kScreenH;
  cfg.offset_x = 0;
  cfg.offset_y = 0;
  panel_.config(cfg);
  // 2x so the 6x8 program text is comfortable on a retina display.
  panel_.setScaling(2, 2);
  panel_.setWindowTitle("pIRCIS");
  setPanel(&panel_);
}

void CydDisplay::setPanelSize(int w, int h) {
  auto cfg = panel_.config();
  cfg.memory_width  = w;
  cfg.memory_height = h;
  cfg.panel_width   = w;
  cfg.panel_height  = h;
  panel_.config(cfg);
}

bool CydDisplay::resizePanel(int w, int h) {
  const bool ok = panel_.resizeBuffers(w, h);
  setRotation(getRotation());          // the device's idea of its width follows the panel's
  setClipRect(0, 0, w, h);
  return ok;
}

// The mouse is the touch panel, so there is nothing to calibrate.
bool CydDisplay::beginTouch(bool) { return true; }

#endif
