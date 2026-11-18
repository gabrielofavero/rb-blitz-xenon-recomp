// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "settings/runtime_values.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::graphics {
// Include every registered graphics input: feature selection and translator
// switches can change pipelines even when no hand-maintained key names them.
// Active values describe running pipelines; staged restart edits do not.
inline std::string PreparationSettingsIdentity(std::string_view resolved_driver) {
  std::vector<std::string> names;
  for (const auto* setting : REX_SETTINGS().Enumerate("gpu"))
    names.emplace_back(setting->GetName());
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  std::string identity = "graphics-settings-v2;";
  const auto append = [&](std::string_view value) {
    identity.append(std::to_string(value.size())).push_back(':');
    identity.append(value);
  };
  append(resolved_driver);
  for (const auto& name : names) {
    const auto setting = settings::InspectRuntimeSetting(name);
    if (!setting.registered) continue;
    append(setting.canonical_name);
    append(setting.active_value);
  }
  return identity;
}
}  // namespace rb_blitz::graphics
