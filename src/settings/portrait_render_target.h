// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "settings/capabilities.h"

namespace rb_blitz::settings {

// The active visual orientation can differ from the selected quality profile:
// pause and confirmations retain gameplay quality while using Interface layout.
inline bool CompactPortraitContext(bool game_screen, bool gameplay_profile,
                                    bool interface_portrait, bool gameplay_portrait,
                                    bool authored_interface) {
  const bool portrait = game_screen ? gameplay_portrait : interface_portrait;
  return portrait && (game_screen || gameplay_profile || authored_interface);
}

// Keep the original source's vertical coverage while allocating only the
// centered portrait crop. Resolution preferences continue to describe the
// landscape source, so their stored pixel-height and native semantics survive.
inline GuestScreenPlan CompactPortraitRenderTarget(GuestScreenPlan source, bool enabled) {
  if (!source.supported || !enabled) return source;
  const auto width = source.guest_size.width;
  const auto height = source.guest_size.height;
  if (width <= 0 || height <= 0 || width <= height) return source;
  source.guest_size.width = std::max(1, int32_t(
      (int64_t(height) * height + width / 2) / width));
  return source;
}

inline float PortraitTargetProjectionScale(uint32_t source_width, uint32_t target_width) {
  return source_width && target_width && source_width > target_width
      ? float(source_width) / target_width : 1.f;
}

// The local frustum's horizontal planes must describe the same crop as the
// projection. Renormalize distance too, preserving native sphere-radius tests.
// This also handles orthographic cameras without changing their vertical span.
inline std::array<float, 4> PortraitTargetHorizontalPlane(std::array<float, 4> plane,
                                                        float scale) {
  if (!std::isfinite(scale) || scale <= 1.f) return plane;
  auto adjusted = plane;
  adjusted[0] *= scale;
  const float length = std::hypot(adjusted[0], adjusted[1], adjusted[2]);
  if (!std::isfinite(length) || length <= 0.f) return plane;
  for (auto& value : adjusted) value /= length;
  return adjusted;
}

// Native output observation and pointer mapping use the same frontbuffer aspect.
inline DisplayMode PresentationPointerSource(DisplayMode rendered, DisplayMode video,
                                              bool source_aspect) {
  if (!source_aspect) return video;
  if (rendered.height <= 0) return video;
  if (rendered.width <= 0) rendered.width = int32_t(int64_t(rendered.height) * 16 / 9);
  return rendered;
}

// Full-screen cameras render the same centered crop that the presenter used
// previously. Custom texture cameras and partial viewports have independent
// projections and retain their native setup.
inline bool PortraitTargetCameraEligible(uint32_t texture, float x, float y,
                                         float width, float height) {
  return !texture && x == 0.f && y == 0.f && width == 1.f && height == 1.f;
}

}  // namespace rb_blitz::settings
