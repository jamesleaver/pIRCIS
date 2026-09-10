// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
// pIRCIS -- https://github.com/jamesleaver/pIRCIS
//
// The phone's own sheets: the share sheet for handing a program to another
// app or to Files, and the document picker for taking one in. Both are
// UIKit, which insists on the main thread, while the program runs on its
// own; so each request hops to the main queue, and what the picker returns
// waits here until the program next asks.
#if defined(SK_HOST) && defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "Platform.h"
#include "Display.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <atomic>
#include <mutex>
#include <utility>
#include <string>

namespace {
  std::mutex  g_pickMx;
  bool        g_pickReady = false;
  std::string g_pickName, g_pickText;

  // The window the picture is in: SDL's own if it exists, else the key
  // window of the scene.
  UIViewController* frontController() {
    SDL_Window* win = gfx.sdl().window();
    if (win) {
      SDL_SysWMinfo info; SDL_VERSION(&info.version);
      if (SDL_GetWindowWMInfo(win, &info) && info.subsystem == SDL_SYSWM_UIKIT && info.info.uikit.window &&
          info.info.uikit.window.rootViewController)
        return info.info.uikit.window.rootViewController;
    }
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
      if (![scene isKindOfClass:UIWindowScene.class]) continue;
      for (UIWindow* w in ((UIWindowScene*)scene).windows)
        if (w.isKeyWindow && w.rootViewController) return w.rootViewController;
    }
    return nil;
  }
}

// The picker talks to a delegate object; one lives for the whole run.
@interface PircisPickerDelegate : NSObject <UIDocumentPickerDelegate>
@end
@implementation PircisPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  if (urls.count == 0) return;
  NSURL* url = urls.firstObject;
  const BOOL scoped = [url startAccessingSecurityScopedResource];
  NSString* text = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:nil];
  if (scoped) [url stopAccessingSecurityScopedResource];
  if (!text) return;
  std::lock_guard<std::mutex> g(g_pickMx);
  g_pickName  = url.lastPathComponent.stringByDeletingPathExtension.UTF8String;
  g_pickText  = text.UTF8String;
  g_pickReady = true;
}
@end

// ---------------------------------------------------------------------------
// The keyboard, drawn by UIKit over the panel's key area. The program says
// which key goes where in panel pixels; this view sits exactly over that
// rectangle, draws the keys at the screen's own resolution, clicks under the
// finger, and hands each press back through injectKey().
// ---------------------------------------------------------------------------
namespace {
  UIColor* colour565(uint16_t c) {
    return [UIColor colorWithRed:((c >> 11) & 0x1F) / 31.0
                           green:((c >> 5) & 0x3F) / 63.0
                            blue:(c & 0x1F) / 31.0 alpha:1];
  }
  bool sameKeys(const plat::NativeKeys& a, const plat::NativeKeys& b) {
    return a.shown == b.shown && a.x == b.x && a.y == b.y && a.cols == b.cols && a.rows == b.rows &&
           a.keyW == b.keyW && a.keyH == b.keyH && a.gap == b.gap && a.keys == b.keys && a.commands == b.commands &&
           a.bg == b.bg && a.panel == b.panel && a.text == b.text && a.accent == b.accent;
  }
}

@interface PircisKeyView : UIView
@property (nonatomic) plat::NativeKeys spec;
@property (nonatomic) CGFloat keyW, keyH;      // in points
@property (nonatomic) int pressed;
@property (nonatomic, strong) UIImpactFeedbackGenerator* haptic;
- (void)applySpec:(const plat::NativeKeys&)spec frame:(CGRect)frame;
@end

