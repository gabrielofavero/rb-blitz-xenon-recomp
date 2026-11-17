// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace rb_blitz::input {
enum class MenuDialogScope { kNone, kShortcuts, kPanel };
inline MenuDialogScope DialogScopeFor(std::string_view panel, std::string_view focus) {
  if (panel == "error_popup_panel" || panel == "rbbz_download_panel" ||
      panel == "rbbz_search_loading_panel" || panel == "rbbz_library_panel")
    return MenuDialogScope::kPanel;
  return focus == "shortcuts.lst" ? MenuDialogScope::kShortcuts : MenuDialogScope::kNone;
}

struct DialogPromptSpan { float left, right; uint32_t action; };
// Advances are the cumulative UTF-16 positions produced by the guest's normal
// RndText layout, including zero-width markup. No font approximation or extra
// guest layout pass is needed to distinguish "A Refresh   B Close".
inline std::vector<DialogPromptSpan> DialogPromptSpans(
    std::u16string_view text, const std::vector<float>& advances) {
  if (advances.size() != text.size() + 1 || text.empty() ||
      !std::isfinite(advances.back()) || advances.back() <= 0) return {};
  struct Action { size_t start; uint32_t action; };
  std::vector<Action> actions;
  const auto tag_at = [text](size_t at, std::u16string_view tag) {
    if (at > text.size() || tag.size() > text.size() - at) return false;
    for (size_t i = 0; i < tag.size(); ++i) {
      auto code = text[at + i];
      if (code >= u'A' && code <= u'Z') code += u'a' - u'A';
      if (code != tag[i]) return false;
    }
    return true;
  };
  for (size_t at = 0; at < text.size(); ++at) {
    size_t length = tag_at(at, u"<alt1>") ? 6 : tag_at(at, u"<alt>") ? 5 : 0;
    if (!length) continue;
    const size_t glyph = at + length;
    const auto close = length == 6 ? u"</alt1>" : u"</alt>";
    if (glyph >= text.size() || !tag_at(glyph + 1, close)) continue;
    const char16_t code = text[glyph];
    const uint32_t action = code == u'A' || code == 0x81 ? 1 :
                            code == u'B' || code == 0x82 ? 2 :
                            code == u'Y' || code == 0x83 ? 3 :
                            code == u'X' || code == 0x8E ? 14 : 0;
    if (action) actions.push_back({at, action});
  }
  if (actions.empty() || (actions.size() > 1 && text.find(u'\n') != text.npos)) return {};
  if (actions.size() == 1) return {{0, 1, actions.front().action}};
  std::vector<DialogPromptSpan> result;
  for (size_t i = 0; i < actions.size(); ++i) {
    size_t end = i + 1 < actions.size() ? actions[i + 1].start : text.size();
    while (end && (text[end - 1] == u' ' || text[end - 1] == u'\t')) --end;
    const float left = advances[i ? actions[i].start : 0] / advances.back();
    const float right = advances[end] / advances.back();
    if (!std::isfinite(left) || !std::isfinite(right) || left < 0 || right > 1 || right <= left)
      return {};
    result.push_back({left, right, actions[i].action});
  }
  return result;
}
}  // namespace rb_blitz::input
