// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>

namespace rb_blitz::loading {
// Disabled by default; configured on the host before the guest exists.
void ConfigureLoadTrace();
// Shared UIManager::Poll observer. Never calls guest code or issues input.
void ObserveLoadFrame(uint8_t* base, uint32_t manager);
}  // namespace rb_blitz::loading
