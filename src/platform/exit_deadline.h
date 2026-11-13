// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <chrono>

namespace rb_blitz::platform {

inline constexpr auto kGuestExitDeadline = std::chrono::seconds(2);

// Call only after the guest's main thread has exited. Normal host cleanup may
// proceed, but a deadlocked destructor must not keep the process alive. This
// process-owned watchdog captures no app objects and stays armed through static
// destruction. Repeated calls cannot extend the original deadline.
// False means the watchdog thread could not be created.
bool ArmExitDeadline(std::chrono::milliseconds timeout = kGuestExitDeadline);

}  // namespace rb_blitz::platform
