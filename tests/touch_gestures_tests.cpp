// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "input/touch_gestures.h"
#include <limits>

using namespace rb_blitz::test;
using namespace rb_blitz::input::touch;
namespace {
bool Held(const Actions& sample, Action action) { return sample[static_cast<size_t>(action)]; }
bool Empty(const Actions& sample) { for (bool down : sample) if (down) return false; return true; }
Gestures Ready() {
  Gestures g;
  g.SetLayout(MakeLayout(874, 402, {62, 0, 750, 381}));
  g.SetActive(true);
  g.DrainGameplay();
  return g;
}
void Event(Gestures& g, Phase phase, float x, float y = .6f, uint64_t finger = 1, uint64_t device = 1) {
  g.Observe({device, finger, phase, x, y});
}
void Tap(Gestures& g, float x, float y = .6f, uint64_t finger = 1) {
  Event(g, Phase::kDown, x, y, finger);
  Event(g, Phase::kUp, x, y, finger);
}
}
int main() {
  BeginCase("a note fires on down; release resolves that same touch without another strike");
  auto g = Ready();
  Event(g, Phase::kDown, .1f);
  CHECK_TRUE(Held(g.Sample(), Action::kFirstNote));
  auto events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(1));
  CHECK_TRUE(events[0].kind == GameplayKind::kPress);
  const auto first = events[0].token;
  Event(g, Phase::kUp, .1f);
  CHECK_TRUE(Empty(g.Sample()));
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(1));
  CHECK_EQ(events[0].token, first);
  CHECK_TRUE(events[0].kind == GameplayKind::kRelease);
  CHECK_FALSE(events[0].swiped);
  Tap(g, .5f);
  CHECK_TRUE(Held(g.Sample(), Action::kSecondNote));
  CHECK_TRUE(Empty(g.Sample()));

  BeginCase("swipes invert direction and forgive only their own starting press");
  for (bool right : {false, true}) {
    g = Ready();
    const float start = right ? .1f : .8f, end = right ? .8f : .1f;
    Event(g, Phase::kDown, start);
    CHECK_TRUE(Held(g.Sample(), right ? Action::kFirstNote : Action::kSecondNote));
    events = g.DrainGameplay();
    const auto token = events[0].token;
    Event(g, Phase::kMove, end);
    CHECK_TRUE(Held(g.Sample(), right ? Action::kMoveLaneLeft : Action::kMoveLaneRight));
    events = g.DrainGameplay();
    CHECK_EQ(events.size(), size_t(1));
    CHECK_TRUE(events[0].kind == GameplayKind::kSwipe);
    CHECK_EQ(events[0].token, token);
    CHECK_TRUE(events[0].swiped);
    CHECK_TRUE(Empty(g.Sample()));
    Event(g, Phase::kMove, start);
    Event(g, Phase::kUp, start);
    events = g.DrainGameplay();
    CHECK_EQ(events.size(), size_t(1));
    CHECK_TRUE(events[0].kind == GameplayKind::kRelease);
    CHECK_TRUE(events[0].swiped);
    CHECK_EQ(events[0].token, token);
    CHECK_TRUE(Empty(g.Sample()));
  }
  BeginCase("a complete swipe between polls keeps press, swipe, release order");
  g = Ready();
  Event(g, Phase::kDown, .2f);
  Event(g, Phase::kUp, .4f);
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(3));
  CHECK_TRUE(events[0].kind == GameplayKind::kPress);
  CHECK_TRUE(events[1].kind == GameplayKind::kSwipe);
  CHECK_TRUE(events[2].kind == GameplayKind::kRelease);
  CHECK_EQ(events[0].token, events[2].token);
  CHECK_TRUE(events[2].swiped);

  BeginCase("vertical movement and jitter cannot forgive an ordinary wrong press");
  for (float y : {.604f, .9f}) {
    g = Ready();
    Event(g, Phase::kDown, .2f);
    Event(g, Phase::kMove, .207f, y);
    Event(g, Phase::kUp, .203f);
    events = g.DrainGameplay();
    CHECK_EQ(events.size(), size_t(2));
    CHECK_TRUE(events[1].kind == GameplayKind::kRelease);
    CHECK_FALSE(events[1].swiped);
  }
  BeginCase("two touches resolve independently even when one swipes");
  g = Ready();
  Event(g, Phase::kDown, .2f, .6f, 1);
  Event(g, Phase::kDown, .8f, .6f, 2);
  auto chord = g.Sample();
  CHECK_TRUE(Held(chord, Action::kFirstNote));
  CHECK_TRUE(Held(chord, Action::kSecondNote));
  Event(g, Phase::kMove, .4f, .6f, 1);
  Event(g, Phase::kUp, .8f, .6f, 2);
  Event(g, Phase::kUp, .4f, .6f, 1);
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(5));
  CHECK_FALSE(events[0].token == events[1].token);
  CHECK_EQ(events[0].token, events[2].token);
  CHECK_EQ(events[1].token, events[3].token);
  CHECK_FALSE(events[3].swiped);
  CHECK_TRUE(events[4].swiped);

  BeginCase("rapid same-note touches retain distinct identities; duplicate downs do not strike again");
  g = Ready();
  Tap(g, .2f); Tap(g, .2f);
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(4));
  CHECK_FALSE(events[0].token == events[2].token);
  Event(g, Phase::kDown, .2f);
  Event(g, Phase::kDown, .2f);
  CHECK_EQ(g.DrainGameplay().size(), size_t(1));

  BeginCase("device ID is part of touch identity");
  g = Ready();
  Event(g, Phase::kDown, .2f, .6f, 1, 1);
  Event(g, Phase::kDown, .8f, .6f, 1, 2);
  Event(g, Phase::kUp, .2f, .6f, 1, 1);
  Event(g, Phase::kUp, .8f, .6f, 1, 2);
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(4));
  CHECK_EQ(events[0].token, events[2].token);
  CHECK_EQ(events[1].token, events[3].token);

  BeginCase("pause stays inside the safe area and cancels pending touch judgements");
  const auto layout = MakeLayout(874, 402, {62, 0, 750, 381});
  CHECK_TRUE(layout.pause.x >= 62);
  CHECK_TRUE(layout.pause.x < 74); // exactly the first safe slot, no authored blank before pause
  CHECK_TRUE(layout.pause.x + layout.pause.width <= 812);
  CHECK_TRUE(layout.pause.height >= 44);
  CHECK_TRUE(layout.pause.x + layout.pause.width < layout.width / 2);
  CHECK_TRUE(layout.powerup.x > layout.pause.x + layout.pause.width);
  CHECK_EQ(layout.pause.y, layout.powerup.y);
  for (const auto& sized : {layout, MakeLayout(1194, 834, {0, 24, 1194, 790}),
                          MakeLayout(402, 874, {0, 62, 402, 778})}) {
    CHECK_TRUE(sized.pause.x >= 0 && sized.pause.y >= 0);
    CHECK_TRUE(sized.powerup.x + sized.powerup.width <= sized.width);
    CHECK_TRUE(sized.pause.y + sized.pause.height < sized.height);
  }
  const float px = (layout.pause.x + layout.pause.width / 2) / layout.width;
  const float py = (layout.pause.y + layout.pause.height / 2) / layout.height;
  g = Ready();
  Tap(g, .2f);
  Event(g, Phase::kDown, .3f, .6f, 2);
  Tap(g, px, py);
  auto pause = g.Sample();
  CHECK_TRUE(Held(pause, Action::kPause));
  CHECK_FALSE(Held(pause, Action::kFirstNote));
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(1));
  CHECK_TRUE(events[0].kind == GameplayKind::kCancel);
  Event(g, Phase::kUp, .3f, .6f, 2);
  Tap(g, .8f);
  CHECK_TRUE(g.DrainGameplay().empty());
  CHECK_TRUE(Held(g.Sample(), Action::kPause));
  CHECK_TRUE(Empty(g.Sample()));

  BeginCase("ready power-up is a button, not a note, and appears at the first available poll");
  const float power_x = (layout.powerup.x + layout.powerup.width / 2) / layout.width;
  g = Ready();
  g.SetPowerupAvailable(true);
  Tap(g, power_x, py);
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(2));
  CHECK_TRUE(events[0].kind == GameplayKind::kPress && events[0].action == Action::kPowerup);
  CHECK_TRUE(events[1].kind == GameplayKind::kRelease && events[1].action == Action::kPowerup);
  CHECK_TRUE(Empty(g.Sample()));
  g = Ready();
  Tap(g, power_x, py);
  CHECK_TRUE(Held(g.Sample(), Action::kFirstNote)); // no invisible unavailable button
  g = Ready();
  g.SetPowerupAvailable(true);
  Event(g, Phase::kDown, power_x, py);
  g.SetPowerupAvailable(false);
  Event(g, Phase::kUp, power_x, py);
  CHECK_TRUE(g.DrainGameplay().empty()); // readiness lost while held cannot deploy
  g = Ready();
  Event(g, Phase::kDown, .2f, .6f);
  g.SetPowerupAvailable(true);
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(1));
  CHECK_TRUE(events[0].kind == GameplayKind::kPress); // charging cannot cancel held notes

  BeginCase("the former top-right pause position is ordinary gameplay; dragging off pause cancels");
  g = Ready();
  Tap(g, .9f, .08f);
  CHECK_TRUE(Held(g.Sample(), Action::kSecondNote));
  g = Ready();
  Event(g, Phase::kDown, px, py);
  Event(g, Phase::kMove, px + .2f, py);
  Event(g, Phase::kUp, px, py);
  CHECK_TRUE(Empty(g.Sample()));
  CHECK_TRUE(g.DrainGameplay().empty());

  BeginCase("cancellation releases and discards a touch's pending wrong press");
  g = Ready();
  Event(g, Phase::kDown, .2f);
  Event(g, Phase::kCancel, .2f);
  Event(g, Phase::kUp, .2f);
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(2));
  CHECK_TRUE(events[1].kind == GameplayKind::kRelease);
  CHECK_TRUE(events[1].swiped);
  for (bool resize : {false, true}) {
    g = Ready();
    Event(g, Phase::kDown, .2f);
    if (resize) g.SetLayout(MakeLayout(1206, 2622, {}));
    else g.SetActive(false);
    events = g.DrainGameplay();
    CHECK_EQ(events.size(), size_t(1));
    CHECK_TRUE(events[0].kind == GameplayKind::kCancel);
    g.SetActive(true);
    g.DrainGameplay();  // Reactivation is another cancellation boundary.
    Event(g, Phase::kMove, .8f);
    Event(g, Phase::kUp, .8f);
    CHECK_TRUE(g.DrainGameplay().empty());
  }
  g = Ready();
  Event(g, Phase::kDown, .2f);
  Event(g, Phase::kUp, std::numeric_limits<float>::quiet_NaN());
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(2));
  CHECK_TRUE(events[1].swiped);

  BeginCase("mouse/pen synthesis cannot enter the touch judgement path");
  g = Ready();
  for (uint64_t device : {UINT64_MAX, UINT64_MAX - 1}) {
    Event(g, Phase::kDown, .2f, .6f, 1, device);
    Event(g, Phase::kMove, .8f, .6f, 1, device);
    Event(g, Phase::kUp, .8f, .6f, 1, device);
    CHECK_TRUE(g.DrainGameplay().empty());
    CHECK_TRUE(Empty(g.Sample()));
  }
  MouseGate gate;
  for (bool gameplay : {false, true})
    for (Phase phase : {Phase::kDown, Phase::kMove, Phase::kUp})
      CHECK_FALSE(gate.Consume(false, phase, gameplay));
  CHECK_FALSE(gate.Consume(true, Phase::kDown, false));
  CHECK_FALSE(gate.Consume(true, Phase::kUp, false));
  CHECK_TRUE(gate.Consume(true, Phase::kDown, true));
  CHECK_TRUE(gate.Consume(true, Phase::kMove, true));
  CHECK_TRUE(gate.Consume(true, Phase::kUp, false));
  CHECK_FALSE(gate.Consume(true, Phase::kDown, false));
  gate.Capture();
  CHECK_TRUE(gate.Consume(true, Phase::kDown, false));
  CHECK_TRUE(gate.Consume(true, Phase::kUp, false));
  CHECK_FALSE(gate.Consume(false, Phase::kDown, true));
  return Finish();
}
