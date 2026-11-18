// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <rex/settings_registry.h>

namespace rb_blitz::settings {

// Values and their sources are separate: a restart edit has a staged source,
// while the SDK continues to serve the earlier active value.
struct RuntimeSettingSnapshot {
  std::string canonical_name;
  bool registered = false;
  std::string active_value;
  std::string staged_value;
  rex::SettingLayer staged_source = rex::SettingLayer::kDefault;
  bool restart_pending = false;
};

enum class RuntimeSettingWrite {
  kApplied,
  kUnchanged,
  kShadowed,
  kPendingRestart,
  kDeferred,
  kRefused,
  kUnknown,
};

struct RuntimeSettingWriteResult {
  RuntimeSettingWrite state = RuntimeSettingWrite::kUnknown;
  std::string error;
  bool accepted() const {
    return state != RuntimeSettingWrite::kRefused && state != RuntimeSettingWrite::kUnknown;
  }
};

// Only the title's named schema bindings and the SDK bindings it uses are resolved.
// An unknown name stays distinct from a registered setting with an empty value.
std::optional<std::string> ResolveRuntimeSettingName(std::string_view name);
RuntimeSettingSnapshot InspectRuntimeSetting(std::string_view name);
RuntimeSettingWriteResult WriteRuntimeSetting(std::string_view name, std::string_view value,
                                             rex::SettingLayer layer);

// Register persisted SDK names and legacy environment names before SDK CLI/file loading.
// Canonical environment values are applied by the SDK afterward and take precedence.
rex::Result<void> RegisterRuntimeSettingAliases(rex::SettingsRegistry& registry);
// Project an explicit historical MiB input after file/environment/CLI loading.
rex::Result<void> ApplyLegacyLoggingInputs();
// Consume historical category levels before the SDK reads the same config file.
rex::Result<void> RegisterLegacyLogCategoryConfig(rex::SettingsRegistry& registry,
                                                const std::filesystem::path& file);

}  // namespace rb_blitz::settings
