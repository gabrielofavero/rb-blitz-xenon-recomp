// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
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
  static constexpr size_t kAtlasColumns = 7;
  static constexpr size_t kAtlasRows = 7;
  static constexpr size_t kFramesPerAtlas = kAtlasColumns * kAtlasRows;
  static constexpr size_t kAtlasCount =
      (boot_loading_art::kAnimationFrameCount + kFramesPerAtlas - 1) /
      kFramesPerAtlas;
  static constexpr uint32_t kAtlasSize =
      boot_loading_art::kFrameSize * kAtlasColumns;
  static_assert(kAtlasSize <= 512 && kAtlasCount <= 8,
                "boot spinner atlases must fit the SDK image texture limits");

  void OnDraw(ImGuiIO& io) override;
  void ReleaseTextures();

  std::function<uint64_t()> guest_output_sequence_;
  std::function<bool()> native_panel_ready_;
  std::function<bool()> native_splash_ready_;
  std::function<bool()> native_logo_load_started_;
  std::array<std::vector<uint8_t>, kAtlasCount> atlases_rgba_;
  std::array<ImTextureID, kAtlasCount> textures_{};
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
