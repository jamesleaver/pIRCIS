// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

// Desktop emulator entry point.
//
// Runs the real firmware -- the same interpreter, the same UI code, the same
// console -- against an SDL2 window standing in for the 320x240 panel. The
// mouse is the touch screen; stdin is the serial port.
#if defined(SK_HOST)

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#if defined(SDL_h_)

#include "App.h"
#include "Platform.h"
#include "Config.h"
#include "Display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if TARGET_OS_IPHONE
#include <string>
// On a phone the real main is SDL's: it starts the UIKit application and
// calls this once that is up. Naming it here does not depend on the header
// rename, which the panel header defeats by including SDL first.
#define pircis_main SDL_main
extern "C" int SDL_main(int argc, char** argv);
#else
#define pircis_main main
#endif

// Mirrors lgfx::Panel_sdl::main(), with one addition: that helper pumps SDL
// until every window is closed, so a `quit` from the console would end the
// user thread and leave the window up with nothing driving it. Here the
// console can end the process too.
// The screen is another size. Ask the program to park; the loop below keeps
// turning meanwhile -- the program's drawing waits on this loop to show
// each frame, so this loop must never wait on the program -- and once it
// has parked the display is rebuilt here, on the thread that owns the
// window, and the program released.
#if TARGET_OS_IPHONE
static plat::ScreenArea g_nextArea, g_area;
// A tablet's screen holds far more than a phone's, and its points are larger
// and further from the eye, so the picture is drawn at twice the size: the
// panel is half the screen in points and every panel pixel is a 2x2 block.
// The board's own layout then fits it the way it fits the board, twice over.
// An iPad mini, 744 points across, is on the phone's side of the line: its
// points are a phone's size, and it is better as a large phone than as a
// small tablet drawn twice over.
static plat::ScreenArea shrink(plat::ScreenArea a) {
  const int k = std::min(a.w, a.h) >= 780 ? 2 : 1;
  a.fullW /= k; a.fullH /= k; a.x /= k; a.y /= k; a.w /= k; a.h /= k;
  return a;
}
#endif
static bool resizeScreen(int w, int h) {
  if (w == screen::w && h == screen::h) return false;
  return app::requestResize(w, h);
}
static void finishResizeIfParked() {
  if (!app::resizeParked()) return;
  int w, h;
  app::resizeSize(w, h);
  screen::w = w; screen::h = h;
  gfx.resizePanel(w, h);
#if TARGET_OS_IPHONE
  g_area = g_nextArea;
  gfx.sdl().setFrame(g_nextArea.fullW, g_nextArea.fullH, g_nextArea.x, g_nextArea.y);
#endif
  gfx.sdl().resizeTexture();
#if !TARGET_OS_IPHONE
  if (plat::deviceMode()) gfx.sdl().setFrame(w, h, 0, 0);
  else                    gfx.sdl().fit(gfx.sdl().scale());
#endif
  gfx.sdl().keepFilled();
  plat::nativeKeysRelayout();
  app::clearResize();
}

#if !TARGET_OS_IPHONE
// A screen this program is the whole of. The picture is drawn at a whole
// number of pixels per panel pixel -- the largest that still leaves the
// board's 480x320 or more -- so a small display gets the board's layout
// and a large one gets it bigger, and nothing is blurred by a fraction.
static int pictureScale(int w, int h) {
  int k = std::min(w / 480, h / 320);
  return k < 1 ? 1 : k;
}
static int g_displayW = 0, g_displayH = 0;   // --display WxH: a window standing in for a screen
static int g_fixedK = 0;                     // a picture scale chosen by hand, or 0 for the fit
#endif

static int userFunc(bool* running) {
  app::setup();
  while (*running) app::loop();
  return 0;
}

