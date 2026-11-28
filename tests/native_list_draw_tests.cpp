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
constexpr uint32_t kWidgets = 0x4000, kRows = 0x7000;
struct Widget {
  uint32_t object, slots, elements, spare;
};
constexpr Widget kLabel{0x5000, 0x6000, 0x8000, 0x9000};
constexpr Widget kMesh{0x5100, 0x6100, 0x8100, 0x9100};
std::vector<uint8_t> memory(0x20000);
std::vector<Widget> widgets;
uint32_t display, direction;
unsigned starts = 0, completes = 0, draws = 0;
bool throw_draw = false;
uint32_t Word(uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(memory.data() + address);
}
void Word(uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(memory.data() + address, value);
}
void Fixture(unsigned rows, bool native_scroll = false, bool powerups = false) {
  std::memset(memory.data(), 0, memory.size());
  widgets = powerups ? std::vector{kLabel, kMesh} : std::vector{kLabel};
  display = powerups ? 2 : 7;
  direction = powerups ? 1 : uint32_t(-1);
  Word(kState + 4, display);
  Word(kState + 48, 10); Word(kState + 52, native_scroll ? 11 : 10);
  Word(kResource + 640, direction);
  Word(kDraw + 56, kRows); Word(kDraw + 60, kRows + rows * 44);
  uint32_t entry = kWidgets + 16;
  Word(kWidgets, entry);
  for (const auto& widget : widgets) {
    Word(entry, widget.object);
    entry += 4;
    const uint32_t end = widget.slots + display * 4;
    Word(widget.object + 92, widget.slots);
    Word(widget.object + 96, end);
    Word(widget.object + 108, widget.spare);
    for (unsigned i = 0; i < display; ++i) Word(widget.slots + i * 4, widget.elements + i * 16);
    if (native_scroll) {
      Word(end, widget.spare); Word(widget.object + 96, end + 4);
      Word(widget.object + 108, 0);
    }
  }
  Word(kWidgets + 4, entry);
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
  for (const auto& widget : widgets) {
    CHECK_EQ(Word(widget.object + 96), widget.slots + display * 4);
    CHECK_EQ(Word(widget.object + 108), widget.spare);
    for (unsigned i = 0; i < display; ++i)
      CHECK_EQ(Word(widget.slots + i * 4), widget.elements + i * 16);
  }
  CHECK_EQ(Word(kResource + 640), direction);
  CHECK_EQ(Word(kState + 48), uint32_t(10));
  CHECK_EQ(Word(kState + 52), uint32_t(10));
}
}

// These stand-ins enforce the retail ABI and spare-element ownership contract.
extern "C" REX_FUNC(sub_824E56F8) {
  CHECK_TRUE(base == memory.data());
  CHECK_EQ(ctx.r3.u32, kResource); CHECK_EQ(ctx.r4.u32, kState);
  CHECK_EQ(ctx.r5.u32, kWidgets); CHECK_EQ(ctx.r6.u32, uint32_t(1));
  CHECK_EQ(ctx.r7.u32, uint32_t(1)); // Fill the entering edge row as well as attaching it.
  Word(kResource + 640, 1);
  for (const auto& widget : widgets) {
    const uint32_t end = widget.slots + display * 4;
    CHECK_EQ(Word(widget.object + 96), end);
    CHECK_EQ(Word(widget.object + 108), widget.spare);
    Word(end, widget.spare);
    Word(widget.object + 108, 0); Word(widget.object + 96, end + 4);
    Word(widget.spare, 10 + display);
  }
  ++starts;
  ctx.r31.u32 = 0;
}
extern "C" REX_FUNC(sub_824E5788) {
  CHECK_TRUE(base == memory.data());
  CHECK_EQ(ctx.r3.u32, kResource); CHECK_EQ(ctx.r4.u32, kState);
  CHECK_EQ(ctx.r5.u32, kWidgets);
  CHECK_EQ(Word(kResource + 640), uint32_t(-1)); // Return the entering row and skip refilling.
  for (const auto& widget : widgets) {
    const uint32_t end = widget.slots + display * 4;
    CHECK_EQ(Word(widget.object + 96), end + 4);
    CHECK_EQ(Word(widget.object + 108), uint32_t(0));
    Word(widget.object + 108, Word(end));
    Word(widget.object + 96, end);
  }
  ++completes;
  ctx.r3.u32 = 0; ctx.r31.u32 = 0;
}
extern "C" REX_FUNC(__imp__sub_824E5020) {
  CHECK_EQ(ctx.r3.u32, kResource); CHECK_EQ(ctx.r4.u32, kDraw);
  CHECK_EQ(ctx.r5.u32, kState); CHECK_EQ(ctx.r6.u32, kWidgets);
  const unsigned rows = (Word(kDraw + 60) - Word(kDraw + 56)) / 44;
  for (const auto& widget : widgets) {
    for (unsigned i = 0; i < rows; ++i) {
      CHECK_TRUE(widget.slots + i * 4 < Word(widget.object + 96));
      CHECK_TRUE(Word(widget.slots + i * 4) != 0);
    }
    if (starts) CHECK_EQ(Word(widget.spare), 10 + display);
  }
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

  BeginCase("power-up swipe returns spare elements through widget dispatch for a mixed list");
  Fixture(3, false, true);
  Draw(); CheckReturned();
  Draw(); CheckReturned();
  CHECK_EQ(starts, 2u); CHECK_EQ(completes, 2u); CHECK_EQ(draws, 2u);

  BeginCase("native scrolling keeps ownership of its existing extra row");
  Fixture(8, true); Draw();
  CHECK_EQ(starts, 0u); CHECK_EQ(completes, 0u);
  CHECK_EQ(Word(kLabel.object + 96), kLabel.slots + 8 * 4);
  CHECK_EQ(Word(kLabel.object + 108), 0u);

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
