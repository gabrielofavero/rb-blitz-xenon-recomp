// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Profile path resolution (see profile_path.h and docs/plans/launcher-plan.md D2). The
// two process-environment reads are deliberately in one place, so a test can supply the
// same inputs directly instead of mutating the environment.
//
// std::getenv is used deliberately: the alternative MSVC offers is _dupenv_s, and the
// alternative this project offers is rex::filesystem, which is the SDK dependency the
// module exists to avoid. Reading an environment variable is the whole operation, so the
// CRT's deprecation is silenced here rather than swapped for something non-portable -
// the same call the installer makes for _wfopen.

#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

// The settings-folder pointer lives in the default folder rather than beside the settings:
// `app_data_dir` is user-writable and expected to exist, so it is the one place that is always
// there, whichever folder the settings were moved to.
#include "launcher/profile_path.h"

#include <cstdlib>
#include <fstream>

namespace rb_blitz::launcher {

namespace fs = std::filesystem;

namespace {

fs::path FromEnvironment(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return {};
  }
  return fs::path(value);
}

fs::path SettingsDirPointerPath(const fs::path& app_data_dir) {
  if (app_data_dir.empty()) {
    return {};
  }
  return app_data_dir / kProfileDirName / kSettingsDirFileName;
}

}  // namespace

bool IsPortable(const fs::path& executable_dir) {
  if (executable_dir.empty()) {
    return false;
  }
  std::error_code ec;
  return fs::exists(executable_dir / kPortableMarkerName, ec);
}

fs::path DefaultProfileDir(const fs::path& app_data_dir) {
  return app_data_dir.empty() ? fs::path{} : app_data_dir / kProfileDirName;
}

fs::path ReadSettingsDirPointer(const fs::path& app_data_dir) {
  const fs::path pointer = SettingsDirPointerPath(app_data_dir);
  if (pointer.empty()) {
    return {};
  }
  std::ifstream stream(pointer, std::ios::binary);
  if (!stream) {
    return {};
  }
  std::string text;
  std::getline(stream, text);
  // Trailing whitespace and a stray CR (the file is written on whichever platform made it)
  // are not part of a path.
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
    text.pop_back();
  }
  if (text.empty()) {
    return {};
  }
  return fs::path(text);
}

bool WriteSettingsDirPointer(const fs::path& app_data_dir, const fs::path& dir,
                             std::string* error) {
  const fs::path pointer = SettingsDirPointerPath(app_data_dir);
  if (pointer.empty()) {
    *error = "there is no app data folder to record the settings location in";
    return false;
  }
  std::error_code code;
  // The default has no pointer, so choosing it is the same operation as moving the settings
  // back: one state on disk, not two spellings of it.
  if (dir.empty() || dir == DefaultProfileDir(app_data_dir)) {
    code.clear();
    fs::remove(pointer, code);
    if (code) {
      *error = "cannot remove " + pointer.string() + ": " + code.message();
      return false;
    }
    return true;
  }
  fs::create_directories(pointer.parent_path(), code);
  std::ofstream stream(pointer, std::ios::binary | std::ios::trunc);
  if (!stream) {
    *error = "cannot write " + pointer.string();
    return false;
  }
  stream << dir.string() << "\n";
  if (!stream) {
    *error = "cannot write " + pointer.string();
    return false;
  }
  return true;
}

fs::path ResolveProfilePath(const ProfilePathInputs& inputs) {
  if (!inputs.command_line_value.empty()) {
    return fs::path(inputs.command_line_value);
  }
  if (!inputs.environment_value.empty()) {
    return fs::path(inputs.environment_value);
  }
  if (fs::path chosen = ReadSettingsDirPointer(inputs.app_data_dir); !chosen.empty()) {
    return chosen / kProfileFileName;
  }
  if (IsPortable(inputs.executable_dir)) {
    return inputs.executable_dir / kProfileFileName;
  }
  if (!inputs.app_data_dir.empty()) {
    return inputs.app_data_dir / kProfileDirName / kProfileFileName;
  }
  return {};
}

fs::path RoamingAppDataDir() {
  if (fs::path appdata = FromEnvironment("APPDATA"); !appdata.empty()) {
    return appdata;
  }
  if (fs::path config_home = FromEnvironment("XDG_CONFIG_HOME"); !config_home.empty()) {
    return config_home;
  }
  if (fs::path home = FromEnvironment("HOME"); !home.empty()) {
    return home / ".config";
  }
  return {};
}

ProfilePathInputs ProfilePathInputsFromEnvironment(fs::path executable_dir) {
  ProfilePathInputs inputs;
  if (const char* override = std::getenv(kProfileEnvVar);
      override != nullptr && *override != '\0') {
    inputs.environment_value = override;
  }
  inputs.executable_dir = std::move(executable_dir);
  inputs.app_data_dir = RoamingAppDataDir();
  return inputs;
}

}  // namespace rb_blitz::launcher
