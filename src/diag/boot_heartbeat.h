// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string_view>

namespace rb_blitz::diag::boot_heartbeat {
// Host-only startup watchdog. It reads no guest memory and never changes loading.
void Start();
void Phase(std::string_view phase);
void Finish(std::string_view reason);
}  // namespace rb_blitz::diag::boot_heartbeat
