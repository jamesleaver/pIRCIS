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

// Mirrors lgfx::Panel_sdl::main(), with one addition: that helper pumps SDL
// until every window is closed, so a `quit` from the console would end the
// user thread and leave the window up with nothing driving it. Here the
// console can end the process too.
static int userFunc(bool* running) {
  app::setup();
  while (*running) app::loop();
  return 0;
}

int main(int, char**) {
  if (lgfx::Panel_sdl::setup() != 0) return 1;

  bool running = true;
  SDL_Thread* thread = SDL_CreateThread((SDL_ThreadFunction)userFunc, "firmware", &running);

  while (lgfx::Panel_sdl::loop() == 0 && !app::quitRequested()) { }

  running = false;
  SDL_WaitThread(thread, nullptr);
  return lgfx::Panel_sdl::close();
}

#endif
#endif
