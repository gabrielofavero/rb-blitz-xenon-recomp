// SPDX-License-Identifier: GPL-2.0-only
#include "input/native_list_draw.h"

#include <rex/memory/endian.h>
#include <rex/ppc/call.h>
#include <rex/ppc/hook.h>
#include <rex/utility.h>

REX_EXTERN(__imp__sub_824E5020); // UIListDir::Draw
REX_EXTERN(sub_824E56F8);       // UIListDir::ScrollStart (attach and fill spare elements)
REX_EXTERN(sub_824BF538);       // UIListWidget::ScrollComplete (return spare element)

namespace rb_blitz::input {
void DrawNativeList(PPCContext& ctx, uint8_t* base) {
  const auto word = [base](uint32_t address) {
    return rex::memory::load_and_swap<uint32_t>(base + address);
  };
  const uint32_t resource = ctx.r3.u32, draw = ctx.r4.u32;
  const uint32_t state = ctx.r5.u32, widgets = ctx.r6.u32;
  const uint32_t first = word(draw + 56), end = word(draw + 60);
  const uint32_t display = word(state + 4);
  const uint32_t rows = end >= first ? (end - first) / 44 : 0;
  // A synthetic fractional draw has one more row than the settled viewport.
  // Native ScrollStart normally lends each widget its cached spare first; a
  // temporary BuildDrawState interpolation alone does not perform that step.
  const bool extra = end >= first && (end - first) % 44 == 0 &&
      rows > 0 && rows - 1 == display &&
      word(state + 48) == word(state + 52);
  if (!extra) {
    __imp__sub_824E5020(ctx, base);
    return;
  }

  const uint32_t direction = word(resource + 640);
  const auto call = [&](auto& function, uint32_t object, uint32_t a,
                        uint32_t b, uint32_t c = 0, uint32_t d = 0) {
    PPCContext side(ctx);
    rex::ppc::OpenCallFrame(side, base, 256);
    rex::ppc::Call<void>(side, base, function, object, a, b, c, d);
    ctx.fpscr = side.fpscr;
  };
  call(sub_824E56F8, resource, state, widgets, 1, 1);
  rex::ScopeGuard restore([&] {
    // Return the last element, not the first: this was a draw-only loan, so
    // the integer viewport and the ordering of its existing elements stay put.
    for (auto entry = word(widgets); entry < word(widgets + 4); entry += 4)
      call(sub_824BF538, word(entry), state, uint32_t(-1));
    rex::memory::store_and_swap<uint32_t>(base + resource + 640, direction);
  });
  __imp__sub_824E5020(ctx, base);
}
}  // namespace rb_blitz::input
