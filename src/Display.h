// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
// and is not covered by this notice.

#pragma once

#ifndef LGFX_USE_V1        // also set in platformio.ini; keep both builds happy
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>

#include "Config.h"

#if defined(SK_HOST)

// Desktop emulator: the same LovyanGFX drawing surface, rendered into an SDL2
// window. The UI code above does not know the difference.
// The SDL panel keeps its window and its scaling to itself. Fitting the
// window to a screen -- which is what running on a Raspberry Pi with a
// display attached wants -- needs both, so this is the panel with the two
// doors opened.
class PanelSdl : public lgfx::Panel_sdl {
public:
  SDL_Window* window() { return monitor.window; }
  float scale() { return monitor.scaling_x; }
  // Scale the panel by `s` (halves allowed) and resize the window to match.
  void fit(float s) { _update_scaling(&monitor, s, s); }
  // The other way round: a window whose size is not ours to choose -- a
  // phone's screen -- gets the largest half-step scale that fits inside it.
  // The picture sits inside a frame: on the desktop the frame is the
  // picture, on a phone it is the whole screen with the picture placed
  // clear of what the phone reserves. The frame is what is fitted to the
  // window, so on a phone it fills it exactly.
  void setFrame(int fw, int fh, int ix, int iy) {
    frameW_ = fw; frameH_ = fh; innerX_ = ix; innerY_ = iy;
    monitor.frame_width = fw; monitor.frame_height = fh;
    monitor.frame_inner_x = ix; monitor.frame_inner_y = iy;
  }
  int frameW() { return frameW_ ? frameW_ : _cfg.panel_width; }
  int frameH() { return frameH_ ? frameH_ : _cfg.panel_height; }
  float fitScale() {
    int rw, rh;
    SDL_GetRendererOutputSize(monitor.renderer, &rw, &rh);
    float s = rw / (float)frameW();
    if (rh / (float)frameH() < s) s = rh / (float)frameH();
    s = (float)((int)(s * 2)) / 2;
    return s < 1 ? 1 : s;
  }
  // Where the picture's top-left is on the output, in pixels: the frame is
  // centred in the window and the picture is at its inner offset.
  void pictureOrigin(float& ox, float& oy) {
    int rw, rh;
    SDL_GetRendererOutputSize(monitor.renderer, &rw, &rh);
    ox = rw / 2.0f - frameW() * monitor.scaling_x / 2 + innerX_ * monitor.scaling_x;
    oy = rh / 2.0f - frameH() * monitor.scaling_y / 2 + innerY_ * monitor.scaling_y;
  }
  void fillWindow() { const float s = fitScale(); _update_scaling(&monitor, s, s); }
  // Present the picture again on the next turn even though nothing in it
  // changed: after a return to the front or a change of shape the frame
  // drawn during the transition can land in a drawable of the old size, and
  // with nothing else to draw the system keeps showing its stretched
  // snapshot of the old picture until something moves.
  void present() { sdl_invalidate(); }
  // The panel's own answer to a window resize scales each axis on its own
  // and stretches the picture. Called every turn of the loop, this puts the
  // uniform fit back the moment that happens.
  void keepFilled() {
    if (!monitor.window || !monitor.renderer) return;
    const float s = fitScale();
    if (monitor.scaling_x != s || monitor.scaling_y != s) _update_scaling(&monitor, s, s);
  }
  // The panel maps the pointer as if the picture filled the window, which
  // holds only while the window is exactly the picture's size. Map through
  // the rectangle the picture is actually drawn in instead, so a window
  // that has been resized, or one that is a whole screen, still lands taps
  // where the finger is.
  // A press is polled, not delivered, and the panel's own idea of the
  // pointer is only brought up to date when its render loop pumps events,
  // which can be long after the finger arrived. Polling that copy lost
  // quick taps altogether and reported a slow one twice. So the finger is
  // followed from the events themselves, the moment they are queued, and
  // every press is held until a poll has reported it once.
  uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t) override {
    if (!monitor.window || !monitor.renderer) return 0;
    watchPresses();
    bool down = down_;
    int mx = posX_, my = posY_;
    if (pending_) { mx = pendX_; my = pendY_; down = true; pending_ = false; }
    int w, h, rw, rh;
    SDL_GetWindowSize(monitor.window, &w, &h);
    SDL_GetRendererOutputSize(monitor.renderer, &rw, &rh);
    const float px = mx * (float)rw / (w ? w : 1), py = my * (float)rh / (h ? h : 1);
    float dx, dy;
    pictureOrigin(dx, dy);
    tp->x = (int)((px - dx) / monitor.scaling_x);
    tp->y = (int)((py - dy) / monitor.scaling_y);
    tp->size = down ? 1 : 0;
    tp->id = 0;
    return down;
  }
