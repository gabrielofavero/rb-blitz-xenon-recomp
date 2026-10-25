// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>

struct ImFontAtlas;
namespace rex::ui {
struct Style;
}

namespace rb_blitz::ui {
// Register fonts before the SDK bakes its atlas, then theme its existing XAM
// keyboard modal. The game still owns the prompt and the search result
// handling. The achievement unlock toast shares this palette and font atlas.
void ConfigureKeyboardFonts(ImFontAtlas *atlas);
void ConfigureKeyboardTheme(rex::ui::Style &style, std::function<void*()> native_window);
}  // namespace rb_blitz::ui
