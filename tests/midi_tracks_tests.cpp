// SPDX-License-Identifier: GPL-2.0-only
// Exercise the actual retail decision and skip functions, with isolated guest
// memory and only symbol/config/stream services supplied by the fixture.
#include "rb_blitz_pch.h"
#include "tests/check.h"
#include <array>
#include <string_view>

namespace {
constexpr uint32_t kParser = 0x1000, kReader = 0x2000, kSinkTable = 0x3000;
constexpr uint32_t kName = 0x4000, kStream = 0x5000, kEnd = 0xE2F91;
unsigned seeks = 0, ends = 0;
void Indirect(PPCContext& ctx, uint8_t*, uint32_t address) {
  CHECK_EQ(address, 0x12345678);
  CHECK_EQ(ctx.r3.u32, kParser);
  ++ends;
}
}
#undef REX_CALL_INDIRECT_FUNC
#define REX_CALL_INDIRECT_FUNC(address) Indirect(ctx, base, address)

REX_EXTERN(sub_82438880);
REX_EXTERN(__imp__sub_82438880);
REX_EXTERN(sub_824390B0);
REX_EXTERN(sub_824A8D90);
REX_EXTERN(sub_824A7280);
REX_EXTERN(__savegprlr_28);
REX_EXTERN(__restgprlr_28);
// Symbol(out, chars) returns an already interned fixture string.
REX_HOOK_RAW(sub_8245C878) { REX_STORE_U32(ctx.r3.u32, ctx.r4.u32); }
// These config lookups are not reached by the first-pass rejection protocol.
REX_HOOK_RAW(sub_824378F8) { std::abort(); }
REX_HOOK_RAW(sub_824387D8) { std::abort(); }
REX_HOOK_RAW(sub_82199308) { std::abort(); }
REX_HOOK_RAW(sub_82437990) { std::abort(); }
REX_HOOK_RAW(sub_82438A90) { std::abort(); }
// Stream::Seek(offset, absolute). The native skip supplies the MTrk end,
// without interpreting or delivering its 38,000+ optional text events.
REX_HOOK_RAW(sub_82461290) {
  CHECK_EQ(ctx.r3.u32, kStream);
  CHECK_EQ(ctx.r4.u32, kEnd);
  CHECK_EQ(ctx.r5.u32, 0);
  ++seeks;
}
#include "midi_tracks_guest.inc"

int main() {
  using namespace rb_blitz::test;
  std::array<uint8_t, 0x30000> memory{};
  auto* base = memory.data();
  REX_STORE_U32(kParser + 4, kReader);
  REX_STORE_U32(kParser, kSinkTable);
  REX_STORE_U32(kSinkTable + 8, 0x12345678);
  REX_STORE_U32(kReader, kStream);
  REX_STORE_U32(kReader + 16, kParser);
  REX_STORE_U16(kReader + 24, 12);
  REX_STORE_U32(kReader + 32, kEnd);
  REX_STORE_U32(kReader + 36, 11); // a following track still exists
  const auto context = [&] {
    PPCContext ctx{};
    ctx.r1.u64 = 0x2F000;
    ctx.r3.u64 = kParser;
    ctx.r4.u64 = kName;
    ctx.lr = 0x82439100;
    return ctx;
  };
  BeginCase("retail first pass admits LIPSYNC1; extension rejects only optional tracks");
  for (const auto* name : {"LIPSYNC1", "LIPSYNC2", "LIPSYNC3", "LIPSYNC4"}) {
    std::memcpy(base + kName, name, std::strlen(name) + 1);
    auto ctx = context();
    __imp__sub_82438880(ctx, base);
    CHECK_EQ(ctx.r3.u32, 1);
    ctx = context();
    sub_82438880(ctx, base);
    CHECK_EQ(ctx.r3.u32, 0);
    CHECK_EQ(ctx.r1.u32, 0x2F000);
    CHECK_EQ(ctx.lr, 0x82439100);
  }

  BeginCase("native OnTrackName rejection seeks to track end and notifies exactly once");
  std::memcpy(base + kName, "LIPSYNC1", 9);
  auto ctx = context();
  ctx.r4.u32 = 0; // tick
  ctx.r5.u32 = kName;
  REX_STORE_U32(kReader + 20, 2); // native reading-track state
  sub_824390B0(ctx, base);
  CHECK_EQ(ctx.r3.u32, 0);
  CHECK_EQ(ctx.r1.u32, 0x2F000);
  CHECK_EQ(seeks, 1);
  CHECK_EQ(ends, 1);
  CHECK_EQ(REX_LOAD_U32(kReader + 20), 1); // native ready-for-next-track state

  BeginCase("playable and event tracks retain retail acceptance, including near matches");
  for (const auto* name : {"PART GUITAR", "PART BASS", "PART DRUMS", "PART VOCALS", "PART KEYS",
                           "HARM1", "HARM2", "HARM3", "EVENTS", "BEAT", "VENUE", "",
                           "LIPSYNC", "LIPSYNC0", "LIPSYNC5", "LIPSYNC10", "LIPSYNC1_extra",
                           "lipsync1", "LIP"}) {
    std::memcpy(base + kName, name, std::strlen(name) + 1);
    ctx = context();
    sub_82438880(ctx, base);
    CHECK_EQ(ctx.r3.u32, 1);
  }
  CHECK_EQ(seeks, 1);
  CHECK_EQ(ends, 1);
  return Finish();
}
