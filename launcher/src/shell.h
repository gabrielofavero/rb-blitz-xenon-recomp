// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's tab shell (docs/plans/launcher-plan.md A1, B1, B4).
//
// The shell owns the layout (built from the schema table), one focus ring per tab, the two
// NavSources and the session - the profile plus its write path (B4). It draws one frame at a
// time and answers whether the user asked to leave. It is deliberately thin: the rows come from
// schema_view, the ring from nav, the General tab from general_tab, and everything else is A1's
// read-only rendering. The bottom bar (A2) and the gamepad source (A3) are other prompts' work.

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "general_tab.h"
#include "controller_tab.h"
#include "launcher/profile.h"
#include "nav.h"
#include "profile_session.h"
#include "schema_view.h"
#include "settings_table.h"
#include "ultimate_state.h"

namespace rb_blitz::launcher {

class Shell {
 public:
  // `session` is the profile and its file: B1 and B4 edit it, and B4's block is what writes it.
  // `roots` is where the game's data was found. `environment` is what a row's `visible` rule is
  // decided from (a single-display machine has no Monitor row).
  Shell(ProfileSession session, GameRoots roots, RowEnvironment environment);

  // Draws one frame. Returns false when the user asked to leave (Esc or B).
  bool Frame();

  // What the model is showing: the tab and the focused row inside it. Exposed so the
  // window title and the tests can see the state rather than guess it.
  settings::Tab CurrentTab() const;
  std::size_t FocusedRow() const;

  // The session, so the caller can keep the window geometry (A1) in the same file and see where
  // the portable switch moved it.
  ProfileSession& session() { return session_; }

 private:
  void ApplyAction(NavAction action);
  void RequestTab(int delta);
  void DrawTab(const TabLayout& tab, FocusModel& ring);

  GameRoots roots_;
  ProfileSession session_;
  GeneralTab general_;
  ControllerTab controller_;
  std::vector<TabLayout> layout_;
  std::vector<FocusModel> rings_;
  std::size_t tab_ = 0;
  NavAction action_ = NavAction::kNone;
  std::unique_ptr<NavSource> keyboard_;
  std::unique_ptr<NavSource> gamepad_;
};

}  // namespace rb_blitz::launcher
