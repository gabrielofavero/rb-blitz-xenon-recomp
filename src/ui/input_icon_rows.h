// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "input/input_icons.h"

namespace rb_blitz::ui::input_icon_rows {

struct Page {
  std::vector<std::string> lines;
  std::vector<input::icons::Icon> icons;
  std::vector<size_t> window_starts;
  bool overflow = false;
};

inline constexpr size_t kVisibleLongRowIcons = 6;

// `symbols` and `fallback` are the same native list order. A row keeps its original position and
// selection index; only its displayed text changes. The caller publishes `icons` atomically with
// these rows. `windows` is the per-row first binding to show, in multiples of six. Rows with more
// than eight sources expose six at a time, so the guest list keeps room for a page indicator.
// Repeated physical sources reuse one glyph tile. If the font's slots are exhausted, that row
// retains its complete textual list; other rows continue to use images where capacity permits.
Page Build(const std::vector<std::string>& symbols,
           const std::vector<std::string>& fallback,
           const input::icons::View& view,
           const std::vector<size_t>& windows = {});

}  // namespace rb_blitz::ui::input_icon_rows
