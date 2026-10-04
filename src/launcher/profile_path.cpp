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

#include "launcher/profile_path.h"

#include <cstdlib>

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

}  // namespace

bool IsPortable(const fs::path& executable_dir) {
  if (executable_dir.empty()) {
    return false;
  }
  std::error_code ec;
  return fs::exists(executable_dir / kPortableMarkerName, ec);
}

fs::path ResolveProfilePath(const ProfilePathInputs& inputs) {
  if (!inputs.command_line_value.empty()) {
    return fs::path(inputs.command_line_value);
  }
  if (!inputs.environment_value.empty()) {
    return fs::path(inputs.environment_value);
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
