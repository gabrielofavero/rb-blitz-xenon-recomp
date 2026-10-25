// SPDX-License-Identifier: GPL-2.0-only
// Production WorldXfm hook against isolated guest memory; no game/UI navigation.
#include "check.h"
#include "hooks/hud_layout_runtime.h"

#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#include <windows.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/xmemory.h>

REX_EXTERN(sub_821F13D0);
REX_EXTERN(sub_821F65C8);
REX_EXTERN(sub_82226CD8);
REXCVAR_DECLARE(bool, hud_reanchor);
REXCVAR_DECLARE(bool, hud_reanchor_apply);
REXCVAR_DECLARE(bool, hud_reanchor_scan);

namespace {
uint32_t width = 2560, height = 1080;
uint8_t* memory = nullptr;
constexpr uint32_t kHeap = 0x40000000;
constexpr uint32_t kTables = 0x82010000;
constexpr uint32_t kHud = kHeap + 0x100;
constexpr uint32_t kOtherDir = kHeap + 0x500;
uint32_t next_string = kHeap + 0xD000;
uint32_t next_table = kTables;
unsigned original_calls = 0;
unsigned dirty_calls = 0, draw_calls = 0;
std::array<uint32_t, 16> transforms{};
uint32_t drawn_child = 0;
float drawn_x = 0;


void Word(uint32_t a, uint32_t v) { rex::memory::store_and_swap<uint32_t>(memory + a, v); }
uint32_t Word(uint32_t a) { return rex::memory::load_and_swap<uint32_t>(memory + a); }
void Float(uint32_t a, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  Word(a, bits);
}
float Float(uint32_t a) {
  const uint32_t bits = Word(a);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}
uint32_t Text(const char* text) {
  const uint32_t address = next_string;
  next_string += uint32_t(std::strlen(text)) + 1;
  std::memcpy(memory + address, text, std::strlen(text) + 1);
  return address;
}
void ObjectView(uint32_t derived, uint32_t header) {
  const uint32_t table = next_table;
  next_table += 16;
  Word(derived + 4, table);
  Word(table + 4, header - derived - 4);
}
void Reset() {
  std::memset(memory + kHeap, 0, 0x10000);
  transforms = {}; drawn_child = 0;
  next_string = kHeap + 0xD000;
  next_table = kTables;
  width = 2560; height = 1080;
  ObjectView(kHud, kHud + 0x200);
  Word(kHud + 0x64, Text("track/hud.milo"));
  ObjectView(kOtherDir, kOtherDir + 0x200);
  Word(kOtherDir + 0x64, Text("ui/menu.milo"));
}
struct Shape { const char* name; uint32_t vtable; int offset; int side; };
constexpr std::array<Shape, 6> shapes{{
    {"star_board", 0x8201C784, -0x114, 1},
    {"band_meter_hud", 0x8201C784, -0x114, -1},
    {"letter_collect_hud", 0x8201C784, -0x114, -1},
    {"remedial_help.grp", 0x82024C4C, -0x108, -1},
    {"dynamic_leaderboard", 0x82018EA4, -0x1B4, 1},
    {"score.lbl", 0x8203A95C, 0x7C, 1},
}};
uint32_t Make(unsigned slot, const Shape& shape, float x, uint32_t dir = kHud,
              uint32_t parent = 0) {
  const uint32_t t = kHeap + 0x2000 + slot * 0x500;
  const uint32_t header = uint32_t(int64_t(t) - shape.offset);
  ObjectView(t, header);
  Word(header, shape.vtable);
  Word(header + 32, Text(shape.name));
  Word(header + 36, dir);
  Float(t + 0x38, x); Float(t + 0x3C, 25); Float(t + 0x40, 4.35f);
  Word(t + 0x94, parent);
  memory[t + 189] = 1;
  transforms[slot] = t;
  return t;
}
void Resolve(uint32_t t) {
  PPCContext ctx{};
  ctx.r3.u32 = t;
  ctx.r31.u32 = 0xDEADBEEF;
  sub_821F13D0(ctx, memory);
  CHECK_EQ(ctx.r3.u32, t + 72);
  CHECK_EQ(ctx.r31.u32, 0xDEADBEEF);
  CHECK_EQ(ctx.r11.u32, 0x12345678); // preserve original callee's return registers
}
void Close(float actual, double expected) {
  CHECK_TRUE(std::isfinite(actual) && std::fabs(double(actual) - expected) < 0.0002);
}
// Both production draw hooks receive a RndDrawable view, distinct from the
// transformable and Hmx::Object views. Constructors give offsets +80 (RndDir)
// and +64 (RndGroup) from drawable to transformable.
void Draw(uint32_t t, const Shape& shape, bool group = false) {
  const uint32_t drawable = t - (group ? 64 : 80);
  ObjectView(drawable, uint32_t(int64_t(t) - shape.offset));
  PPCContext ctx{};
  ctx.r3.u32 = drawable;
  ctx.r4.u32 = 0xCAFEBABE;
  ctx.r31.u32 = 0xDEADBEEF;
  const auto before = draw_calls;
  if (group) sub_82226CD8(ctx, memory); else sub_821F65C8(ctx, memory);
  CHECK_EQ(draw_calls, before + 1);
  CHECK_EQ(ctx.r3.u32, drawable);
  CHECK_EQ(ctx.r4.u32, 0xCAFEBABE);
  CHECK_EQ(ctx.r31.u32, 0xDEADBEEF);
}
constexpr double delta = (2560.0 / 1080.0 - 16.0 / 9.0) * 240.0;
}  // namespace

