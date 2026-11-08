// SPDX-License-Identifier: GPL-2.0-only
#include "ui/boot_loading_icon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <rex/ui/imgui_drawer.h>
#include <rex/logging.h>

namespace rb_blitz::ui {

namespace {

ImU32 AuthoredSpinnerTint(float seconds) {
  // spinner.mnm's five color keys and texture scroll both span 240 frames
  // at 30 fps.
  struct Key {
    float frame, r, g, b;
  };
  constexpr std::array<Key, 5> keys{{
      {0, 1.0f, 0.502f, 0.0f},
      {60, 0.0f, 0.502f, 0.0f},
      {120, 1.0f, 1.0f, 0.0f},
      {180, 0.0f, 0.502f, 1.0f},
      {240, 1.0f, 0.502f, 0.251f},
  }};
  const float frame = std::fmod(seconds * 30.0f, 240.0f);
  size_t key = 0;
  while (key + 2 < keys.size() && frame >= keys[key + 1].frame) ++key;
  const auto& a = keys[key];
  const auto& b = keys[key + 1];
  const float weight = (frame - a.frame) / (b.frame - a.frame);
  auto channel = [&](float start, float end) {
    return int(std::lround(std::clamp(
        start + (end - start) * weight, 0.0f, 1.0f) * 255.0f));
  };
  return IM_COL32(channel(a.r, b.r), channel(a.g, b.g),
                  channel(a.b, b.b), 255);
}

}  // namespace

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
  constexpr size_t kFrameStride = boot_loading_art::kFrameSize * 4;
  constexpr size_t kFrameBytes = kFrameStride * boot_loading_art::kFrameSize;
  constexpr size_t kAtlasStride = kAtlasSize * 4;
  for (auto& atlas : atlases_rgba_)
    atlas.assign(kAtlasSize * kAtlasSize * 4, 0);
  for (size_t frame = 0; frame < frames.size(); ++frame) {
    if (frames[frame].size() != kFrameBytes) continue;
    const size_t atlas_index = frame / kFramesPerAtlas;
    const size_t cell = frame % kFramesPerAtlas;
    const size_t base_x = (cell % kAtlasColumns) * boot_loading_art::kFrameSize;
    const size_t base_y = (cell / kAtlasColumns) * boot_loading_art::kFrameSize;
    auto& atlas = atlases_rgba_[atlas_index];
    for (size_t row = 0; row < boot_loading_art::kFrameSize; ++row)
      std::copy_n(frames[frame].data() + row * kFrameStride, kFrameStride,
                  atlas.data() + (base_y + row) * kAtlasStride + base_x * 4);
  }
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
    for (auto& atlas : atlases_rgba_) atlas.clear();
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
    for (auto& atlas : atlases_rgba_) atlas.clear();
  }
  if (!panel_observed_ && native_panel_ready_ && native_panel_ready_()) {
    panel_observed_ = true;
  }
  if (!finished_ && panel_observed_ && guest_output_sequence_ &&
      guest_output_sequence_() > 0) {
    finished_ = true;
    REXLOG_INFO("boot_loading: autosave icon handed off to native guest screen");
    ReleaseTextures();
    for (auto& atlas : atlases_rgba_) atlas.clear();
  }
  if (finished_ || io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;

  const float seconds = std::chrono::duration<float>(
      std::chrono::steady_clock::now() - animation_started_).count();
  const size_t frame_index = static_cast<size_t>(seconds * 30.0f) %
                             boot_loading_art::kAnimationFrameCount;
  const size_t atlas_index = frame_index / kFramesPerAtlas;
  if (atlases_rgba_[atlas_index].size() != kAtlasSize * kAtlasSize * 4)
    return;

  const auto generation = imgui_drawer()->image_texture_generation();
  if (texture_generation_ != generation) {
    ReleaseTextures();
    texture_generation_ = generation;
  }
  // Five 504x504 atlases hold the full 240-frame authored scroll at 30 fps
  // within the SDK's eight-texture limit.
  for (size_t i = 0; i < textures_.size(); ++i) {
    if (!textures_[i] && !atlases_rgba_[i].empty())
      textures_[i] = imgui_drawer()->CreateImageTexture(
          kAtlasSize, kAtlasSize, atlases_rgba_[i].data());
  }
  const auto texture = textures_[atlas_index];
  if (!texture) return;

  // The title's menu places the small autosave spinner near the upper right.
  // The 2339x986 reference puts its visible motif near (1860, 72), about
  // 48 px across. The source frame has transparent padding around that motif.
  const float side = io.DisplaySize.y * 0.055f;
  // The startup warning's motif stays oriented while its material scrolls.
  // The separate spinner panel's PropAnim rotation is not played here.
  const float half = side * 0.5f;
  const float center_x = io.DisplaySize.x * 0.795f;
  const float center_y = io.DisplaySize.y * 0.073f;
  auto corner = [&](float x, float y) {
    return ImVec2{center_x + x, center_y + y};
  };
  const size_t cell = frame_index % kFramesPerAtlas;
  const float uv_left = float((cell % kAtlasColumns) * boot_loading_art::kFrameSize) /
                        float(kAtlasSize);
  const float uv_top = float((cell / kAtlasColumns) * boot_loading_art::kFrameSize) /
                       float(kAtlasSize);
  const float uv_right = uv_left + 1.0f / float(kAtlasColumns);
  const float uv_bottom = uv_top + 1.0f / float(kAtlasRows);
  ImGui::GetForegroundDrawList()->AddImageQuad(
      texture, corner(-half, -half), corner(half, -half),
      corner(half, half), corner(-half, half),
      ImVec2(uv_left, uv_top), ImVec2(uv_right, uv_top),
      ImVec2(uv_right, uv_bottom), ImVec2(uv_left, uv_bottom),
      AuthoredSpinnerTint(seconds));
  if (!first_draw_logged_) {
    first_draw_logged_ = true;
    REXLOG_INFO("boot_loading: autosave icon first draw");
  }
}

}  // namespace rb_blitz::ui
