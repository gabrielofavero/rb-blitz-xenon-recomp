// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "input/native_menu.h"
#include "input/pad_packet.h"
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

  BeginCase("icon-only powerup cells support direct selection without text");
  std::vector<MenuRow> icons{
      {{.15f,.30f}, 0, true, MenuBounds{.08f,.17f,.23f,.43f}},
      {{.30f,.30f}, 1, true, MenuBounds{.23f,.17f,.38f,.43f}},
      {{.45f,.30f}, 2, true, MenuBounds{.37f,.17f,.53f,.43f}},
      {{.60f,.30f}, 3, true, MenuBounds{.52f,.17f,.67f,.43f}}};
  for (int i = 0; i < 4; ++i)
    CHECK_EQ(HitMenuRow(icons[i].center, {.08f,.17f,.67f,.43f}, icons), i);
  CHECK_EQ(HitMenuRow({.60f,.6f}, {0,0,1,1}, icons), -1);

  BeginCase("library rows include their full width but artist headings remain inert");
  std::vector<MenuRow> library{
      {{.34f,.31f}, 4, true, MenuBounds{.09f,.287f,.59f,.339f}},
      {{.34f,.376f}, 5, false, MenuBounds{.09f,.35f,.59f,.402f}},
      {{.34f,.439f}, 6, true, MenuBounds{.09f,.413f,.59f,.465f}}};
  CHECK_EQ(HitMenuRow({.55f,.439f}, {0,0,1,1}, library), 6);
  CHECK_EQ(HitMenuRow({.55f,.376f}, {0,0,1,1}, library), -1);
  CHECK_EQ(HitMenuRow({.55f,.407f}, {0,0,1,1}, library), -1);

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
  CHECK_EQ(q.SamplePad(), 0x1000);
  CHECK_FALSE(q.NextFrame());
  CHECK_EQ(q.pulse(), 0x1000);
  CHECK_FALSE(q.NextFrame());
  CHECK_EQ(q.pulse(), 0);
  CHECK_EQ(q.SamplePad(), 0);
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kMove);

  BeginCase("fractional wheel, fast detents, and reversal");
  q.Wheel(60); CHECK_FALSE(q.NextFrame());
  q.Wheel(60); CHECK_TRUE(q.NextFrame()->action == MouseAction::kUp);
  q.Wheel(-360);
  for (int i = 0; i < 3; ++i) CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);
  CHECK_FALSE(q.NextFrame());
  q.Wheel(60); q.Wheel(-120);
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);

  BeginCase("scroll animations retain wheel detents without retaining button pulses");
  q.Wheel(-240);
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);
  q.Pulse(MouseAction::kDown);
  CHECK_EQ(q.SamplePad(), 2);
  CHECK_FALSE(q.NextFrame(false));
  CHECK_FALSE(q.NextFrame(false)); CHECK_EQ(q.pulse(), 0);
  CHECK_EQ(q.SamplePad(), 0);
  CHECK_FALSE(q.NextFrame(false));
  CHECK_TRUE(q.NextFrame(true)->action == MouseAction::kDown);
  q.Push(MouseAction::kMove, {.5f,.5f});
  CHECK_TRUE(q.NextFrame(false)->action == MouseAction::kMove);

  BeginCase("horizontal powerup wheel pulses release before the next direction");
  q.Pulse(MouseAction::kRight); CHECK_EQ(q.pulse(), 8);
  CHECK_EQ(q.SamplePad(), 8);
  CHECK_FALSE(q.NextFrame()); CHECK_FALSE(q.NextFrame()); CHECK_EQ(q.pulse(), 0);
  q.Pulse(MouseAction::kLeft); CHECK_EQ(q.pulse(), 4);
  q.Reset();

  BeginCase("a delayed input thread cannot lose a click or its release");
  MenuMouseQueue delayed;
  delayed.Context(1, 2, true);
  delayed.Push(MouseAction::kAccept);
  delayed.Pulse(delayed.NextFrame()->action);
  delayed.Push(MouseAction::kAccept);
  // UI frames and connectivity reads do not acknowledge delivery to player one.
  for (int i = 0; i < 60; ++i) {
    CHECK_FALSE(delayed.NextFrame());
    CHECK_EQ(delayed.pulse(), 0x1000);
  }
  CHECK_EQ(delayed.SamplePad(), 0x1000);
  CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.pulse(), 0);
  // A second click must wait until the input thread also sees the release.
  for (int i = 0; i < 60; ++i) CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.SamplePad(), 0);
  auto second = delayed.NextFrame();
  CHECK_TRUE(second && second->action == MouseAction::kAccept);

  BeginCase("many pad polls cannot shorten a press or skip the released frame");
  delayed.Pulse(MouseAction::kAccept);
  for (int i = 0; i < 20; ++i) CHECK_EQ(delayed.SamplePad(), 0x1000);
  CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.SamplePad(), 0x1000);
  delayed.Push(MouseAction::kBack);
  CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.SamplePad(), 0);
  CHECK_TRUE(delayed.NextFrame()->action == MouseAction::kBack);

  BeginCase("new submenu clicks wait for the previous category click to release");
  delayed.Pulse(MouseAction::kAccept);
  CHECK_EQ(delayed.SamplePad(), 0x1000);
  delayed.Context(1, 3, true);
  delayed.Push(MouseAction::kAccept);
  CHECK_EQ(delayed.pulse(), 0);
  CHECK_FALSE(delayed.NextFrame());
  CHECK_EQ(delayed.SamplePad(), 0);
  CHECK_TRUE(delayed.NextFrame()->action == MouseAction::kAccept);

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

  BeginCase("final pad packets expose mouse edges beside an idle controller");
  PadPacket packets;
  PadSample pad;
  // Upstream merging can keep returning the same large physical-device packet
  // while the mouse's smaller packet advances. Only final state matters here.
  auto packet = packets.Observe(pad);
  CHECK_EQ(packets.Observe(pad), packet);
  pad.buttons = 0x1000;
  CHECK_EQ(packets.Observe(pad), ++packet);
  for (int i = 0; i < 20; ++i) CHECK_EQ(packets.Observe(pad), packet);
  pad.buttons = 0;
  CHECK_EQ(packets.Observe(pad), ++packet);
  pad.buttons = 0x1000;
  CHECK_EQ(packets.Observe(pad), ++packet);
  pad.buttons = 0x2000;
  CHECK_EQ(packets.Observe(pad), ++packet);

  BeginCase("packets cover every analog axis and trigger as well as buttons");
  pad.left_trigger = 1; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.right_trigger = 1; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.thumb_lx = -32767; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.thumb_ly = 32767; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.thumb_rx = -123; CHECK_EQ(packets.Observe(pad), ++packet);
  pad.thumb_ry = 456; CHECK_EQ(packets.Observe(pad), ++packet);
  CHECK_EQ(packets.Observe(pad), packet);
  PadPacket player_two;
  CHECK_EQ(player_two.Observe({}), 1);
  CHECK_EQ(packets.Observe(pad), packet);
  return Finish();
}
