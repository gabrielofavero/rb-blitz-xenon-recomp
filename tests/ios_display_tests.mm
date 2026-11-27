// SPDX-License-Identifier: GPL-2.0-only
// Runs in a UIKit application without initializing or linking SDL.
#include "platform/ios_display.h"
#include "ui/portrait_menu.h"
#import <UIKit/UIKit.h>
#include <cstdio>
#include <cstdlib>

@interface DisplayTestDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation DisplayTestDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
  const auto pixels = rb_blitz::platform::IOSNativeDisplaySize();
  if (pixels.first <= 0 || pixels.second <= 0) std::exit(1);
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  self.window.rootViewController = [UIViewController new];
  [self.window makeKeyAndVisible];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    using namespace rb_blitz::ui;
    const auto viewport = rb_blitz::platform::IOSWindowViewport((__bridge void*)self.window);
    bool ok = viewport.width > 0 && viewport.height > viewport.width &&
              viewport.safe_width > 0 && viewport.safe_height > 0;
    if (ok) {
      const float aspect = viewport.width / viewport.height;
      ok &= std::abs(float(pixels.second) / pixels.first - aspect) < .005f;
      for (float authored : {aspect, 9.f / 16, .75f}) {
        const auto fit = FitPortraitViewport(authored, viewport);
        for (float x : {-240 * authored, 240 * authored})
          for (float z : {-240.f, 240.f}) {
            const float px = viewport.width / 2 + fit.X(x) * viewport.height / 480;
            const float py = viewport.height / 2 - fit.Z(z) * viewport.height / 480;
            ok &= px > viewport.safe_x && px < viewport.safe_x + viewport.safe_width &&
                  py > viewport.safe_y && py < viewport.safe_y + viewport.safe_height;
          }
        // Full measured mesh extents include the pinball and rocket, not just
        // the centered wordmark. Both extremes must stay in the fitted canvas.
        const auto logo = PortraitSplash(authored);
        ok &= -237.4035f * logo.logo_scale >= -240 * authored &&
               262.047f * logo.logo_scale <= 240 * authored;
      }
    }
    std::printf("ios_display: %s; native=%dx%d viewport=%.0fx%.0f safe=%.0f,%.0f %.0fx%.0f\n",
                ok ? "PASS" : "FAIL", pixels.first, pixels.second,
                viewport.width, viewport.height, viewport.safe_x, viewport.safe_y,
                viewport.safe_width, viewport.safe_height);
    std::fflush(stdout);
    std::exit(ok ? 0 : 1);
  });
  return YES;
}
@end

int main(int argc, char** argv) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass(DisplayTestDelegate.class));
  }
}
