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

// One stable slot assignment for every binding page in the current input
// revision. Page entry only formats text; it never changes the font texture.
struct Catalog {
  std::vector<input::icons::Icon> icons;
  size_t Find(const input::icons::Icon& icon) const;
};

Catalog BuildCatalog(const std::vector<std::vector<std::string>>& panel_symbols,
                     const input::icons::View& view,
                     const Catalog* previous = nullptr);

inline constexpr size_t kVisibleLongRowIcons = 6;

// `symbols` and `fallback` are the same native list order. A row keeps its original position and
// selection index; only its displayed text changes. With a Catalog, glyph codes refer to stable
// slots prepared before page entry. Without one, `icons` contains page-local art for callers that
// publish their own atlas. `windows` is the per-row first binding to show, in multiples of six. Rows with more
// than eight sources expose six at a time, so the guest list keeps room for a page indicator.
// Repeated physical sources reuse one glyph tile. If the font's slots are exhausted, that row
// retains its complete textual list; other rows continue to use images where capacity permits.
Page Build(const std::vector<std::string>& symbols,
           const std::vector<std::string>& fallback,
           const input::icons::View& view,
           const std::vector<size_t>& windows = {},
           const Catalog* catalog = nullptr);

}  // namespace rb_blitz::ui::input_icon_rows
