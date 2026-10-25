// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>

namespace rb_blitz::hooks {
// Called only after the guest has freshly resolved this live transformable's
// world matrix. The WorldXfm hook owns ordering and preserves the guest ABI.
// See docs/settings/graphics.md part 3 §9.2.7.
// Returns true for a live eligible anchor, including one inheriting its shift.
bool HudReanchorWorld(uint8_t* base, uint32_t transformable);
}  // namespace rb_blitz::hooks