namespace rb_blitz::settings {
uint32_t BootRenderWidth() { return width; }
uint32_t BootRenderHeight() { return height; }
}

// Stand-in for the original guest evaluator: fresh local -> world; dirty parents
// resolve through the production hook before their child inherits the result.
extern "C" REX_FUNC(__imp__sub_821F13D0) {
  ++original_calls;
  const uint32_t t = ctx.r3.u32;
  const uint32_t parent = Word(t + 0x94);
  if (parent && memory[parent + 189]) {
    ctx.r3.u32 = parent;
    sub_821F13D0(ctx, base);
  }
  for (uint32_t axis = 0; axis < 12; axis += 4) {
    Float(t + 0x78 + axis, Float(t + 0x38 + axis) +
          (parent ? Float(parent + 0x78 + axis) : 0));
  }
  memory[t + 189] = 0;
  ctx.r3.u32 = t + 72;
  ctx.r11.u32 = 0x12345678;
}

// Faithful dirty propagation: dirty descendants stay dirty; clean descendants
// are invalidated. This call must not leave the already resolved parent dirty.
extern "C" REX_FUNC(sub_821F0300) {
  ++dirty_calls;
  const uint32_t parent = ctx.r3.u32;
  memory[parent + 189] = 1;
  for (const auto child : transforms) {
    if (!child || Word(child + 0x94) != parent || memory[child + 189]) continue;
    ctx.r3.u32 = child;
    sub_821F0300(ctx, base);
  }
  ctx.r3.u32 = parent;
}

// The guest's draw path only invokes WorldXfm when a child's dirty byte is set.
// Reading a clean child here deliberately bypasses the old WorldXfm-only fix.
extern "C" REX_FUNC(__imp__sub_821F65C8) {
  ++draw_calls;
  CHECK_EQ(ctx.r4.u32, 0xCAFEBABE);
  if (drawn_child) {
    if (memory[drawn_child + 189]) Resolve(drawn_child);
    drawn_x = Float(drawn_child + 0x78);
  }
}
extern "C" REX_FUNC(__imp__sub_82226CD8) { __imp__sub_821F65C8(ctx, base); }

