// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "input/input_icons.h"

namespace rb_blitz::assets {

// Internal P1 marker and fingerprint probe. No guest pixels or scene files are
// read here. Disabled unless RB_BLITZ_P1_TRACE or a marker hash is set.
void InstallP1TextureOverride();
// Scan at boot, publish the validated catalog, and install the texture provider.
void InstallControllerSchemes(const std::filesystem::path& install_root,
                              const std::filesystem::path& user_root);
// Queue a selected, validated token for the GPU thread. Returns false for an unavailable token.
bool SelectControllerScheme(const std::string& id);
// Publish the image glyphs used by the currently entered mapping page. The guest list text
// receives matching character codes; the GPU thread consumes this immutable batch in Poll().
void PublishInputIcons(std::string signature, std::vector<input::icons::Icon> icons);
bool InputIconsAvailable();
const controller_schemes::Registry* ControllerSchemeRegistry();

}  // namespace rb_blitz::assets
