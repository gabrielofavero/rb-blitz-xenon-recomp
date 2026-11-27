// SPDX-License-Identifier: GPL-2.0-only
#include "platform/ios_display.h"
#import <UIKit/UIKit.h>

namespace rb_blitz::platform {
std::pair<int, int> IOSNativeDisplaySize() {
  @autoreleasepool {
    const CGSize size = UIScreen.mainScreen.nativeBounds.size;
    return {int(std::max(size.width, size.height)), int(std::min(size.width, size.height))};
  }
}

ui::PortraitViewport IOSWindowViewport(void* native_window) {
  @autoreleasepool {
    UIWindow* window = (__bridge UIWindow*)native_window;
    if (!window) return {};
    const CGSize size = window.bounds.size;
    const UIEdgeInsets insets = window.safeAreaInsets;
    return {float(size.width), float(size.height), float(insets.left), float(insets.top),
            float(size.width - insets.left - insets.right),
            float(size.height - insets.top - insets.bottom)};
  }
}
}
