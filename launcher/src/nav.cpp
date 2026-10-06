// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The focus model (launcher/src/nav.h, A1).

#include "nav.h"

#include <algorithm>
#include <utility>

namespace rb_blitz::launcher {

void FocusModel::Reset(std::vector<std::size_t> row_options) {
  row_options_ = std::move(row_options);
  row_start_.clear();
  row_start_.reserve(row_options_.size());
  std::size_t next = 0;
  for (const std::size_t options : row_options_) {
    row_start_.push_back(next);
    // A row always has at least one option, so a malformed count cannot make the ring unreachable.
    next += options == 0 ? 1 : options;
  }
  count_ = next;
  Adopt(0, 0);
}

std::size_t FocusModel::RowOptions(std::size_t row) const {
  if (row >= row_options_.size()) {
    return 1;
  }
  return row_options_[row] == 0 ? 1 : row_options_[row];
}

std::size_t FocusModel::RowStart(std::size_t row) const {
  return row < row_start_.size() ? row_start_[row] : 0;
}

void FocusModel::Adopt(std::size_t row, std::size_t option) {
  if (row_options_.empty()) {
    row_ = 0;
    option_ = 0;
    index_ = 0;
    return;
  }
  row_ = row % row_options_.size();
  option_ = option % RowOptions(row_);
  index_ = RowStart(row_) + option_;
}

void FocusModel::MoveNext() {
  if (row_options_.empty()) {
    return;
  }
  // Carry the option down the column when the next row is wide enough for it.
  Adopt(row_ + 1, std::min(option_, RowOptions(row_ + 1) - 1));
}

void FocusModel::MovePrevious() {
  if (row_options_.empty()) {
    return;
  }
  const std::size_t target = (row_ + row_options_.size() - 1) % row_options_.size();
  Adopt(target, std::min(option_, RowOptions(target) - 1));
}

void FocusModel::MoveNextOption() {
  if (row_options_.empty()) {
    return;
  }
  Adopt(row_, option_ + 1);
}

void FocusModel::MovePreviousOption() {
  if (row_options_.empty()) {
    return;
  }
  Adopt(row_, option_ + RowOptions(row_) - 1);
}

void FocusModel::MoveFirst() { Adopt(0, 0); }

void FocusModel::MoveLast() {
  if (!row_options_.empty()) {
    Adopt(row_options_.size() - 1, 0);
  }
}

void FocusModel::SetIndex(std::size_t index) {
  if (row_options_.empty() || index >= count_) {
    return;
  }
  // Binary search would be overkill for a tab's tens of rows; a walk is one comparison each.
  for (std::size_t row = row_options_.size(); row-- > 0;) {
    if (index >= row_start_[row]) {
      Adopt(row, index - row_start_[row]);
      return;
    }
  }
}

void FocusModel::FocusIf(std::size_t index, bool condition) {
  if (condition) {
    SetIndex(index);
  }
}

}  // namespace rb_blitz::launcher
