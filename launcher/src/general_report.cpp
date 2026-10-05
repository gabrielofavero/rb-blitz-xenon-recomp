// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the General tab's text report (launcher/src/general_report.h, B1).

#include "general_report.h"

#include "path_validate.h"
#include "row_ui.h"
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

std::string DescribeGeneral(const ProfileSession& session, const GameRoots& roots) {
  const Profile& profile = session.profile();
  const UltimateState state = DetectUltimateState(roots.game_root);

  std::string out;
  out += "settings file  : ";
  out += session.path().empty() ? "(nowhere to save)" : session.path().string();
  out += session.portable() ? " (portable)\n" : "\n";
  out += "profile        : ";
  if (!session.CanSave()) {
    out += "MALFORMED, left alone: ";
    out += session.Refusal();
  } else if (session.path().empty()) {
    out += "nowhere to keep it";
  } else if (session.Dirty()) {
    out += "unsaved changes";
  } else {
    out += "saved";
  }
  out += "\n";
  const ResetPlan reset = session.WhatResetWouldRemove();
  out += "reset would    : ";
  if (reset.empty()) {
    out += "remove nothing\n";
  } else {
    out += "remove ";
    bool first = true;
    for (const std::string& key : reset.settings) {
      out += first ? "" : ", ";
      out += key;
      first = false;
    }
    for (const std::string& item : reset.launch) {
      out += first ? "" : ", ";
      out += item;
      first = false;
    }
    out += "\n";
  }
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
      // B4: the same precedence line the row's badge draws, when the game's own file decides it.
      const RowOverride over =
          session.OverrideFor(setting.key, LauncherValueText(session, setting),
                              setting.default_text);
      if (over.overridden) {
        out += "    badge: ";
        out += OverrideNoteText(LauncherValueText(session, setting), over.game_value);
        out += "\n";
      }
    }
  }
  return out;
}

std::string DescribePrecedence(const ProfileSession& session) {
  const GameConfig& config = session.game_config();

  std::string out;
  out += "game config    : ";
  out += config.path.string();
  if (!config.present) {
    out += " (not there: nothing to override anything)\n";
  } else if (config.unreadable) {
    out += " (unreadable: ";
    out += config.error;
    out += ")\n";
  } else {
    out += "\n";
  }

  // Every tab, in the table's order: the badge is decided per row, and a run that only looked
  // at the General tab would never see the case it exists for (a display or input row the
  // launcher leaves at its compiled default and the game's own file then decides).
  std::size_t badged = 0;
  for (const TabLayout& tab : BuildLayout()) {
    for (const LayoutGroup& group : tab.groups) {
      for (const LayoutRow& row : group.rows) {
        if (row.setting == nullptr) {
          continue;
        }
        const settings::Setting& setting = *row.setting;
        const std::string_view launcher_value = LauncherValueText(session, setting);
        const RowOverride over =
            session.OverrideFor(setting.key, launcher_value, setting.default_text);
        if (!over.overridden) {
          continue;
        }
        ++badged;
        out += "badge ";
        out += settings::TabName(tab.tab);
        out += " ";
        out += setting.key;
        out += " : ";
        out += OverrideNoteText(launcher_value, over.game_value);
        out += "\n";
      }
    }
  }
  out += "rows badged    : ";
  out += std::to_string(badged);
  out += "\n";
  return out;
}

}  // namespace rb_blitz::launcher