private:
  void watchPresses() {
    if (watching_) return;
    watching_ = true;
    // The panel's own answer to a window changing size scales each axis
    // separately and draws that before this class can put the fit right,
    // so a phone being turned showed a stretched picture for a frame or
    // two. The event is taken here, before it is queued: the picture is
    // given its uniform fit at once and the panel never sees the event.
    SDL_SetEventFilter([](void* self, SDL_Event* e) -> int {
      auto* p = static_cast<PanelSdl*>(self);
      if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_RESIZED &&
          p->monitor.window && e->window.windowID == SDL_GetWindowID(p->monitor.window)) {
        const float s = p->fitScale();
        p->monitor.scaling_x = s; p->monitor.scaling_y = s;
        p->sdl_invalidate();
        return 0;
      }
      return 1;
    }, this);
    SDL_AddEventWatch([](void* self, SDL_Event* e) -> int {
      auto* p = static_cast<PanelSdl*>(self);
      if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        p->posX_ = p->pendX_ = e->button.x; p->posY_ = p->pendY_ = e->button.y;
        p->pending_ = true; p->down_ = true;
      }
      else if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        p->posX_ = e->button.x; p->posY_ = e->button.y;
        p->down_ = false;
      }
      else if (e->type == SDL_MOUSEMOTION && p->down_) {
        p->posX_ = e->motion.x; p->posY_ = e->motion.y;
      }
      // Two fingers: SDL reports how far apart they have moved since the
      // last report, as a fraction of the screen, and where their middle is.
      else if (e->type == SDL_MULTIGESTURE && e->mgesture.numFingers >= 2) {
        p->pinch_ += e->mgesture.dDist;
        p->pinchNx_ = e->mgesture.x; p->pinchNy_ = e->mgesture.y;
      }
      // A wheel: notches, with the pointer where it was at the time. A
      // wheel flipped to scroll "naturally" reports the opposite sign, and
      // SDL says which; either way up means towards the top of the grid.
      else if (e->type == SDL_MOUSEWHEEL) {
        const int flip = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
        p->wheelY_ += e->wheel.y * flip; p->wheelX_ += e->wheel.x * flip;
        int mx, my; SDL_GetMouseState(&mx, &my);
        p->wheelPx_ = mx; p->wheelPy_ = my;
      }
      return 1;
    }, this);
  }
  int frameW_ = 0, frameH_ = 0, innerX_ = 0, innerY_ = 0;
  bool watching_ = false;
  volatile bool pending_ = false, down_ = false;
  volatile int pendX_ = 0, pendY_ = 0, posX_ = 0, posY_ = 0;
  volatile float pinch_ = 0, pinchNx_ = 0, pinchNy_ = 0;
  volatile int wheelY_ = 0, wheelX_ = 0, wheelPx_ = 0, wheelPy_ = 0;
