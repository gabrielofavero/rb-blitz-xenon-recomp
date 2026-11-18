// SPDX-License-Identifier: GPL-2.0-only
// Exercise the real CAMetalLayer without opening a window or launching the game.
#include "check.h"
#include "gpu/xenos/host/metal_surface.h"
#import <QuartzCore/CAMetalLayer.h>

int main() {
  using namespace rb_blitz::test;
  @autoreleasepool {
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.bounds = CGRectMake(0, 0, 874, 402);
    layer.contentsScale = 3;
    void* surface = (__bridge void*)layer;
    BeginCase("retiring a same-size swapchain cannot leave its replacement at 1x1");
    layer.drawableSize = CGSizeMake(2622, 1206);
    // MoltenVK 1.4.1's pending-presentation flush shrinks the shared layer.
    layer.drawableSize = CGSizeMake(1, 1);
    CHECK_TRUE(rex::gpu::RestoreMetalDrawableSize(surface, 2622, 1206));
    CHECK_EQ(layer.drawableSize.width, 2622);
    CHECK_EQ(layer.drawableSize.height, 1206);
    CHECK_EQ(layer.bounds.size.width, 874);
    CHECK_EQ(layer.contentsScale, 3);
    CHECK_FALSE(rex::gpu::RestoreMetalDrawableSize(surface, 2622, 1206));
    BeginCase("an actual resize uses the replacement extent without a DPI round trip");
    CHECK_TRUE(rex::gpu::RestoreMetalDrawableSize(surface, 2732, 2048));
    CHECK_EQ(layer.drawableSize.width, 2732);
    CHECK_EQ(layer.drawableSize.height, 2048);
    CHECK_FALSE(rex::gpu::RestoreMetalDrawableSize(surface, 0, 0));
    CHECK_EQ(layer.drawableSize.width, 2732);
    void* absent = nullptr;
    CHECK_FALSE(rex::gpu::RestoreMetalDrawableSize(absent, 2622, 1206));
  }
  return Finish();
}
