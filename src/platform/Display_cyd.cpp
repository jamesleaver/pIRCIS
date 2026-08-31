// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#if !defined(SK_HOST)

#include "Display.h"
#include "Store.h"
#include "Theme.h"
#include <math.h>

CydDisplay gfx;

CydDisplay::CydDisplay() {
  {
    auto cfg = bus_.config();
    cfg.spi_host   = SPI2_HOST;
    cfg.spi_mode   = 0;
    cfg.freq_write = 40000000;
    cfg.freq_read  = 16000000;
    cfg.spi_3wire  = false;
    cfg.use_lock   = true;
    cfg.dma_channel = SPI_DMA_CH_AUTO;
    cfg.pin_sclk   = TFT_SCLK;
    cfg.pin_mosi   = TFT_MOSI;
    cfg.pin_miso   = TFT_MISO;
    cfg.pin_dc     = TFT_DC;
    bus_.config(cfg);
    panel_.setBus(&bus_);
  }
  {
    auto cfg = panel_.config();
    cfg.pin_cs   = TFT_CS;
    cfg.pin_rst  = TFT_RST;
    cfg.pin_busy = -1;
    cfg.panel_width      = TFT_WIDTH;
    cfg.panel_height     = TFT_HEIGHT;
    cfg.memory_width     = TFT_WIDTH;
    cfg.memory_height    = TFT_HEIGHT;
    cfg.offset_rotation  = 0;
    cfg.readable         = true;
    cfg.invert           = false;
    cfg.rgb_order        = false;
    cfg.bus_shared       = true;    // the touch controller is on this bus too
    panel_.config(cfg);
  }
  {
    auto cfg = light_.config();
    cfg.pin_bl      = TFT_BL;
    cfg.invert      = false;
    cfg.freq        = 44100;
    cfg.pwm_channel = 7;
    light_.config(cfg);
    panel_.setLight(&light_);
  }
  {
    auto cfg = touch_.config();
    cfg.x_min = 300;  cfg.x_max = 3900;
    cfg.y_min = 200;  cfg.y_max = 3700;
    cfg.pin_int = TOUCH_IRQ;
    cfg.offset_rotation = 0;
    cfg.bus_shared = true;
    cfg.spi_host = SPI2_HOST;       // same bus as the panel
    cfg.freq     = 2500000;         // Freenove's SPI_TOUCH_FREQUENCY
    cfg.pin_sclk = TOUCH_SCLK;
    cfg.pin_mosi = TOUCH_MOSI;
    cfg.pin_miso = TOUCH_MISO;
    cfg.pin_cs   = TOUCH_CS;
    touch_.config(cfg);
    panel_.setTouch(&touch_);
  }
  setPanel(&panel_);
}

bool CydDisplay::beginTouch(bool force_recalibrate) {
  uint16_t params[8];
  if (!force_recalibrate && Store::loadTouchCalibration(params)) {
    setTouchCalibrate(params);
    return true;
  }

  fillScreen(theme::bg);
  setTextColor(theme::text, theme::bg);
  setTextDatum(textdatum_t::middle_center);
  setFont(&fonts::Font2);
  drawString("Touch calibration", width() / 2, height() / 2 - 20);
  drawString("tap each corner marker", width() / 2, height() / 2 + 4);
  calibrateTouch(params, theme::accent, theme::bg, 20);
  Store::saveTouchCalibration(params);
  return true;
}

// Three targets, well inside the panel where a fingertip actually sits -- the
// calibration's own points are the extreme corners, which are the hardest
// place to be accurate and where any bias gets amplified across the screen.
// Returns the worst miss in pixels.
int CydDisplay::checkTouch() {
  const int32_t pts[3][2] = {
    { width() / 6,     height() / 4 },
    { width() / 2,     height() * 3 / 4 },
    { width() * 5 / 6, height() / 4 },
  };
  int worst = 0;
  for (int i = 0; i < 3; ++i) {
    fillScreen(theme::bg);
    setTextColor(theme::text, theme::bg);
    setTextDatum(textdatum_t::middle_center);
    setFont(&fonts::Font2);
    drawString("Tap the centre of each ring", width() / 2, 24);
    char n[24];
    snprintf(n, sizeof(n), "%d of 3", i + 1);
    drawString(n, width() / 2, height() - 24);

    const int32_t px = pts[i][0], py = pts[i][1];
    drawCircle(px, py, 14, theme::accent);
    drawCircle(px, py, 2, theme::accent);

    // Wait for a clean press, then for it to lift, so one tap is one point.
    int32_t tx = 0, ty = 0;
    while (!getTouch(&tx, &ty)) delay(5);
    int32_t hx = tx, hy = ty;
    while (getTouch(&tx, &ty)) delay(5);

    const int dx = (int)(hx - px), dy = (int)(hy - py);
    int d = (int)(sqrtf((float)(dx * dx + dy * dy)) + 0.5f);
    if (d > worst) worst = d;
  }
  return worst;
}

#endif