static void usage() {
  std::puts(
    "pircis [options]\n"
    "  --kiosk        the program is the screen: full screen, whole-pixel scale,\n"
    "                 the cursor only when there is a mouse\n"
    "  --display WxH  as --kiosk, in a window of that size (to try a screen out)\n"
    "  --scale N      window scale (1, 1.5, 2 ...); the default is 2. In kiosk\n"
    "                 mode, a fixed picture scale instead of the fit\n"
    "  --size WxH     screen size in pixels; the default is the board's 480x320\n"
    "  --ui N         control size, percent of the board's; a phone uses 125\n"
    "  --data DIR     keep settings, programs and saved runs in DIR\n"
    "  --cursor       keep the mouse cursor in kiosk mode\n"
    "  --help\n"
    "\n"
    "The console reads commands from standard input; `help` lists them.\n");
}

int pircis_main(int argc, char** argv) {
  bool kiosk = false, cursor = false;
  float scale = 0;
  const char* data = nullptr;
#if TARGET_OS_IPHONE
  // A phone: no options, the screen is the window, and everything the
  // program keeps lives in the app's Documents folder, which the Files app
  // can show.
  static std::string docs = std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/Documents";
  data = docs.c_str();
  (void)argc; (void)argv;
#else
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--kiosk"))        kiosk = true;
    else if (!std::strcmp(argv[i], "--display") && i + 1 < argc) {
      if (std::sscanf(argv[++i], "%dx%d", &g_displayW, &g_displayH) != 2 || g_displayW < 320 || g_displayH < 240) {
        std::fprintf(stderr, "--display wants WxH, at least 320x240\n"); return 2;
      }
      kiosk = true;
    }
    else if (!std::strcmp(argv[i], "--cursor"))  cursor = true;
    else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = (float)std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--data")  && i + 1 < argc) data = argv[++i];
    else if (!std::strcmp(argv[i], "--ui")    && i + 1 < argc) {
      const int n = std::atoi(argv[++i]);
      if (n < 100 || n > 200) { std::fprintf(stderr, "--ui wants 100..200\n"); return 2; }
      screen::ui = n;
    }
    else if (!std::strcmp(argv[i], "--size")  && i + 1 < argc) {
      int w = 0, h = 0;
      if (std::sscanf(argv[++i], "%dx%d", &w, &h) != 2 || w < 320 || h < 240 || w > 4096 || h > 4096) {
        std::fprintf(stderr, "--size wants WxH, at least 320x240\n"); return 2;
      }
      screen::w = w; screen::h = h;
    }
    else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) { usage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", argv[i]); usage(); return 2; }
  }
#endif
  // Everything the program keeps -- settings, its own programs, saved runs --
  // is relative to the working directory. On a Pi that is started by a
  // service, that is wherever the service put it.
  if (data && chdir(data) != 0) { std::perror(data); return 2; }

  // LovyanGFX's SDL panel takes r and l to rotate the window and 1-6 to scale
  // it, with no modifier at all, so typing an r into a program spun the screen.
  // Putting them behind the left alt key gives the characters back and keeps
  // the shortcuts for anyone who wants them.
  lgfx::Panel_sdl::setShortcutKeymod(KMOD_LALT);
