// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

// ---------------------------------------------------------------------------
// Freenove FNK0114S -- 4.0" 320x480 ST7796 TN touch panel (board E32R40T),
// driven landscape at 480x320.
//
// These pins are not guessed. They come from Freenove's own TFT_eSPI setup
// file FNK0114S_4.0_320x480_ST7796.h and their SD example sketch:
//
//   ST7796_DRIVER, 320x480, USE_HSPI_PORT, SPI 80 MHz
//   MISO 12  MOSI 13  SCLK 14  CS 15  DC 2  RST -1  BL 27 (active HIGH)
//   TOUCH_CS 33 -- XPT2046 sharing the panel's bus, clocked at 2.5 MHz
//   SD on VSPI: SCK 18  MISO 19  MOSI 23  CS 5
//
// If the screen stays dark, check TFT_BL. If the colours are inverted or the
// image is offset, the panel is probably an ILI9488 instead -- build the
// ili9488 environment.
// ---------------------------------------------------------------------------

// Display: ST7796 on SPI2 / HSPI.
#define TFT_SCLK   14
#define TFT_MOSI   13
#define TFT_MISO   12
#define TFT_DC      2
#define TFT_CS     15
#define TFT_RST    -1
#define TFT_BL     27
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
#define TFT_ROTATION 1

// Resistive touch: XPT2046, sharing the display bus. Only the chip select is
// its own, and it must be clocked far slower than the panel.
#define TOUCH_SCLK TFT_SCLK
#define TOUCH_MOSI TFT_MOSI
#define TOUCH_MISO TFT_MISO
#define TOUCH_CS   33
#define TOUCH_IRQ  -1

// microSD slot, on its own VSPI host -- no contention with the panel or the
// touch controller, so SD access needs no bus juggling.
#define SD_SCLK    18
#define SD_MOSI    23
#define SD_MISO    19
#define SD_CS       5

// On-board RGB LED (active LOW) -- used as a run indicator.
#define LED_R       4
#define LED_G      16
#define LED_B      17

// Screen geometry, landscape.
static constexpr int kScreenW = 480;
static constexpr int kScreenH = 320;

static constexpr int kHeaderH = 22;
static constexpr int kTabH    = 28;
static constexpr int kTabY    = kScreenH - kTabH;
static constexpr int kBodyY   = kHeaderH;
static constexpr int kBodyH   = kTabY - kHeaderH;

// Two ways to look at a program too wide to read whole.
//
// WIDE shows as many columns as fit in one unbroken block, so a runner
// crossing the middle does not jump between blocks. The edge bars shift the
// window so a wider program's remaining columns can still be reached.
// Scrolling is done with bars along the edges of the program. A bar is drawn
// over the outermost row or column, and only when there is something that way
// to reach -- so the row it covers is the one you are scrolling away from, and
// a program with nowhere to go shows every row it has.
static constexpr int kEdgeBar   = 16;
static constexpr int kWideCellW = 6;     // built-in font, its natural 1 px gap
static constexpr int kWideCellH = 15;
static constexpr int kWideCols  = kScreenW / kWideCellW;                    // 80
// Level with the editor's grid, so the program does not shift by a pixel when
// you move between the two pages.
static constexpr int kWideY     = kHeaderH + 4;

// ZOOM gives up fitting the width and makes the text big instead: a quarter of
// the columns at a time, panned by the edge bars or by following a runner. This
// is the view for reading the program carefully: FreeMono12pt (regular, not
// bold) in a cell wide enough to tap.
// The zoomed cell is the same on RUN and on EDIT. It is sized to be tapped,
// which the editor needs, and using one size means a program does not change
// size when you move between the two pages.
static constexpr int kZoomCellW = 24;
static constexpr int kZoomCellH = 26;
static constexpr int kZoomCols  = kScreenW / kZoomCellW;                   // 34
