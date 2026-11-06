// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "input/input_icons.h"

namespace rb_blitz::assets {

// Unused action-hint slots carry the header's Previous/Next binding images to
// the button-sheet provider. The header labels themselves use buttons.milo.
inline constexpr std::size_t kHeaderPreviousIcon = 0;
inline constexpr std::size_t kHeaderNextIcon = 13;

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
// Publish every device variant together so the texture worker can prepare them
// before the next keyboard, mouse, or controller input changes the active art.
void PublishHintIcons(std::string signature,
    std::array<std::vector<input::icons::Icon>, 3> variants,
    std::array<input::icons::Icon, 3> calibration,
    input::icons::Family active_family);
bool InputIconsAvailable();
const controller_schemes::Registry* ControllerSchemeRegistry();

}  // namespace rb_blitz::assets