#if TARGET_OS_IPHONE
  // The picture is scaled up several times to fill the screen. Smoothing
  // that turns a pixel font to fog; whole pixels keep it sharp.
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  // Every way up the phone can be held; the pages lay themselves out to
  // whichever shape that gives.
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait LandscapeLeft LandscapeRight");
#endif
#if !TARGET_OS_IPHONE
  if (kiosk) {
    // The screen's size decides the panel's: the window opens at the
    // screen's own size and the picture fills it in whole pixels.
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 2; }
    int W = g_displayW, H = g_displayH;
    if (!W) {
      SDL_DisplayMode m;
      if (SDL_GetDesktopDisplayMode(0, &m) == 0) { W = m.w; H = m.h; } else { W = 960; H = 640; }
    }
    if (scale >= 1) g_fixedK = (int)scale;
    const int k = g_fixedK ? g_fixedK : pictureScale(W, H);
    screen::w = W / k; screen::h = H / k;
    scale = (float)k;
    // The frame is the picture: the panel's own idea of its frame is set
    // before the window exists, and a fit taken from it lands on the
    // window's own size rather than on the board's.
    gfx.sdl().setFrame(screen::w, screen::h, 0, 0);
    bool keyboard, mouse;
    plat::probeInput(keyboard, mouse);
    const bool touch = SDL_GetNumTouchDevices() > 0;
    plat::setDeviceMode(true, touch, keyboard, mouse);
    // A finger on glass wants the phone's larger controls, where there is
    // the room: on a panel no taller than the board's they would not fit.
    if (screen::ui == 100 && touch && screen::h >= 400) screen::ui = 125;
    std::fprintf(stderr, "screen %dx%d, picture x%d, panel %dx%d; %s%s%s\n", W, H, k, screen::w, screen::h,
                 touch ? "touch " : "", mouse ? "mouse " : "", keyboard ? "keyboard" : "no keyboard");
    // The panel's own keys resize its window; the window is the screen here.
    lgfx::Panel_sdl::setShortcutKeymod((SDL_Keymod)0xFFFF);
  }
#endif
  if (scale >= 1 && scale < 8) gfx.sdl().setScaling((uint8_t)scale, (uint8_t)scale);
  if (lgfx::Panel_sdl::setup() != 0) return 1;
#if TARGET_OS_IPHONE
  // The phone's screen, in points, the long way round, less what the phone
  // keeps for itself: the panel is that size, so at the screen's own scale
  // the picture fills it in whole pixels.
  {
    plat::ScreenArea a;
    if (plat::screenArea(a)) {
      const bool tablet = std::min(a.w, a.h) >= 780;
      a = shrink(a);
      g_area = a; screen::w = a.w; screen::h = a.h; gfx.sdl().setFrame(a.fullW, a.fullH, a.x, a.y);
      // A finger on glass: a quarter up on a phone. A tablet is drawn at
      // twice the size already, so its controls are the board's own.
      screen::ui = tablet ? 100 : 125;
    }

  }
#endif
  gfx.setPanelSize(screen::w, screen::h);
#if TARGET_OS_IPHONE
  // The window appears on the first pass of the loop below, and the fit
  // has to wait for it.
  bool fitted = false, wasInactive = false;
  int lastW = 0, lastH = 0;
  Uint32 lastCheck = 0;
  Uint32 presentUntil = 0;    // keep presenting until this tick, after a return or a new shape
  SDL_ShowCursor(SDL_DISABLE);
  // The system ends the app by calling exit() from one of its own threads
  // while the program thread is still running. Its statics must not be torn
  // down under it: leave at once instead, with nothing left unsaved, since
  // settings and programs are written as they change.
  std::atexit([] { _Exit(0); });
#endif

#if !TARGET_OS_IPHONE
  // In kiosk mode the window appears on the first pass of the loop below,
  // and is made the screen there.
  bool placed = !kiosk;
  if (!kiosk && scale >= 1 && scale != std::floor(scale)) gfx.sdl().fit(scale);
