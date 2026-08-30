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

#endif
