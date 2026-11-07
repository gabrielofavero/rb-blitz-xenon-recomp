// SPDX-License-Identifier: GPL-2.0-only
#include "ui/boot_loading_icon.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <rex/ui/imgui_drawer.h>
#include <rex/logging.h>

namespace rb_blitz::ui {

BootLoadingIcon::BootLoadingIcon(rex::ui::ImGuiDrawer* drawer,
                                 std::function<uint64_t()> guest_output_sequence,
                                 std::function<bool()> native_panel_ready,
                                 std::function<bool()> native_splash_ready,
                                 std::function<bool()> native_logo_load_started)
    : ImGuiDialog(drawer),
      guest_output_sequence_(std::move(guest_output_sequence)),
      native_panel_ready_(std::move(native_panel_ready)),
      native_splash_ready_(std::move(native_splash_ready)),
      native_logo_load_started_(std::move(native_logo_load_started)) {}

BootLoadingIcon::~BootLoadingIcon() { ReleaseTextures(); }

void BootLoadingIcon::SetFrames(boot_loading_art::BootLoadingFrames frames) {
  ReleaseTextures();
  frames_rgba_ = std::move(frames);
  animation_started_ = std::chrono::steady_clock::now();
}

void BootLoadingIcon::ReleaseTextures() {
  for (auto& texture : textures_) {
    if (texture) imgui_drawer()->ReleaseImageTexture(texture, texture_generation_);
    texture = {};
  }
}

void BootLoadingIcon::OnDraw(ImGuiIO& io) {
  // The native splash can be visible without a new presenter refresh counter.
  // Never paint the host boot icon over that authored screen.
  if (!finished_ && native_splash_ready_ && native_splash_ready_()) {
    finished_ = true;
    REXLOG_INFO("boot_loading: autosave icon handed off to native splash");
    ReleaseTextures();
    for (auto& frame : frames_rgba_) frame.clear();
  }
  if (!logo_load_observed_ && native_logo_load_started_ &&
      native_logo_load_started_()) {
    logo_load_observed_ = true;
    logo_guest_sequence_ = guest_output_sequence_ ? guest_output_sequence_() : 0;
  }
  if (!finished_ && logo_load_observed_ && guest_output_sequence_ &&
      guest_output_sequence_() > logo_guest_sequence_) {
    finished_ = true;
    REXLOG_INFO("boot_loading: autosave icon handed off to startup logo");
    ReleaseTextures();
    for (auto& frame : frames_rgba_) frame.clear();
  }
  if (!panel_observed_ && native_panel_ready_ && native_panel_ready_()) {
    panel_observed_ = true;
  }
  if (!finished_ && panel_observed_ && guest_output_sequence_ &&
      guest_output_sequence_() > 0) {
    finished_ = true;
    REXLOG_INFO("boot_loading: autosave icon handed off to native guest screen");
    ReleaseTextures();
    for (auto& frame : frames_rgba_) frame.clear();
  }
  if (finished_ || io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;

  constexpr size_t kFrameBytes = boot_loading_art::kFrameSize *
                                 boot_loading_art::kFrameSize * 4;
  const float seconds = std::chrono::duration<float>(
      std::chrono::steady_clock::now() - animation_started_).count();
  const size_t frame_index = static_cast<size_t>(seconds * 30.0f) %
                             boot_loading_art::kAnimationFrameCount;
  if (frames_rgba_[frame_index].size() != kFrameBytes) return;

  const auto generation = imgui_drawer()->image_texture_generation();
  if (texture_generation_ != generation) {
    ReleaseTextures();
    texture_generation_ = generation;
  }
  auto& texture = textures_[frame_index];
  if (!texture)
    texture = imgui_drawer()->CreateImageTexture(
        boot_loading_art::kFrameSize, boot_loading_art::kFrameSize,
        frames_rgba_[frame_index].data());
  if (!texture) return;

  const float side = std::min<float>(boot_loading_art::kFrameSize,
                                     std::min(io.DisplaySize.x, io.DisplaySize.y) * 0.18f);
  // PropAnim.anim rotates spinner.trans by -45 degrees over 45 frames at
  // 30 fps. Eightfold symmetry makes the authored reset seamless.
  constexpr float kRadiansPerSecond = -3.14159265f / 6.0f;
  const float angle = seconds * kRadiansPerSecond;
  const float cosine = std::cos(angle), sine = std::sin(angle);
  const float half = side * 0.5f;
  const float center_x = io.DisplaySize.x * 0.5f;
  const float center_y = io.DisplaySize.y * 0.5f;
  auto corner = [&](float x, float y) {
    return ImVec2{center_x + x * cosine - y * sine,
                  center_y + x * sine + y * cosine};
  };
  ImGui::GetForegroundDrawList()->AddImageQuad(
      texture, corner(-half, -half), corner(half, -half),
      corner(half, half), corner(-half, half));
  if (!first_draw_logged_) {
    first_draw_logged_ = true;
    REXLOG_INFO("boot_loading: autosave icon first draw");
  }
}

}  // namespace rb_blitz::ui
