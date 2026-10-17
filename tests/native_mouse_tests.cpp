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

  BeginCase("the mouse's unhover lifts only the highlight it owns (owner rule)");
  MouseHighlight h;
  h.Hover(0x10, 3);
  CHECK_FALSE(h.Lifted(0x10, 3));  // a hover draws the highlight
  CHECK_TRUE(h.PointerLeft(0x10, 3) == MouseHighlight::Leave::kLifted);
  CHECK_TRUE(h.Lifted(0x10, 3));   // ... and the list stays unlit
  CHECK_TRUE(h.Lifted(0x10, -1));  // scrolled out of the band: same selection, still unlit
  CHECK_TRUE(h.Lifted(0x10, 3));
  CHECK_FALSE(h.Lifted(0x10, 4));  // a controller moved it: the highlight returns
  CHECK_FALSE(h.Lifted(0x10, 4));
  CHECK_FALSE(h.Lifted(0x20, 3));  // another list is never affected
  h.Hover(0x10, 5);
  CHECK_TRUE(h.PointerLeft(0x10, 6) == MouseHighlight::Leave::kKept);  // moved before the leave
  CHECK_FALSE(h.Lifted(0x10, 6));
  h.Hover(0x10, 2);
  CHECK_TRUE(h.PointerLeft(0x10, 2) == MouseHighlight::Leave::kLifted);
  CHECK_TRUE(h.Lifted(0x10, 2));
  h.Hover(0x10, 2);                // landing on the row again owns it and lifts nothing
  CHECK_FALSE(h.Lifted(0x10, 2));
  CHECK_TRUE(h.PointerLeft(0x99, 2) == MouseHighlight::Leave::kIgnored);
  h.Hover(0x10, 2);
  h.Reset();
  CHECK_TRUE(h.PointerLeft(0x10, 2) == MouseHighlight::Leave::kIgnored);
  CHECK_FALSE(h.Lifted(0x10, 2));

  BeginCase("a move outside the game rectangle leaves every row");
  MenuMouseQueue outside;
  outside.Context(1, 2, true);
  outside.Push(MouseAction::kMove, {.5f, .5f});
  outside.Push(MouseAction::kMove, {NAN, NAN});  // what the window thread sends for outside
  auto miss = outside.NextFrame();
  CHECK_TRUE(miss && miss->action == MouseAction::kMove);
  CHECK_EQ(HitMenuRow(miss->point, {0,0,1,1}, library), -1);

  BeginCase("a scroll the mouse applied re-catches the hover under the pointer");
  MouseScrollFollow follow;
  CHECK_FALSE(follow.Take().has_value());  // no scroll waiting: nothing lands
  follow.Arm({.4f, .6f});
  auto land = follow.Take();
  CHECK_TRUE(land && land->x == .4f && land->y == .6f);
  CHECK_FALSE(follow.Take().has_value());  // ... exactly once per applied scroll
  follow.Arm({.4f, .6f});
  follow.Pointer({.5f, .7f});              // a later pointer event re-targets the catch
  land = follow.Take();
  CHECK_TRUE(land && land->x == .5f && land->y == .7f);
  follow.Pointer({.5f, .7f});              // an event without a scroll arms nothing
  CHECK_FALSE(follow.Take().has_value());
  follow.Arm({.1f, .2f});
  follow.Reset();                          // a screen or focus change discards it
  CHECK_FALSE(follow.Take().has_value());

  BeginCase("wheel detents carry the pointer and the burst drains before the catch");
  MenuMouseQueue wheel;
  wheel.Context(1, 2, true);
  wheel.Wheel(120, {.5f, .6f});
  auto detent = wheel.NextFrame();
  CHECK_TRUE(detent && detent->action == MouseAction::kUp && detent->point.x == .5f && detent->point.y == .6f);
  CHECK_FALSE(wheel.Pending());
  wheel.Wheel(-240, {.2f, .3f});
  CHECK_TRUE(wheel.Pending());             // both detents are still queued
  detent = wheel.NextFrame();
  CHECK_TRUE(detent && detent->action == MouseAction::kDown && detent->point.y == .3f);
  CHECK_TRUE(wheel.Pending());
  CHECK_TRUE(wheel.NextFrame()->action == MouseAction::kDown);
  CHECK_FALSE(wheel.Pending());
  wheel.Wheel(360, {.5f, .5f});
  wheel.Reset();
  CHECK_FALSE(wheel.Pending());            // discarded input is not followed either

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
  q.Wheel(60, {.5f, .5f}); CHECK_FALSE(q.NextFrame());
  q.Wheel(60, {.5f, .5f}); CHECK_TRUE(q.NextFrame()->action == MouseAction::kUp);
  q.Wheel(-360, {.5f, .5f});
  for (int i = 0; i < 3; ++i) CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);
  CHECK_FALSE(q.NextFrame());
  q.Wheel(60, {.5f, .5f}); q.Wheel(-120, {.5f, .5f});
  CHECK_TRUE(q.NextFrame()->action == MouseAction::kDown);

  BeginCase("scroll animations retain wheel detents without retaining button pulses");
  q.Wheel(-240, {.5f, .5f});
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
    q.Pulse(MouseAction::kBack); q.Wheel(480, {.5f, .5f}); q.Push(MouseAction::kAccept);
    q.Context(boundary == 0 ? 9 : 1, boundary == 1 ? 9 : 2, boundary != 2);
    CHECK_EQ(q.pulse(), 0); CHECK_FALSE(q.NextFrame());
  }
  q.Push(MouseAction::kAccept); q.Wheel(-120, {.5f, .5f});
  q.Context(1, 2, true);
  CHECK_FALSE(q.NextFrame());

  BeginCase("scrollbar drags coalesce and never lose a press or release");
  MenuMouseQueue rail;
  rail.Context(1, 2, true);
  rail.Scrollbar(MouseAction::kScrollbarDown, {.6f, .3f});
  rail.Scrollbar(MouseAction::kScrollbarDrag, {.6f, .31f});
  rail.Scrollbar(MouseAction::kScrollbarDrag, {.6f, .35f});
  rail.Scrollbar(MouseAction::kScrollbarUp, {.6f, .35f});
  auto drag = rail.NextFrame();
  CHECK_TRUE(drag && drag->action == MouseAction::kScrollbarDown && drag->point.y == .3f);
  drag = rail.NextFrame();
  CHECK_TRUE(drag && drag->action == MouseAction::kScrollbarDrag && drag->point.y == .35f);
  drag = rail.NextFrame();
  CHECK_TRUE(drag && drag->action == MouseAction::kScrollbarUp);
  CHECK_FALSE(rail.NextFrame());

  BeginCase("scrollbar traffic flows while a button pulse waits for its release");
  rail.Push(MouseAction::kAccept, {.5f, .5f});
  rail.Pulse(rail.NextFrame()->action);
  rail.Scrollbar(MouseAction::kScrollbarDrag, {.6f, .2f});
  CHECK_TRUE(rail.NextFrame()->action == MouseAction::kScrollbarDrag);
  CHECK_EQ(rail.pulse(), 0x1000);
  rail.SamplePad();
  CHECK_FALSE(rail.NextFrame());
  rail.Reset();
  CHECK_FALSE(rail.NextFrame());

  BeginCase("rail thumb math walks firstShowing across the measured rail");
  std::vector<MenuRow> rail_rows{
      {{.3f, .2f}, 0, true, MenuBounds{.2f, .15f, .4f, .25f}},
      {{.3f, .3f}, 1, false, MenuBounds{.2f, .25f, .4f, .35f}},
      {{.3f, .4f}, 2, true, MenuBounds{.2f, .35f, .4f, .45f}},
      {{.3f, .5f}, 3, true, MenuBounds{.2f, .45f, .4f, .55f}}};
  const float pitch = RailPitch(rail_rows);
  CHECK_TRUE(std::abs(pitch - .1f) < .001f);
  CHECK_TRUE(RailPitch({{{.5f, .5f}, 0, true}}) == 0);
  // The library's rail: top .2175, bottom .8348, one-row-pitch thumb, 41 scrollable rows.
  const float per = RailPerFirst(.8348f - .2175f, .0624f, 41);
  CHECK_TRUE(std::abs(per - (.8348f - .2175f - .0624f) / 41) < 1e-7f);
  CHECK_EQ(RailPerFirst(.6f, .06f, 0), 0);
  CHECK_EQ(RailPerFirst(.05f, .06f, 41), 0);
  CHECK_EQ(RailFirstFromY(.2175f, .2175f, per, 41), 0);
  CHECK_EQ(RailFirstFromY(.8348f, .2175f, per, 41), 41);
  CHECK_EQ(RailFirstFromY(.95f, .2175f, per, 41), 41);
  CHECK_EQ(RailFirstFromY(.05f, .2175f, per, 41), 0);
  CHECK_EQ(RailFirstFromY(.5f, .2175f, per, 41), int(std::lround((.5f - .2175f) / per)));
  CHECK_EQ(RailFirstFromY(.5f, .2175f, 0, 41), 0);
  CHECK_TRUE(RailYOnThumb(.2175f + per * 10 + .01f, .2175f, per, .0624f, 10));
  CHECK_FALSE(RailYOnThumb(.2175f + per * 10 + .09f, .2175f, per, .0624f, 10));
  CHECK_TRUE(RailYOnThumb(.5f, .2175f, per, .4f, 0));

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
