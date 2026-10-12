// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/render_settings.h"

#include <atomic>
#include <rex/hook.h>

namespace {
std::atomic<uint32_t> g_render_height{720};
}

void rb_blitz::settings::SetBootRenderHeight(uint32_t height) {
  g_render_height.store(height == 1080 ? 1080 : 720, std::memory_order_relaxed);
}

void RenderHeightHook(PPCRegister& r11) {
  r11.u32 = g_render_height.load(std::memory_order_relaxed);
}
