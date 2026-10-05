// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The General tab (docs/plans/launcher-plan.md D4, D5; prompt B1).
//
// It draws the three rows §1.1 gives M1 - the launch target with the payload availability the
// files say, and the save and DLC locations - and edits them in the profile it is handed. B4
// is what writes that profile to disk, B8 is what adds the *Install Ultimate...* action, and
// the game-directory override and *Verify installation* are D4's later rows, not M1's.
//
// The rules are not re-implemented here: the payload state comes from ultimate_state.h, the
// value rules from path_validate.h (which is src/fs/dlc_layout.h and src/fs/path_policy.h),
// and a row's own text and tooltip from the schema table.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "launcher/profile.h"
#include "nav.h"
#include "path_validate.h"
#include "schema_view.h"
#include "ultimate_state.h"

namespace rb_blitz::launcher {

class GeneralTab {
 public:
  explicit GeneralTab(GameRoots roots);

  // `action` is this frame's NavAction. B1's rows are the first with anything to activate, so
  // Enter and Space on the focused row open the folder picker / move the target on.
  void Draw(const TabLayout& tab, FocusModel& ring, Profile& profile, NavAction action);

  // What the last Draw read off the file system. Exposed so the caller can say it out loud
  // rather than the tab being the only thing that knows.
  UltimateState state() const { return state_; }

  // SDL's dialog callback target. Public only because the C callback needs it; nothing else
  // in the launcher calls it.
  void OnFolderChosen(const char* const* filelist);

 private:
  // One row's editable text, so typing survives the frame. Reloaded whenever the profile's
  // own value changes underneath - a pick, a refusal, a different tab.
  struct PathRowState {
    std::string key;
    char buffer[512] = {};
    std::string source;
  };

  PathRowState& RowStateFor(std::string_view key);
  // The verdict for a row's current value, computed once per value rather than every frame:
  // the DLC rule walks the folder, and a row that is not being edited does not change.
  const PathVerdict& VerdictFor(std::string_view key, const std::string& value);
  void ApplyChosenPath(Profile& profile);
  void DrawTargetValue(Profile& profile, std::size_t index, FocusModel& ring, NavAction action);
  void DrawPathValue(const settings::Setting& setting, float value_width, Profile& profile,
                     std::size_t index, FocusModel& ring, NavAction action);
  void DrawMessages(const settings::Setting& setting, const Profile& profile);

  GameRoots roots_;
  UltimateState state_ = UltimateState::kMissing;
  std::vector<PathRowState> path_rows_;
  // The last value each path row was judged at, so the rules are applied when something
  // changes and not sixty times a second.
  struct VerdictCache {
    std::string key;
    std::string value;
    bool valid = false;
    PathVerdict verdict;
  };
  std::vector<VerdictCache> verdicts_;
  // SDL's folder dialog answers asynchronously, during the event pump - after Draw() has
  // returned - so the callback only records the choice and the next Draw applies it.
  std::string chosen_key_;
  std::string chosen_path_;
  // The last *refused* pick, and the row it belongs to, so the reason survives the frame even
  // though the value it was refused for was never applied.
  std::string message_key_;
  std::string message_;
};

}  // namespace rb_blitz::launcher
