// SPDX-License-Identifier: GPL-2.0-only
// Production WorldXfm hook against isolated guest memory; no game/UI navigation.
#include "check.h"
#include "hooks/hud_layout_runtime.h"

#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "settings/runtime_settings.h"
#include <rex/ppc/hook.h>
#include <rex/memory/endian.h>
#include "input/touch_controls.h"
#include "ui/portrait_viewport.h"
REX_EXTERN(sub_823FF258);

REX_EXTERN(sub_821F13D0);
REX_EXTERN(sub_821F65C8);
REX_EXTERN(sub_82226CD8);

namespace {
uint32_t width = 2560, height = 1080, crop_width = 0, crop_height = 0;
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
bool using_touch = false, buttons_enabled = true, powerup_ready = false;
unsigned property_writes = 0;
unsigned lane_captures = 0;
rb_blitz::input::touch::Layout touch_layout{874, 402, {90, 28, 46, 46}};


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
  rb_blitz::ui::PublishPortraitViewport({});
  std::memset(memory + kHeap, 0, 0x10000);
  transforms = {}; drawn_child = 0;
  next_string = kHeap + 0xD000;
  next_table = kTables;
  width = 2560; height = 1080; crop_width = crop_height = 0;
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
  PPCContext ctx{}; ctx.r1.u32 = kHeap + 0x10000;
  ctx.r3.u32 = t;
  ctx.r31.u32 = 0xDEADBEEF;
  sub_821F13D0(ctx, memory);
  CHECK_EQ(ctx.r3.u32, t + 72);
  CHECK_EQ(ctx.r31.u32, 0xDEADBEEF);
  CHECK_EQ(ctx.r1.u32, kHeap + 0x10000);
  CHECK_EQ(ctx.r11.u32, 0x12345678); // preserve original callee's return registers
}
void Close(float actual, double expected) {
  CHECK_TRUE(std::isfinite(actual) && std::fabs(double(actual) - expected) < 0.0002);
}
// Both production draw hooks receive a RndDrawable view, distinct from the
// transformable and Hmx::Object views. Constructors give offsets +80 (RndDir)
// and +64 (RndGroup) from drawable to transformable.
void Draw(uint32_t t, const Shape& shape, bool group = false, bool visible = true) {
  const uint32_t drawable = t - (group ? 64 : 80);
  ObjectView(drawable, uint32_t(int64_t(t) - shape.offset));
  PPCContext ctx{}; ctx.r1.u32 = kHeap + 0x10000;
  ctx.r3.u32 = drawable;
  ctx.r4.u32 = 0xCAFEBABE;
  ctx.r31.u32 = 0xDEADBEEF;
  const auto before = draw_calls;
  if (group) sub_82226CD8(ctx, memory); else sub_821F65C8(ctx, memory);
  CHECK_EQ(draw_calls, before + (visible ? 1 : 0));
  CHECK_EQ(ctx.r3.u32, drawable);
  CHECK_EQ(ctx.r4.u32, 0xCAFEBABE);
  CHECK_EQ(ctx.r31.u32, 0xDEADBEEF);
  CHECK_EQ(ctx.r1.u32, kHeap + 0x10000);
}
constexpr double delta = (2560.0 / 1080.0 - 16.0 / 9.0) * 240.0;
}  // namespace

namespace rb_blitz::settings {
uint32_t BootRenderWidth() { return width; }
uint32_t BootRenderHeight() { return height; }
uint32_t GameplayPortraitWidth() { return crop_width; }
uint32_t GameplayPortraitHeight() { return crop_height; }
}
namespace rb_blitz::input {
void CaptureLanePowerup(PPCContext&, uint8_t*, uint32_t) { ++lane_captures; }
}
namespace rb_blitz::input::touch {
Layout CurrentLayout() { return touch_layout; }
bool PreviewEnabled() { return false; }
bool TouchInputActive() { return using_touch; }
bool ButtonsEnabled() { return buttons_enabled; }
bool ControlsVisible() { return using_touch && buttons_enabled; }
bool PowerupReady() { return powerup_ready; }
void PublishPowerupReady(bool ready) { powerup_ready = ready; }
}
extern "C" REX_FUNC(__imp__sub_823FF258) {
  ++property_writes;
  ctx.r3.u32 = 0x12345678;
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
  for (uint32_t row = 0; row < 3; ++row)
    for (uint32_t column = 0; column < 3; ++column)
      Float(t + 72 + row * 16 + column * 4, parent ? Float(parent + 72 + row * 16 + column * 4) : (row == column ? 1.f : 0.f));
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
  CHECK_TRUE(rb_blitz::settings::RegisterTitleSettings(REX_SETTINGS()).has_value());
  CHECK_TRUE(rb_blitz::settings::title::show_run_comparison.Set(true).has_value());
  using namespace rb_blitz::test;
  // Reserve an addressable guest range, committing only two tiny fixture regions.
#if defined(_WIN32)
  memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, size_t(0x83000000), MEM_RESERVE, PAGE_NOACCESS));
  CHECK_TRUE(memory != nullptr);
  if (!memory) return Finish();
  CHECK_TRUE(VirtualAlloc(memory + kHeap, 0x10000, MEM_COMMIT, PAGE_READWRITE) != nullptr);
  CHECK_TRUE(VirtualAlloc(memory + kTables, 0x10000, MEM_COMMIT, PAGE_READWRITE) != nullptr);
