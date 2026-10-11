// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The path file and native settings location in src/settings/paths.h. The reader is
// deliberately a thin wrapper over the store's TOML editor: paths.toml is small, but the
// installer and a hand edit both write it, so unknown keys and comments must survive a
// rewrite and a failed write must keep the file it had.

#include "settings/paths.h"

#include <string>

#include "settings/store.h"

namespace rb_blitz::settings {

namespace fs = std::filesystem;

fs::path ResolvePathFile(const fs::path& executable_dir) {
  if (executable_dir.empty()) {
    return {};
  }
  return executable_dir / kPathFileName;
}

PathFileResult LoadPathFile(const fs::path& file) {
  PathFileResult result;
  std::string text;
  bool existed = false;
  if (!ReadFileText(file, &text, &existed)) {
    result.status = PathFileResult::Status::kMalformed;
    result.error = "cannot read " + file.string();
    return result;
  }
  if (!existed) {
    result.status = PathFileResult::Status::kMissing;
    return result;
  }

  bool ok = false;
  std::string error;
  toml::Document doc = toml::Document::Parse(text, &ok, &error);
  if (!ok) {
    result.status = PathFileResult::Status::kMalformed;
    result.error = file.string() + ": " + error;
    return result;
  }

  if (const toml::Item version = doc.Get("", "schema_version"); version.found) {
    int64_t parsed = 0;
    if (!toml::ParseBareInt(version.value, &parsed)) {
      result.status = PathFileResult::Status::kMalformed;
      result.error = file.string() + ": schema_version is not an integer";
      return result;
    }
    if (parsed != kPathSchemaVersion) {
      result.status = PathFileResult::Status::kMalformed;
      result.error = file.string() + ": schema_version " + std::to_string(parsed) +
                     " is not supported";
      return result;
    }
    result.schema_version = static_cast<int32_t>(parsed);
  } else {
    result.status = PathFileResult::Status::kMalformed;
    result.error = file.string() + ": no schema_version";
    return result;
  }

  if (const toml::Item item = doc.Get("paths", "dlc_root"); item.found && item.scalar) {
    result.paths.dlc_root = item.value;
  }
  if (const toml::Item item = doc.Get("paths", "user_data_root"); item.found && item.scalar) {
    result.paths.user_data_root = item.value;
  }

  result.status = PathFileResult::Status::kOk;
  return result;
}

bool SavePathFile(const fs::path& file, const PathEntry& entry, std::string* error) {
  std::string text;
  bool existed = false;
  if (!ReadFileText(file, &text, &existed)) {
    if (error != nullptr) {
      *error = "cannot read " + file.string();
    }
    return false;
  }

  bool ok = true;
  std::string parse_error;
  toml::Document doc = toml::Document::Parse(existed ? text : std::string(), &ok, &parse_error);
  if (!ok) {
    if (error != nullptr) {
      *error = file.string() + ": " + parse_error;
    }
    return false;
  }

  doc.SetIfAbsent("", "schema_version", std::to_string(kPathSchemaVersion));
  if (entry.dlc_root.empty()) {
    doc.Erase("paths", "dlc_root");
  } else {
    doc.Set("paths", "dlc_root", toml::RenderLiteral(entry.dlc_root));
  }
  if (entry.user_data_root.empty()) {
    doc.Erase("paths", "user_data_root");
  } else {
    doc.Set("paths", "user_data_root", toml::RenderLiteral(entry.user_data_root));
  }

  if (!doc.dirty()) {
    return true;
  }
  return WriteFileAtomic(file, doc.Render(), error);
}

fs::path ResolvePathValue(const std::string& value, const fs::path& file, const fs::path& fallback) {
  if (value.empty()) {
    return fallback;
  }
  fs::path path = fs::path(value);
  if (path.is_relative() && !file.empty()) {
    path = file.parent_path() / path;
  }
  return path;
}

fs::path SettingsFileForProfile(const fs::path& legacy_profile) {
  if (legacy_profile.empty()) {
    return {};
  }
  if (legacy_profile.filename() == rb_blitz::launcher::kProfileFileName) {
    return legacy_profile.parent_path() / "settings.toml";
  }
  std::string stem = legacy_profile.stem().string();
  if (stem.empty()) {
    stem = legacy_profile.filename().string();
  }
  return legacy_profile.parent_path() / (stem + ".game-settings.toml");
}

fs::path ResolveSettingsFile(const rb_blitz::launcher::ProfilePathInputs& inputs) {
  return SettingsFileForProfile(rb_blitz::launcher::ResolveProfilePath(inputs));
}

}  // namespace rb_blitz::settings
