// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/profile/profile_apply.h for why the game reads Controller Settings' profile itself. This
// file is the two operations that need: find the file, and put its `[settings]` into the cvar
// registry at the config rank.

#include "profile/profile_apply.h"

#include <filesystem>
#include <string>
#include <utility>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "profile/profile_path.h"
#include "settings/cvars.h"

// The cvar keeps its name: `--launcher_profile=<path>` is the flag the game and every harness
// spell, and the SDK maps a flag to a cvar of the same name.
REXCVAR_DEFINE_STRING(launcher_profile, "", "Runtime",
                      "Path to the settings profile, when Controller Settings started this "
                      "process; empty resolves the file it would write itself");

// §8's narrowing, now the default (P11b cutover, owner decision 2026-10-13): the game keeps its
// own settings file, its Help & Options edits it, and the profile's other `[settings]`
// rows are inert preservation data. The two facts that used to hold this back are gone - the
// store exists (P11a) and the installer/paths.toml own the DLC and save locations (P7) - so a
// profile row other than the controller rows and the host-only host rows is no longer applied.
// A build that needs the old behaviour can set this off again from the command line.
REXCVAR_DEFINE_BOOL(profile_controller_rows_only, true, "Compatibility",
                    "Apply only the controller rows (mouse_ui_nav, input_backend, "
                    "keyboard_support, gameplay_mapping_controller, "
                    "gameplay_mapping_keyboard_mouse) and the host-only host rows (monitor, "
                    "safe areas, dither, overscan cutoff, audio) from the profile; every other "
                    "row is preserved in the file and not applied");

namespace rb_blitz::profile {

std::optional<Profile> ReadProfile(std::string* why) {
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

ProfileApplyReport ApplyProfileSettings() {
  ProfileApplyReport report;
  const std::optional<Profile> profile = ReadProfile(&report.unreadable);
  if (!profile) {
    return report;
  }
  report.found = true;
  report.rows = profile->settings.size();

  // §8: once the game keeps its own settings file, the profile is a source of controller
  // state and of the host rows no in-game surface can edit, and nothing else. The rows that are
  // left out are not dropped from the file - Controller Settings still writes them, and a person's hand
  // edits stay - they simply stop being applied.
  const bool narrowed = REXCVAR_GET(profile_controller_rows_only);
  const std::vector<ProfileSetting> rows =
      narrowed ? settings::ProfileRowsTheGameApplies(profile->settings) : profile->settings;
  if (narrowed) {
    report.rows_ignored = report.rows - rows.size();
  }

  for (const ProfileSetting& setting : rows) {
    if (rex::cvar::SetFlagFromConfig(setting.key, setting.value)) {
      ++report.applied;
    } else {
      // The row is either a cvar this build does not have (a profile from a newer Controller Settings, or a
      // hand-edited key) or a value the flag's own constraints refuse. Either way the game keeps
      // the value something else chose and says which row it could not take.
      report.refused.push_back(setting.key + " = " + setting.value);
    }
  }
  return report;
}

void LogProfileApplyReport(const ProfileApplyReport& report) {
  if (!report.unreadable.empty()) {
    REXLOG_WARN("Controller Settings: {}", report.unreadable);
  }
  if (!report.found) {
    return;
  }
  for (const std::string& row : report.refused) {
    REXLOG_WARN("Controller Settings: profile row {} was refused by this build", row);
  }
  if (report.rows_ignored != 0) {
    REXLOG_INFO(
        "Controller Settings: {} retired setting(s) in the profile are inert (the game keeps its own "
        "settings file now)",
        report.rows_ignored);
  }
  REXLOG_INFO("Controller Settings: applied {} of {} setting(s) from the profile", report.applied,
              report.rows);
}

}  // namespace rb_blitz::profile
