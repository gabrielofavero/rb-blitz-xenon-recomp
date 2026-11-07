// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <chrono>
#include <functional>
#include <vector>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

#include "ui/boot_loading_art.h"

namespace rb_blitz::ui {

// Displays the game's autosave spinner only until the first guest image is
// published. Its pixels come from the installed game archive at runtime.
class BootLoadingIcon final : public rex::ui::ImGuiDialog {
 public:
  BootLoadingIcon(rex::ui::ImGuiDrawer* drawer,
                  std::function<uint64_t()> guest_output_sequence,
                  std::function<bool()> native_panel_ready,
                  std::function<bool()> native_splash_ready,
                  std::function<bool()> native_logo_load_started);
  ~BootLoadingIcon() override;

  void SetFrames(boot_loading_art::BootLoadingFrames frames);

 private:
  void OnDraw(ImGuiIO& io) override;
  void ReleaseTextures();

  std::function<uint64_t()> guest_output_sequence_;
  std::function<bool()> native_panel_ready_;
  std::function<bool()> native_splash_ready_;
  std::function<bool()> native_logo_load_started_;
  boot_loading_art::BootLoadingFrames frames_rgba_;
  std::array<ImTextureID, boot_loading_art::kAnimationFrameCount> textures_{};
  uint64_t texture_generation_ = 0;
  std::chrono::steady_clock::time_point animation_started_ =
      std::chrono::steady_clock::now();
  bool finished_ = false;
  bool first_draw_logged_ = false;
  bool panel_observed_ = false;
  bool logo_load_observed_ = false;
  uint64_t logo_guest_sequence_ = 0;
};

}  // namespace rb_blitz::ui
