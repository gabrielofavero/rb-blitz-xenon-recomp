// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace rb_blitz::settings {
// Set by the host before the guest starts; no registry or disk access per draw.
void SetBootRenderHeight(uint32_t height);
uint32_t BootRenderHeight();
}
