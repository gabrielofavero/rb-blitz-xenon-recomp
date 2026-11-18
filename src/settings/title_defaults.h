// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <rex/settings_registry.h>

namespace rb_blitz::settings {
inline bool CanSetTitleDefault(const rex::SettingBase& setting) {
  return setting.GetLayer() <= rex::SettingLayer::kTitle;
}

// Defaults may refine one another during configuration; explicit sources remain authoritative.
inline rex::Result<void> SetTitleDefault(rex::SettingBase& setting, std::string_view value) {
  if (!CanSetTitleDefault(setting)) return rex::Ok();
  return setting.SetText(rex::SettingLayer::kTitle, value);
}

inline rex::Result<void> SetTitleDefault(rex::SettingsRegistry& registry,
                                       std::string_view name, std::string_view value) {
  if (auto setting = registry.Find(name)) return SetTitleDefault(**setting, value);
  return registry.SetText(rex::SettingLayer::kTitle, name, value);
}
}  // namespace rb_blitz::settings
