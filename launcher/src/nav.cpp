// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The focus model (launcher/src/nav.h, A1).

#include "nav.h"

namespace rb_blitz::launcher {

void FocusModel::Reset(std::size_t count) {
  count_ = count;
  index_ = 0;
}

void FocusModel::MoveNext() {
  if (count_ != 0) {
    index_ = (index_ + 1) % count_;
  }
}

void FocusModel::MovePrevious() {
  if (count_ != 0) {
    index_ = (index_ + count_ - 1) % count_;
  }
}

void FocusModel::MoveFirst() { index_ = 0; }

void FocusModel::MoveLast() {
  if (count_ != 0) {
    index_ = count_ - 1;
  }
}

void FocusModel::SetIndex(std::size_t index) {
  if (index < count_) {
    index_ = index;
  }
}

void FocusModel::FocusIf(std::size_t index, bool condition) {
  if (condition && index < count_) {
    index_ = index;
  }
}

}  // namespace rb_blitz::launcher
