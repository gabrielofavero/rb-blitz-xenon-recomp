// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The migration in src/settings/migration.h. It reads the profile as data (it does
// not rewrite it): the controller tables, the retired keys and everything unknown stay where
// they are so a person's hand edits survive, and the runtime simply stops applying the
// retired rows once it narrows to the controller allowlist (P8). What moves into
// settings.toml is the supported native subset, translated by the explicit rules in the
// parent plan §4.3.

#include "settings/migration.h"

#include <string>
#include <utility>
#include <vector>

#include "settings/schema.h"

namespace rb_blitz::settings {

namespace fs = std::filesystem;
using rb_blitz::profile::LaunchTargetName;
using rb_blitz::profile::Profile;
using rb_blitz::profile::ProfileSetting;

namespace {

// The legacy rows P3 knows are retired and must not become a new active preference.
bool IsRetiredKey(std::string_view key) {
  static constexpr std::string_view kRetired[] = {
      "enhancements_rename_mod_settings",
      "enhancements_menu_dlc_song_count",
      "fullscreen",
      "monitor",
      "present_letterbox",
      "present_safe_area_x",
      "present_safe_area_y",
      "present_allow_overscan_cutoff",
      "present_dither",
      "present_effect",
      "audio_mute",
      "guide_button",
      "keybind_guide",
      "unlockall",
  };
  for (std::string_view retired : kRetired) {
    if (retired == key) {
      return true;
    }
  }
  return false;
}

Origin OriginFor(SettingId id, const Value& value) {
  const schema::SchemaRow* row = schema::FindRow(id);
  if (row != nullptr && ValueEquals(row->descriptor.compiled_default, value)) {
    return Origin::kDefault;
  }
  return Origin::kManual;
}

// True when the value carries no behavior change against the row's compiled default, so it
// is deliberately not written. The origin rule is untouched: it never downgrades a manual
// choice, and a legacy row that happens to sit at the default keeps first-run policy usable.
bool AtCompiledDefault(SettingId id, const Value& value) {
  const schema::SchemaRow* row = schema::FindRow(id);
  return row != nullptr && ValueEquals(row->descriptor.compiled_default, value);
}

bool ParseLegacyBool(const ProfileSetting& setting, bool* out) {
  if (setting.value == "true") {
    *out = true;
    return true;
  }
  if (setting.value == "false") {
    *out = false;
    return true;
  }
  return false;
}

bool ParseLegacyInt(const ProfileSetting& setting, int64_t* out) {
  if (setting.value.empty()) {
    return false;
  }
  std::size_t i = 0;
  bool negative = false;
  if (setting.value.front() == '-' || setting.value.front() == '+') {
    negative = setting.value.front() == '-';
    i = 1;
  }
  if (i >= setting.value.size()) {
    return false;
  }
  int64_t value = 0;
  for (; i < setting.value.size(); ++i) {
    if (setting.value[i] < '0' || setting.value[i] > '9') {
      return false;
    }
    value = value * 10 + (setting.value[i] - '0');
  }
  *out = negative ? -value : value;
  return true;
}

std::vector<std::string> SplitCsv(const std::string& text) {
  std::vector<std::string> out;
  std::string token;
  for (const char character : text) {
    if (character == ',') {
      if (!token.empty()) {
        out.push_back(token);
      }
      token.clear();
      continue;
    }
    if (character != ' ' && character != '\t') {
      token += character;
    }
  }
  if (!token.empty()) {
    out.push_back(token);
  }
  return out;
}

Value BoolValue(bool value) {
  Value out;
  out.kind = ValueKind::kBool;
  out.boolean = value;
  return out;
}

Value IntValue(int32_t value) {
  Value out;
  out.kind = ValueKind::kInt;
  out.integer = value;
  return out;
}

Value EnumValue(std::string token) {
  Value out;
  out.kind = ValueKind::kEnum;
  out.text = std::move(token);
  return out;
}

Value ResolutionValue(int32_t width, int32_t height) {
  Value out;
  out.kind = ValueKind::kResolution;
  out.width = width;
  out.height = height;
  return out;
}

Value ListValue(std::vector<std::string> list) {
  Value out;
  out.kind = ValueKind::kList;
  out.list = std::move(list);
  return out;
}

// Import a value only when the row is not already in settings.toml and the value is not
// already the compiled default. Returns true when the value was imported.
bool Import(Store& store, SettingId id, Value value, MigrationResult* result) {
  if (store.document().values.count(id) != 0) {
    return false;
  }
  if (AtCompiledDefault(id, value)) {
    return false;
  }
  store.MarkChanged(id, value, OriginFor(id, value));
  ++result->stats.imported;
  return true;
}

constexpr const char* kLegacyHiddenMenuDefault =
    "splash_leaderboard,splash_achievements,splash_dlc";

std::string FilteringToken(int64_t code) {
  switch (code) {
    case -1:
      return "native";
    case 0:
      return "none";
    case 1:
      return "1x";
    case 2:
      return "2x";
    case 3:
      return "4x";
    case 4:
      return "8x";
    case 5:
      return "16x";
    default:
      return {};
  }
}

}  // namespace

MigrationResult Migrate(const Profile& legacy, Store& store, const PathEntry& existing_paths) {
  return Migrate(legacy, store, existing_paths, MigrationOptions{});
}

MigrationResult Migrate(const Profile& legacy, Store& store, const PathEntry& existing_paths,
                       const MigrationOptions& options) {
  MigrationResult result;
  const StorePaths& store_paths = store.paths();

  if (!options.legacy_usable) {
    result.status = MigrationStatus::kMalformedLegacy;
    result.diagnostics.push_back(
        "the profile could not be read; it is left untouched and no settings were "
        "imported");
    return result;
  }

  std::error_code ec;
  const bool legacy_file_exists =
      !store_paths.legacy_profile.empty() && fs::exists(store_paths.legacy_profile, ec);
  const bool legacy_present = legacy_file_exists || !legacy.source_text.empty();
  if (!legacy_present) {
    result.status = MigrationStatus::kNoLegacy;
    return result;
  }

  // A byte-identical backup before the first write; a later run keeps the pristine copy.
  if (legacy_file_exists) {
    fs::path backup = store_paths.legacy_profile;
    backup += ".pre-migration.bak";
    if (!fs::exists(backup, ec)) {
      std::string text;
      bool existed = false;
      std::string error;
      if (ReadFileText(store_paths.legacy_profile, &text, &existed) && existed &&
          !WriteFileAtomic(backup, text, &error)) {
        result.status = MigrationStatus::kInterruptedResumed;
        result.diagnostics.push_back("could not back up the profile: " + error);
        return result;
      }
    }
    result.backup = backup;
  }

  const bool already = store.document().migration_imported;

  // --- target ---------------------------------------------------------------
  {
    Value target = EnumValue(std::string(LaunchTargetName(legacy.target)));
    if (Import(store, SettingId::kTarget, target, &result)) {
      result.stats.target_imported = true;
    }
  }

  // The AA pair is one row in the new model, so it is handled together and its two legacy
  // keys are skipped in the loop below.
  bool have_swap = false;
  bool have_msaa = false;
  std::string swap_value = "none";
  bool msaa_value = true;
  if (const ProfileSetting* setting = legacy.FindSetting("swap_post_effect")) {
    have_swap = true;
    swap_value = setting->value;
  }
  if (const ProfileSetting* setting = legacy.FindSetting("native_2x_msaa")) {
    have_msaa = true;
    bool parsed = false;
    if (ParseLegacyBool(*setting, &parsed)) {
      msaa_value = parsed;
    }
  }
  if (have_swap || have_msaa) {
    std::string token;
    if (swap_value == "fxaa") {
      token = "fxaa";
    } else if (swap_value == "fxaa_extreme") {
      token = "fxaa_extreme";
    } else {
      token = msaa_value ? "native" : "none";
    }
    if (msaa_value && (swap_value == "fxaa" || swap_value == "fxaa_extreme")) {
      result.diagnostics.push_back(
          "the legacy profile had native anti-aliasing and FXAA on at once; the new "
          "anti-aliasing row keeps the FXAA choice and the simultaneous native+FXAA "
          "combination is intentionally lost");
    }
    if (Import(store, SettingId::kAntiAliasing, EnumValue(token), &result)) {
      ++result.stats.translated;
    }
  }

  // --- the supported [settings] subset --------------------------------------
  for (const ProfileSetting& setting : legacy.settings) {
    const std::string& key = setting.key;

    if (key == "swap_post_effect" || key == "native_2x_msaa") {
      continue;  // handled with the AA pair above
    }
    if (key == "user_data_root" || key == "dlc_root") {
      continue;  // handled as paths below
    }
    if (IsRetiredKey(key)) {
      ++result.stats.dropped;
      continue;
    }
    if (key == "input_backend") {
      ++result.stats.preserved;  // controller allowlist; stays in launcher.toml
      continue;
    }

    bool recognised = true;
    if (key == "enhancements_dlc_cache") {
      bool value = false;
      if (ParseLegacyBool(setting, &value)) {
        Import(store, SettingId::kCacheDlcLibrary, BoolValue(value), &result);
      }
    } else if (key == "dlc_scan_threads") {
      int64_t value = 0;
      if (ParseLegacyInt(setting, &value)) {
        if (value < 1) {
          value = 1;
        }
        Import(store, SettingId::kDlcScanThreads, IntValue(static_cast<int32_t>(value)), &result);
      }
    } else if (key == "enhancements_menu_dlc_songs") {
      bool value = false;
      if (ParseLegacyBool(setting, &value)) {
        Import(store, SettingId::kDlcMenuSongs, BoolValue(value), &result);
      }
    } else if (key == "enhancements_skip_offline_dialog") {
      bool value = false;
      if (ParseLegacyBool(setting, &value)) {
        Import(store, SettingId::kHideOfflinePrompts, BoolValue(value), &result);
      }
    } else if (key == "enhancements_hide_menu_options") {
      bool value = false;
      if (ParseLegacyBool(setting, &value)) {
        Import(store, SettingId::kHideMainMenu, BoolValue(value), &result);
      }
    } else if (key == "enhancements_hidden_menu_options") {
      // The old free-form CSV: an empty string meant the default offline trio, which the new
      // model spells as an absent selection. Preserve the trio's behavior by importing
      // nothing, and let the default selection apply.
      if (!setting.value.empty() && setting.value != kLegacyHiddenMenuDefault) {
        Import(store, SettingId::kHiddenMainMenu, ListValue(SplitCsv(setting.value)), &result);
      }
    } else if (key == "vsync") {
      bool value = false;
      if (ParseLegacyBool(setting, &value)) {
        Import(store, SettingId::kVsync, BoolValue(value), &result);
      }
    } else if (key == "gpu_backend") {
      if (setting.value == "d3d12" || setting.value == "vulkan") {
        Import(store, SettingId::kRenderer, EnumValue(setting.value), &result);
      }
    } else if (key == "resolution_scale") {
      int64_t scale = 0;
      if (ParseLegacyInt(setting, &scale) && scale >= 1 && scale <= 16) {
        const Value value =
            ResolutionValue(static_cast<int32_t>(1280 * scale), static_cast<int32_t>(720 * scale));
        if (Import(store, SettingId::kResolution, value, &result)) {
          ++result.stats.translated;
        }
      }
    } else if (key == "anisotropic_override") {
      int64_t code = 0;
      if (ParseLegacyInt(setting, &code)) {
        const std::string token = FilteringToken(code);
        if (!token.empty() && Import(store, SettingId::kFiltering, EnumValue(token), &result)) {
          ++result.stats.translated;
        }
      }
    } else {
      recognised = false;
    }

    if (!recognised) {
      ++result.stats.preserved;  // an unknown key stays in launcher.toml untouched
    }
  }

  // --- paths ---------------------------------------------------------------
  PathEntry entry = existing_paths;
  bool paths_changed = false;
  if (options.apply_paths) {
    std::string user_data_root;
    if (const ProfileSetting* setting = legacy.FindSetting("user_data_root")) {
      user_data_root = setting->value;
    } else {
      user_data_root = legacy.user_data_dir;
    }
    if (!user_data_root.empty() && entry.user_data_root.empty()) {
      entry.user_data_root = user_data_root;
      paths_changed = true;
    }

    std::string dlc_root;
    if (const ProfileSetting* setting = legacy.FindSetting("dlc_root")) {
      dlc_root = setting->value;
    } else {
      dlc_root = legacy.dlc_dir;
    }
    if (!dlc_root.empty() && entry.dlc_root.empty()) {
      entry.dlc_root = dlc_root;
      paths_changed = true;
    }
  } else {
    result.diagnostics.push_back(
        "the profile belongs to a different installation; its path fields were not "
        "applied");
  }

  // --- writes --------------------------------------------------------------
  store.MarkMigrationImported();
  const SaveOutcome outcome = store.Save();
  result.wrote_settings = outcome.wrote;
  if (outcome.status == CommitStatus::kConflict || outcome.status == CommitStatus::kInvalid ||
      outcome.status == CommitStatus::kIoError) {
    for (const std::string& error : outcome.errors) {
      result.diagnostics.push_back(error);
    }
    result.status = MigrationStatus::kInterruptedResumed;
    return result;
  }

  if (paths_changed) {
    if (store_paths.paths_file.empty()) {
      result.diagnostics.push_back("cannot write paths.toml: the installation folder is unknown");
    } else {
      std::string error;
      if (SavePathFile(store_paths.paths_file, entry, &error)) {
        result.wrote_paths = true;
        result.stats.paths_imported = true;
      } else {
        result.diagnostics.push_back(error);
      }
    }
  }

  if (!result.wrote_settings && !result.wrote_paths) {
    result.status = MigrationStatus::kAlreadyCurrent;
  } else if (already && result.stats.imported == 0) {
    result.status = MigrationStatus::kInterruptedResumed;
  } else {
    result.status = MigrationStatus::kMigrated;
  }
  return result;
}

}  // namespace rb_blitz::settings
