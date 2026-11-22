// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/portrait_layout.h"
#include "input/native_menu.h"

#include <bit>
#include <rex/ppc/hook.h>
#include <rex/ppc/call.h>
#include <rex/memory/endian.h>
#include <rex/utility.h>

REX_EXTERN(__imp__sub_824C8E88); // PanelDir::DrawShowing, before component traversal
REX_EXTERN(sub_821F0300);       // RndTransformable::SetWorldDirty

namespace {
thread_local rb_blitz::ui::PortraitFit draw_fit;
thread_local bool drawing = false;
}

rb_blitz::ui::PortraitFit rb_blitz::hooks::PortraitDrawFit() { return draw_fit; }

rb_blitz::input::MenuPoint rb_blitz::input::PortraitDrawPoint(float x, float z) {
  return {draw_fit.X(x), draw_fit.Z(z)};
}

REX_HOOK_RAW(sub_824C8E88) {
  const uint32_t drawable = ctx.r3.u32;
  const auto aspect = drawing ? std::optional<float>{} :
      rb_blitz::input::PortraitPanelDrawAspect(base, drawable);
  if (!aspect) {
    __imp__sub_824C8E88(ctx, base);
    return;
  }
  const auto fit = rb_blitz::ui::FitPortraitViewport(*aspect,
      rb_blitz::ui::CurrentPortraitViewport());
  // Fit once at the owning panel, before components and ordinary drawables.
  // PanelDir's RndDrawable view has its transform at +80 (821F65C8).
  // Nested resource dirs inherit it, and the native camera projects hit bounds
  // through exactly the same matrices. No fitted state survives the draw.
  const uint32_t transform = drawable + 80;
  std::array<uint32_t, 16> prior{};
  std::array<float, 16> matrix{};
  for (uint32_t i = 0; i < prior.size(); ++i) {
    prior[i] = rex::memory::load_and_swap<uint32_t>(base + transform + 8 + i * 4);
    matrix[i] = std::bit_cast<float>(prior[i]);
  }
  matrix = rb_blitz::ui::FitPortraitTransform(matrix, fit);
  for (uint32_t i = 0; i < matrix.size(); ++i)
    rex::memory::store_and_swap<float>(base + transform + 8 + i * 4, matrix[i]);
  rex::ppc::Call<void>(ctx, base, sub_821F0300, transform);
  draw_fit = fit;
  drawing = true;
  rex::ScopeGuard restore([&] {
    for (uint32_t i = 0; i < prior.size(); ++i)
      rex::memory::store_and_swap<uint32_t>(base + transform + 8 + i * 4, prior[i]);
    rex::ppc::Call<void>(ctx, base, sub_821F0300, transform);
    draw_fit = {};
    drawing = false;
  });
  __imp__sub_824C8E88(ctx, base);
}
