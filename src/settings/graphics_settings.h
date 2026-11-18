// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <optional>
#include <string>
#include <rex/settings_registry.h>

namespace rb_blitz::settings {
// Historical input names are parsed into title-owned startup settings. Runtime
// consumers use the SDK's canonical pair rather than reading these inputs back.
rex::Result<void> RegisterLegacyGraphicsInputs(rex::SettingsRegistry& registry);
rex::Result<void> ApplyLegacyGraphicsInputs();
std::optional<int32_t> UniformResolutionScale(bool staged = false);
std::string ActiveDisplayMode();
}  // namespace rb_blitz::settings
