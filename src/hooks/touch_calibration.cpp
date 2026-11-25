// SPDX-License-Identifier: GPL-2.0-only
#include "input/touch_controls.h"
#include "input/touch_timing.h"
#include "input/native_menu.h"
#include "input/title_input.h"
#include "settings/runtime_settings.h"
#include "settings/runtime_values.h"
#include "ui/achievements_overlay.h"

#include <SDL3/SDL.h>
#include <rex/ppc/hook.h>
#include <rex/memory/endian.h>
#include <rex/logging/log_category.h>

REX_EXTERN(__imp__sub_822BCE90);  // CalibrationPanel::Poll
REX_EXTERN(__imp__sub_822BB310);  // CalibrationPanel::Exit
REX_EXTERN(__imp__sub_822B89A8);  // CalibrationPanel's live elapsed milliseconds
REX_EXTERN(sub_822B8A30);        // CalibrationPanel's elapsed cycle count
REX_EXTERN(sub_822BC0C0);        // CalibrationPanel's native input sample

namespace {
thread_local uint64_t g_sample_timestamp = 0, g_sample_now = 0;
uint32_t Read(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}
bool Sampling(PPCContext& ctx, uint8_t* base, uint32_t panel) {
  using namespace rb_blitz::input;
#if !defined(RB_BLITZ_IOS)
  if (!touch::PreviewEnabled()) return false;
#endif
  // Native ButtonDown accepts manual samples only in state 2, after five
  // countdown cycles. Hardware calibration retains its sensor input path.
  if (Read(base, panel + 100) != 2 || base[panel + 108] || !MenuAvailable() ||
      !MenuScreenIs(base[panel + 85] ? "cal_visuals_screen" : "cal_audio_screen") ||
      !HostInputFocused() || SystemUIActive() || rb_blitz::ui::achievements::Active()) return false;
  PPCContext call = ctx;
  call.r1.u32 -= 256;
  call.r3.u32 = panel;
  sub_822B8A30(call, base);
  return call.r3.s32 >= 5;
}
}

REX_HOOK_RAW(sub_822BCE90) {
  using namespace rb_blitz::input::touch;
  const uint32_t panel = ctx.r3.u32;
  // Drain before the native poll can finish the test. Every tap keeps its own
  // time, even if multiple contacts complete during one delayed UI frame.
  const auto taps = DrainCalibrationTaps(panel);
  for (const auto timestamp : taps) {
    const auto now = SDL_GetTicksNS();
    if (!Sampling(ctx, base, panel) || InputAgeNs(timestamp, now) > kMaxInputAgeNs) continue;
    g_sample_timestamp = timestamp;
    g_sample_now = now;
    PPCContext call = ctx;
    call.r1.u32 -= 256;
    call.r3.u32 = panel;
    call.r4.u32 = Read(base, panel + 160);  // user chosen by start_test
    sub_822BC0C0(call, base);
    g_sample_timestamp = g_sample_now = 0;
    if (rb_blitz::settings::InspectRuntimeSetting("input_source_trace").active_value == "true")
      REXLOG_INFO("touch calibration: panel={:#x} queue_ms={:.3f}", panel,
          double(InputAgeNs(timestamp, now)) * 1e-6);
  }
  __imp__sub_822BCE90(ctx, base);
  PublishCalibrationTarget(Sampling(ctx, base, panel) ? panel : 0);
}

REX_HOOK_RAW(sub_822BB310) {
  rb_blitz::input::touch::PublishCalibrationTarget(0);
  __imp__sub_822BB310(ctx, base);
}

REX_HOOK_RAW(sub_822B89A8) {
  __imp__sub_822B89A8(ctx, base);
  if (g_sample_timestamp)
    ctx.f1.f64 = rb_blitz::input::touch::EventTimeMs(ctx.f1.f64, g_sample_timestamp, g_sample_now);
}
