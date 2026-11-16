// SPDX-License-Identifier: GPL-2.0-only
#include "platform/ios_runtime.h"
#include <stdexcept>
#include <algorithm>
#include "fs/screenshots.h"
#include <rex/logging.h>
#include <rex/ui/presenter.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace rb_blitz::platform {
void SaveIOSStartupFrame(const std::filesystem::path& documents, const rex::ui::RawImage& frame) {
  const auto directory = documents / "diagnostics";
  size_t nonblack = 0, visible = 0;
  uint8_t peak = 0;
  for (size_t i = 0; i + 3 < frame.data.size(); i += 4) {
    const auto value = std::max({frame.data[i], frame.data[i + 1], frame.data[i + 2]});
    nonblack += value != 0;
    visible += value > 16;
    peak = std::max(peak, value);
  }
  REXLOG_INFO("ios_frame: guest={}x{} nonblack={} visible={} pixels={} peak_rgb={}",
              frame.width, frame.height, nonblack, visible, frame.data.size() / 4, unsigned(peak));
  std::filesystem::path saved;
  std::string error;
  if (!rb_blitz::screenshots::SaveBitmap(directory, "ios-guest-frame", frame.width,
          frame.height, frame.stride, frame.data, saved, error)) {
    REXLOG_WARN("ios_frame: could not save guest image: {}", error);
    return;
  }
  // POSIX rename replaces the preceding capture atomically; keep only one image.
  std::error_code ec;
  const auto latest = directory / "ios-guest-frame.bmp";
  std::filesystem::rename(saved, latest, ec);
  if (ec) {
    REXLOG_WARN("ios_frame: could not publish guest image: {}", ec.message());
    std::filesystem::remove(saved, ec);
  } else {
    REXLOG_INFO("ios_frame: guest image saved to {}", latest.string());
  }
}

namespace {
void LogView(UIView* view, unsigned depth, unsigned& remaining) {
  if (!view || !remaining || depth > 6) return;
  --remaining;
  const CGRect frame = view.frame;
  REXLOG_INFO("ios_view: depth={} view={} layer={} hidden={} alpha={} attached={} "
              "frame={},{},{}x{} scale={}", depth, NSStringFromClass(view.class).UTF8String,
              NSStringFromClass(view.layer.class).UTF8String, bool(view.hidden), double(view.alpha),
              view.window != nil, double(frame.origin.x), double(frame.origin.y),
              double(frame.size.width), double(frame.size.height), double(view.layer.contentsScale));
  for (UIView* child in view.subviews) LogView(child, depth + 1, remaining);
  if ([view.layer isKindOfClass:CAMetalLayer.class]) {
    const CGSize drawable = ((CAMetalLayer*)view.layer).drawableSize;
    REXLOG_INFO("ios_view: Metal drawable={}x{}", double(drawable.width), double(drawable.height));
  }
}
}

void LogIOSPresentationState() {
  @autoreleasepool {
    REXLOG_INFO("ios_view: application_state={}", long(UIApplication.sharedApplication.applicationState));
    unsigned remaining = 32;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
      if (![scene isKindOfClass:UIWindowScene.class]) continue;
      for (UIWindow* window in ((UIWindowScene*)scene).windows) LogView(window, 0, remaining);
    }
  }
}

std::filesystem::path PrepareIOSDocuments() {
  @autoreleasepool {
    NSURL* documents = [NSFileManager.defaultManager
        URLForDirectory:NSDocumentDirectory inDomain:NSUserDomainMask
        appropriateForURL:nil create:YES error:nil];
    if (!documents)
      throw std::runtime_error("Could not locate the iOS Documents directory");
    const std::filesystem::path root(documents.fileSystemRepresentation);
    std::filesystem::create_directories(root / "DLCs");
    std::filesystem::create_directories(root / "Import");
    return root;
  }
}

std::vector<std::string> IOSCommandLineArguments() {
  @autoreleasepool {
    std::vector<std::string> result;
    for (NSString* argument in NSProcessInfo.processInfo.arguments)
      result.emplace_back(argument.UTF8String);
    return result;
  }
}
}  // namespace rb_blitz::platform