#else
  memory = static_cast<uint8_t*>(mmap(nullptr, size_t(0x83000000), PROT_NONE,
                                     MAP_PRIVATE | MAP_ANON, -1, 0));
  CHECK_TRUE(memory != MAP_FAILED);
  if (memory == MAP_FAILED) return Finish();
  CHECK_EQ(mprotect(memory + kHeap, 0x10000, PROT_READ | PROT_WRITE), 0);
  CHECK_EQ(mprotect(memory + kTables, 0x10000, PROT_READ | PROT_WRITE), 0);
#endif

#if !defined(RB_BLITZ_IOS)
  BeginCase("16:9 and disabled reanchoring inspect no guest metadata");
  width = 0;
  rb_blitz::hooks::HudReanchorWorld(nullptr, kHeap);
  width = 1920;
  rb_blitz::hooks::HudReanchorWorld(nullptr, kHeap);
  width = 2560;
  CHECK_TRUE(rb_blitz::settings::title::hud_reanchor.Set(false).has_value());
  rb_blitz::hooks::HudReanchorWorld(nullptr, kHeap);
  CHECK_TRUE(rb_blitz::settings::title::hud_reanchor.Set(true).has_value());
  CHECK_TRUE(rb_blitz::settings::title::hud_reanchor_apply.Set(false).has_value());
  rb_blitz::hooks::HudReanchorWorld(nullptr, kHeap);
  CHECK_TRUE(rb_blitz::settings::title::hud_reanchor_apply.Set(true).has_value());
