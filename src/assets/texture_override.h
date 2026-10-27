// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>

namespace rb_blitz::assets {

// Internal P1 marker and fingerprint probe. No guest pixels or scene files are
// read here. Disabled unless RB_BLITZ_P1_TRACE or a marker hash is set.
void InstallP1TextureOverride();
// P2: scan controller packages at boot. Selection is retail until P3 wires the setting;
// RB_BLITZ_SCHEME is a developer-only package probe.
void InstallControllerSchemes(const std::filesystem::path& install_root,
                              const std::filesystem::path& user_root);

}  // namespace rb_blitz::assets
