// SPDX-License-Identifier: GPL-2.0-only
#include "settings/display_probe_mac.h"
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace rb_blitz::settings {
namespace {
SDL_DisplayID DisplayForMonitor(int32_t index) {
  if (index <= 0) return SDL_GetPrimaryDisplay();
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  const SDL_DisplayID selected = displays && index <= count ? displays[index - 1] : 0;
  SDL_free(displays);
  return selected;
}

DisplayMode BackingSize(const SDL_DisplayMode& mode) {
  if (!std::isfinite(mode.pixel_density) || mode.pixel_density <= 0) return {};
  return {int32_t(std::lround(mode.w * mode.pixel_density)),
          int32_t(std::lround(mode.h * mode.pixel_density))};
}
}  // namespace

void ProbeMacDisplay(CapabilityInputs& inputs) {
  const SDL_DisplayID display = SDL_GetPrimaryDisplay();
  const SDL_DisplayMode* current = SDL_GetCurrentDisplayMode(display);
  if (!current) return;
  inputs.primary_display_size = BackingSize(*current);
  if (inputs.primary_display_size.width <= 0 || inputs.primary_display_size.height <= 0) return;
  inputs.display_enumeration_available = true;
  inputs.primary_display_id = "sdl:" + std::to_string(display);
  inputs.display_modes.push_back(inputs.primary_display_size);
  int count = 0;
  SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
  if (modes) {
    for (int i = 0; i < count; ++i) inputs.display_modes.push_back(BackingSize(*modes[i]));
    SDL_free(modes);
  }
}

DesktopMetrics ProbeMacDesktop(int32_t monitor_index) {
  DesktopMetrics desktop;
  const SDL_DisplayID display = DisplayForMonitor(monitor_index);
  const SDL_DisplayMode* mode = display ? SDL_GetCurrentDisplayMode(display) : nullptr;
  SDL_Rect usable{};
  if (!mode || !std::isfinite(mode->pixel_density) || mode->pixel_density <= 0 ||
      !SDL_GetDisplayUsableBounds(display, &usable)) return desktop;
  const double density = mode->pixel_density;
  // SDL's Cocoa backend cannot query window borders. Measure the decorated
  // frame through Cocoa without opening a temporary window.
  const NSRect content = NSMakeRect(0, 0, 640, 360);
  const NSRect frame = [NSWindow frameRectForContentRect:content
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable];
  desktop.work_area = {int32_t(std::lround(usable.w * density)),
                       int32_t(std::lround(usable.h * density))};
  desktop.frame_extent = {int32_t(std::lround((frame.size.width - content.size.width) * density)),
                          int32_t(std::lround((frame.size.height - content.size.height) * density))};
  desktop.scale_percent = uint32_t(std::lround(density * 100));
  desktop.available = desktop.work_area.width > 0 && desktop.work_area.height > 0;
  return desktop;
}

void LimitMacRenderPath(RenderPathLimits& limits) {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return;
    const uint64_t max_buffer = device.maxBufferLength;
    [device release];
    // MoltenVK has no sparse buffers: the scaled resolve address space must
    // fit in one Metal buffer (512 MiB times the square of the draw scale).
    std::erase_if(limits.scales, [max_buffer](int32_t scale) {
      return uint64_t(512) * 1024 * 1024 * uint64_t(scale) * uint64_t(scale) > max_buffer;
    });
  }
}
}  // namespace rb_blitz::settings
