// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's tab shell (docs/plans/launcher-plan.md A1).
//
// The shell owns the layout (built from the schema table), one focus ring per tab, and the
// two NavSources. It draws one frame at a time and answers whether the user asked to leave.
// It is deliberately thin: the rows come from schema_view, the ring from nav, and the
// per-kind widgets are the only ImGui this prompt puts on screen. Saving (B4), the bottom
// bar (A2) and the gamepad source (A3) are other prompts' work.

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "nav.h"
#include "schema_view.h"
#include "settings_table.h"

namespace rb_blitz::launcher {

class Shell {
 public:
  Shell();

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

  std::vector<TabLayout> layout_;
  std::vector<FocusModel> rings_;
  std::size_t tab_ = 0;
  std::unique_ptr<NavSource> keyboard_;
  std::unique_ptr<NavSource> gamepad_;
};

}  // namespace rb_blitz::launcher
