// SPDX-License-Identifier: GPL-2.0-only
#include "diag/boot_heartbeat.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <rex/logging.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace rb_blitz::diag::boot_heartbeat {
namespace {
std::mutex g_mutex;
std::condition_variable_any g_wake;
std::jthread g_worker;
bool g_active = false;
std::string g_phase;

uint64_t ProcessCpuMs() {
#if defined(_WIN32)
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
  ULARGE_INTEGER k{}, u{};
  k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
  return (k.QuadPart + u.QuadPart) / 10000;
#else
  return 0;
#endif
}
}  // namespace

void Start() {
  std::lock_guard lock(g_mutex);
  if (g_active) return;
  g_active = true;
  g_phase = "guest_launch";
  g_worker = std::jthread([](std::stop_token stop) {
    const auto started = std::chrono::steady_clock::now();
    uint64_t previous_cpu = ProcessCpuMs();
    while (!stop.stop_requested()) {
      std::string phase;
      {
        std::unique_lock lock(g_mutex);
        g_wake.wait_for(lock, stop, std::chrono::seconds(15), [] { return false; });
        if (stop.stop_requested() || !g_active) break;
        phase = g_phase;
      }
      const uint64_t cpu = ProcessCpuMs();
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count();
      if (phase != "waiting_for_start")
        REXLOG_INFO("boot_heartbeat: elapsed_ms={} phase={} process_cpu_delta_ms={}",
                    elapsed, phase, cpu >= previous_cpu ? cpu - previous_cpu : 0);
      previous_cpu = cpu;
    }
  });
  REXLOG_INFO("boot_heartbeat: started (15 second interval; CPU is all process threads)");
}

void Phase(std::string_view phase) {
  std::lock_guard lock(g_mutex);
  if (g_active) g_phase = phase;
}

void Finish(std::string_view reason) {
  std::jthread worker;
  {
    std::lock_guard lock(g_mutex);
    if (!g_active) return;
    g_active = false;
    worker = std::move(g_worker);
  }
  worker.request_stop();
  g_wake.notify_all();
  if (worker.joinable()) worker.join();
  REXLOG_INFO("boot_heartbeat: stopped ({})", reason);
}
}  // namespace rb_blitz::diag::boot_heartbeat
