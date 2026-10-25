// SPDX-License-Identifier: GPL-2.0-only
#include "ui/keyboard_theme.h"

#include "input/native_menu.h"
#include "input/title_input.h"
#include "platform/virtual_keyboard.h"

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
// heading (46 logical pixels) so larger windows never enlarge a tiny debug
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
  // Approximate the retail popup's bold sans heading and condensed prompts
  // using installed fonts; the game's typefaces are packed guest assets.
  const char *regular[] = {"arialnb.ttf", "tahomabd.ttf", "arialbd.ttf"};
  const char *heading[] = {"arialbd.ttf", "tahomabd.ttf", "seguisb.ttf"};
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

void ConfigureKeyboardTheme(rex::ui::Style &style, std::function<void*()> native_window) {
  auto &keyboard = style.keyboard;
  keyboard.enabled = true;
  keyboard.request_keyboard = [native_window] {
    return platform::RequestVirtualKeyboard(native_window ? native_window() : nullptr);
  };
  keyboard.dismiss_keyboard = [native_window] {
    platform::DismissVirtualKeyboard(native_window ? native_window() : nullptr);
  };
  keyboard.sample_controller = [] {
    using Pad = rex::ui::KeyboardControllerSample;
    using namespace rex::input;
    X_INPUT_STATE state{};
    Pad sample;
    sample.connected = input::SamplePhysicalPad(&state, true);
    // The window's actual focus is authoritative; text/IME keyboard focus and
    // ImGui capture must not disable this dialog's own cancel button.
    sample.focused = input::HostInputFocused();
    if (!sample.connected) {
      return sample;
    }
    const uint16_t buttons = state.gamepad.buttons;
    if (buttons & X_INPUT_GAMEPAD_A) sample.buttons |= Pad::ShowKeyboard;
    if (buttons & X_INPUT_GAMEPAD_B) sample.buttons |= Pad::Cancel;
    return sample;
  };
  keyboard.font = g_font;
  keyboard.heading_font = g_heading_font;
  keyboard.background = ImVec4(0.025f, 0.015f, 0.02f, 0.66f);
  keyboard.border = ImVec4(1.0f, 0.08f, 0.10f, 1.0f);
  keyboard.border_highlight = ImVec4(1.0f, 0.70f, 0.65f, 0.95f);
  keyboard.layered_border = true;
  keyboard.text = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  keyboard.muted_text = ImVec4(0.94f, 0.92f, 0.91f, 1.0f);
  keyboard.input = ImVec4(0.0f, 0.0f, 0.0f, 0.48f);
  keyboard.accent = ImVec4(0.55f, 0.035f, 0.045f, 0.60f);
  keyboard.accent_hovered = ImVec4(0.85f, 0.06f, 0.08f, 0.78f);
  keyboard.accent_active = ImVec4(0.40f, 0.02f, 0.03f, 0.85f);
  keyboard.secondary = ImVec4(0.06f, 0.02f, 0.025f, 0.40f);
  keyboard.dim = ImVec4(0.0f, 0.0f, 0.0f, 0.12f);

  // Keep the existing unlock toast compact while sharing the keyboard's fonts
  // and palette, so the two player-facing SDK surfaces stay visually in sync.
  auto &toast = style.toast;
  toast.themed = true;
  toast.font = keyboard.font;
  toast.heading_font = keyboard.heading_font;
  toast.background = keyboard.background;
  toast.background_alpha = keyboard.background.w;
  toast.border = keyboard.border;
  toast.border_highlight = keyboard.border_highlight;
  toast.text = keyboard.text;
  toast.title = keyboard.text;
  toast.muted_text = keyboard.muted_text;
}
}  // namespace rb_blitz::ui
