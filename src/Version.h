// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

// The release this firmware was cut from. Shown on ABOUT THIS DEVICE beside
// the build stamp, and used by tools/release.sh to name the artifacts, so a
// binary on the Releases page and a device in your hand can be compared
// without guessing. Bump it before cutting a release.
#define PIRCIS_VERSION "1.2.0"

// Which panel this binary drives. The 4" boards ship with either controller
// and the wrong build shows a blank screen or inverted colours, so the device
// says which one it was built for rather than leaving you to guess.
#if defined(PANEL_ST7796)
#define SK_PANEL_NAME "ST7796"
#elif defined(PANEL_ILI9488)
#define SK_PANEL_NAME "ILI9488"
#else
#define SK_PANEL_NAME "emulator"
#endif
