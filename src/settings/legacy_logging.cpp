// SPDX-License-Identifier: GPL-2.0-only
#include "settings/runtime_values.h"
#include <algorithm>
#include <functional>
#include <toml++/toml.hpp>

namespace rb_blitz::settings {
namespace {
std::string CategoryName(std::string_view name) {
  const auto dot = name.find('.');
  const auto root = name.substr(0, dot);
  const auto mapped = root == "fs" ? "filesystem" : root == "krnl" ? "kernel" : root == "sys" ? "system" : root;
  return std::string(mapped) + (dot == name.npos ? "" : std::string(name.substr(dot)));
}

std::string LegacyLevel(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return c >= 'A' && c <= 'Z' ? char(c-'A'+'a') : char(c); });
  if (value == "warning") value = "warn";
  if (value == "err") value = "error";
  for (const auto* level : {"trace", "debug", "info", "warn", "error", "critical", "off"})
    if (value == level) return value;
  return {};
}
}  // namespace

rex::Result<void> RegisterLegacyLogCategoryConfig(rex::SettingsRegistry& registry,
                                                const std::filesystem::path& file) {
  if (file.empty()) return rex::Ok();
  std::error_code error;
  if (!std::filesystem::exists(file, error)) {
    if (error) return rex::Err(rex::ErrorCategory::kIO, file.string()+": "+error.message());
    return rex::Ok();
  }
  toml::table parsed;
  try { parsed = toml::parse_file(file.string()); }
  catch (const toml::parse_error& e) {
    return rex::Err(rex::ErrorCategory::kConfig, file.string()+": "+std::string(e.description()));
  }
  const auto* log = parsed["log"].as_table();
  const auto* levels = log ? (*log)["levels"].as_table() : nullptr;
  if (!levels) return rex::Ok();
  const auto* canonical = (*log)["categories"].as_table();
  std::function<rex::Result<void>(const toml::table&, const std::string&)> consume;
  consume = [&](const toml::table& table, const std::string& prefix)->rex::Result<void> {
    for (const auto& [key, node] : table) {
      const auto name = prefix+std::string(key.str());
      if (node.is_array())
        return rex::Err(rex::ErrorCategory::kConfig, file.string()+": unsupported array at "+name+
                        "; use a string level under [log.categories]");
      if (const auto* child = node.as_table()) REX_TRY_VOID(consume(*child, name+"."));
      else registry.AddAlias(name, "");
    }
    return rex::Ok();
  };
  // Consume only the historical category table. All other config validation and
  // canonical category settings continue through the SDK's normal loader.
  REX_TRY_VOID(consume(*levels, "log.levels."));
  for (const auto& [key, node] : *levels) {
    const auto* text = node.as_string();
    if (!text) continue; // The historical logger ignored non-string values.
    const auto category = CategoryName(key.str());
    if (category != key.str()) {
      if (const auto* current = levels->get_as<std::string>(category);
          current && !LegacyLevel(current->get()).empty()) continue;
    }
    if (canonical && (canonical->contains(category) || parsed.at_path("log.categories."+category))) continue;
    const auto level = LegacyLevel(text->get());
    if (level.empty()) continue; // Invalid historical levels never changed the logger.
    const auto name = "log.categories."+category;
    if (auto current = registry.Find(name); current && (*current)->GetLayer() >= rex::SettingLayer::kFile)
      continue;
    REX_TRY_VOID(registry.SetText(rex::SettingLayer::kFile, name, level));
  }
  return rex::Ok();
}
}  // namespace rb_blitz::settings
