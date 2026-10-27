// SPDX-License-Identifier: GPL-2.0-only
#include "diag/shader_trace.h"
#include "graphics/preparation_loading.h"
#include <rex/cvar.h>
#include <rex/graphics/compile_trace.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <string>

REX_EXTERN(__imp__sub_822CB0D0); // ServerConnectPanel::start_connect_process
REX_EXTERN(
    __imp__sub_822CA8E8); // set_state: +56, update_state message, then advance

namespace trace = rex::graphics::compile_trace;
namespace rb_blitz::diag::shader_trace {
namespace {
bool menu_seen = false; // guest/UI thread only
bool in_game_screen = false;
} // namespace
void LogConfiguration() {
  if (!trace::Enabled())
    return;
  for (const char *name : {"gpu",
                           "gpu_backend",
                           "cache_root",
                           "game_data_root",
                           "user_data_root",
                           "update_data_root",
                           "launcher_profile",
                           "license_mask",
                           "dlc_root",
                           "dlc_library",
                           "ultimate_mode",
                           "ultimate_payload_root",
                           "vsync",
                           "async_shader_compilation",
                           "store_shaders",
                           "d3d12_pipeline_creation_threads",
                           "vulkan_pipeline_creation_threads",
                           "render_target_path_d3d12",
                           "render_target_path_vulkan",
                           "d3d12_readback_resolve",
                           "vulkan_readback_resolve",
                           "draw_resolution_scale_x",
                           "draw_resolution_scale_y",
                           "resolution_scale",
                           "enhancements_skip_offline_dialog",
                           "mouse_ui_nav"}) {
    REXLOG_INFO("shader_trace: config {}={}", name,
                rex::cvar::GetFlagByName(name));
  }
}
void ObserveScreen(std::string_view screen, std::string_view panel,
                   std::string_view focus, bool transitioning) {
  // Small, always-on startup evidence for intermittent blank/blue boots. This
  // observes native UI state only; it cannot establish that pixels rendered.
  // Keep it independent of the expensive shader diagnostic switch.
  static bool startup_finished = false;
  static unsigned startup_budget = 16;
  static std::string startup_context;
  if (!startup_finished && startup_budget) {
    const std::string context = std::string(screen) + "/" + std::string(panel) +
                                (transitioning ? "/transition" : "/stable");
    if (context != startup_context) {
      startup_context = context;
      --startup_budget;
      REXLOG_INFO("startup: native UI {} focus={}", context, focus);
    }
    startup_finished = !transitioning && screen == "splash_screen" && panel == "splash_panel";
  }
  if (!trace::Enabled())
    return;
  static std::string last_context;
  static uint64_t context_begin = 0;
  static unsigned context_budget = 0;
  const uint64_t now = trace::Now();
  const std::string context = std::string(screen) + "/" + std::string(panel) +
                              "/" + std::string(focus) +
                              (transitioning ? "/transition" : "/stable");
  if (last_context != context) {
    last_context = context;
    context_begin = now;
    if (context_budget++ < 512)
      REXLOG_INFO("shader_trace: ui_context={} t_us={}", context, trace::Now());
  }
  in_game_screen = screen == "game_screen" && !transitioning;
  if (transitioning) {
    if (trace::CurrentPhase() == trace::Phase::kActiveSong)
      trace::SetPhase(trace::Phase::kUnknown, "game UI transition");
    return;
  }
  if (screen == "loading_screen") {
    trace::SetPhase(trace::Phase::kSongLoading, "loading_screen (including press A to begin)");
  } else if (in_game_screen) {
    if (panel == "pause_panel")
      trace::SetPhase(trace::Phase::kPaused, "pause_panel");
    else if (trace::CurrentPhase() != trace::Phase::kActiveSong)
      trace::SetPhase(trace::Phase::kSongLoading,
                      "game_screen awaiting enabled controller");
  } else if (screen == "server_connect_screen" ||
             panel == "server_connect_panel") {
    trace::SetPhase(trace::Phase::kPostALoading, "server_connect_screen");
  } else if (screen == "splash_screen" && panel == "splash_panel" &&
             focus == "start.but") {
    // Cancel returns to this same title screen; it must not count as menu
    // readiness. On connect exit the old start.but focus survives for one poll
    // before start.lst is published (measured 9 ms). Debounce only this diagnostic
    // title label; this neither delays guest execution nor establishes a gate.
    if ((menu_seen || trace::CurrentPhase() == trace::Phase::kPostALoading) &&
        now - context_begin < 50000)
      return;
    trace::SetPhase(trace::Phase::kPreA, "splash start.but");
  } else if (screen == "splash_screen" && panel == "splash_panel" &&
             focus == "start.lst") {
    menu_seen = true;
    trace::SetPhase(trace::Phase::kMenu,
                    "splash start.lst with no transition pending");
  } else if (menu_seen) {
    trace::SetPhase(trace::Phase::kMenu, "non-game menu after start.lst");
  }
}
void ObserveSongController(bool accepts_input) {
  if (!trace::Enabled() || !in_game_screen)
    return;
  if (accepts_input)
    trace::SetPhase(trace::Phase::kActiveSong, "song controller +52 enabled");
  else if (trace::CurrentPhase() == trace::Phase::kActiveSong)
    trace::SetPhase(trace::Phase::kSongLoading, "song controller disabled");
}
} // namespace rb_blitz::diag::shader_trace

REX_HOOK_RAW(sub_822CB0D0) {
  rb_blitz::graphics::BeginLoadingAttempt();
  trace::SetPhase(trace::Phase::kPostALoading, "start_connect_process");
  __imp__sub_822CB0D0(ctx, base);
}

REX_HOOK_RAW(sub_822CA8E8) {
  if (trace::Enabled()) {
    static unsigned budget = 0; // all calls on the guest/UI owner thread
    if (budget++ < 512) {
      REXLOG_INFO("shader_trace: connect_state panel={:08X} previous={} "
                  "requested={} t_us={} lr={:08X}",
                  ctx.r3.u32,
                  rex::memory::load_and_swap<uint32_t>(base + ctx.r3.u32 + 56),
                  ctx.r4.u32, trace::Now(), uint32_t(ctx.lr));
    }
  }
  // In particular, OfflineMode can reenter this function from update_state.
  // The probe never delays the callback or rewrites the state/registers.
  __imp__sub_822CA8E8(ctx, base);
}
