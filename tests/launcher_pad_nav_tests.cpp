// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pad's own rules (docs/plans/launcher-plan.md A3, D6): the deadzone, the binding table, the
// repeat timing, and what the bar is told to call a pad's buttons. No SDL and no window - which is
// why pad_nav.cpp exists separately from the SDL glue in pad_source.cpp.

#include "check.h"

#include "pad_nav.h"

namespace {

using rb_blitz::launcher::AxisDirection;
using rb_blitz::launcher::AxisDirectionFor;
using rb_blitz::launcher::ButtonNamesFor;
using rb_blitz::launcher::FaceLabel;
using rb_blitz::launcher::FaceLabelText;
using rb_blitz::launcher::kRepeatDelaySeconds;
using rb_blitz::launcher::kRepeatIntervalSeconds;
using rb_blitz::launcher::kStickDeadzone;
using rb_blitz::launcher::NavAction;
using rb_blitz::launcher::PadFamily;
using rb_blitz::launcher::PadNavButton;
using rb_blitz::launcher::PadNavModel;
using rb_blitz::launcher::PadNavState;

constexpr double kFrame = 1.0 / 60.0;

PadNavState Pressed(PadNavButton button) {
  PadNavState state;
  state[button] = true;
  return state;
}

const PadNavState kNothing;

void TestDeadzone() {
  rb_blitz::test::BeginCase("stick deadzone");
  // At rest is not zero, and the band around the centre is what stops the ring crawling: a stick
  // leaning a little is a stick nobody is pushing.
  CHECK_EQ(AxisDirectionFor(0), AxisDirection::kNeutral);
  CHECK_EQ(AxisDirectionFor(kStickDeadzone - 1), AxisDirection::kNeutral);
  CHECK_EQ(AxisDirectionFor(-(kStickDeadzone - 1)), AxisDirection::kNeutral);
  CHECK_EQ(AxisDirectionFor(kStickDeadzone), AxisDirection::kPositive);
  CHECK_EQ(AxisDirectionFor(-kStickDeadzone), AxisDirection::kNegative);
  CHECK_EQ(AxisDirectionFor(32767), AxisDirection::kPositive);
  CHECK_EQ(AxisDirectionFor(-32768), AxisDirection::kNegative);
  // The deadzone is a parameter so a future settings row - C1's - can move it without this
  // function changing.
  CHECK_EQ(AxisDirectionFor(kStickDeadzone, 100000), AxisDirection::kNeutral);
}

void TestBindingTable() {
  rb_blitz::test::BeginCase("the binding table D6 names");
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kUp), 0.0), NavAction::kPrevious);
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kDown), 0.0), NavAction::kNext);
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kConfirm), 0.0), NavAction::kActivate);
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kCancel), 0.0), NavAction::kCancel);
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kTabForward), 0.0), NavAction::kNextTab);
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kTabBack), 0.0), NavAction::kPreviousTab);
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kLaunch), 0.0), NavAction::kLaunch);
  // A direction is one thing whichever input sent it: the D-pad's left and right are the tab
  // strip's, exactly as the keyboard's own arrows are.
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kLeft), 0.0), NavAction::kPreviousTab);
  CHECK_EQ(PadNavModel{}.Update(Pressed(PadNavButton::kRight), 0.0), NavAction::kNextTab);
}

void TestEdgesAreNotStates() {
  rb_blitz::test::BeginCase("a press is an edge, not a state");
  PadNavModel model;
  // Nothing held is nothing asked for: the ring sits still while the user reads.
  for (int frame = 0; frame < 5; ++frame) {
    CHECK_EQ(model.Update(kNothing, frame * kFrame), NavAction::kNone);
  }
  CHECK_EQ(model.Update(Pressed(PadNavButton::kDown), 0.0), NavAction::kNext);
  // Held is not pressed again until the repeat is due.
  CHECK_EQ(model.Update(Pressed(PadNavButton::kDown), kFrame), NavAction::kNone);
  // Letting go is not an action either.
  CHECK_EQ(model.Update(kNothing, 2 * kFrame), NavAction::kNone);
  // And a new press is a new edge, so the next press moves the next row.
  CHECK_EQ(model.Update(Pressed(PadNavButton::kDown), 3 * kFrame), NavAction::kNext);
}

void TestRepeat() {
  rb_blitz::test::BeginCase("a held direction repeats");
  PadNavModel model;
  const PadNavState down = Pressed(PadNavButton::kDown);
  // The press itself, then nothing until the delay is up: the delay is what keeps one press from
  // being read as a run of rows.
  CHECK_EQ(model.Update(down, 0.0), NavAction::kNext);
  CHECK_EQ(model.Update(down, kRepeatDelaySeconds - kFrame), NavAction::kNone);
  CHECK_EQ(model.Update(down, kRepeatDelaySeconds), NavAction::kNext);
  CHECK_EQ(model.Update(down, kRepeatDelaySeconds + kFrame), NavAction::kNone);
  CHECK_EQ(model.Update(down, kRepeatDelaySeconds + kRepeatIntervalSeconds), NavAction::kNext);
  // And then on the interval, so a held D-pad walks the list rather than crawling or running.
  int repeats = 0;
  for (double now = kRepeatDelaySeconds + 2 * kRepeatIntervalSeconds; now < 3.0;
       now += kRepeatIntervalSeconds) {
    if (model.Update(down, now) == NavAction::kNext) {
      ++repeats;
    }
  }
  CHECK_TRUE(repeats > 5);
}

