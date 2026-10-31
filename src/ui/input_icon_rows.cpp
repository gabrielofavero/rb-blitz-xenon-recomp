// SPDX-License-Identifier: GPL-2.0-only
#include "ui/input_icon_rows.h"

#include "profile/input_rows.h"
#include "ui/input_font_scene.h"

namespace rb_blitz::ui::input_icon_rows {
namespace {
bool IsBinding(profile::InputRowKind kind) {
  using K = profile::InputRowKind;
  return kind == K::kGeneralBinding || kind == K::kControllerBinding ||
         kind == K::kKeyboardMouseBinding || kind == K::kMusicBinding;
}
void AppendGlyph(std::string* text, uint32_t code) {
  text->push_back(static_cast<char>(0xC0 | (code >> 6)));
  text->push_back(static_cast<char>(0x80 | (code & 0x3F)));
}
}  // namespace

Page Build(const std::vector<std::string>& symbols,
           const std::vector<std::string>& fallback,
           const input::icons::View& view) {
  Page page;
  if (symbols.size() != fallback.size()) return page;
  page.lines = fallback;
  for (size_t row = 0; row < symbols.size(); ++row) {
    const auto token = profile::ParseInputRowSymbol(symbols[row]);
    if (!IsBinding(token.kind)) continue;
    auto ordered = input::icons::Ordered(view, token);
    if (ordered.empty()) ordered.push_back(input::icons::First(view, token));
    // The dedicated alt1 font has its own white material tint. A vertex color
    // tag cannot override the primary font's black hover material multiplier.
    std::string line = profile::BindingRowLabel(token) + ": <alt1>";
    const size_t first_row_icon = page.icons.size();
    bool row_overflow = false;
    for (const auto& icon : ordered) {
      if (line.back() != '>') line += ' ';
      size_t index = 0;
      for (; index < page.icons.size(); ++index) {
        const auto& existing = page.icons[index];
        if (existing.source.kind == icon.source.kind &&
            existing.source.name == icon.source.name && existing.art == icon.art &&
            existing.slot == icon.slot && existing.scheme == icon.scheme) break;
      }
      if (index == page.icons.size()) {
        if (index == input_font::kIconCount) {
          // Keep other rows' images. This row still uses its complete text
          // representation, and tiles reserved only by this row can be reused.
          page.icons.resize(first_row_icon);
          page.overflow = true;
          row_overflow = true;
          break;
        }
        page.icons.push_back(icon);
      }
      AppendGlyph(&line, input_font::GlyphCode(static_cast<uint32_t>(index)));
    }
    if (row_overflow) continue;
    line += "</alt1>";
    page.lines[row] = std::move(line);
  }
  return page;
}
}  // namespace rb_blitz::ui::input_icon_rows
