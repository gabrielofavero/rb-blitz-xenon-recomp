// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <string>
#include <string_view>

namespace rb_blitz::input {

inline constexpr std::array<std::string_view, 3> kStartPromptTokens{
    "splash_press_start", "loading_press_button", "press_start_to_continue"};

// Keep the localized action and capitalization, replacing just the press verb
// and button. Both retail and the effective-binding private glyphs use alt1.
inline std::string TouchStartPrompt(std::string_view text) {
  struct Verb { std::string_view press, touch; };
  constexpr std::array<Verb, 12> verbs{{
      {"PRESS ", "TOUCH"}, {"Press ", "Touch"},
      {"PULSA ", "TOCA"}, {"Pulsa ", "Toca"},
      {"PRESSIONE ", "TOQUE"}, {"Pressione ", "Toque"},
      {"DRÜCKE ", "TIPPE"}, {"Drücke ", "Tippe"},
      {"PREMI ", "TOCCA"}, {"Premi ", "Tocca"},
      {"APPUIE SUR ", "TOUCHE"}, {"Appuie sur ", "Touche"}}};
  for (const auto& verb : verbs) {
    if (!text.starts_with(verb.press)) continue;
    auto tail = text.substr(verb.press.size());
    if (tail.starts_with("<alt1>")) {
      const auto end = tail.find("</alt1>");
      if (end == std::string_view::npos) return std::string(text);
      tail.remove_prefix(end + 7);
    } else if (tail.starts_with("START")) {
      tail.remove_prefix(5);
    } else {
      return std::string(text);
    }
    while (tail.starts_with(' ')) tail.remove_prefix(1);
    std::string result(verb.touch);
    if (!tail.empty() && !tail.starts_with(',')) result += ' ';
    result += tail;
    return result;
  }
  return std::string(text);
}

// Labels sharing a locale token share its original wording. Observe the actual
// text so guest reassignment, language changes and scene reloads replace it.
struct StartPromptText {
  std::string original;
  std::string touch_text;

  std::string Update(std::string_view actual, bool touch) {
    if (touch_text != actual) {
      original = actual;
      touch_text = TouchStartPrompt(original);
    }
    return touch ? touch_text : original;
  }
};

}  // namespace rb_blitz::input