public:
  // The output's size in pixels, which changes when the phone is turned.
  bool outputSize(int& w, int& h) {
    if (!monitor.window || !monitor.renderer) return false;
    SDL_GetRendererOutputSize(monitor.renderer, &w, &h);
    return true;
  }
  // A new panel size, in two halves. The frame buffer is the program's and
  // is rebuilt on its thread while the render loop is held; the texture is
  // the renderer's and is rebuilt on the main thread afterwards.
  bool resizeBuffers(int w, int h) {
    deinitFrameBuffer();
    auto cfg = config();
    cfg.panel_width = cfg.memory_width = w;
    cfg.panel_height = cfg.memory_height = h;
    config(cfg);
    initFrameBuffer((size_t)w * 4, (size_t)h);
    return Panel_FrameBufferBase::init(false);
  }
  void resizeTexture() {
    if (!monitor.renderer) return;
    if (monitor.texture) SDL_DestroyTexture(monitor.texture);
    monitor.texture = SDL_CreateTexture(monitor.renderer, SDL_PIXELFORMAT_RGB24,
                                        SDL_TEXTUREACCESS_STREAMING, _cfg.panel_width, _cfg.panel_height);
    SDL_SetTextureBlendMode(monitor.texture, SDL_BLENDMODE_NONE);
    if (!frameW_) { monitor.frame_width = _cfg.panel_width; monitor.frame_height = _cfg.panel_height; }
    sdl_invalidate();
  }
  // Where a panel point is on the output, in the renderer's pixels, and
  // how wide that output is. The window size SDL reports is not to be
  // trusted on a phone, which keeps the size it was asked for rather than
  // the one it has; the pixels are real.
  bool panelToPixels(float px, float py, float& ox, float& oy, int& outW) {
    if (!monitor.window || !monitor.renderer) return false;
    int rw, rh;
    SDL_GetRendererOutputSize(monitor.renderer, &rw, &rh);
    pictureOrigin(ox, oy);
    ox += px * monitor.scaling_x;
    oy += py * monitor.scaling_y;
    outW = rw;
    return true;
  }
  // The wheel's turns since last asked, with the pointer mapped to panel
  // coordinates the same way a touch is.
  bool takeWheel(int& dy, int& dx, int& x, int& y) {
    if (!wheelY_ && !wheelX_) return false;
    dy = wheelY_; dx = wheelX_; wheelY_ = wheelX_ = 0;
    if (!monitor.window || !monitor.renderer) return false;
    // The pointer comes in window units; the picture is placed in output
    // pixels, which on a high-density screen are more.
    int ww, wh, rw, rh;
    SDL_GetWindowSize(monitor.window, &ww, &wh);
    SDL_GetRendererOutputSize(monitor.renderer, &rw, &rh);
    const float px = wheelPx_ * (ww ? (float)rw / ww : 1), py = wheelPy_ * (wh ? (float)rh / wh : 1);
    float ox, oy;
    pictureOrigin(ox, oy);
    x = (int)((px - ox) / monitor.scaling_x);
    y = (int)((py - oy) / monitor.scaling_y);
    return true;
  }
  // A pinch that has grown past a tenth of the screen, once, with its middle
  // mapped to panel coordinates the same way a touch is.
  bool takePinch(int& dir, int& x, int& y) {
    constexpr float kEnough = 0.10f;
    if (pinch_ > -kEnough && pinch_ < kEnough) return false;
    dir = pinch_ > 0 ? 1 : -1;
    pinch_ = 0;
    if (!monitor.window || !monitor.renderer) return false;
    int rw, rh;
    SDL_GetRendererOutputSize(monitor.renderer, &rw, &rh);
    const float px = pinchNx_ * rw, py = pinchNy_ * rh;
    float dx, dy;
    pictureOrigin(dx, dy);
    x = (int)((px - dx) / monitor.scaling_x);
    y = (int)((py - dy) / monitor.scaling_y);
    return true;
  }
};

class CydDisplay : public lgfx::LGFX_Device {
public:
  CydDisplay();
  bool beginTouch(bool force_recalibrate = false);
  int  checkTouch() { return 0; }
  void reinitTouch() { }
  PanelSdl& sdl() { return panel_; }
  // The panel takes the screen's size at construction, before anyone could
  // have said otherwise; a platform with a different screen says so here,
  // before init().
  void setPanelSize(int w, int h);
  // After init: the program's half of a resize (see PanelSdl::resizeBuffers).
  bool resizePanel(int w, int h);
private:
  PanelSdl panel_;
};

#else

// LovyanGFX device for the Cheap Yellow Display: panel on SPI2, resistive
// touch on SPI3.
class CydDisplay : public lgfx::LGFX_Device {
public:
  CydDisplay();
  // Loads the stored touch calibration, or runs the four-corner routine if
  // there is none.
  bool beginTouch(bool force_recalibrate = false);

  // Tap three targets and report the worst miss, in pixels. The calibration
  // itself cannot be made finer -- setTouchCalibrate stores the four corners
  // and nothing else, and the library already averages sixteen stabilised
  // readings at each -- so the useful thing is being able to see whether the
  // one you have is any good.
  int checkTouch();

  // The SD card and the touch controller share the VSPI host on this board
  // (on different pins), so anything that claims the bus for the card must
  // hand it back afterwards. See plat::writeRunFile().
  void reinitTouch() { touch_.init(); }

private:
#if defined(PANEL_ST7796)
  lgfx::Panel_ST7796 panel_;
#elif defined(PANEL_ILI9488)
  lgfx::Panel_ILI9488 panel_;
#elif defined(PANEL_ST7789)
  lgfx::Panel_ST7789 panel_;
#else
  lgfx::Panel_ILI9341 panel_;
#endif
  lgfx::Bus_SPI       bus_;
  lgfx::Light_PWM     light_;
  lgfx::Touch_XPT2046 touch_;
};

#endif

extern CydDisplay gfx;
