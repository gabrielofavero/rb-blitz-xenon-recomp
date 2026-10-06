// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The General tab (docs/plans/launcher-plan.md D4, D5; prompts B1, B8).
//
// It draws the three rows §1.1 gives M1 - the launch target with the payload availability the
// files say, and the save and DLC locations - and edits them in the session's profile, which
// the settings-file block at the end of the tab is what writes to disk. The launch target is
// drawn as one stacked entry per choice, and when the payload is not there the Ultimate entry
// is replaced by the *Install Ultimate* action (D5's "never hard-disable", B8), which drives the
// installer's own helper. The game-directory override and *Verify installation* are D4's later
// rows, not M1's.
//
// The rules are not re-implemented here: the payload state comes from ultimate_state.h, the
// value rules from path_validate.h (which is src/fs/dlc_layout.h and src/fs/path_policy.h),
// and a row's own text and tooltip from the schema table.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "install_ultimate.h"
#include "launcher/profile.h"
#include "nav.h"
#include "nav_ui.h"
#include "path_validate.h"
#include "profile_session.h"
#include "profile_ui.h"
#include "schema_view.h"
#include "ultimate_state.h"

namespace rb_blitz::launcher {

class GeneralTab {
 public:
  // The launch target is drawn as one stacked entry per choice - Rock Band Blitz, its demo,
  // and either Ultimate or the action that installs it - so it occupies this many ring rows.
  // The schema's `launch.target` row must declare exactly this many choices: the ring counts
  // one entry per choice (schema_view's FocusEntriesFor), which is what makes these agree.
  static constexpr std::size_t kTargetRows = 3;

  // The rows B4 adds after the settings rows: the profile block at the end of the tab
  // (launcher/src/profile_ui.h), and A5's launcher-keys block after it (launcher/src/nav_ui.h).
  // The tab's ring has to be sized for them, which is why this is public. The launch target's
  // extra entries come from its choices, not from here.
  static constexpr std::size_t kExtraRows = ProfilePanel::kRowCount + NavKeysPanel::kRowCount;

  explicit GeneralTab(GameRoots roots);

  // `action` is this frame's NavAction. B1's rows are the first with anything to activate, so
  // Enter and Space on the focused row pick a target, open the folder picker, or start the
  // Ultimate install; B4's block and A5's answer the same way.
  //
  // `keys` is the shell's launcher-keys block rather than a member of this tab, because the shell
  // has to ask whether a capture is running *before* it reads the keyboard (A5) - the same reason
  // the shell owns the pad and the focus log. `safe_mode` is the shell's too: `--safe-mode` says
  // the keys in force are the defaults whatever the file says, and the block has to say so out
  // loud because assigning a key still writes the file.
  void Draw(const TabLayout& tab, FocusModel& ring, ProfileSession& session, NavAction action,
            NavKeysPanel& keys, bool safe_mode);

  // What the last Draw read off the file system. Exposed so the caller can say it out loud
  // rather than the tab being the only thing that knows.
  UltimateState state() const { return state_; }

  // True while a modal owns the keyboard: B4's reset confirmation, or the Ultimate install's
  // progress and its result. The shell leaves Escape to it instead of reading it as "leave the
  // launcher" (A1).
  bool ModalOpen() const { return panel_.ModalOpen() || install_modal_open_; }

  // What the profile panel's last action did, and its last failure. The bottom bar draws them
  // (A2 owns the bar), so they are read rather than printed here.
  const std::string& status_message() const;
  const std::string& status_error() const;

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
  void ApplyChosenPath(ProfileSession& session);
  // The stacked launch-target entries, starting at `first_row`.
  void DrawTargetRows(ProfileSession& session, std::size_t first_row, FocusModel& ring,
                      NavAction action);
  void DrawInstallModal(NavAction action);
  void DrawPathValue(const settings::Setting& setting, float value_width,
                     ProfileSession& session, std::size_t index, FocusModel& ring,
                     NavAction action);
  void DrawMessages(const settings::Setting& setting, ProfileSession& session);

  GameRoots roots_;
  UltimateState state_ = UltimateState::kMissing;
  ProfilePanel panel_;
  UltimateInstaller install_;
  bool install_requested_ = false;
  bool install_modal_open_ = false;
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
