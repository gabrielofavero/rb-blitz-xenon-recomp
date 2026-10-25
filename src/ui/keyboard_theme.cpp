// SPDX-License-Identifier: GPL-2.0-only
#include "ui/keyboard_theme.h"

#include <imgui.h>
#include <rex/logging.h>
#include <rex/ui/style.h>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace rb_blitz::ui {
namespace {
ImFont *g_font = nullptr;
ImFont *g_heading_font = nullptr;
// The legacy renderer uploads its atlas once. Bake above the largest drawn
// heading (56 logical pixels) so larger windows never enlarge a tiny debug
// face.
constexpr float kBakedSize = 64.0f;

ImFont *LoadFont(ImFontAtlas *atlas, const char *const *candidates,
                 size_t count) {
#ifdef _WIN32
  const char *windir = std::getenv("WINDIR");
  if (windir && windir[0]) {
    for (size_t i = 0; i < count; ++i) {
      const auto path = std::filesystem::path(windir) / "Fonts" / candidates[i];
      std::error_code ec;
      if (!std::filesystem::is_regular_file(path, ec)) {
        continue;
      }
      if (auto *font =
              atlas->AddFontFromFileTTF(path.string().c_str(), kBakedSize)) {
        REXLOG_INFO("keyboard: font '{}' baked at 64 px", candidates[i]);
        return font;
      }
    }
  }
#endif
  return nullptr;
}
}  // namespace

void ConfigureKeyboardFonts(ImFontAtlas *atlas) {
  if (!atlas) {
    return;
  }
  const char *regular[] = {"segoeui.ttf", "tahoma.ttf", "arial.ttf"};
  const char *heading[] = {"seguisb.ttf", "tahomabd.ttf", "arialbd.ttf"};
  g_font = LoadFont(atlas, regular, 3);
  g_heading_font = LoadFont(atlas, heading, 3);
  if (!g_font) {
    ImFontConfig config;
    config.SizePixels = kBakedSize;
    g_font = atlas->AddFontDefault(&config);
    REXLOG_WARN(
        "keyboard: system sans unavailable; using the bundled font at 64 px");
  }
  if (!g_heading_font) {
    g_heading_font = g_font;
  }
}

void ConfigureKeyboardTheme(rex::ui::Style &style) {
  auto &keyboard = style.keyboard;
  keyboard.enabled = true;
  keyboard.font = g_font;
  keyboard.heading_font = g_heading_font;
  keyboard.background = ImVec4(0.14f, 0.075f, 0.19f, 1.0f);
  keyboard.border = ImVec4(0.48f, 0.28f, 0.53f, 1.0f);
  keyboard.text = ImVec4(1.0f, 0.97f, 1.0f, 1.0f);
  keyboard.muted_text = ImVec4(0.81f, 0.72f, 0.85f, 1.0f);
  keyboard.input = ImVec4(0.085f, 0.04f, 0.12f, 1.0f);
  keyboard.accent = ImVec4(0.64f, 0.16f, 0.50f, 1.0f);
  keyboard.accent_hovered = ImVec4(0.77f, 0.23f, 0.61f, 1.0f);
  keyboard.accent_active = ImVec4(0.51f, 0.10f, 0.39f, 1.0f);
  keyboard.secondary = ImVec4(0.27f, 0.16f, 0.33f, 1.0f);
  keyboard.dim = ImVec4(0.06f, 0.025f, 0.09f, 0.55f);
}
}  // namespace rb_blitz::ui
