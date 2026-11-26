// SPDX-License-Identifier: GPL-2.0-only
// Exercise production draw orchestration against isolated guest objects, without game UI.
#include "check.h"
#include "input/native_list_draw.h"

#include <cstring>
#include <stdexcept>
#include <vector>
#include <rex/memory/endian.h>
#include <rex/ppc/hook.h>

namespace {
constexpr uint32_t kResource = 0x1000, kState = 0x2000, kDraw = 0x3000;
constexpr uint32_t kWidgets = 0x4000, kWidget = 0x5000, kSlots = 0x6000;
constexpr uint32_t kRows = 0x7000, kElements = 0x8000, kSpare = 0x9000;
std::vector<uint8_t> memory(0x20000);
unsigned starts = 0, completes = 0, draws = 0;
bool throw_draw = false;
uint32_t Word(uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(memory.data() + address);
}
void Word(uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(memory.data() + address, value);
}
void Fixture(unsigned rows, bool native_scroll = false) {
  std::memset(memory.data(), 0, memory.size());
  Word(kState + 4, 7);
  Word(kState + 48, 10); Word(kState + 52, native_scroll ? 11 : 10);
  Word(kResource + 640, uint32_t(-1));
  Word(kDraw + 56, kRows); Word(kDraw + 60, kRows + rows * 44);
  Word(kWidgets, kWidgets + 16); Word(kWidgets + 4, kWidgets + 20);
  Word(kWidgets + 16, kWidget);
  Word(kWidget + 92, kSlots); Word(kWidget + 96, kSlots + 7 * 4);
  Word(kWidget + 108, kSpare);
  for (unsigned i = 0; i < 7; ++i) Word(kSlots + i * 4, kElements + i * 16);
  if (native_scroll) {
    Word(kSlots + 7 * 4, kSpare); Word(kWidget + 96, kSlots + 8 * 4);
    Word(kWidget + 108, 0);
  }
  starts = completes = draws = 0;
  throw_draw = false;
}
void Draw() {
  PPCContext ctx{};
  ctx.r1.u32 = 0x18000;
  ctx.r3.u32 = kResource; ctx.r4.u32 = kDraw;
  ctx.r5.u32 = kState; ctx.r6.u32 = kWidgets;
  ctx.r31.u32 = 0x12345678;
  rb_blitz::input::DrawNativeList(ctx, memory.data());
  CHECK_EQ(ctx.r1.u32, uint32_t(0x18000));
  CHECK_EQ(ctx.r3.u32, uint32_t(0xABCDEF)); // The native draw's output survives cleanup.
  CHECK_EQ(ctx.r31.u32, uint32_t(0x12345678));
}
void CheckReturned() {
  CHECK_EQ(Word(kWidget + 96), kSlots + 7 * 4);
  CHECK_EQ(Word(kWidget + 108), kSpare);
  CHECK_EQ(Word(kResource + 640), uint32_t(-1));
  CHECK_EQ(Word(kState + 48), uint32_t(10));
  CHECK_EQ(Word(kState + 52), uint32_t(10));
  for (unsigned i = 0; i < 7; ++i) CHECK_EQ(Word(kSlots + i * 4), kElements + i * 16);
}
}

// These stand-ins enforce the retail ABI and spare-element ownership contract.
extern "C" REX_FUNC(sub_824E56F8) {
  CHECK_TRUE(base == memory.data());
  CHECK_EQ(ctx.r3.u32, kResource); CHECK_EQ(ctx.r4.u32, kState);
  CHECK_EQ(ctx.r5.u32, kWidgets); CHECK_EQ(ctx.r6.u32, uint32_t(1));
  CHECK_EQ(ctx.r7.u32, uint32_t(1)); // Fill the entering edge row as well as attaching it.
  CHECK_EQ(Word(kWidget + 96), kSlots + 7 * 4);
  CHECK_EQ(Word(kWidget + 108), kSpare);
  Word(kResource + 640, 1);
  Word(kSlots + 7 * 4, Word(kWidget + 108));
  Word(kWidget + 108, 0); Word(kWidget + 96, kSlots + 8 * 4);
  Word(kSpare, 17); // Provider fills the row immediately following viewport [10, 16].
  ++starts;
  ctx.r31.u32 = 0;
}
extern "C" REX_FUNC(sub_824BF538) {
  CHECK_EQ(ctx.r3.u32, kWidget); CHECK_EQ(ctx.r4.u32, kState);
  CHECK_EQ(ctx.r5.u32, uint32_t(-1)); // Return the last row, preserving the first seven.
  CHECK_EQ(Word(kWidget + 96), kSlots + 8 * 4);
  CHECK_EQ(Word(kWidget + 108), uint32_t(0));
  Word(kWidget + 108, Word(kSlots + 7 * 4));
  Word(kWidget + 96, kSlots + 7 * 4);
  ++completes;
  ctx.r3.u32 = 0; ctx.r31.u32 = 0;
}
extern "C" REX_FUNC(__imp__sub_824E5020) {
  CHECK_EQ(ctx.r3.u32, kResource); CHECK_EQ(ctx.r4.u32, kDraw);
  CHECK_EQ(ctx.r5.u32, kState); CHECK_EQ(ctx.r6.u32, kWidgets);
  const unsigned rows = (Word(kDraw + 60) - Word(kDraw + 56)) / 44;
  // UIListLabel::Draw indexes this vector directly, including its eighth row.
  for (unsigned i = 0; i < rows; ++i) {
    CHECK_TRUE(kSlots + i * 4 < Word(kWidget + 96));
    CHECK_TRUE(Word(kSlots + i * 4) != 0);
  }
  if (starts) CHECK_EQ(Word(kSpare), uint32_t(17));
  ++draws;
  if (throw_draw) throw std::runtime_error("draw failed");
  ctx.r3.u32 = 0xABCDEF;
}

int main() {
  using namespace rb_blitz::test;
  BeginCase("seven settled song rows draw without borrowing an element");
  Fixture(7); Draw();
  CHECK_EQ(starts, 0u); CHECK_EQ(completes, 0u); CHECK_EQ(draws, 1u);
  CheckReturned();

  BeginCase("an eighth interpolated row has a filled element for every render pass");
  Fixture(8);
  for (unsigned pass = 0; pass < 3; ++pass) {
    Draw(); CheckReturned();
    CHECK_EQ(starts, pass + 1); CHECK_EQ(completes, pass + 1);
  }

  BeginCase("native scrolling keeps ownership of its existing extra row");
  Fixture(8, true); Draw();
  CHECK_EQ(starts, 0u); CHECK_EQ(completes, 0u);
  CHECK_EQ(Word(kWidget + 96), kSlots + 8 * 4);
  CHECK_EQ(Word(kWidget + 108), 0u);

  BeginCase("empty and short filtered lists need no spare element");
  for (unsigned rows : {0u, 1u, 6u}) {
    Fixture(rows); Draw(); CheckReturned();
    CHECK_EQ(starts, 0u); CHECK_EQ(completes, 0u);
  }

  BeginCase("a failed draw still returns its borrowed element");
  Fixture(8); throw_draw = true;
  try { Draw(); CHECK_TRUE(false); }
  catch (const std::runtime_error&) {}
  CheckReturned();
  CHECK_EQ(starts, 1u); CHECK_EQ(completes, 1u);
  return Finish();
}