@implementation PircisKeyView
- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  self.opaque = YES;
  self.multipleTouchEnabled = NO;
  self.pressed = -1;
  self.haptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
  return self;
}
- (void)applySpec:(const plat::NativeKeys&)spec frame:(CGRect)frame {
  _spec = spec;
  self.frame = frame;
  self.keyW = frame.size.width  / (spec.cols ? spec.cols : 1);
  self.keyH = frame.size.height / (spec.rows ? spec.rows : 1);
  self.backgroundColor = colour565(spec.bg);
  self.hidden = !spec.shown;
  [self setNeedsDisplay];
}
- (int)keyAt:(CGPoint)p {
  const int c = (int)(p.x / self.keyW), r = (int)(p.y / self.keyH);
  if (c < 0 || r < 0 || c >= _spec.cols || r >= _spec.rows) return -1;
  const int i = r * _spec.cols + c;
  return _spec.keys[i] ? i : -1;
}
- (void)drawRect:(CGRect)rect {
  UIColor* panel = colour565(_spec.panel);
  UIColor* text = colour565(_spec.text);
  UIColor* accent = colour565(_spec.accent);
  UIFont* font = [UIFont monospacedSystemFontOfSize:self.keyH * 0.5 weight:UIFontWeightRegular];
  const CGFloat gap = MAX(1.0, _spec.gap * self.keyH / (_spec.keyH ? _spec.keyH : 26));   // the panel's gap, in points
  for (int r = 0; r < _spec.rows; ++r)
    for (int c = 0; c < _spec.cols; ++c) {
      const int i = r * _spec.cols + c;
      const char k = _spec.keys[i];
      if (!k) continue;
      CGRect box = CGRectMake(c * self.keyW + gap, r * self.keyH + gap, self.keyW - 2 * gap, self.keyH - 2 * gap);
      UIColor* fill = (i == self.pressed) ? accent : panel;
      [fill setFill];
      [[UIBezierPath bezierPathWithRoundedRect:box cornerRadius:self.keyH * 0.15] fill];
      const bool command = _spec.commands.find(k) != std::string::npos;
      UIColor* ink = (i == self.pressed) ? colour565(_spec.bg) : (command ? accent : text);
      NSString* label = [NSString stringWithFormat:@"%c", k == ' ' ? '_' : k];
      NSDictionary* attrs = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: ink };
      CGSize sz = [label sizeWithAttributes:attrs];
      [label drawAtPoint:CGPointMake(CGRectGetMidX(box) - sz.width / 2, CGRectGetMidY(box) - sz.height / 2)
          withAttributes:attrs];
    }
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  const int i = [self keyAt:[touches.anyObject locationInView:self]];
  self.pressed = i;
  if (i >= 0) { [self.haptic impactOccurred]; [self.haptic prepare]; }
  [self setNeedsDisplay];
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  const int i = [self keyAt:[touches.anyObject locationInView:self]];
  if (i != self.pressed) { self.pressed = i; [self setNeedsDisplay]; }
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  const int i = [self keyAt:[touches.anyObject locationInView:self]];
  if (i >= 0 && i == self.pressed) plat::injectKey(_spec.keys[i]);
  self.pressed = -1;
  [self setNeedsDisplay];
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  self.pressed = -1;
  [self setNeedsDisplay];
}
@end

namespace {
  PircisKeyView* g_keyView = nil;
  plat::NativeKeys g_lastKeys;
  CGRect g_lastFrame = CGRectZero;
  bool g_haveLastKeys = false;
}

namespace plat {
  namespace {
    std::atomic<bool> g_screenChanged{false};
    bool g_watchingScreen = false;
    // Told when the app comes back to the front or the device turns: the
    // loop asks for the area again.
    void watchScreen() {
      if (g_watchingScreen) return;
      g_watchingScreen = true;
      NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
      [nc addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:nil
                  usingBlock:^(NSNotification*) { g_screenChanged = true; }];
      [UIDevice.currentDevice beginGeneratingDeviceOrientationNotifications];
      [nc addObserverForName:UIDeviceOrientationDidChangeNotification object:nil queue:nil
                  usingBlock:^(NSNotification*) { g_screenChanged = true; }];
    }
  }

  bool screenChanged() { return g_screenChanged.exchange(false); }
  bool isActive() {
    __block bool active = true;
    void (^read)(void) = ^{ active = UIApplication.sharedApplication.applicationState != UIApplicationStateBackground; };
    if (NSThread.isMainThread) read(); else dispatch_sync(dispatch_get_main_queue(), read);
    return active;
  }

