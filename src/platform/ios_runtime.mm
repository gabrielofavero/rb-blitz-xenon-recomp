// SPDX-License-Identifier: GPL-2.0-only
#include "platform/ios_runtime.h"
#include <stdexcept>
#include <condition_variable>
#include <mutex>
#include "fs/ios_documents.h"
#include <rex/logging.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace rb_blitz::platform {
namespace {
std::mutex g_activation_mutex;
std::condition_variable g_activation_changed;
bool g_active = true;
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

void SetIOSApplicationActive(bool active) {
  {
    std::lock_guard lock(g_activation_mutex);
    g_active = active;
  }
  if (active) g_activation_changed.notify_all();
}
void WaitForIOSForeground() {
  std::unique_lock lock(g_activation_mutex);
  g_activation_changed.wait(lock, [] { return g_active; });
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
    rb_blitz::fs::PrepareIOSDocumentsFolders(root);
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