#endif

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

  BeginCase("portrait HUD uses separate compact slots and fresh matrices on repeated resolves");
  Reset();
  width = 400; height = 720;
  const auto portrait_stars = Make(0, shapes[0], 270);
  const auto portrait_score = Make(1, shapes[5], 0, kHud, portrait_stars);
  const auto portrait_meter = Make(2, shapes[1], -300);
  const auto portrait_comparison = Make(3, shapes[4], 300);
  const auto portrait_letters = Make(5, shapes[2], -294);
  for (int frame = 0; frame < 20; ++frame) {
    Resolve(portrait_stars);
    Resolve(portrait_score);
    Resolve(portrait_meter);
    Resolve(portrait_comparison);
    Resolve(portrait_letters);
    Close(Float(portrait_stars + 0x78), 400.0 / 720 * 240 - 92);
    Close(Float(portrait_stars + 0x80), 210);
    Close(Float(portrait_score + 0x78), 0);
    Close(Float(portrait_score + 0x80), 232);
    Close(Float(portrait_meter + 0x80), 0);
    Close(Float(portrait_comparison + 0x80), 0);
    Close(Float(portrait_letters + 0x80), 110);
    Close(Float(portrait_stars + 72), .7);
    Close(Float(portrait_score + 72), .5); // parent scale inherited exactly once
    Close(Float(portrait_meter + 72), .65);
    Close(Float(portrait_comparison + 72), .65);
    Close(Float(portrait_stars + 0x38), 270); // local animation data stays authored
  }
  const auto portrait_menu = Make(4, shapes[0], 270, kOtherDir);
  Resolve(portrait_menu);
  Close(Float(portrait_menu + 0x78), 270);
  Close(Float(portrait_menu + 72), 1);

  BeginCase("portrait layout uses crop bounds while the guest renders an unchanged landscape source");
  Reset();
  width = 1280; height = 720; crop_width = 405; crop_height = 720;
  const auto cropped_stars = Make(0, shapes[0], 270);
  const auto cropped_meter = Make(1, shapes[1], -300);
  Resolve(cropped_stars); Resolve(cropped_meter);
  Close(Float(cropped_stars + 0x78), 43);
  Close(Float(cropped_meter + 0x78), -103.8);
  const Shape timer_shape{"timer.lbl", 0x8203A95C, 0x7C, 0};
  const auto cropped_timer = Make(2, timer_shape, -123, kHud, cropped_meter);
  Resolve(cropped_timer);
  Close(Float(cropped_timer + 0x78), 0);
  Close(Float(cropped_timer + 0x80), -202);
  Close(Float(cropped_timer + 72), .7);
  // Visibility/animation changes rebuild the world matrix without accumulating scale.
  for (int frame = 0; frame < 30; ++frame) {
    Resolve(cropped_timer);
    Close(Float(cropped_timer + 72), .7);
    Close(Float(cropped_timer + 0x80), -202);
  }
  const Shape streak_shape{"streak.lbl", 0x8203A95C, 0x7C, 0};
  const auto cropped_streak = Make(7, streak_shape, -123, kHud, cropped_meter);
  Resolve(cropped_streak);
  Close(Float(cropped_streak + 0x78), -123);
  Close(Float(cropped_streak + 0x80), -80);
  Close(Float(cropped_streak + 72), 1.2);
  BeginCase("the compact multiplier ladder stays at the far left without changing the score counter");
  Close(Float(cropped_meter + 0x78), -103.8);
  Close(Float(cropped_meter + 0x80), 0);
  Close(Float(cropped_meter + 72), .65);
  const auto unchanged_score = Make(4, shapes[5], 270);
  Resolve(unchanged_score);
  Close(Float(unchanged_score + 0x78), 0);
  Close(Float(unchanged_score + 72), .5);
  const auto cropped_comparison = Make(5, shapes[4], 300);
  Resolve(cropped_comparison);
  Close(Float(cropped_comparison + 0x78), 90);
  Close(Float(cropped_comparison + 0x80), 0);
  Close(Float(cropped_comparison + 72), .65);

  BeginCase("personal-best notification sits above the comparator and retains hidden animation keys");
  const Shape personal_best_shape{"pb.lbl", 0x8203A95C, 0x7C, 0};
  const auto personal_best = Make(8, personal_best_shape, -257, kHud, cropped_comparison);
  Resolve(personal_best);
  Close(Float(personal_best + 0x78), 123);
  Close(Float(personal_best + 0x80), 70);
  Close(Float(personal_best + 72), .5);
  Float(personal_best + 0x38, 900);
  Resolve(personal_best);
  Close(Float(personal_best + 0x78), 1035);
  Float(personal_best + 0x38, -257);
  Resolve(personal_best);
  Close(Float(personal_best + 0x78), 123);

  BeginCase("portrait song progress rotates clockwise at the bottom without accumulating rotation");
  const auto cropped_progress = Make(6, slider, 123);
  for (int frame = 0; frame < 30; ++frame) {
    Resolve(cropped_progress);
    Close(Float(cropped_progress + 0x78), -67.2);
    Close(Float(cropped_progress + 0x80), -212);
    // Local right points down; local up points right. Depth is preserved.
    Close(Float(cropped_progress + 72), 0);
    Close(Float(cropped_progress + 80), -.8);
    Close(Float(cropped_progress + 92), .8);
    Close(Float(cropped_progress + 104), .8);
    Close(Float(cropped_progress + 112), 0);
  }
  crop_width = crop_height = 0;
  Resolve(cropped_meter); Resolve(cropped_timer); Resolve(cropped_progress);
  Close(Float(cropped_timer + 0x78), -423); // landscape local placement remains authored
  Close(Float(cropped_progress + 72), 1);
  Close(Float(cropped_progress + 80), 0);
  Close(Float(cropped_progress + 104), 0);
  Close(Float(cropped_progress + 112), 1);
  Close(Float(cropped_progress + 0x78), 123);

