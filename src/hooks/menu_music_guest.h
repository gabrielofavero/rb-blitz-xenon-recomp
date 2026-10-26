// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string_view>
#include <rex/hook.h>
#include <rex/system/xmemory.h>

REX_EXTERN(__imp__sub_8245C878);  // Symbol(out_slot, chars)

namespace rb_blitz::shell_music {
// read_file("rbbz-play/song") returns a native Symbol DataNode. Use the guest
// intern table, preserving case, ownership and the caller's register context.
inline void ReturnSongSymbol(PPCContext& ctx, uint8_t* base, std::string_view key) {
  if (key.size() > 4096) key = {};  // bound scratch stack use for malformed metadata
  rex::CallFrame frame(ctx);
  auto& call = frame.ctx;
  call.r1.u32 -= static_cast<uint32_t>((key.size() + 1 + 0x100 + 15) & ~size_t{15});
  const uint32_t slot = call.r1.u32 + 0x70;
  const uint32_t chars = call.r1.u32 + 0x80;
  if (!key.empty()) std::memcpy(base + chars, key.data(), key.size());
  base[chars + key.size()] = 0;
  call.r3.u32 = slot;
  call.r4.u32 = chars;
  __imp__sub_8245C878(call, base);
  const auto symbol = rex::memory::load_and_swap<uint32_t>(base + slot);
  rex::memory::store_and_swap<uint32_t>(base + ctx.r3.u32, symbol);
  rex::memory::store_and_swap<uint32_t>(base + ctx.r3.u32 + 4, 5);
}
}  // namespace rb_blitz::shell_music
