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
// Dependency-free on purpose - no ImGui, no SDL - so the shape is unit-testable (E1) and
// A3 can fill the gamepad source without touching widget code.

#pragma once

#include <cstddef>

namespace rb_blitz::launcher {

// What a device asked for. A1 maps keys to these and stubs the pad source; A3 makes the
// pad source return the same values.
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

// The single seam a device plugs into (D6). A1 supplies the keyboard source and a stub
// gamepad source; A3 fills the stub in behind this same interface.
class NavSource {
 public:
  virtual ~NavSource() = default;
  // Called once per frame. One action per frame is enough: a press, not a state.
  virtual NavAction Poll() = 0;
};

}  // namespace rb_blitz::launcher
