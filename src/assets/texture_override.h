// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::assets {

// Internal P1 marker and fingerprint probe. No guest pixels or scene files are
// read here. Disabled unless RB_BLITZ_P1_TRACE or a marker hash is set.
void InstallP1TextureOverride();

}  // namespace rb_blitz::assets
