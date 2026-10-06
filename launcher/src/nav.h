// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's focus model and its one input seam (docs/plans/launcher-plan.md A1, D6).
//
// The model is deliberately smaller than a widget toolkit: a flat, ordered list of rows
// per tab and one focused index. Keys, the mouse and the pad all become the same NavAction
// vocabulary, so moving the ring is one code path and no widget learns which device moved
// it (D6's "the last device to move owns the focus ring").
//
// Dependency-free on purpose - no ImGui, no SDL - so the shape is unit-testable (E1), and so the
// pad's own rules could be written without a widget learning that anything changed (A3).

#pragma once

#include <cstddef>
#include <vector>

namespace rb_blitz::launcher {

// What a device asked for. The keyboard (A1) and the pad (A3) both answer in these words, and
// nothing downstream asks which of the two said it.
//
// The vocabulary is two-dimensional: kNext/kPrevious move between rows (down and up), and
// kNextOption/kPreviousOption move between the options a row offers side by side (right and
// left) - an enum's choices, a row of buttons, or a slider's value. A row with a single option
// ignores the horizontal pair, so nothing has to know which kind of row it is to move the ring.
enum class NavAction {
  kNone,
  kNext,
  kPrevious,
  kNextOption,
  kPreviousOption,
  kFirst,
  kLast,
  kNextTab,
  kPreviousTab,
  kActivate,
  kCancel,
  // Start on a pad (D6): not a move of the ring, so it is the shell's own rather than anything a
  // widget answers - it is the launch the bottom bar's button offers, without the mouse.
  kLaunch,
  // The two the bottom bar and the precedence badge own: the bar's buttons and B4's *Copy the
  // effective value* were mouse-only, so a keyboard alone could not save, start the game, or take
  // the value the game's own file decides. Neither moves the ring; kCopyEffectiveValue acts on the
  // row the ring is on, which is why it belongs in this vocabulary rather than beside the badge
  // that draws it.
  kSave,
  kCopyEffectiveValue,
  // The right analog stick, and the only thing a mouse and a keyboard do not need: it scrolls the
  // tab body's scrollbar, so a long tab can be skimmed without moving the ring. It belongs to the
  // shell, so nothing downstream of the bar answers it.
  kScrollUp,
  kScrollDown,
};

// The focus ring for one tab, as a list of rows each offering one or more options.
//
// A *row* is one line the user moves between with Up and Down; its *options* are what Left and
// Right move between inside it. Entries are still addressed by a flat index - the sum of the
// option counts of the rows before it, plus the option - so a widget that knows its own flat index
// (a checkbox, a slider, one radio of an enum) keeps working and a click can adopt the ring
// without knowing which row it is on. Every move wraps.
class FocusModel {
 public:
  // `row_options` is one entry per focusable row, each naming how many horizontal options that row
  // has (at least one). An empty list is a tab with nothing in it.
  void Reset(std::vector<std::size_t> row_options);

  // Row moves (Up and Down). The option is carried across where the destination row has one to
  // carry it to, so walking down a column of enums keeps the column.
  void MoveNext();
  void MovePrevious();
  // Option moves (Left and Right), wrapping inside the current row. A row with one option does
  // not move.
  void MoveNextOption();
  void MovePreviousOption();
  void MoveFirst();
  void MoveLast();
  // Adopts the row an option belongs to: a flat entry index, as a widget's own index is one.
  void SetIndex(std::size_t index);
  // A pointer is a device like any other (D6): hovering a row adopts the ring.
  void FocusIf(std::size_t index, bool condition);

  std::size_t Count() const { return count_; }  // flat entries across every row
  std::size_t Index() const { return index_; }  // the flat entry the ring is on
  std::size_t RowCount() const { return row_options_.size(); }
  std::size_t Row() const { return row_; }
  std::size_t Option() const { return option_; }
  std::size_t RowOptions(std::size_t row) const;
  std::size_t RowStart(std::size_t row) const;
  bool Empty() const { return row_options_.empty(); }

 private:
  void Adopt(std::size_t row, std::size_t option);

  std::vector<std::size_t> row_options_;  // options per row, in draw order
  std::vector<std::size_t> row_start_;    // prefix sums: where each row's first option sits
  std::size_t count_ = 0;
  std::size_t index_ = 0;  // flat entry
  std::size_t row_ = 0;
  std::size_t option_ = 0;
};

// The single seam a device plugs into (D6). A1 supplies the keyboard source; A3 supplies the
// pad's (launcher/src/pad_source.h), which is the only file that knows SDL has a pad in it.
class NavSource {
 public:
  virtual ~NavSource() = default;
  // Called once per frame. One action per frame is enough: a press, not a state.
  virtual NavAction Poll() = 0;
};

}  // namespace rb_blitz::launcher
