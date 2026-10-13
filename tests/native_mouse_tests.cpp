// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "input/native_menu.h"
using namespace rb_blitz::input;
using namespace rb_blitz::test;

int main() {
  BeginCase("pointer mapping respects letterboxing, stretch and overscan");
  auto p = MenuPointer(1280, 360, 2560, 720, 16, 9, true, false, 90, 90);
  CHECK_TRUE(p.x == .5f && p.y == .5f);
  p = MenuPointer(640, 360, 2560, 720, 16, 9, true, false, 90, 90);
  CHECK_TRUE(p.x == 0 && p.y == .5f);
  p = MenuPointer(640, 360, 2560, 720, 16, 9, false, false, 90, 90);
  CHECK_TRUE(p.x == .25f && p.y == .5f);
  p = MenuPointer(400, 0, 800, 800, 16, 9, true, false, 90, 90);
  CHECK_TRUE(p.x == .5f && p.y < 0);
  p = MenuPointer(1280, 0, 2560, 720, 16, 9, true, true, 90, 90);
  CHECK_TRUE(std::abs(p.y - .05f) < .001f);
  CHECK_FALSE(std::isfinite(MenuPointer(0, 0, 0, 0, 16, 9, true, false, 90, 90).x));
  BeginCase("native geometry selects distant rows directly and rejects disabled/outside rows");
  MenuBounds bounds{.2f, .1f, .8f, .9f};
  std::vector<MenuRow> rows{{{.5f,.2f}, 0, true}, {{.5f,.5f}, 7, false}, {{.5f,.8f}, 25, true}};
  CHECK_EQ(HitMenuRow({.5f,.8f}, bounds, rows), 25);
  CHECK_EQ(HitMenuRow({.5f,.5f}, bounds, rows), -1);
  CHECK_EQ(HitMenuRow({.9f,.8f}, bounds, rows), -1);
  CHECK_EQ(HitMenuRow({NAN,.8f}, bounds, rows), -1);

  BeginCase("move coalescing preserves click ordering and never synthesizes directions");
  MenuMouseQueue q;
  q.Context(1, 2, true);
  q.Push(MouseAction::kMove, {.1f,.1f});
  q.Push(MouseAction::kMove, {.5f,.8f});
  q.Push(MouseAction::kAccept, {.5f,.8f});
  q.Push(MouseAction::kMove, {.5f,.2f});
  auto e = q.NextFrame();
  CHECK_TRUE(e && e->point.y == .8f);
  q.Pulse(e->action);
  CHECK_EQ(q.pulse(), 0);
  e = q.NextFrame();
  CHECK_TRUE(e && e->action == MouseAction::kAccept && e->point.y == .8f);
  q.Pulse(e->action);
  for (int i = 0; i < 100; ++i) CHECK_EQ(q.pulse(), 0x1000);
  CHECK_FALSE(q.NextFrame());
  CHECK_EQ(q.pulse(), 0x1000);
  CHECK_FALSE(q.NextFrame());
  CHECK_EQ(q.pulse(), 0);
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kMove);

  BeginCase("fractional wheel, fast detents, and reversal");
  q.Wheel(60); CHECK_FALSE(q.NextFrame());
  q.Wheel(60); CHECK_TRUE(q.NextFrame()->action == MouseAction::kUp);
  q.Wheel(-360);
  for (int i = 0; i < 3; ++i) CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);
  CHECK_FALSE(q.NextFrame());
  q.Wheel(60); q.Wheel(-120);
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);

  BeginCase("gameplay, focus loss and transitions discard pulses and pending input");
  for (int boundary = 0; boundary < 3; ++boundary) {
    q.Context(1, 2, true);
    q.Pulse(MouseAction::kBack); q.Wheel(480); q.Push(MouseAction::kAccept);
    q.Context(boundary == 0 ? 9 : 1, boundary == 1 ? 9 : 2, boundary != 2);
    CHECK_EQ(q.pulse(), 0); CHECK_FALSE(q.NextFrame());
  }
  q.Push(MouseAction::kAccept); q.Wheel(-120);
  q.Context(1, 2, true);
  CHECK_FALSE(q.NextFrame());
  return Finish();
}
