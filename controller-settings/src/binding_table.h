// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The three-column binding table, shared by General Mapping and the two gameplay tables
// (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md §4, P4).
//
// The Controller tab draws three tables with the same bones - a control or action, what presses
// it, and that row's Assign/Reset - so the bones live here once. What varies is data: the label,
// the measured spans, whether a listen is running and whether Reset is enabled. The gameplay
// tables add one thing the general table does not have: a source of the other input type is
// refused at capture, and a source whose device is switched off is drawn crossed rather than
// removed (plan R7/R11).
//
// The ring arithmetic is the caller's: `first_row` is the flat ring index of the first row's
// Assign button and every row takes two entries, so the block that sized the ring and this table
// walk the same count (controller_block.h).

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "binding_text.h"
#include "nav.h"

namespace rb_blitz::profile {

struct BindingRowView {
  // The caller's row number: it scopes the row's ImGui ids and comes back in the result.
  std::size_t id = 0;
  std::string_view label;
  // What the value column shows. Empty means the column stays empty (the pad still reports the
  // control by itself); `nothing_bound` is the different fact of an explicit empty binding.
  std::vector<binding_text::Span> spans;
  bool nothing_bound = false;
  // One capture session across every table: while any row is listening the others' Assign is
  // disabled, and the row that owns the listen reads Cancel.
  bool listening = false;
  bool listening_here = false;
  std::string listening_text;
  bool assign_enabled = true;
  bool reset_enabled = true;
  // A crossed source's reason, shown when the pointer rests on the value ("Keyboard support is
  // off"). Empty when nothing is crossed.
  std::string disabled_hint;
};

struct BindingTableResult {
  std::optional<std::size_t> assign;  // the row id whose Assign was activated
  std::optional<std::size_t> reset;   // the row id whose Reset was activated
};

// One focusable button inside the Controller tab's block. A row's Assign/Reset, the two switches
// and the two help buttons all obey the same ring rule, so they all draw through here.
bool DrawBlockButton(std::size_t index, FocusModel& ring, NavAction action, const char* label,
                     bool enabled);

// Draws the table at the current cursor. The value column wraps inside its own cell and draws
// each source as its own span, with a strike line under every disabled one.
BindingTableResult DrawBindingTable(const char* table_id, const std::vector<BindingRowView>& rows,
                                    std::size_t first_row, FocusModel& ring, NavAction action);

}  // namespace rb_blitz::profile