void TestOnlyDirectionsRepeat() {
  rb_blitz::test::BeginCase("only a direction repeats");
  // *Install Ultimate* and *Launch Game* are both things a repeat would do twice, and a confirm
  // held down must not press the row under the ring again and again.
  for (const PadNavButton button :
       {PadNavButton::kConfirm, PadNavButton::kCancel, PadNavButton::kLaunch,
        PadNavButton::kTabForward, PadNavButton::kTabBack}) {
    PadNavModel model;
    const PadNavState held = Pressed(button);
    (void)model.Update(held, 0.0);
    int actions = 0;
    double now = kFrame;
    for (int frame = 0; frame < 120; ++frame, now += kFrame) {
      if (model.Update(held, now) != NavAction::kNone) {
        ++actions;
      }
    }
    CHECK_EQ(actions, 0);
  }
}

void TestTwoPressesAtOnce() {
  rb_blitz::test::BeginCase("two buttons at once");
  PadNavModel model;
  PadNavState both;
  both[PadNavButton::kDown] = true;
  both[PadNavButton::kConfirm] = true;
  // One action a frame, and the table's order decides which of two presses goes first.
  CHECK_EQ(model.Update(both, 0.0), NavAction::kNext);
  // The other press was not lost: it is served on the next frame instead of being dropped, which
  // is what lets a confirm aimed at a row the repeat is running over still land.
  CHECK_EQ(model.Update(both, kFrame), NavAction::kActivate);
  CHECK_EQ(model.Update(both, 2 * kFrame), NavAction::kNone);
}

void TestPadArrivesAndLeaves() {
  rb_blitz::test::BeginCase("a pad that arrives and leaves mid-press");
  PadNavModel model;
  // A direction already down when the pad arrives has no edge: a stick resting against its stop
  // must not move the ring the moment the pad is plugged in.
  CHECK_EQ(model.Update(Pressed(PadNavButton::kDown), 0.0), NavAction::kNext);
  CHECK_EQ(model.Update(kNothing, 1.0), NavAction::kNone);
  // The pad goes while the button is held. What arrives then is a state with nothing in it - there
  // is no release coming from a pad that is not there - and that has to be enough to forget the
  // press rather than leave the ring waiting for it.
  CHECK_EQ(model.Update(kNothing, 2.0), NavAction::kNone);
  // The same pad comes back and is pressed again: that is a new press, so it is a new action.
  CHECK_EQ(model.Update(Pressed(PadNavButton::kDown), 3.0), NavAction::kNext);
}

void TestStateAny() {
  rb_blitz::test::BeginCase("a state knows whether anything is down");
  // pad_source reads this to tell the shell the pad is the device that last moved, including on
  // the frames where the action it produced was swallowed by a modal.
  PadNavState state;
  CHECK_FALSE(state.any());
  state[PadNavButton::kLaunch] = true;
  CHECK_TRUE(state.any());
  state[PadNavButton::kLaunch] = false;
  CHECK_FALSE(state.any());
}

void TestButtonNames() {
  rb_blitz::test::BeginCase("what the bar calls a pad's buttons");
  // A pad whose database entry says nothing gets the 360's letters, which are also the words this
  // project's own remap targets are spelled with.
  const auto standard =
      ButtonNamesFor(PadFamily::kUnknown, FaceLabel::kUnknown, FaceLabel::kUnknown);
  CHECK_TRUE(standard.confirm == "A");
  CHECK_TRUE(standard.cancel == "B");
  CHECK_TRUE(standard.shoulder_left == "LB");
  CHECK_TRUE(standard.shoulder_right == "RB");
  CHECK_TRUE(standard.start == "Start");
  // A PlayStation pad is not told to press A: the face labels come from SDL's own database and the
  // shoulders and Start from the family's names.
  const auto sony = ButtonNamesFor(PadFamily::kPlayStation, FaceLabel::kCross, FaceLabel::kCircle);
  CHECK_TRUE(sony.confirm == "Cross");
  CHECK_TRUE(sony.cancel == "Circle");
  CHECK_TRUE(sony.shoulder_left == "L1");
  CHECK_TRUE(sony.shoulder_right == "R1");
  CHECK_TRUE(sony.start == "Options");
  // A half-known pad falls back per button rather than per pad.
  const auto mixed = ButtonNamesFor(PadFamily::kXbox, FaceLabel::kUnknown, FaceLabel::kB);
  CHECK_TRUE(mixed.confirm == "A");
  CHECK_TRUE(mixed.cancel == "B");
  CHECK_TRUE(FaceLabelText(FaceLabel::kTriangle) == "Triangle");
  CHECK_TRUE(FaceLabelText(FaceLabel::kUnknown) == "A");
}

}  // namespace

// The other half of this binary: A2's help, in tests/launcher_help_tests.cpp.
void RunLauncherHelpChecks();

int main() {
  TestDeadzone();
  TestBindingTable();
  TestEdgesAreNotStates();
  TestRepeat();
  TestOnlyDirectionsRepeat();
  TestTwoPressesAtOnce();
  TestPadArrivesAndLeaves();
  TestStateAny();
  TestButtonNames();
  RunLauncherHelpChecks();
  return rb_blitz::test::Finish();
}
