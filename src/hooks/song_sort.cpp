// SPDX-License-Identifier: GPL-2.0-only
// Rock Band Blitz's difficulty heading lookup indexes the `rank` DataArray
// directly with RankTier(). A missing/zero band rank returns tier 0, but slot 0
// is the symbol "rank", not a tier array. Reading it as an array dereferences
// ASCII "rank" + 4 (guest 0x72616E6F, owner logs rb_blitz_322/323).
//
// Only the heading lookup needs a fallback: put unranked songs under the first
// difficulty heading. Keep the original numeric ranks and sorting order, and
// preserve the retail lookup for every normal tier (1 through 7).
#include <rex/hook.h>

REX_EXTERN(__imp__sub_821D0F30);

REX_HOOK_RAW(sub_821D0F30) {
  if (ctx.r5.s32 <= 0) {
    ctx.r5.s64 = 1;
  }
  __imp__sub_821D0F30(ctx, base);
}