#if defined(RB_BLITZ_IOS)
  BeginCase("mobile top HUD follows cutouts and refreshes cached art when the safe area changes");
  Reset();
  width = 1280; height = 720; crop_width = 405; crop_height = 720;
  rb_blitz::ui::PublishPortraitViewport({402, 874, 0, 62, 402, 778});
  const Shape top_shape{"portrait_top_hud.grp", 0x82024C4C, -0x108, 0};
  const auto top = Make(0, top_shape, 0);
  Float(top + 0x40, 0);
  const auto top_bar = Make(1, group, 0, kHud, top);
  Float(top_bar + 0x40, 185);
  const auto top_score = Make(2, shapes[5], 270);
  Resolve(top_bar); Resolve(top_score);
  const float first_inset = 62.f / 874 * 480 + 8;
  Close(Float(top + 0x80), 40 - first_inset);
  Close(Float(top_bar + 0x80), 225 - first_inset);
  Close(Float(top_score + 0x80), 232 - first_inset);
  Close(Float(top + 72), 1); // native Blitz animation size is preserved
  Draw(top, top_shape, true);
  rb_blitz::ui::PublishPortraitViewport({402, 874, 0, 80, 402, 760});
  Draw(top, top_shape, true);
  CHECK_EQ(memory[top_bar + 189], 1);
  Resolve(top_bar);
  Close(Float(top_bar + 0x80), 225 - (80.f / 874 * 480 + 8));
  // Stars share the pause disc's centre, including layout-only changes.
  const auto mobile_stars = Make(3, shapes[0], 270);
  const Shape mobile_pause_shape{"touch_pause.grp", 0x82024C4C, -0x108, -1};
  const auto mobile_pause = Make(4, mobile_pause_shape, -289);
  const auto previous_layout = touch_layout;
  touch_layout = {402, 874, {12, 92, 44, 44}};
  using_touch = true;
  Resolve(mobile_stars); Resolve(mobile_pause);
  Close(Float(mobile_stars + 0x80), Float(mobile_pause + 0x80));
  Close(Float(mobile_stars + 72), .7);
  Close(Float(mobile_stars + 0x78), 43);
  Draw(mobile_stars, shapes[0], true);
  touch_layout.pause.y += 10;
  Draw(mobile_stars, shapes[0], true);
  Resolve(mobile_pause);
  Close(Float(mobile_stars + 0x80), Float(mobile_pause + 0x80));
  touch_layout = previous_layout;
  using_touch = false;
  Reset();
