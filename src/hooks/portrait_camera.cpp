// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/render_settings.h"
#include "settings/portrait_render_target.h"

#include <mutex>
#include <unordered_map>
#include <rex/logging/log_category.h>
#include <rex/memory/endian.h>
#include <rex/ppc/call.h>
#include <rex/ppc/hook.h>

REX_EXTERN(__imp__sub_821FF728);  // RndCam: rebuild projection and inverse.
REX_EXTERN(__imp__sub_821FFBC8);  // RndCam: select the current camera.
REX_EXTERN(sub_821FF8E8);        // Rebuild world/screen matrices from projection.
REX_EXTERN(sub_821FF728);
REX_EXTERN(__imp__sub_821FFD30);  // RndCam destructor, virtual object view +900.
REX_EXTERN(sub_821FFBC8);

namespace {
std::mutex camera_mutex;
std::unordered_map<uint32_t, float> camera_scales;

float ProjectionScale(uint8_t* base, uint32_t camera) {
  if (!base || !camera) return 1.f;
  const auto word = [&](uint32_t offset) {
    return rex::memory::load_and_swap<uint32_t>(base + camera + offset);
  };
  const auto number = [&](uint32_t offset) {
    return rex::memory::load_and_swap<float>(base + camera + offset);
  };
  if (!rb_blitz::settings::PortraitTargetCameraEligible(
          word(756), number(728), number(732), number(736), number(740))) return 1.f;
  return rb_blitz::settings::PortraitRenderTargetProjectionScale();
}
}  // namespace

void rb_blitz::settings::RefreshPortraitRenderTargetCamera(
    PPCContext& ctx, uint8_t* base, uint32_t camera) {
  if (base && camera) rex::ppc::Call<void>(ctx, base, sub_821FFBC8, camera);
}

REX_HOOK_RAW(sub_821FF728) {
  const auto camera = ctx.r3.u32;
  __imp__sub_821FF728(ctx, base);
  if (!base || !camera) return;
  const auto scale = ProjectionScale(base, camera);
  if (scale != 1.f) {
    // The native projection uses X as its first diagonal. Narrow its horizontal
    // coverage without changing Y, depth, FOV preferences or camera transforms.
    const auto x = rex::memory::load_and_swap<float>(base + camera + 256);
    const auto inverse_x = rex::memory::load_and_swap<float>(base + camera + 320);
    rex::memory::store_and_swap<float>(base + camera + 256, x * scale);
    rex::memory::store_and_swap<float>(base + camera + 320, inverse_x / scale);
    // Native camera culling uses the local six-plane frustum at +512 and its
    // transformed copy at +608. Match horizontal culling to the compact raster
    // target, keeping near/far and vertical coverage unchanged.
    for (const uint32_t offset : {544u, 560u}) {
      std::array<float, 4> plane;
      for (size_t i = 0; i < plane.size(); ++i)
        plane[i] = rex::memory::load_and_swap<float>(base + camera + offset + i * 4);
      plane = rb_blitz::settings::PortraitTargetHorizontalPlane(plane, scale);
      for (size_t i = 0; i < plane.size(); ++i)
        rex::memory::store_and_swap<float>(base + camera + offset + i * 4, plane[i]);
    }
    rex::ppc::Call<void>(ctx, base, sub_821FF8E8, camera);
  }
  float previous = 1.f;
  {
    std::lock_guard lock(camera_mutex);
    if (const auto it = camera_scales.find(camera); it != camera_scales.end()) previous = it->second;
    if (scale == 1.f) camera_scales.erase(camera);
    else camera_scales[camera] = scale;
  }
  if (previous != scale)
    REXLOG_INFO("portrait: camera {:08X} horizontal projection scale {}", camera, scale);
}

REX_HOOK_RAW(sub_821FFBC8) {
  const auto camera = ctx.r3.u32;
  const auto desired = ProjectionScale(base, camera);
  float applied = 1.f;
  {
    std::lock_guard lock(camera_mutex);
    if (const auto it = camera_scales.find(camera); it != camera_scales.end()) applied = it->second;
  }
  if (camera && desired != applied) {
    // Native camera selection otherwise reuses projections after resolution
    // changes. Reconstruct from authored values before applying the new crop;
    // this also restores existing cameras on return to landscape/gameplay.
    rex::ppc::Call<void>(ctx, base, sub_821FF728, camera);
  }
  __imp__sub_821FFBC8(ctx, base);
}

REX_HOOK_RAW(sub_821FFD30) {
  // Native destruction compares this same -900 view with RndCam::sCurrent.
  // Retire host metadata before the guest can reuse the camera's address.
  if (ctx.r3.u32 >= 900) {
    std::lock_guard lock(camera_mutex);
    camera_scales.erase(ctx.r3.u32 - 900);
  }
  __imp__sub_821FFD30(ctx, base);
}
