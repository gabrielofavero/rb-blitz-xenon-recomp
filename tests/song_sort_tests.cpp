// SPDX-License-Identifier: GPL-2.0-only
// Execute the generated retail rank and heading routines against isolated data.
// No game process, files or UI are involved.
#include "rb_blitz_pch.h"
#include "tests/check.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>

REX_EXTERN(sub_821D16A8);
REX_EXTERN(sub_821D0F30);
REX_EXTERN(__imp__sub_821D0F30);
REX_EXTERN(__savegprlr_25);
REX_EXTERN(__restgprlr_25);
REX_EXTERN(sub_8245C878);
REX_EXTERN(sub_82361DA0);
REX_EXTERN(sub_823F9630);
REX_EXTERN(sub_823F93D0);
REX_EXTERN(sub_823F94A0);
REX_EXTERN(sub_821C9550);
REX_EXTERN(sub_823FAE00);

// The isolated fixture has no script commands or symbol construction. These
// four engine services supply its already-evaluated nodes and config table;
// the rank calculation and lookup below are the actual generated retail code.
REX_HOOK_RAW(sub_8245C878) { std::abort(); }
REX_HOOK_RAW(sub_82361DA0) { ctx.r3.u64 = 0x10000; }
REX_HOOK_RAW(sub_823F93D0) {}
REX_HOOK_RAW(sub_823F94A0) {
  REX_STORE_U32(ctx.r3.u32, REX_LOAD_U32(ctx.r4.u32));
  REX_STORE_U32(ctx.r3.u32 + 4, REX_LOAD_U32(ctx.r4.u32 + 4));
}

#include "song_sort_guest.inc"

uintptr_t fault_address;
int FaultFilter(EXCEPTION_POINTERS* info) {
  fault_address = info->ExceptionRecord->ExceptionInformation[1];
  return info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
             ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
bool OriginalFaults(PPCContext& ctx, uint8_t* base) {
  __try { __imp__sub_821D0F30(ctx, base); }
  __except (FaultFilter(GetExceptionInformation())) { return true; }
  return false;
}

int main() {
  using namespace rb_blitz::test;
  auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000000ull,
                                                MEM_RESERVE, PAGE_NOACCESS));
  if (!base || !VirtualAlloc(base + 0x10000, 0x20000, MEM_COMMIT, PAGE_READWRITE) ||
      !VirtualAlloc(base + 0x82840000, 0x20000, MEM_COMMIT, PAGE_READWRITE)) return 2;
  const uint32_t rank = 0x10000, nodes = 0x11000, band = 0x20000;
  std::memcpy(base + 0x20100, "rank\0\0\0\0song_groupings", 22);
  REX_STORE_U32(rank, nodes);
  REX_STORE_U16(rank + 8, uint16_t(8));
  REX_STORE_U32(nodes, 0x20100);
  REX_STORE_U32(nodes + 4, 5);
  for (uint32_t tier = 1; tier <= 7; ++tier) {
    const auto row = 0x12000 + tier * 0x100;
    REX_STORE_U32(nodes + tier * 8, row);
    REX_STORE_U32(nodes + tier * 8 + 4, 16);
    REX_STORE_U32(row, row + 0x20);
    REX_STORE_U16(row + 8, uint16_t(1));
    REX_STORE_U32(row + 0x20, row + 0x40);
    REX_STORE_U32(row + 0x24, 16);
    REX_STORE_U32(row + 0x40, row + 0x60);
    REX_STORE_U32(row + 0x60, band);
    REX_STORE_U32(row + 0x64, 5);
    REX_STORE_U32(row + 0x68, 0x21000 + tier);
    REX_STORE_U32(row + 0x6c, 5);
  }
  REX_STORE_U32(0x82850000u - 25948, 7);
  REX_STORE_U32(0x82850000u - 25960, band);
  REX_STORE_U32(0x18000 + 196, 0x16000);
  REX_STORE_U32(0x16000, 0x18000 + 196);
  REX_STORE_U32(0x16008, band);
  REX_STORE_U32(0x1600c, 0x17000);
  REX_STORE_U32(0x16010, 0x17000 + 28);
  const int thresholds[] = {1, 165, 215, 243, 267, 292, 345};
  for (int i = 0; i < 7; ++i) REX_STORE_U32(0x17000 + i * 4, uint32_t(thresholds[i]));

  BeginCase("missing band rank reproduces the retail ASCII-rank pointer fault");
  PPCContext original{};
  original.r1.u64 = 0x2f000;
  original.r3.u64 = 0x19000;
  original.r5.u64 = 0;
  CHECK_TRUE(OriginalFaults(original, base));
  CHECK_EQ(fault_address - reinterpret_cast<uintptr_t>(base), 0x72616e6f);

  BeginCase("native rank calculation and patched heading agree at every band threshold");
  const float ranks[] = {-1, 0, 0.5f, 1, 164, 165, 214, 215, 242, 243,
                         266, 267, 291, 292, 344, 345, 1000};
  for (float value : ranks) {
    PPCContext ctx{};
    ctx.r1.u64 = 0x2f000;
    ctx.r3.u64 = 0x18000;
    ctx.r5.u64 = band;
    ctx.f1.f64 = value;
    sub_821D16A8(ctx, base);
    const auto expected = std::upper_bound(std::begin(thresholds), std::end(thresholds), value)
                          - std::begin(thresholds);
    CHECK_EQ(ctx.r3.u32, expected);
    ctx.r3.u64 = 0x19000;
    ctx.r4.u64 = 0x18000;
    ctx.r5.u64 = expected;
    sub_821D0F30(ctx, base);
    CHECK_EQ(ctx.r3.u32, 0x19000);
    CHECK_EQ(REX_LOAD_U32(0x19000), 0x21000 + std::max<ptrdiff_t>(1, expected));
    CHECK_EQ(REX_LOAD_U32(0x19004), 5);
    CHECK_EQ(ctx.r1.u32, 0x2f000);
    if (expected > 0) {
      ctx.r3.u64 = 0x19100;
      ctx.r4.u64 = 0x18000;
      ctx.r5.u64 = expected;
      __imp__sub_821D0F30(ctx, base);
      CHECK_EQ(REX_LOAD_U32(0x19100), REX_LOAD_U32(0x19000));
    }
  }
  VirtualFree(base, 0, MEM_RELEASE);
  return Finish();
}
