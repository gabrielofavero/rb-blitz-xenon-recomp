// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

struct PPCContext;

namespace rb_blitz::input {
// Draws fractional viewports with the same spare elements used by native scrolling.
void DrawNativeList(PPCContext& ctx, uint8_t* base);
}
