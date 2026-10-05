// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's tab shell (docs/plans/launcher-plan.md A1, B1).
//
// The shell owns the layout (built from the schema table), one focus ring per tab, the two
// NavSources and the session's profile. It draws one frame at a time and answers whether the
// user asked to leave. It is deliberately thin: the rows come from schema_view, the ring from
// nav, the General tab from general_tab, and everything else is A1's read-only rendering. The
// bottom bar (A2), the gamepad source (A3) and the write path (B4) are other prompts' work.

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "general_tab.h"
#include "launcher/profile.h"
#include "nav.h"
#include "schema_view.h"
#include "settings_table.h"
#include "ultimate_state.h"

namespace rb_blitz::launcher {

class Shell {
 public:
  // `profile` is the session's editable copy - B1 edits the General rows in it, B4 is what
  // writes it back - and `roots` is where the game's data was found. The caller keeps the
  // file's own copy for the window geometry it persists on exit, so an edit made here does
  // not reach the disk until B4 lands.
  Shell(Profile profile, GameRoots roots);

  // Draws one frame. Returns false when the user asked to leave (Esc or B).
  bool Frame();

  // What the model is showing: the tab and the focused row inside it. Exposed so the
  // window title and the tests can see the state rather than guess it.
  settings::Tab CurrentTab() const;
  std::size_t FocusedRow() const;

 private:
  void ApplyAction(NavAction action);
  void RequestTab(int delta);
  void DrawTab(const TabLayout& tab, FocusModel& ring);

  GameRoots roots_;
  Profile profile_;
  GeneralTab general_;
  std::vector<TabLayout> layout_;
  std::vector<FocusModel> rings_;
  std::size_t tab_ = 0;
  NavAction action_ = NavAction::kNone;
  std::unique_ptr<NavSource> keyboard_;
  std::unique_ptr<NavSource> gamepad_;
};

}  // namespace rb_blitz::launcher
