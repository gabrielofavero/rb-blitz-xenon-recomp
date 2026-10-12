// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/render_settings.h"

#include <atomic>
#include <rex/hook.h>

namespace {
std::atomic<uint32_t> g_render_height{720};
}

void rb_blitz::settings::SetBootRenderHeight(uint32_t height) {
  // Keep the title's measured 540p allocation and let the Xenos backend scale
  // render targets, viewports and resolves. This is rendering at the requested
  // resolution, not resizing an already rendered frame. The 1080p guest allocation path
  // corrupts title-screen assets before the menu is reached.
  g_render_height.store(height == 540 ? 540 : 720, std::memory_order_relaxed);
}

uint32_t rb_blitz::settings::BootRenderHeight() {
  return g_render_height.load(std::memory_order_relaxed);
}

void RenderHeightHook(PPCRegister& r11) {
  r11.u32 = g_render_height.load(std::memory_order_relaxed);
}
