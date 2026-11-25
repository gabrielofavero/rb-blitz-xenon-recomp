// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "input/native_menu.h"

namespace rb_blitz::input {
// Capture local geometry before upload discards it, then project the current draw.
void SyncMesh(PPCContext& ctx, uint8_t* base);
std::optional<MenuBounds> ProjectMesh(PPCContext& ctx, uint8_t* base, uint32_t mesh,
                                     uint32_t transform = 0);
uint32_t SceneMesh(PPCContext& ctx, uint8_t* base, uint32_t dir, const char* name);
} // namespace rb_blitz::input
