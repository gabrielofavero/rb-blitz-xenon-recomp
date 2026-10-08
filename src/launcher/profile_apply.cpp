// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/launcher/profile_apply.h for why the game reads the launcher's profile itself. This
// file is the two operations that need: find the file, and put its `[settings]` into the cvar
// registry at the config rank.

#include "launcher/profile_apply.h"

#include <filesystem>
#include <string>
#include <utility>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "launcher/profile_path.h"

REXCVAR_DEFINE_STRING(launcher_profile, "", "Runtime",
                      "Path to the launcher's profile, when the launcher started this process; "
                      "empty resolves the file the launcher itself would write");

namespace rb_blitz::launcher {

std::optional<Profile> ReadLauncherProfile(std::string* why) {
  if (why != nullptr) {
    why->clear();
  }
  ProfilePathInputs inputs = ProfilePathInputsFromEnvironment(
      rex::filesystem::GetExecutableFolder());
  inputs.command_line_value = REXCVAR_GET(launcher_profile);

  const std::filesystem::path path = ResolveProfilePath(inputs);
  if (path.empty()) {
    return std::nullopt;
  }
  ProfileLoadResult loaded = LoadProfile(path);
  if (!loaded.usable()) {
    if (why != nullptr) {
      *why = path.string() + " could not be read: " + loaded.error;
    }
    return std::nullopt;
  }
  return std::move(loaded.profile);
}

ProfileApplyReport ApplyLauncherProfileSettings() {
  ProfileApplyReport report;
  const std::optional<Profile> profile = ReadLauncherProfile(&report.unreadable);
  if (!profile) {
    return report;
  }
  report.found = true;
  report.rows = profile->settings.size();

  for (const ProfileSetting& setting : profile->settings) {
    if (rex::cvar::SetFlagFromConfig(setting.key, setting.value)) {
      ++report.applied;
    } else {
      // The row is either a cvar this build does not have (a profile from a newer launcher, or a
      // hand-edited key) or a value the flag's own constraints refuse. Either way the game keeps
      // the value something else chose and says which row it could not take.
      report.refused.push_back(setting.key + " = " + setting.value);
    }
  }
  return report;
}

void LogProfileApplyReport(const ProfileApplyReport& report) {
  if (!report.unreadable.empty()) {
    REXLOG_WARN("launcher: {}", report.unreadable);
  }
  if (!report.found) {
    return;
  }
  for (const std::string& row : report.refused) {
    REXLOG_WARN("launcher: profile row {} was refused by this build", row);
  }
  REXLOG_INFO("launcher: applied {} of {} setting(s) from the launcher profile", report.applied,
              report.rows);
}

}  // namespace rb_blitz::launcher
