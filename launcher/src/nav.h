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

namespace rb_blitz::launcher {

// What a device asked for. The keyboard (A1) and the pad (A3) both answer in these words, and
// nothing downstream asks which of the two said it.
enum class NavAction {
  kNone,
  kNext,
  kPrevious,
  kFirst,
  kLast,
  kNextTab,
  kPreviousTab,
  kActivate,
  kCancel,
  // Start on a pad (D6): not a move of the ring, so it is the shell's own rather than anything a
  // widget answers - it is the launch the bottom bar's button offers, without the mouse.
  kLaunch,
  // The three the bottom bar and the precedence badge own, and the reason A5 binds them: the bar's
  // buttons and B4's *Copy the effective value* were mouse-only, so a keyboard alone could not
  // save, read the command line, start the game, or take the value the game's own file decides.
  // None of them moves the ring; kCopyEffectiveValue acts on the row the ring is on, which is why
  // it belongs in this vocabulary rather than beside the badge that draws it.
  kSave,
  kCopyCommand,
  kCopyEffectiveValue,
};

// The focus ring for one tab. Every move wraps, because the row list is a ring: Next on
// the last row returns to the first and Previous on the first goes to the last.
class FocusModel {
 public:
  void Reset(std::size_t count);
  void MoveNext();
  void MovePrevious();
  void MoveFirst();
  void MoveLast();
  void SetIndex(std::size_t index);
  // A pointer is a device like any other (D6): hovering a row adopts the ring.
  void FocusIf(std::size_t index, bool condition);

  std::size_t Count() const { return count_; }
  std::size_t Index() const { return index_; }
  bool Empty() const { return count_ == 0; }

 private:
  std::size_t count_ = 0;
  std::size_t index_ = 0;
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
