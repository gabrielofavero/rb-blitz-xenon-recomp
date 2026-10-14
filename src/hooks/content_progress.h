// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include "ui/content_progress.h"

namespace rb_blitz::content_progress {
void Configure();
void Tick();
// UIManager::Poll supplies the focused panel and transition state.
void ObserveScreen(std::string_view screen, std::string_view panel, bool available);
struct Display {
  Progress progress;
  std::string detail;
  bool visible = false;
};
Display Snapshot();

// Timings of synchronous file calls, not the guest parser's CPU time.
struct IoObservation { int kind = 0; int64_t start_us = 0; };
IoObservation BeginIo(uint32_t handle, bool write);
void EndIo(IoObservation observation, uint32_t status);
}  // namespace rb_blitz::content_progress
