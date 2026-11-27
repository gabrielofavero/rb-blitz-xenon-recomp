// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "input/touch_gestures.h"
#include "input/touch_timing.h"
#include <algorithm>
#include <limits>
#include <cmath>

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
void TimingCases() {
  BeginCase("touch judgements retain tap spacing and the native calibration offset");
  constexpr uint64_t now = 1'000'000'000;
  CHECK_TRUE(std::abs(EventTimeMs(2000, now - 40'000'000, now) - 1960) < 1e-6);
  CHECK_TRUE(std::abs(EventTimeMs(2000, now - 10'000'000, now) - 1990) < 1e-6);
  CHECK_TRUE(std::abs(EventTimeMs(2075, now - 40'000'000, now) - 2035) < 1e-6);
  CHECK_TRUE(EventTimeMs(2000, 0, now) == 2000);
  CHECK_TRUE(EventTimeMs(2000, now + 1, now) == 2000);
  CHECK_TRUE(InputAgeNs(now - kMaxInputAgeNs - 1, now) > kMaxInputAgeNs);

  BeginCase("buffered swipes suppress only their own starting note and preserve timestamps");
  auto buffered = Ready();
  buffered.Observe({1, 1, Phase::kDown, .2f, .6f, now - 40'000'000});
  buffered.Observe({1, 2, Phase::kDown, .8f, .6f, now - 30'000'000});
  buffered.Observe({1, 1, Phase::kMove, .4f, .6f, now - 10'000'000});
  auto batch = buffered.DrainGameplay();
  CHECK_EQ(batch.size(), size_t(3));
  CHECK_TRUE(batch[0].swiped);
  CHECK_FALSE(batch[1].swiped);
  CHECK_EQ(batch[0].timestamp_ns, now - 40'000'000);
  CHECK_EQ(batch[1].timestamp_ns, now - 30'000'000);
  CHECK_EQ(batch[2].timestamp_ns, now - 10'000'000);
  buffered.Observe({1, 2, Phase::kUp, .8f, .6f, now});
  batch = buffered.DrainGameplay();
  CHECK_EQ(batch[0].timestamp_ns, now);

  BeginCase("calibration uses raw down edges and discards retired sampling intervals");
  CalibrationTaps calibration;
  CHECK_FALSE(calibration.Observe(Phase::kDown, 100));
  calibration.SetTarget(1);
  CHECK_TRUE(calibration.Drain(1).empty()); // a held finger cannot become a sample
  CHECK_FALSE(calibration.Observe(Phase::kDown, 105)); // neither can a duplicate down
  CHECK_FALSE(calibration.Observe(Phase::kUp, 110));
  CHECK_TRUE(calibration.Observe(Phase::kDown, 200));
  CHECK_TRUE(calibration.Observe(Phase::kDown, 201)); // duplicate down
  CHECK_TRUE(calibration.Observe(Phase::kUp, 210));
  CHECK_TRUE(calibration.Observe(Phase::kDown, 300));
  auto taps = calibration.Drain(1);
  CHECK_EQ(taps.size(), size_t(2));
  CHECK_EQ(taps[0], uint64_t(200));
  CHECK_EQ(taps[1], uint64_t(300));
  calibration.SetTarget(0);
  CHECK_TRUE(calibration.Observe(Phase::kUp, 310)); // owns its synthetic release
  calibration.SetTarget(2);
  CHECK_TRUE(calibration.Drain(2).empty());
  CHECK_TRUE(calibration.Observe(Phase::kDown, 400));
  calibration.SetTarget(3);
  CHECK_TRUE(calibration.Drain(2).empty());
  CHECK_TRUE(calibration.Drain(3).empty());
  calibration.Cancel();
  calibration.SetTarget(3);
  CHECK_TRUE(calibration.Observe(Phase::kDown, 500));
  taps = calibration.Drain(3);
  CHECK_EQ(taps.size(), size_t(1));
  CHECK_EQ(taps[0], uint64_t(500));
}
}
int main() {
  TimingCases();

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

  BeginCase("both swipe directions forgive only their own starting press");
  for (bool inverted : {true, false}) for (bool right : {false, true}) {
    g = Ready();
    if (!inverted) g.SetInvertedLaneSwipe(false); // untouched default remains inverted
    const float start = right ? .1f : .8f, end = right ? .8f : .1f;
    Event(g, Phase::kDown, start);
    CHECK_TRUE(Held(g.Sample(), right ? Action::kFirstNote : Action::kSecondNote));
    events = g.DrainGameplay();
    const auto token = events[0].token;
    Event(g, Phase::kMove, end);
    const auto lane = right == inverted ? Action::kMoveLaneLeft : Action::kMoveLaneRight;
    CHECK_TRUE(Held(g.Sample(), lane));
    events = g.DrainGameplay();
    CHECK_EQ(events.size(), size_t(1));
    CHECK_TRUE(events[0].kind == GameplayKind::kSwipe);
    CHECK_TRUE(events[0].action == lane);
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
  CHECK_TRUE(std::abs(layout.pause.y + layout.pause.height / 2 -
                      402.f * (59.3836f / 480.f)) < .01f);
  CHECK_TRUE(layout.pause.x + layout.pause.width < layout.width / 2);
  for (const auto& sized : {layout, MakeLayout(1194, 834, {0, 24, 1194, 790}),
                          MakeLayout(402, 874, {0, 62, 402, 778})}) {
    CHECK_TRUE(sized.pause.x >= 0 && sized.pause.y >= 0);
    CHECK_TRUE(sized.pause.y + sized.pause.height < sized.height);
  }
  const auto portrait_controls = MakeLayout(405, 720, {0, 24, 405, 662});
  CHECK_EQ(portrait_controls.pause.width, 44.f);
  CHECK_EQ(portrait_controls.pause.y, 150.f);
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

  BeginCase("the removed power-up button is ordinary gameplay, even when ready");
  g = Ready();
  g.SetPowerupAvailable(true);
  Tap(g, (layout.pause.x + layout.pause.width + 30) / layout.width, py);
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(2));
  CHECK_TRUE(events[0].action == Action::kFirstNote);

  BeginCase("the lane slot follows the presented game viewport in both orientations");
  for (const Rect viewport : {Rect{0, 0, 305, 662}, Rect{20, 30, 1280, 720}}) {
    const auto slot = LanePowerupSlot(viewport);
    CHECK_TRUE(slot.Contains(viewport.x + viewport.width * .5f,
                             viewport.y + viewport.height * .86f));
    CHECK_TRUE(slot.Contains(viewport.x + viewport.width * .5f,
                             viewport.y + viewport.height * .89f));
    CHECK_FALSE(slot.Contains(viewport.x + viewport.width * .5f,
                              viewport.y + viewport.height * .77f));
    CHECK_FALSE(slot.Contains(viewport.x + viewport.width * .25f,
                              viewport.y + viewport.height * .86f));
  }
  CHECK_TRUE(LanePowerupSlot({}) == Rect{});
  BeginCase("native lane icon accepts mouse and direct touch without generating note events");
  for (uint64_t device : {uint64_t(1), UINT64_MAX}) {
    PowerupTarget target;
    target.SetTarget({400, 330, 74, 64}, true);
    CHECK_FALSE(target.Observe({device, 1, Phase::kDown, 390, 350}).consumed);
    CHECK_FALSE(target.Observe({device, 1, Phase::kUp, 420, 350}).deploy);
    CHECK_TRUE(target.Observe({device, 1, Phase::kDown, 420, 350}).consumed);
    target.SetTarget({500, 330, 74, 64}, true); // moving artwork retains original press bounds
    const auto released = target.Observe({device, 1, Phase::kUp, 420, 350});
    CHECK_TRUE(released.consumed && released.deploy);
    CHECK_FALSE(target.Observe({device, 1, Phase::kUp, 420, 350}).consumed);
    target.SetTarget({400, 330, 74, 64}, true);
    CHECK_TRUE(target.Observe({device, 1, Phase::kDown, 420, 350}).consumed);
    CHECK_TRUE(target.Observe({device, 1, Phase::kMove, 500, 350}).consumed);
    CHECK_FALSE(target.Observe({device, 1, Phase::kUp, 420, 350}).deploy);
    target.Observe({device, 1, Phase::kDown, 420, 350});
    target.SetTarget({}, false); // pause, stale bounds, lost readiness, or focus loss
    target.SetTarget({400, 330, 74, 64}, true);
    CHECK_FALSE(target.Observe({device, 1, Phase::kUp, 420, 350}).deploy);
    target.Observe({device, 1, Phase::kDown, 420, 350});
    CHECK_FALSE(target.Observe({device, 1, Phase::kCancel, 420, 350}).deploy);
    CHECK_FALSE(target.Observe({device, 1, Phase::kUp, 420, 350}).consumed);
    target.Observe({device, 1, Phase::kDown, 420, 350});
    CHECK_FALSE(target.Observe({device, 1, Phase::kMove, std::numeric_limits<float>::quiet_NaN(), 350}).deploy);
    CHECK_FALSE(target.Observe({device, 1, Phase::kUp, 420, 350}).deploy);
    target.Observe({device, 1, Phase::kDown, 420, 350});
    target.Cancel();
    CHECK_FALSE(target.Observe({device, 1, Phase::kUp, 420, 350}).deploy);
  }
  BeginCase("simultaneous mouse and touch contacts share the ready power-up");
  PowerupTarget target;
  target.SetTarget({400, 330, 74, 64}, true);
  target.Observe({1, 1, Phase::kDown, 420, 350});
  target.Observe({UINT64_MAX, 0, Phase::kDown, 420, 350});
  CHECK_TRUE(target.Observe({1, 1, Phase::kUp, 420, 350}).deploy);
  target.SetTarget({400, 330, 74, 64}, true);
  CHECK_FALSE(target.Observe({UINT64_MAX, 0, Phase::kUp, 420, 350}).deploy);

  BeginCase("motion uses the same readiness, pause, and active gameplay gates as taps");
  g = Ready();
  g.DeployPowerup();
  CHECK_TRUE(g.DrainGameplay().empty());
  g.SetPowerupAvailable(true);
  g.DeployPowerup();
  events = g.DrainGameplay();
  CHECK_EQ(events.size(), size_t(2));
  CHECK_TRUE(events[0].action == Action::kPowerup);
  Tap(g, px, py);
  g.DrainGameplay();
  g.DeployPowerup();
  CHECK_TRUE(g.DrainGameplay().empty());

  BeginCase("a lighter shake deploys, with cooldown and quiet rearming");
  ShakeDetector shake;
  uint64_t now = 1'000'000'000;
  auto acceleration = [&](float x, float y = 9.80665f, float z = 0) {
    now += 50'000'000;
    return shake.Observe(now, x, y, z);
  };
  CHECK_FALSE(acceleration(0));
  for (int i = 0; i < 30; ++i) CHECK_FALSE(acceleration(float(i % 3) - 1));
  CHECK_TRUE(acceleration(10)); // below the former 12 m/s^2 filtered threshold
  CHECK_FALSE(acceleration(-20)); // its rebound cannot deploy again
  for (int i = 0; i < 25; ++i) CHECK_FALSE(acceleration(0));
  CHECK_TRUE(acceleration(-20)); // settled and cooldown elapsed
  shake.Reset();
  CHECK_FALSE(acceleration(0, 0, -9.80665f));
  CHECK_TRUE(acceleration(0, 0, 10)); // one sharp tilt, in another orientation
  for (int i = 0; i < 25; ++i) CHECK_FALSE(acceleration(0, 0, -9.80665f));
  CHECK_TRUE(acceleration(0, 20, -9.80665f)); // any axis can trigger it
  shake.Reset();
  CHECK_FALSE(acceleration(0));
  for (int i = 0; i < 100; ++i) CHECK_FALSE(acceleration(float(i) * .25f)); // slow motion
  shake.Reset();
  CHECK_FALSE(acceleration(0));
  for (int i = 0; i < 30; ++i) {
    now += 500'000'000;
    CHECK_FALSE(acceleration(i % 2 ? -20 : 20)); // stale samples establish a new baseline
  }
  shake.Reset();
  CHECK_FALSE(acceleration(0));
  CHECK_FALSE(acceleration(std::numeric_limits<float>::quiet_NaN()));
  CHECK_FALSE(acceleration(20)); // invalid sample discards the baseline
  CHECK_FALSE(shake.Observe(now, 0, 0, 0)); // non-increasing timestamps reset too

  BeginCase("forward wrist tilt deploys once and rearms only after returning, in every orientation");
  for (const int step_ms : {10, 20, 50}) for (const float axis : {0.f, 1.5707963f, 3.1415926f, 4.712389f}) {
    TiltDetector tilt;
    uint64_t clock = 0;
    int deployments = 0;
    auto pose = [&](float degrees, float roll = 0.f) {
      const float radians = degrees / 57.2957795f;
      clock += uint64_t(step_ms) * 1'000'000;
      deployments += tilt.Observe(clock, 9.80665f * std::cos(radians) * std::cos(axis + roll),
          9.80665f * std::cos(radians) * std::sin(axis + roll), 9.80665f * std::sin(radians));
    };
    auto hold = [&](float degrees, int ms) { for (int t = 0; t < ms; t += step_ms) pose(degrees); };
    auto tip = [&] { for (int t = step_ms; t <= 200; t += step_ms) pose(35.f + 35.f * t / 200.f); };
    hold(35, 300);
    tip(); hold(70, 200);
    CHECK_EQ(deployments, 1);
    hold(70, 1500); // cooldown alone cannot rearm a held tilt
    CHECK_EQ(deployments, 1);
    for (int t = step_ms; t <= 200; t += step_ms) pose(70.f - 35.f * t / 200.f);
    hold(35, 400);
    CHECK_EQ(deployments, 1); // returning is not another deployment
    tip(); hold(70, 200);
    CHECK_EQ(deployments, 2);
  }
  BeginCase("tilt rejects taps, sideways roll, backwards tilt, slow grip changes, and linear shakes");
  for (int scenario = 0; scenario < 5; ++scenario) {
    TiltDetector tilt;
    uint64_t clock = 1'000'000'000;
    auto pose = [&](float degrees, float roll = 0.f, float jolt = 0.f) {
      const float radians = degrees / 57.2957795f;
      clock += 20'000'000;
      return tilt.Observe(clock, 9.80665f * std::cos(radians) * std::cos(roll) + jolt,
          9.80665f * std::cos(radians) * std::sin(roll), 9.80665f * std::sin(radians));
    };
    for (int i = 0; i < 20; ++i) CHECK_FALSE(pose(35));
    for (int i = 0; i < 150; ++i) {
      if (scenario == 0) CHECK_FALSE(pose(35.f + (i % 3 - 1)));
      if (scenario == 1) CHECK_FALSE(pose(35, i * .04f));
      if (scenario == 2) CHECK_FALSE(pose(35.f - std::min(i, 50) * .7f));
      if (scenario == 3) CHECK_FALSE(pose(35.f + std::min(i, 100) * .35f));
      if (scenario == 4) CHECK_FALSE(pose(35, 0, i % 2 ? 10.f : -10.f));
    }
  }
  BeginCase("tilt can deploy from a low playing angle after the meter becomes ready");
  {
    g = Ready();
    MotionDetector detector;
    uint64_t clock = 0;
    auto pose = [&](float degrees, float acceleration = 9.80665f) {
      const float radians = degrees / 57.2957795f;
      clock += 20'000'000;
      if (detector.Observe(MotionMode::kTilt, clock, 0,
                           acceleration * std::cos(radians), acceleration * std::sin(radians)))
        g.DeployPowerup();
    };
    for (int i = 0; i < 20; ++i) pose(70);
    CHECK_TRUE(g.DrainGameplay().empty());
    g.SetPowerupAvailable(true);
    for (int i = 1; i <= 8; ++i) pose(70.f + i * 2.125f, i == 4 ? 13.f : 9.80665f);
    for (int i = 0; i < 12; ++i) pose(87);
    events = g.DrainGameplay();
    CHECK_EQ(events.size(), size_t(2));
    CHECK_TRUE(events.size() == 2 && events[0].action == Action::kPowerup &&
               events[1].kind == GameplayKind::kRelease);
  }
  BeginCase("invalid, stale, reversed and reset tilt samples discard partial gestures");
  for (int scenario = 0; scenario < 5; ++scenario) {
    TiltDetector tilt;
    uint64_t clock = 1'000'000'000;
    auto pose = [&](float degrees) {
      const float radians = degrees / 57.2957795f;
      clock += 20'000'000;
      return tilt.Observe(clock, 9.80665f * std::cos(radians), 0, 9.80665f * std::sin(radians));
    };
    for (int i = 0; i < 20; ++i) CHECK_FALSE(pose(35));
    for (int i = 1; i <= 4; ++i) CHECK_FALSE(pose(35.f + i * 4.f));
    if (scenario == 0) tilt.Reset();
    if (scenario == 1) CHECK_FALSE(tilt.Observe(clock + 1, std::numeric_limits<float>::quiet_NaN(), 0, 0));
    if (scenario == 2) CHECK_FALSE(tilt.Observe(clock + 1, 0, 0, std::numeric_limits<float>::infinity()));
    if (scenario == 3) clock += 500'000'000;
    if (scenario == 4) CHECK_FALSE(tilt.Observe(clock, 0, 0, 9.80665f));
    for (int i = 0; i < 30; ++i) CHECK_FALSE(pose(70));
  }
  BeginCase("motion selection disables input or starts a fresh detector when switching modes");
  CHECK_TRUE(ParseMotionMode("tilt") == MotionMode::kTilt);
  CHECK_TRUE(ParseMotionMode("shake") == MotionMode::kShake);
  CHECK_TRUE(ParseMotionMode("none") == MotionMode::kNone);
  CHECK_TRUE(ParseMotionMode("invalid") == MotionMode::kNone);
  MotionDetector motion;
  now = 1'000'000'000;
  CHECK_FALSE(motion.Observe(MotionMode::kShake, now, 0, 9.80665f, 0));
  CHECK_TRUE(motion.Observe(MotionMode::kShake, now += 50'000'000, 10, 9.80665f, 0));
  CHECK_FALSE(motion.Observe(MotionMode::kNone, now += 50'000'000, -20, 9.80665f, 0));
  CHECK_FALSE(motion.Observe(MotionMode::kTilt, now += 50'000'000, 0, 0, 9.80665f));
  CHECK_FALSE(motion.Observe(MotionMode::kShake, now += 50'000'000, -20, 9.80665f, 0));
  motion.Reset();
  CHECK_FALSE(motion.Observe(MotionMode::kShake, now += 50'000'000, 20, 9.80665f, 0));

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

  BeginCase("queued native list gestures swallow only their own synthesized mouse events");
  gate = {};
  gate.MenuBegin(true);
  gate.MenuBegin(false); // a footer tap follows a list swipe before mouse delivery
  gate.MenuBegin(true);
  CHECK_FALSE(gate.Consume(false, Phase::kDown, false)); // a physical mouse does not drain it
  CHECK_TRUE(gate.Consume(true, Phase::kDown, false));
  CHECK_TRUE(gate.Consume(true, Phase::kMove, false));
  CHECK_TRUE(gate.Consume(true, Phase::kUp, false));
  CHECK_FALSE(gate.Consume(true, Phase::kDown, false));
  CHECK_FALSE(gate.Consume(true, Phase::kUp, false));
  CHECK_TRUE(gate.Consume(true, Phase::kDown, false));
  CHECK_TRUE(gate.Consume(true, Phase::kUp, false));
  CHECK_FALSE(gate.Consume(true, Phase::kDown, false));
  CHECK_FALSE(gate.Consume(true, Phase::kUp, false));
  return Finish();
}