int main() {
  using namespace rb_blitz::test;
  // Reserve an addressable guest range, committing only two tiny fixture regions.
  memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, size_t(0x83000000), MEM_RESERVE, PAGE_NOACCESS));
  CHECK_TRUE(memory != nullptr);
  if (!memory) return Finish();
  CHECK_TRUE(VirtualAlloc(memory + kHeap, 0x10000, MEM_COMMIT, PAGE_READWRITE) != nullptr);
  CHECK_TRUE(VirtualAlloc(memory + kTables, 0x10000, MEM_COMMIT, PAGE_READWRITE) != nullptr);

  BeginCase("16:9 and disabled reanchoring inspect no guest metadata");
  width = 0;
  rb_blitz::hooks::HudReanchorWorld(nullptr, kHeap);
  width = 1920;
  rb_blitz::hooks::HudReanchorWorld(nullptr, kHeap);
  width = 2560;
  REXCVAR_SET(hud_reanchor, false);
  rb_blitz::hooks::HudReanchorWorld(nullptr, kHeap);
  REXCVAR_SET(hud_reanchor, true);
  REXCVAR_SET(hud_reanchor_apply, false);
  rb_blitz::hooks::HudReanchorWorld(nullptr, kHeap);
  REXCVAR_SET(hud_reanchor_apply, true);

  BeginCase("every subset of missing HUD elements places the survivors on their first resolve");
  for (unsigned subset = 0; subset < 64; ++subset) {
    Reset();
    // No progress_bar.sld, timer, gold-progress or other optional object exists.
    for (unsigned i = 0; i < shapes.size(); ++i) {
      if (!(subset & (1u << i))) continue;
      const float x = shapes[i].side * 270.0f;
      const auto t = Make(i, shapes[i], x);
      const auto before = original_calls;
      Resolve(t);
      CHECK_EQ(original_calls, before + 1);  // no lookup retries or extra dirty calls
      Close(Float(t + 0x78), x + shapes[i].side * delta);
      Close(Float(t + 0x38), x);  // authored local is never rewritten
      Close(Float(t + 0x7C), 25); Close(Float(t + 0x80), 4.35f);
    }
  }

  BeginCase("repeated dirty and explicit clean resolves never accumulate or flicker");
  Reset();
  const auto meter = Make(0, shapes[1], -298.537f);
  const auto started = std::chrono::steady_clock::now();
  for (unsigned frame = 0; frame < 3000; ++frame) {
    memory[meter + 189] = uint8_t(frame % 2);
    if (frame % 37 == 0) Float(meter + 0x38, -298.537f + float(frame % 7));
    Resolve(meter);
    Close(Float(meter + 0x78), double(Float(meter + 0x38)) - delta);
  }

  const double us = std::chrono::duration<double, std::micro>(
      std::chrono::steady_clock::now() - started).count() / 3000;
  std::printf("[ INFO ] simulated meter resolve including assertions: %.3f us/call\n", us);

  BeginCase("named children inherit once, with dirty and already clean parents");
  Reset();
  const auto stars = Make(0, shapes[0], 270);
  const auto score = Make(1, shapes[5], 0, kHud, stars);
  Resolve(score); Close(Float(score + 0x78), 270 + delta);
  Resolve(score); Close(Float(score + 0x78), 270 + delta);
  Float(stars + 0x38, 280); memory[stars + 189] = 1;
  Resolve(score); Close(Float(score + 0x78), 280 + delta);
  // A non-anchor intermediate group still carries its ancestor's world shift.
  const Shape group{"score.grp", 0x82024C4C, -0x108, 0};
  const auto intermediate = Make(2, group, 5, kHud, stars);
  Word(score + 0x94, intermediate);
  Resolve(score); Close(Float(score + 0x78), 285 + delta);

  BeginCase("non-anchor children inherit placement without being scanned or individually shifted");
  const Shape slider{"progress_bar.sld", 0x8203A95C, 0x7C, 0};
  const auto progress = Make(3, slider, -100, kHud, stars);
  Resolve(progress); Close(Float(progress + 0x78), 180 + delta);
  // Removing the optional child has no effect on its parent or siblings.
  std::memset(memory + progress, 0, 0x300);
  Resolve(stars); Close(Float(stars + 0x78), 280 + delta);
  Resolve(score); Close(Float(score + 0x78), 285 + delta);

  BeginCase("centred groups, unknown classes and names outside HUD stay authored");
  Reset();
  const auto centred = Make(0, group, 0);
  Resolve(centred); Close(Float(centred + 0x78), 0);
  const Shape unknown{"band_meter_hud", 0x82012340, -0x114, -1};
  const auto skipped = Make(1, unknown, -300);
  Resolve(skipped); Close(Float(skipped + 0x78), -300);
  const auto menu = Make(2, shapes[1], -300, kOtherDir);
  Resolve(menu); Close(Float(menu + 0x78), -300);
  // Same class/name under a child directory of the HUD is eligible.
  Word(kOtherDir + 0x200 + 36, kHud);
  Resolve(menu); Close(Float(menu + 0x78), -300 - delta);

  BeginCase("new scenes and reused object addresses do not depend on cached identity");
  for (unsigned scene = 0; scene < 10; ++scene) {
    Reset();
    const auto t = Make(0, shapes[1], -300 - float(scene));
    Resolve(t); Close(Float(t + 0x78), -300 - double(scene) - delta);
    Word(kHud + 0x64, Text("ui/menu.milo"));
    Resolve(t); Close(Float(t + 0x78), -300 - double(scene));
    Word(kHud + 0x64, Text("track/hud.milo"));
    Resolve(t); Close(Float(t + 0x78), -300 - double(scene) - delta);
  }

  BeginCase("narrow frames shift inward and 16:9 is unchanged");
  Reset();
  const auto t = Make(0, shapes[1], -300);
  width = 1440; height = 1080;
  Resolve(t); Close(Float(t + 0x78), -300 - (4.0 / 3.0 - 16.0 / 9.0) * 240.0);
  width = 1920;
  Resolve(t); Close(Float(t + 0x78), -300);

  BeginCase("first draw repairs fully cached left HUD loaded before its path was assigned");
  Reset();
  Word(kHud + 0x64, Text("loading.milo"));
  const auto cached_meter = Make(0, shapes[1], -298.537f);
  const auto cached_art = Make(1, group, -10, kHud, cached_meter);
  // All matrices were resolved while the directory was not yet recognizable.
  Resolve(cached_art);
  CHECK_EQ(memory[cached_meter + 189], 0);
  CHECK_EQ(memory[cached_art + 189], 0);
  Close(Float(cached_art + 0x78), -308.537f);
  Word(kHud + 0x64, Text("track/hud.milo"));
  drawn_child = cached_art;
  auto old_dirty_calls = dirty_calls;
  Draw(cached_meter, shapes[1]);
  Close(drawn_x, double(Float(cached_meter + 0x38)) - 10 - delta);
  CHECK_EQ(memory[cached_meter + 189], 0);
  CHECK_EQ(memory[cached_art + 189], 0);
  CHECK_TRUE(dirty_calls > old_dirty_calls);
  old_dirty_calls = dirty_calls;
  for (unsigned frame = 0; frame < 1000; ++frame) {
    Draw(cached_meter, shapes[1]);
    Close(drawn_x, double(Float(cached_meter + 0x38)) - 10 - delta);
  }
  CHECK_EQ(dirty_calls, old_dirty_calls); // no repeated dirty-tree walk

  BeginCase("an eligible parent correction invalidates already clean child art");
  Reset();
  const auto late_parent = Make(0, shapes[1], -300);
  const auto late_child = Make(1, group, -5, kHud, late_parent);
  Float(late_child + 0x78, -305); memory[late_child + 189] = 0;
  Resolve(late_parent);
  CHECK_EQ(memory[late_parent + 189], 0);
  CHECK_EQ(memory[late_child + 189], 1);
  drawn_child = late_child;
  Draw(late_parent, shapes[1]);
  Close(drawn_x, -305 - delta);

  BeginCase("cached lane-switch groups use the same pre-draw guarantee");
  Reset();
  Word(kHud + 0x64, Text("loading.milo"));
  const auto cached_prompt = Make(0, shapes[3], -289);
  const auto prompt_art = Make(1, group, -2, kHud, cached_prompt);
  Resolve(prompt_art);
  Word(kHud + 0x64, Text("track/hud.milo"));
  drawn_child = prompt_art;
  Draw(cached_prompt, shapes[3], true);
  Close(drawn_x, -291 - delta);
  old_dirty_calls = dirty_calls;
  Draw(cached_prompt, shapes[3], true);
  CHECK_EQ(dirty_calls, old_dirty_calls);

  BeginCase("already placed right HUD is not shifted again by pre-draw");
  Reset();
  const auto placed_stars = Make(0, shapes[0], 270);
  Resolve(placed_stars);
  old_dirty_calls = dirty_calls;
  Draw(placed_stars, shapes[0]);
  Close(Float(placed_stars + 0x78), 270 + delta);
  CHECK_EQ(dirty_calls, old_dirty_calls);

  BeginCase("retired scan flag performs no search and invalid path pointers are rejected");
  Reset();
  REXCVAR_SET(hud_reanchor_scan, true);
  const auto t2 = Make(0, shapes[1], -300);
  Word(kHud + 0x64, 0x60000000); // reserved but uncommitted, in the guest heap range
  Resolve(t2); Close(Float(t2 + 0x78), -300);
  Word(kHud + 0x6C, Text("track/hud.milo"));
  Resolve(t2); Close(Float(t2 + 0x78), -300 - delta);
  Float(t2 + 0x38, std::numeric_limits<float>::infinity());
  Resolve(t2); CHECK_TRUE(std::isinf(Float(t2 + 0x78)));

  VirtualFree(memory, 0, MEM_RELEASE);
  return Finish();
}
