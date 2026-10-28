// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string>
#include <vector>

#include "input/input_icons.h"

namespace rb_blitz::ui::input_icon_rows {

struct Page {
  std::vector<std::string> lines;
  std::vector<input::icons::Icon> icons;
  bool overflow = false;
};

// `symbols` and `fallback` are the same native list order. A row keeps its original position and
// selection index; only its displayed text changes. The caller publishes `icons` atomically with
// these rows. If the 30 measured guest glyph slots are exhausted, the complete textual rows are
// retained so no binding disappears while the detail route is being developed.
Page Build(const std::vector<std::string>& symbols,
           const std::vector<std::string>& fallback,
           const input::icons::View& view);

}  // namespace rb_blitz::ui::input_icon_rows