#endif

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

  BeginCase("personal-best comparison draws its group directly, bypassing the cached anchor dir");
  Reset();
  Word(kHud + 0x64, Text("loading.milo"));
  const auto comparison = Make(0, shapes[4], 300);
  const Shape comparison_group{"draw.grp", 0x82024C4C, -0x108, 0};
  const auto comparison_draw = Make(1, comparison_group, 0, kHud, comparison);
  const Shape best_label{"name.lbl", 0x8203A95C, 0x7C, 0};
  const auto best_text = Make(2, best_label, 12, kHud, comparison_draw);
  Resolve(best_text);
  CHECK_EQ(memory[comparison + 189], 0);
  CHECK_EQ(memory[comparison_draw + 189], 0);
  CHECK_EQ(memory[best_text + 189], 0);
  Word(kHud + 0x64, Text("track/hud.milo"));
  drawn_child = best_text;
  // ulti_game_enter attaches draw.grp straight to hud.grp: the directory's
  // own DrawShowing is never entered on this route.
  Draw(comparison_draw, comparison_group, true);
  Close(drawn_x, 312 + delta);
  Close(Float(comparison + 0x38), 300);
  old_dirty_calls = dirty_calls;
  const auto prepared_calls = original_calls;
  for (unsigned frame = 0; frame < 100; ++frame) {
    Draw(comparison_draw, comparison_group, true);
    Close(drawn_x, 312 + delta);
  }
  CHECK_EQ(dirty_calls, old_dirty_calls);
  CHECK_EQ(original_calls, prepared_calls); // no forced evaluator/virtual update per child draw
  // Reopening/comparison animation can update its child while the anchor stays clean.
  Float(best_text + 0x38, 17); memory[best_text + 189] = 1;
  Draw(comparison_draw, comparison_group, true);
  Close(drawn_x, 317 + delta);

  BeginCase("prepared anchor reuse still follows dirty animation and changed live state");
  Float(comparison + 0x38, 310); memory[comparison + 189] = 1;
  Draw(comparison_draw, comparison_group, true);
  Close(drawn_x, 327 + delta);
  // A clean but newly authored local transform must not reuse the old result.
  Float(comparison + 0x38, 320);
  Draw(comparison_draw, comparison_group, true);
  Close(drawn_x, 337 + delta);
  // Reloaded art at the same addresses must be placed from its live matrices.
  Float(comparison + 0x78, 320); // an unshifted clean world cache after a load
  Float(best_text + 0x78, 337);
  Draw(comparison_draw, comparison_group, true);
  Close(drawn_x, 337 + delta);
  const auto animated_calls = original_calls;
  Draw(comparison_draw, comparison_group, true);
  CHECK_EQ(original_calls, animated_calls);

  BeginCase("ordinary venue groups do not prepare owning HUD transforms");
  Reset();
  const auto owner_anchor = Make(0, shapes[4], 300);
  const auto owner_dir = owner_anchor - 236;
  ObjectView(owner_dir, owner_anchor + 0x1B4);
  const Shape ordinary_group{"venue.grp", 0x82024C4C, -0x108, 0};
  const auto venue = Make(1, ordinary_group, 0, owner_dir);
  const auto venue_calls = original_calls;
  Draw(venue, ordinary_group, true);
  CHECK_EQ(original_calls, venue_calls);
  CHECK_EQ(memory[owner_anchor + 189], 1);

  BeginCase("comparison draw group can own art without sharing its transform parent");
  Reset();
  Word(kHud + 0x64, Text("loading.milo"));
  const auto owned_comparison = Make(0, shapes[4], 300);
  // DynamicLeaderboard constructor: complete ObjectDir is 236 bytes before its
  // transformable; the Hmx::Object view is another 436 bytes after that base.
  const auto comparison_dir = owned_comparison - 236;
  const auto comparison_header = owned_comparison + 0x1B4;
  ObjectView(comparison_dir, comparison_header);
  const auto independent_draw = Make(1, comparison_group, 0, comparison_dir);
  const auto owned_text = Make(2, best_label, 12, comparison_dir, owned_comparison);
  Resolve(owned_text);
  Resolve(independent_draw);
  Word(kHud + 0x64, Text("track/hud.milo"));
  drawn_child = owned_text;
  Draw(independent_draw, comparison_group, true);
  Close(drawn_x, 312 + delta);
  Close(Float(independent_draw + 0x78), 0); // container stays authored, art inherits its dir
  old_dirty_calls = dirty_calls;
  Draw(independent_draw, comparison_group, true);
  Close(drawn_x, 312 + delta);
  CHECK_EQ(dirty_calls, old_dirty_calls);

  BeginCase("run comparison visibility suppresses native and direct group draws in either orientation");
  CHECK_TRUE(rb_blitz::settings::title::show_run_comparison.Set(false).has_value());
  for (const bool portrait : {false, true}) {
    width = portrait ? 720 : 1280;
    height = portrait ? 1280 : 720;
    Draw(owned_comparison, shapes[4], false, false);
    Draw(independent_draw, comparison_group, true, false);
    // A direct group may use a transform parent without an owning comparison dir.
    const auto parented_draw = Make(3, comparison_group, 0, kHud, owned_comparison);
    Draw(parented_draw, comparison_group, true, false);
    const auto other_draw = Make(4, comparison_group, 0, kHud);
    Draw(other_draw, comparison_group, true);
    const auto non_hud = Make(5, shapes[4], 300, kOtherDir);
    Draw(non_hud, shapes[4]);
    const auto score = Make(6, shapes[0], 270);
    Draw(score, shapes[0]);
  }
  CHECK_TRUE(rb_blitz::settings::title::show_run_comparison.Set(true).has_value());
  Draw(owned_comparison, shapes[4]);
  Draw(independent_draw, comparison_group, true);

  BeginCase("direct group draw prepares nested named anchors in parent-first order");
  Reset();
  Word(kHud + 0x64, Text("loading.milo"));
  const auto comparison_parent = Make(0, shapes[4], 300);
  const auto nested_anchor = Make(1, shapes[0], 15, kHud, comparison_parent);
  const auto nested_group = Make(2, comparison_group, 2, kHud, nested_anchor);
  const auto nested_text = Make(3, best_label, 5, kHud, nested_group);
  Resolve(nested_text);
  Word(kHud + 0x64, Text("track/hud.milo"));
  drawn_child = nested_text;
  Draw(nested_group, comparison_group, true);
  Close(drawn_x, 322 + delta);
  Close(Float(comparison_parent + 0x78), 300 + delta);
  Close(Float(nested_anchor + 0x78), 315 + delta);

  BeginCase("retired scan flag performs no search and invalid path pointers are rejected");
  Reset();
  CHECK_TRUE(rb_blitz::settings::title::hud_reanchor_scan.Set(true).has_value());
  const auto t2 = Make(0, shapes[1], -300);
  Word(kHud + 0x64, 0x60000000); // reserved but uncommitted, in the guest heap range
  Resolve(t2); Close(Float(t2 + 0x78), -300);
  Word(kHud + 0x6C, Text("track/hud.milo"));
  Resolve(t2); Close(Float(t2 + 0x78), -300 - delta);
  Float(t2 + 0x38, std::numeric_limits<float>::infinity());
  Resolve(t2); CHECK_TRUE(std::isinf(Float(t2 + 0x78)));

