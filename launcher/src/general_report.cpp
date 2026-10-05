// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the General tab's text report (launcher/src/general_report.h, B1).

#include "general_report.h"

#include "path_validate.h"
#include "schema_view.h"

namespace rb_blitz::launcher {
namespace {

const char* StateName(UltimateState state) {
  switch (state) {
    case UltimateState::kReady:
      return "ready";
    case UltimateState::kAlsoPresent:
      return "also-present";
    case UltimateState::kMissing:
      return "missing";
    case UltimateState::kDamaged:
      return "damaged";
  }
  return "unknown";
}

const std::string& ProfilePathValue(const Profile& profile, std::string_view key) {
  return key == "user_data_root" ? profile.user_data_dir : profile.dlc_dir;
}

}  // namespace

std::string DescribeGeneral(const ProfileLoadResult& load, const GameRoots& roots) {
  const Profile& profile = load.profile;
  const UltimateState state = DetectUltimateState(roots.game_root);

  std::string out;
  out += "profile        : ";
  switch (load.status) {
    case ProfileStatus::kOk:
      out += "ok";
      break;
    case ProfileStatus::kMissingFile:
      out += "not there yet";
      break;
    case ProfileStatus::kMalformed:
      out += "MALFORMED, left alone: ";
      out += load.error;
      break;
  }
  out += "\n";
  out += "game root      : ";
  out += roots.game_root.string();
  out += roots.game_root_found ? " (found)\n" : " (not found)\n";
  out += "ultimate root  : ";
  out += roots.ultimate_root.string();
  out += "\n";
  out += "ultimate       : ";
  out += StateName(state);
  out += "\n";
  const std::string_view note = UltimateStateText(state);
  if (!note.empty()) {
    out += "ultimate note  : ";
    out += note;
    out += "\n";
  }
  out += "target         : ";
  out += LaunchTargetName(FallbackTarget(profile.target, state));
  out += " (stored ";
  out += LaunchTargetName(profile.target);
  out += ")\n";

  // The rows in the table's order, so this report cannot disagree with the tab about which
  // rows exist or what they are called.
  const TabLayout layout = BuildTabLayout(settings::Tab::kGeneral);
  for (const LayoutGroup& group : layout.groups) {
    for (const LayoutRow& row : group.rows) {
      if (row.setting == nullptr) {
        out += "group          : ";
        out += row.note_group->name;
        out += " (unavailable)\n";
        continue;
      }
      const settings::Setting& setting = *row.setting;
      if (setting.kind != settings::Kind::kPathDir && setting.kind != settings::Kind::kPathFile) {
        continue;  // the target row is already reported above
      }
      const std::string& value = ProfilePathValue(profile, setting.key);
      out += "row ";
      out += setting.key;
      out += " = ";
      out += value.empty() ? "(empty)" : value;
      out += "\n";
      const PathVerdict verdict = ValidatePathValue(setting.validate, value, roots.game_root);
      if (!verdict.ok) {
        out += "    refused: ";
        out += verdict.reason;
        out += "\n";
      } else if (!verdict.note.empty()) {
        out += "    ok: ";
        out += verdict.note;
        out += "\n";
      } else {
        out += "    ok\n";
      }
    }
  }
  return out;
}

}  // namespace rb_blitz::launcher
