// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace rb_blitz::input {

inline float CalibrationTextFit(float width, float height, float available_width, float available_height) {
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
      available_width <= 0 || available_height <= 0) return 1;
  return std::min({1.f, available_width / width, available_height / height});
}

inline std::string TouchCalibrationInstruction(std::string_view text) {
  struct Phrase { std::string_view before, after, touch; };
  constexpr std::array<Phrase, 8> phrases{{
      {"press the ", " button", "tap the screen"},
      {"Drücke jedes Mal die ", "-Taste", "Tippe jedes Mal auf den Bildschirm"},
      {"Drücke jedes Mal die ", " -Taste", "Tippe jedes Mal auf den Bildschirm"},
      {"pulsa el botón ", "", "toca la pantalla"},
      {"appuie sur la touche ", "", "touche l'écran"},
      {"Appuie sur la touche ", "", "Touche l'écran"},
      {"premi ", "", "tocca lo schermo"},
      {"pressione o botão ", "", "toque na tela"}}};
  for (const auto& phrase : phrases) {
    const auto at = text.find(phrase.before);
    if (at == std::string_view::npos) continue;
    const auto icon = at + phrase.before.size();
    if (text.substr(icon, 6) != "<alt1>") continue;
    const auto close = text.find("</alt1>", icon + 6);
    if (close == std::string_view::npos) continue;
    const auto tail = close + 7;
    if (text.substr(tail, phrase.after.size()) != phrase.after) continue;
    return std::string(text.substr(0, at)) + std::string(phrase.touch) +
        std::string(text.substr(tail + phrase.after.size()));
  }
  return std::string(text);
}

inline std::string CalibrationResultLines(std::string_view text) {
  if (text.find('\n') != std::string_view::npos) return std::string(text);
  size_t split = std::string_view::npos, distance = text.size();
  for (size_t at = text.find(' '); at != std::string_view::npos; at = text.find(' ', at + 1)) {
    const auto delta = at > text.size() / 2 ? at - text.size() / 2 : text.size() / 2 - at;
    if (delta < distance) { split = at; distance = delta; }
  }
  std::string result(text);
  if (split != std::string_view::npos) result[split] = '\n';
  return result;
}

// Observe the guest's current localized text so device switches are reversible
// and locale changes at a reused label address invalidate the saved wording.
struct CalibrationPromptText {
  std::string original, touch_text;
  std::string Update(std::string_view actual, bool touch, bool result) {
    if (touch_text != actual) {
      original = actual;
      touch_text = result ? CalibrationResultLines(original) : TouchCalibrationInstruction(original);
    }
    return touch ? touch_text : original;
  }
};

}  // namespace rb_blitz::input
