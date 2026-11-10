// SPDX-License-Identifier: GPL-2.0-only
#include "diag/framerate_probe.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REX_EXTERN(__imp__sub_82190218);
REX_EXTERN(__imp__sub_82470248);

REXCVAR_DEFINE_BOOL(probe_framerate, false, "Diagnostics",
                    "Log bounded one-second counts of completed guest main-loop draws and "
                    "guest present routine calls. Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::diag::framerate_probe {
namespace {
using Clock = std::chrono::steady_clock;

// The draw runs on the main guest thread. The present routine may also be reached
// by a second graphics path, so keep its count atomic for the once-per-second read.
std::atomic<uint64_t> g_guest_presents{0};
std::atomic<uint64_t> g_song_clock_samples{0};
std::atomic<double> g_song_clock_raw{0.0};
std::atomic<double> g_song_clock_filtered{0.0};
uint64_t g_draws = 0;
uint64_t g_previous_draws = 0;
uint64_t g_previous_presents = 0;
uint64_t g_previous_song_clock_samples = 0;
Clock::time_point g_window_start{};
bool g_started = false;
}  // namespace

void CompleteGuestPresent() {
  if (REXCVAR_GET(probe_framerate)) {
    g_guest_presents.fetch_add(1, std::memory_order_relaxed);
  }
}

void ObserveSongClock(double raw, double filtered) {
  if (!REXCVAR_GET(probe_framerate) || !std::isfinite(raw) ||
      !std::isfinite(filtered)) return;
  g_song_clock_raw.store(raw, std::memory_order_relaxed);
  g_song_clock_filtered.store(filtered, std::memory_order_relaxed);
  g_song_clock_samples.fetch_add(1, std::memory_order_relaxed);
}

void CompleteIteration() {
  if (!REXCVAR_GET(probe_framerate)) return;
  const auto now = Clock::now();
  ++g_draws;
  if (!g_started) {
    g_window_start = now;
    g_started = true;
    return;
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - g_window_start).count();
  if (elapsed < 1000) return;
  const uint64_t presents = g_guest_presents.load(std::memory_order_relaxed);
  const uint64_t clock_samples = g_song_clock_samples.load(std::memory_order_relaxed);
  REXLOG_INFO("framerate_probe: window_ms={} guest_iterations={} guest_present_calls={} "
              "song_clock_samples={} song_clock_raw={} song_clock_filtered={} "
              "total_iterations={} total_present_calls={}",
              elapsed, g_draws - g_previous_draws, presents - g_previous_presents,
              clock_samples - g_previous_song_clock_samples,
              g_song_clock_raw.load(std::memory_order_relaxed),
              g_song_clock_filtered.load(std::memory_order_relaxed), g_draws, presents);
  g_previous_draws = g_draws;
  g_previous_presents = presents;
  g_previous_song_clock_samples = clock_samples;
  g_window_start = now;
}
}  // namespace rb_blitz::diag::framerate_probe

// `sub_82190568` calls this at 0x82190644 and immediately branches back to
// 0x821905AC. Counting after the original returns marks completed draw calls in
// complete guest main-loop iterations without replacing the loop's PPC body.
REX_HOOK_RAW(sub_82190218) {
  __imp__sub_82190218(ctx, base);
  rb_blitz::diag::framerate_probe::CompleteIteration();
}

// Blitz's only direct call to this 32-sample clock filter is at 0x8245D55C.
// Record its argument and return without changing either, then correlate the
// sample cadence and filter drift with the completed draw count.
REX_HOOK_RAW(sub_82470248) {
  const double raw = ctx.f1.f64;
  __imp__sub_82470248(ctx, base);
  rb_blitz::diag::framerate_probe::ObserveSongClock(raw, ctx.f1.f64);
}