#endif

  bool running = true;
  SDL_Thread* thread = SDL_CreateThread((SDL_ThreadFunction)userFunc, "firmware", &running);

  while (!app::quitRequested()) {
#if TARGET_OS_IPHONE
    // In the background the renderer is off limits and the window's shape
    // is not to be trusted: a texture made then is not made, and a fit taken
    // then is wrong. Keep the events flowing, so the return is noticed, and
    // do nothing else until it comes; the program waits in its next display.
    if (fitted && !plat::isActive()) { SDL_PumpEvents(); SDL_Delay(50); wasInactive = true; continue; }
#endif
    if (lgfx::Panel_sdl::loop() != 0) break;
    finishResizeIfParked();
#if !TARGET_OS_IPHONE
    {
      int w, h;
      if (app::takeSizeRequest(w, h)) resizeScreen(w, h);
      if (plat::deviceMode()) {
        if (plat::quitAsked()) break;
        SDL_Window* win = gfx.sdl().window();
        if (win && !placed) {
          // The window is the screen, or in a trial a window the size of
          // one, and the picture is fitted to it in whole pixels. The panel
          // counts its scale in the screen's own pixels, so on a dense
          // screen the window is set to its size in points afterwards.
          placed = true;
          SDL_SetWindowBordered(win, SDL_FALSE);
          if (g_displayW) SDL_SetWindowSize(win, g_displayW, g_displayH);
          else            SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
          gfx.sdl().keepFilled();
          if (g_displayW) SDL_SetWindowSize(win, g_displayW, g_displayH);
          SDL_RaiseWindow(win);
          if (!cursor && plat::hideCursor()) SDL_ShowCursor(SDL_DISABLE);
        }
        else if (win && !app::resizeBusy()) {
          // The screen changed shape, or Alt with a digit asked for another
          // picture scale: the panel is made the size that fits again.
          const int ask = plat::takeScaleRequest();
          if (ask) g_fixedK = ask;
          int ww, wh;
          SDL_GetWindowSize(win, &ww, &wh);
          const int k = g_fixedK ? g_fixedK : pictureScale(ww, wh);
          if (ww / k >= 320 && wh / k >= 240 && (ww / k != screen::w || wh / k != screen::h)) {
            std::fprintf(stderr, "screen %dx%d: picture x%d, panel %dx%d\n", ww, wh, k, ww / k, wh / k);
            resizeScreen(ww / k, wh / k);
          }
        }
        gfx.sdl().keepFilled();
      }
    }
#endif
#if TARGET_OS_IPHONE
    if (!fitted && gfx.sdl().window()) {
      // Borderless is what tells SDL to hide the status bar on a phone.
      SDL_SetWindowBordered(gfx.sdl().window(), SDL_FALSE);
      gfx.sdl().fillWindow(); fitted = true;
    }
    else if (fitted) {
      // The output changed shape, the phone was turned, or the app came
      // back to the front: ask for the area again. A new size is a resize;
      // the same size somewhere else -- the cut-out now on the other side
      // -- only moves the picture.
      int ow, oh;
      const bool shape = gfx.sdl().outputSize(ow, oh) && (ow != lastW || oh != lastH);
      const bool due = (SDL_GetTicks() - lastCheck) > 250;    // and every so often regardless
      // Back from the background: whatever happened while away -- a turn of
      // the phone, most likely -- the frame and the fit are taken afresh.
      const bool back = wasInactive; wasInactive = false;
      if (shape || back) presentUntil = SDL_GetTicks() + 1500;
      if ((shape || due || back || plat::screenChanged()) && !app::resizeBusy()) {
        lastW = ow; lastH = oh; lastCheck = SDL_GetTicks();
        plat::ScreenArea a;
        if (plat::screenArea(a)) {
          a = shrink(a);
          if (a.w != screen::w || a.h != screen::h) { g_nextArea = a; resizeScreen(a.w, a.h); }
          else if (back || a.x != g_area.x || a.y != g_area.y || a.fullW != g_area.fullW || a.fullH != g_area.fullH) {
            g_area = a;
            gfx.sdl().setFrame(a.fullW, a.fullH, a.x, a.y);
            gfx.sdl().fillWindow();
          }
        }
      }
      gfx.sdl().keepFilled();
      if ((Sint32)(presentUntil - SDL_GetTicks()) > 0) gfx.sdl().present();
    }
#endif
  }

  running = false;
  SDL_WaitThread(thread, nullptr);
  const int rc = lgfx::Panel_sdl::close();
  // The console reader is a detached thread sitting in a blocking read. Coming
  // out of main runs the static destructors underneath it, and the next thing
  // it touches is a mutex that no longer exists, which aborts on the way out.
  // Nothing is left to write by this point, so leave without them.
  // Only the output streams. Flushing every stream would lock stdin too,
  // and on Windows the console reader thread holds that lock while it waits
  // for a line -- closing the window then hung the process.
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(rc);
}

#endif
#endif
