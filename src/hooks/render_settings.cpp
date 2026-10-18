// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/render_settings.h"

#include <atomic>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

namespace {
// 0 = the title derives the width from the height, exactly as it ships.
std::atomic<uint32_t> g_render_width{0};
std::atomic<uint32_t> g_render_height{720};

// The title's own screen-space -> 16:9 mapping constants (measured 2026-10-12, see the header):
// two adjacent 9/16 floats in the literal pool of the code that lays the UI out.
constexpr uint32_t kLayoutAspectLo = 0x8201E4CC;
constexpr uint32_t kLayoutAspectHi = 0x8201E4D0;
// 9/16 as big-endian float bits: what the two fields above hold on the supported retail image, and
// therefore the shape guard that keeps this write off any other image.
constexpr uint32_t kAspect16By9Inverse = 0x3F100000;
}  // namespace

void rb_blitz::settings::SetBootRenderFrame(uint32_t width, uint32_t height) {
  // A zero height would ask the title for a degenerate frame; keep the measured 720 instead.
  const uint32_t safe_height = height == 0 ? 720 : height;
  g_render_height.store(safe_height, std::memory_order_relaxed);
  // A width of 0, and any width that is exactly 16:9 of the height, is what the title derives for
  // itself. Leaving the override off in that case keeps every existing boot on the title's own
  // arithmetic - including its 4:3 branch, which no plan would have produced.
  const bool title_derives_it = width == 0 || uint64_t(width) * 9 == uint64_t(safe_height) * 16;
  g_render_width.store(title_derives_it ? 0 : width, std::memory_order_relaxed);
}

void rb_blitz::settings::SetBootRenderHeight(uint32_t height) {
  // Keep the title's measured 540p allocation and let the Xenos backend scale
  // render targets, viewports and resolves. This is rendering at the requested
  // resolution, not resizing an already rendered frame. The 1080p guest allocation path
  // corrupts title-screen assets before the menu is reached.
  g_render_height.store(height == 0 ? 720 : height, std::memory_order_relaxed);
}

uint32_t rb_blitz::settings::BootRenderWidth() {
  return g_render_width.load(std::memory_order_relaxed);
}

uint32_t rb_blitz::settings::BootRenderHeight() {
  return g_render_height.load(std::memory_order_relaxed);
}

// 0x823F382C, the title's own `li r11,720`, before it stores its choice at +68 and derives the
// width from it. Setting r11 here is what lets the title lay out at a height it would not have
// chosen (config/functions.toml carries the entry; src/diag/probe_points.cpp forces the
// half-resolution flag when that height is 540, so the title takes its own 540 path).
void RenderHeightHook(PPCRegister& r11) {
  r11.u32 = rb_blitz::settings::BootRenderHeight();
}

// 0x823F3878, the `stw r7,64(r30)` that publishes the frame width, *before* it runs. The title
// reaches here through either aspect branch:
//
//   0x823F385C  divw r7,height*4,3     (4:3)     -> r29 too
//   0x823F3870  divw r7,height*16,9    (16:9)    -> r29 too
//   0x823F3878  stw r7,64(r30)
//
// r7 is the width field; r29 is the same width carried into the two-rect table the function
// builds right after (+848), so both have to agree or the frame and its viewport disagree.
// With no override (width 0) neither register is touched and the title's own arithmetic stands.
void RenderFrameWidthHook(PPCRegister& r7, PPCRegister& r29) {
  const uint32_t width = rb_blitz::settings::BootRenderWidth();
  if (width == 0) {
    return;
  }
  r7.u32 = width;
  r29.u32 = width;
}

namespace {
// Big-endian helpers: the guest is a big-endian machine and these are its own floats.
uint32_t LoadGuestBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void StoreGuestBe32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}
}  // namespace

void rb_blitz::settings::WriteGuestLayoutAspect(uint8_t* base) {
  const uint32_t width = g_render_width.load(std::memory_order_relaxed);
  const uint32_t height = g_render_height.load(std::memory_order_relaxed);
  // No forced width means the title chose a 16:9 (or its own 4:3) frame, which its layout already
  // matches: nothing to do, and no image write at all on an ordinary boot.
  if (base == nullptr || width == 0 || height == 0) {
    return;
  }
  const float inverse_aspect = float(height) / float(width);
  const uint32_t bits = [&] {
    uint32_t value = 0;
    std::memcpy(&value, &inverse_aspect, sizeof(value));
    return value;
  }();
  static uint32_t written = 0;
  if (written == bits) {
    return;  // already this boot's value; do not touch the image again
  }
  // The literal pool is inside the image's code region and the page is read-only (the first
  // attempt on the sibling 16/9 constant was refused as a guest access violation). Our own
  // process owns the page, so it is opened for the write and closed again.
  const auto write_guarded = [&](uint32_t address, uint32_t guard, uint32_t value,
                                 const char* what) -> bool {
    uint8_t* field = rex::memory::GuestPtr<uint8_t*>(base, address);
    if (field == nullptr) {
      return false;
    }
    const uint32_t before = LoadGuestBe32(field);
    // Shape guard: these addresses are only meaningful on the image they were measured on. If the
    // field does not hold its measured value, the image is not that image, and writing a float
    // into whatever is there would be worse than leaving the layout alone.
    if (before != guard) {
      REXLOG_WARN("render: 0x{:08X} holds {:#010X}, not the measured {:#010X}; {} left alone "
                  "(unsupported image?)",
                  address, before, guard, what);
      return false;
    }
    DWORD previous = 0;
    if (::VirtualProtect(field, sizeof(uint32_t), PAGE_READWRITE, &previous) == 0) {
      REXLOG_WARN("render: 0x{:08X} could not be opened for the {}", address, what);
      return false;
    }
    StoreGuestBe32(field, value);
    DWORD ignored = 0;
    ::VirtualProtect(field, sizeof(uint32_t), previous, &ignored);
    REXLOG_INFO("render: {} 0x{:08X} {:#010X} -> {:#010X}", what, address, before, value);
    return true;
  };
  size_t done = 0;
  for (const uint32_t address : {kLayoutAspectLo, kLayoutAspectHi}) {
    if (write_guarded(address, kAspect16By9Inverse, bits, "layout aspect")) {
      ++done;
    }
  }
  // Only latch once the writes really landed, so a boot that reached here before the fields were
  // mapped (or on an image whose guard refused) tries again rather than silently giving up.
  if (done == 2) {
    written = bits;
  }
}
