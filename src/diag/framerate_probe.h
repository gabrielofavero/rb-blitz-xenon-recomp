// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::diag::framerate_probe {

// Diagnostic counters for the guest's complete main-loop draw and present routine.
// Both are off unless probe_framerate is enabled at boot.
void CompleteIteration();
void CompleteGuestPresent();

}  // namespace rb_blitz::diag::framerate_probe