  bool screenArea(ScreenArea& area) {
    __block CGSize size = CGSizeZero;
    __block UIEdgeInsets ins = UIEdgeInsetsZero;
    void (^read)(void) = ^{
      watchScreen();
      // The window the picture is in, once there is one: its bounds and
      // insets are the ones that count, whichever way the phone is held.
      SDL_Window* win = gfx.sdl().window();
      if (win) {
        SDL_SysWMinfo info; SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(win, &info) && info.subsystem == SDL_SYSWM_UIKIT && info.info.uikit.window) {
          UIWindow* uw = info.info.uikit.window;
          size = uw.bounds.size; ins = uw.safeAreaInsets;
          return;
        }
      }
      // Before it: a window on the scene knows the scene's orientation and
      // so its insets. Earlier still the scene is not connected, and the
      // bare screen will do until the window arrives and the area is asked
      // for again.
      for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class]) continue;
        UIWindow* probe = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
        size = probe.bounds.size; ins = probe.safeAreaInsets;
        return;
      }
      size = UIScreen.mainScreen.bounds.size;
    };
    if (NSThread.isMainThread) read(); else dispatch_sync(dispatch_get_main_queue(), read);
    if (size.width <= 0 || size.height <= 0) return false;
    // The shape as held, either way up, less exactly what is reserved and
    // where: the cut-out is on one side and the picture goes up to the
    // other edge.
    area.fullW = (int)size.width;  area.fullH = (int)size.height;
    area.x = (int)ins.left;        area.y = (int)ins.top;
    area.w = (int)(size.width  - ins.left - ins.right);
    area.h = (int)(size.height - ins.top  - ins.bottom);
    return area.w >= 320 && area.h >= 240;
  }

  bool isApp() { return true; }
  bool hasNativeKeys() { return true; }
  void nativeKeysRelayout() { g_haveLastKeys = false; }

  void nativeKeys(const NativeKeys& spec) {
    // Where the key area is on the window right now: a change of fit or a
    // turn of the phone moves it, and that counts as a change too. Only a
    // change crosses to the main thread.
    CGRect frame = CGRectZero;      // in the renderer's pixels
    int outW = 1;
    if (spec.shown) {
      float x0, y0, x1, y1;
      if (!gfx.sdl().panelToPixels(spec.x, spec.y, x0, y0, outW) ||
          !gfx.sdl().panelToPixels(spec.x + spec.cols * spec.keyW, spec.y + spec.rows * spec.keyH, x1, y1, outW))
        return;
      frame = CGRectMake(x0, y0, x1 - x0, y1 - y0);
    }
    if (g_haveLastKeys && sameKeys(spec, g_lastKeys) && CGRectEqualToRect(frame, g_lastFrame)) return;
    g_lastKeys = spec; g_lastFrame = frame; g_haveLastKeys = true;
    const NativeKeys copy = spec;
    dispatch_async(dispatch_get_main_queue(), ^{
      UIViewController* vc = frontController();
      // No window to put it on yet: forget this delivery, so the next tick
      // tries again rather than waiting for something else to change.
      if (!vc) { g_haveLastKeys = false; return; }
      if (!g_keyView) {
        g_keyView = [[PircisKeyView alloc] initWithFrame:CGRectZero];
        [vc.view addSubview:g_keyView];
      }
      [vc.view bringSubviewToFront:g_keyView];
      // Pixels to the view's own points: the view's real width over the
      // output's width.
      const CGFloat k = vc.view.bounds.size.width / (CGFloat)outW;
      [g_keyView applySpec:copy frame:CGRectMake(frame.origin.x * k, frame.origin.y * k,
                                                 frame.size.width * k, frame.size.height * k)];
    });
  }

  bool canShareFiles() { return true; }
  bool canPickFiles()  { return true; }

  bool shareText(const std::string& name, const std::string& text) {
    NSString* file = [NSString stringWithFormat:@"%@.txt",
                      [NSString stringWithUTF8String:name.empty() ? "program" : name.c_str()]];
    NSURL* url = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:file]];
    NSString* body = [NSString stringWithUTF8String:text.c_str()];
    if (![body writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:nil]) return false;
    dispatch_async(dispatch_get_main_queue(), ^{
      UIViewController* vc = frontController();
      if (!vc) return;
      UIActivityViewController* sheet =
        [[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil];
      // An iPad wants to know where the sheet grows from.
      sheet.popoverPresentationController.sourceView = vc.view;
      sheet.popoverPresentationController.sourceRect =
        CGRectMake(CGRectGetMidX(vc.view.bounds), CGRectGetMidY(vc.view.bounds), 1, 1);
      [vc presentViewController:sheet animated:YES completion:nil];
    });
    return true;
  }

  void pickFile() {
    static PircisPickerDelegate* delegate = [PircisPickerDelegate new];
    dispatch_async(dispatch_get_main_queue(), ^{
      UIViewController* vc = frontController();
      if (!vc) return;
      UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypePlainText, UTTypeText]
                                                                    asCopy:YES];
      picker.delegate = delegate;
      picker.allowsMultipleSelection = NO;
      [vc presentViewController:picker animated:YES completion:nil];
    });
  }

  bool takePickedFile(std::string& nameOut, std::string& textOut) {
    std::lock_guard<std::mutex> g(g_pickMx);
    if (!g_pickReady) return false;
    nameOut = g_pickName; textOut = g_pickText;
    g_pickReady = false;
    return true;
  }
}
#endif
#endif