#if defined(RB_BLITZ_IOS)
  BeginCase("touch pause matches its hit rectangle at 16:9 and hides outside touch play");
  Reset();
  width = 1920; height = 1080;
  using_touch = true;
  const Shape pause_shape{"touch_pause.grp", 0x82024C4C, -0x108, -1};
  const auto pause = Make(0, pause_shape, -289.338f);
  Resolve(pause);
  const auto rect = touch_layout.pause;
  Close(Float(pause + 0x78), ((rect.x + rect.width / 2) / touch_layout.width - .5f) * (1920.f / 1080.f) * 480);
  Close(Float(pause + 0x80), (.5f - (rect.y + rect.height / 2) / touch_layout.height) * 480);
  const auto scale = rect.height * 480 / (touch_layout.height * 54);
  Close(Float(pause + 72), scale);
  Resolve(pause);
  Close(Float(pause + 72), scale);
  ObjectView(pause - 64, pause + 0x108);
  PPCContext call{}; call.r1.u32 = kHeap + 0x10000; call.r3.u32 = pause - 64;
  using_touch = false;
  const auto old_draws = draw_calls;
  sub_82226CD8(call, memory);
  CHECK_EQ(draw_calls, old_draws);
  using_touch = true; buttons_enabled = false;
  sub_82226CD8(call, memory);
  CHECK_EQ(draw_calls, old_draws);
#endif

  BeginCase("native deploy readiness is published on every platform, independently of the reminder");
  Reset();
  const auto provider = kHeap + 0xE000, node = kHeap + 0xF000;
  Word(provider + 32, Text("powerups_state.ep"));
  const auto property = Text("powerup_can_deploy");
  using_touch = true; buttons_enabled = true;
  Word(node + 4, 0); // native boolean is a DataNode integer
  for (const bool ready : {false, true, false, true}) {
    Word(node, ready ? 1 : 0);
    PPCContext write{}; write.r1.u32 = kHeap + 0x10000;
    write.r3.u32 = provider; write.r4.u32 = property; write.r5.u32 = node;
    const auto calls = property_writes;
    sub_823FF258(write, memory);
    CHECK_EQ(property_writes, calls + 1);
    CHECK_EQ(write.r3.u32, uint32_t(0x12345678)); // preserve original return registers
    CHECK_EQ(powerup_ready, ready);
  }
  Word(provider + 32, Text("unrelated.ep"));
  Word(node, 0);
  PPCContext unrelated{}; unrelated.r1.u32 = kHeap + 0x10000;
  unrelated.r3.u32 = provider; unrelated.r4.u32 = property; unrelated.r5.u32 = node;
  sub_823FF258(unrelated, memory);
  CHECK_TRUE(powerup_ready); // unrelated providers cannot hide the control

  BeginCase("native lane group publishes its target on every platform without replacing its draw");
  Reset();
  const Shape lane_shape{"powerup.grp", 0x82024C4C, -0x108, 0};
  const auto lane = Make(0, lane_shape, 0);
  ObjectView(lane - 64, lane + 0x108);
  PPCContext lane_draw{}; lane_draw.r1.u32 = kHeap + 0x10000; lane_draw.r3.u32 = lane - 64; lane_draw.r4.u32 = 0xCAFEBABE;
  const auto captures_before = lane_captures, draws_before = draw_calls;
  sub_82226CD8(lane_draw, memory);
  CHECK_EQ(lane_captures, captures_before + 1);
  CHECK_EQ(draw_calls, draws_before + 1);
#if defined(_WIN32)
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  munmap(memory, size_t(0x83000000));
#endif
  return Finish();
}
