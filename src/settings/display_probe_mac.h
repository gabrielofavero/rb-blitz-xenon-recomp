// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "settings/capabilities.h"
#include "settings/window_policy.h"

namespace rb_blitz::settings {
// Main thread, after SDL video initialization; measurements use backing pixels.
void ProbeMacDisplay(CapabilityInputs& inputs);
DesktopMetrics ProbeMacDesktop(int32_t monitor_index);
void LimitMacRenderPath(RenderPathLimits& limits);
}  // namespace rb_blitz::settings
