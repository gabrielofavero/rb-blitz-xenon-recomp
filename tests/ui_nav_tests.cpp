// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the mouse -> left-stick translation in src/input/ui_nav.h, which
// src/input/mouse_ui.cpp feeds from the window's mouse events. No SDK, no game
// image, no boot: the question is pure arithmetic over pixel counters and poll
// counts.
//
// What is pinned here is the shape of what the guest receives, because the guest
// is the thing that cannot be asked to be tolerant. Its menus move one row per
// left-stick *press* and only on the edge, so the two ways this can go wrong are
// both about edges: a deflection that is too short is a row that never moves, and
// travel that keeps producing presses after the hand has stopped is a selection
// that runs away from the user.
//
// A row of travel is a fact about the guest's layout, and the cases below hold the
// translation to it: pointing at where a row's neighbour is has to move exactly
// one row, a swing across several rows has to arrive as that many presses rather
// than being cut short, sub-row motion has to be carried instead of thrown away
// (and must not be invented either), and the caps that keep a flick or a wheel spin
// from banking more rows than the guest can act on have to drop the excess rather
// than save it up.
//
// The pointer is placed absolutely in these cases, as the driver reports it, and
// the first placement is deliberately a starting point only: the offset between
// the pointer and the guest's highlight is not something the host can see, so a
// position can only ever mean "the row I was at plus however far I moved".
//
// The pulse is counted in polls rather than in milliseconds here: turning the
// cvars' milliseconds into those polls needs the guest's poll cadence, which only
// src/input/mouse_ui.cpp can measure. What matters to the stepper is that it is
// told how long a press must last and obeys it.
//
// The click side is pinned the same way, because it is the other half of the same
// ordering problem: a click has to reach the guest after the rows the pointer
// queued before it and not before, must survive being released while it waited,
// and must still be a press the guest's frame can see rather than a flicker it
// misses. What a click must never be is a press that never comes up, since that is
// a menu item stuck down.

#include "check.h"

#include "input/ui_nav.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace rb_blitz::input;
using namespace rb_blitz::test;

struct Emitted {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed() const { return x != 0 || y != 0; }
};

std::vector<Emitted> Poll(UiNavStepper& nav, int count) {
  std::vector<Emitted> out;
  out.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    Emitted e;
    nav.Poll(&e.x, &e.y);
    out.push_back(e);
  }
  return out;
}

// Maximal runs of identical polls, so a case can talk about "the press" and "the
// gap" rather than poll indices.
struct Run {
  int polls = 0;
  bool pressed = false;
  int16_t x = 0;
  int16_t y = 0;
};

std::vector<Run> Runs(const std::vector<Emitted>& polls) {
  std::vector<Run> runs;
  for (const Emitted& e : polls) {
    if (!runs.empty() && runs.back().pressed == e.pressed() && runs.back().x == e.x &&
        runs.back().y == e.y) {
      ++runs.back().polls;
      continue;
    }
    Run run;
    run.polls = 1;
    run.pressed = e.pressed();
    run.x = e.x;
    run.y = e.y;
    runs.push_back(run);
  }
  return runs;
}

int PressCount(const std::vector<Emitted>& polls) {
  int presses = 0;
  bool was_pressed = false;
  for (const Emitted& e : polls) {
    if (e.pressed() && !was_pressed) {
      ++presses;
    }
    was_pressed = e.pressed();
  }
  return presses;
}

bool AllCentred(const std::vector<Emitted>& polls) {
  for (const Emitted& e : polls) {
    if (e.pressed()) {
      return false;
    }
  }
  return true;
}

// Where each press went, in order, so a case can say "down then right" without
// counting polls.
std::vector<Emitted> Presses(UiNavStepper& nav, int polls) {
  std::vector<Emitted> out;
  bool was_pressed = false;
  for (int i = 0; i < polls; ++i) {
    Emitted e;
    nav.Poll(&e.x, &e.y);
    if (e.pressed() && !was_pressed) {
      out.push_back(e);
    }
    was_pressed = e.pressed();
  }
  return out;
}

// A row of travel at the default pitch, so the cases read in rows.
const double kRow = UiNavStepper::kDefaultRowPixels;

// The click side reports one bit per button, in the pulser's own numbering.
const int kLeft = 0;
const int kRight = 1;

std::vector<uint16_t> PollClicks(UiClickPulser& clicks, int count, bool travel_pending) {
  std::vector<uint16_t> out;
  out.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    out.push_back(clicks.Poll(travel_pending));
  }
  return out;
}

// Maximal runs of pressed polls: one run is one press, and two runs are two.
int PressRuns(const std::vector<uint16_t>& polls) {
  int runs = 0;
  bool was_pressed = false;
  for (uint16_t buttons : polls) {
    if (buttons != 0 && !was_pressed) {
      ++runs;
    }
    was_pressed = buttons != 0;
  }
  return runs;
}

bool NoButtons(const std::vector<uint16_t>& polls) {
  for (uint16_t buttons : polls) {
    if (buttons != 0) {
      return false;
    }
  }
  return true;
}

// Polls a stepper until it owes the guest nothing, or until the limit is reached,
// and returns how many polls that took.
int Drain(UiNavStepper& nav, int limit) {
  int polls = 0;
  while (nav.HasPendingRows() && polls < limit) {
    int16_t x = 0;
    int16_t y = 0;
    nav.Poll(&x, &y);
    ++polls;
  }
  return polls;
}

// The pulse a stepper uses until it is told otherwise. Cases that are about the
// waveform rather than about the length of a press read it from a default-built
// stepper instead of naming a number, so that the default stays the default.
const UiNavStepper kDefaultPulse;
const int kDefaultPressPolls = kDefaultPulse.press_polls();
const int kDefaultReleasePolls = kDefaultPulse.release_polls();
// One press and its gap: polling a whole number of these keeps a case that counts
// runs from ending mid-gap, where the tail of the gap is indistinguishable from
// the idle polls that follow it.
const int kStepPolls = kDefaultPressPolls + kDefaultReleasePolls;

}  // namespace

int main() {
  BeginCase("the first position only establishes a starting point");
  {
    UiNavStepper nav;
    // Where the pointer is when the driver starts looking says nothing about
    // where the guest's highlight is, so treating it as a position to travel from
    // would move the selection by the whole distance to a guess.
    nav.OnPointer(400.0, 300.0);
    CHECK_TRUE(AllCentred(Poll(nav, 20)));

    // The next position is measured from it, and one row is one press.
    nav.OnPointer(400.0, 300.0 + kRow);
    const std::vector<Run> runs = Runs(Poll(nav, 5));
    CHECK_EQ(runs.size(), 2);
    CHECK_EQ(runs[0].polls, kDefaultPressPolls);
    CHECK_EQ(runs[1].polls, kDefaultReleasePolls);
    // Screen down is stick down: the list moves the way the pointer moves.
    CHECK_EQ(runs[0].y, -UiNavStepper::kStepDeflection);
    CHECK_EQ(runs[0].x, 0);
  }

  BeginCase("sub-row motion is carried, not thrown away or invented");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(0.0, 0.4 * kRow);
    // Four tenths of a row is under half a row, and half a row is where rounding
    // starts.
    CHECK_TRUE(AllCentred(Poll(nav, 30)));

    // Two tenths more crosses it: the nearest row is the one the user meant.
    nav.OnPointer(0.0, 0.6 * kRow);
    CHECK_EQ(PressCount(Poll(nav, 20)), 1);

    // The four tenths of a row that step rounded past are carried with it, so the
    // next row arrives after nine tenths of a row of travel rather than a whole
    // one.
    nav.OnPointer(0.0, 1.5 * kRow);
    CHECK_EQ(PressCount(Poll(nav, 20)), 1);

    // And it is spent rather than banked: the hand has stopped, so the selection
    // has too.
    CHECK_TRUE(AllCentred(Poll(nav, 40)));
  }

  BeginCase("a wiggle that nets to nothing does nothing");
  {
    UiNavStepper nav;
    nav.OnPointer(100.0, 100.0);
    for (int i = 0; i < 20; ++i) {
      nav.OnPointer(100.0, 106.0);
      nav.OnPointer(100.0, 100.0);
    }
    CHECK_TRUE(AllCentred(Poll(nav, 60)));
  }

  BeginCase("many small steps do not drift");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    for (int i = 1; i <= 10; ++i) {
      nav.OnPointer(0.0, kRow * static_cast<double>(i) / 10.0);
    }
    // A tenth of a row at a time is exactly one row, not two and not none: each
    // step rounds, so what keeps this right is the remainder being carried.
    CHECK_EQ(PressCount(Poll(nav, 20)), 1);
    CHECK_TRUE(AllCentred(Poll(nav, 40)));
  }

  BeginCase("both directions on both axes follow the pointer");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);

    nav.OnPointer(0.0, kRow);
    CHECK_EQ(Presses(nav, 20)[0].y, -UiNavStepper::kStepDeflection);
    nav.OnPointer(0.0, 0.0);
    CHECK_EQ(Presses(nav, 20)[0].y, UiNavStepper::kStepDeflection);

    nav.OnPointer(kRow, 0.0);
    CHECK_EQ(Presses(nav, 20)[0].x, UiNavStepper::kStepDeflection);
    nav.OnPointer(0.0, 0.0);
    CHECK_EQ(Presses(nav, 20)[0].x, -UiNavStepper::kStepDeflection);
  }

  BeginCase("a seven-row swing arrives as seven rows");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(0.0, 7.0 * kRow);
    CHECK_EQ(PressCount(Poll(nav, 60)), 7);
    CHECK_TRUE(AllCentred(Poll(nav, 40)));
  }

  BeginCase("a diagonal drag alternates between the axes");
  {
    // An equal drag on both axes: the vertical wins the first step, and the
    // horizontal then wins the second rather than the vertical winning both.
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(kRow, kRow);
    const std::vector<Run> runs = Runs(Poll(nav, 10));
    // Exactly two presses - down, then right - each with its own gap, and then
    // nothing: the drag queued no rows it did not ask for.
    CHECK_EQ(static_cast<int>(runs.size()), 4);
    CHECK_EQ(runs[0].y, -UiNavStepper::kStepDeflection);
    CHECK_EQ(runs[0].x, 0);
    CHECK_EQ(runs[1].polls, kDefaultReleasePolls);
    CHECK_EQ(runs[2].x, UiNavStepper::kStepDeflection);
    CHECK_EQ(runs[2].y, 0);
    CHECK_TRUE(AllCentred(Poll(nav, 40)));
  }

  BeginCase("the axis owed more rows goes first");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(2.0 * kRow, kRow);
    const std::vector<Emitted> presses = Presses(nav, 20);
    CHECK_EQ(static_cast<int>(presses.size()), 3);
    CHECK_EQ(presses[0].x, UiNavStepper::kStepDeflection);
    CHECK_EQ(presses[1].y, -UiNavStepper::kStepDeflection);
    CHECK_EQ(presses[2].x, UiNavStepper::kStepDeflection);
  }

  BeginCase("a flick cannot queue more than kMaxQueuedRows");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    // 5000 px is 104 rows, and the queue holds kMaxQueuedRows of them: more than a
    // screenful, so a pointer thrown across the window still lands as the single
    // jump it was meant to be.
    nav.OnPointer(0.0, 5000.0);
    const std::vector<Run> runs =
        Runs(Poll(nav, UiNavStepper::kMaxQueuedRows * kStepPolls));
    CHECK_EQ(static_cast<int>(runs.size()), 2 * UiNavStepper::kMaxQueuedRows);
    for (size_t i = 0; i < runs.size(); i += 2) {
      CHECK_EQ(runs[i].polls, kDefaultPressPolls);
      CHECK_EQ(runs[i].y, -UiNavStepper::kStepDeflection);
      CHECK_EQ(runs[i + 1].polls, kDefaultReleasePolls);
    }
    // The hand has stopped, so the selection has too, and the rows past the cap
    // were dropped rather than saved up: one more row is one more press.
    CHECK_TRUE(AllCentred(Poll(nav, 40)));
    nav.OnPointer(0.0, 5000.0 + kRow);
    CHECK_EQ(PressCount(Poll(nav, 20)), 1);
  }

  BeginCase("a diagonal flick is capped on both axes");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(5000.0, -5000.0);
    const std::vector<Emitted> polls = Poll(nav, 300);
    // Each axis may queue a full cap, so the bound is the sum - what must not
    // happen is travel that keeps producing presses after the queue is drained.
    CHECK_TRUE(PressCount(polls) <= 2 * UiNavStepper::kMaxQueuedRows);
    CHECK_TRUE(PressCount(polls) > 0);
    CHECK_TRUE(AllCentred(Poll(nav, 50)));
  }

  BeginCase("a wheel detent steps immediately, ahead of queued travel");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    nav.OnWheel(0, UiNavStepper::kScrollUnitsPerDetent);
    nav.OnPointer(0.0, 2.0 * kRow);
    const std::vector<Run> runs = Runs(Poll(nav, 30));
    // The detent is a deliberate gesture; the cursor motion queued behind it
    // waits. Its press is a full one, not a shortened one that the release
    // swallows.
    CHECK_EQ(runs[0].y, UiNavStepper::kStepDeflection);
    CHECK_EQ(runs[0].polls, kDefaultPressPolls);
    CHECK_EQ(runs[1].polls, kDefaultReleasePolls);
    // Then the two rows of motion, and a tail of silence: 6 runs is three presses
    // and the gaps between them, with the last gap running to the end of the
    // window.
    CHECK_EQ(static_cast<int>(runs.size()), 6);
    CHECK_EQ(runs[2].y, -UiNavStepper::kStepDeflection);
    CHECK_TRUE(AllCentred(Poll(nav, 40)));
  }

  BeginCase("sub-detent wheel deltas accumulate instead of being dropped");
  {
    UiNavStepper nav;
    nav.OnWheel(0, UiNavStepper::kScrollUnitsPerDetent / 2);
    CHECK_TRUE(AllCentred(Poll(nav, 10)));
    nav.OnWheel(0, UiNavStepper::kScrollUnitsPerDetent / 2);
    CHECK_EQ(PressCount(Poll(nav, 10)), 1);
    CHECK_TRUE(AllCentred(Poll(nav, 10)));
  }

  BeginCase("wheel direction and axis map to the stick");
  {
    UiNavStepper up;
    up.OnWheel(0, 2 * UiNavStepper::kScrollUnitsPerDetent);
    const std::vector<Run> up_runs = Runs(Poll(up, 10));
    CHECK_EQ(up_runs[0].y, UiNavStepper::kStepDeflection);
    CHECK_EQ(up_runs.size(), 4);

    UiNavStepper down;
    down.OnWheel(0, -2 * UiNavStepper::kScrollUnitsPerDetent);
    CHECK_EQ(Runs(Poll(down, 1))[0].y, -UiNavStepper::kStepDeflection);

    UiNavStepper across;
    across.OnWheel(UiNavStepper::kScrollUnitsPerDetent, 0);
    CHECK_EQ(Runs(Poll(across, 1))[0].x, UiNavStepper::kStepDeflection);
  }

  BeginCase("a wheel spin cannot queue more than kMaxWheelRows");
  {
    UiNavStepper nav;
    nav.OnWheel(0, UiNavStepper::kScrollUnitsPerDetent * 50);
    CHECK_EQ(PressCount(Poll(nav, 200)), UiNavStepper::kMaxWheelRows);
    CHECK_TRUE(AllCentred(Poll(nav, 50)));
  }

  BeginCase("Reset drops the pointer, queued rows and a press in flight");
  {
    UiNavStepper queued;
    queued.OnPointer(0.0, 0.0);
    queued.OnPointer(0.0, 200.0);
    queued.Reset();
    CHECK_TRUE(AllCentred(Poll(queued, 40)));

    UiNavStepper in_flight;
    in_flight.OnPointer(0.0, 0.0);
    in_flight.OnPointer(0.0, UiNavStepper::kDefaultRowPixels);
    CHECK_EQ(PressCount(Poll(in_flight, 1)), 1);
    in_flight.Reset();
    CHECK_TRUE(AllCentred(Poll(in_flight, 10)));

    UiNavStepper wheel;
    wheel.OnWheel(0, UiNavStepper::kScrollUnitsPerDetent);
    wheel.Reset();
    CHECK_TRUE(AllCentred(Poll(wheel, 10)));

    // And the stepper still works afterwards.
    UiNavStepper reused;
    reused.Reset();
    reused.OnPointer(0.0, 0.0);
    reused.OnPointer(0.0, UiNavStepper::kDefaultRowPixels);
    CHECK_EQ(PressCount(Poll(reused, 20)), 1);
  }

  BeginCase("ForgetPointer re-anchors but keeps what is already queued");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(0.0, 3.0 * kRow);
    // Whatever the pointer does next, the rows the user already asked for are
    // motion rather than a guess, so they still arrive.
    nav.ForgetPointer();
    CHECK_EQ(PressCount(Poll(nav, 40)), 3);

    // And the next position is only a starting point: it is where the pointer
    // landed after the screen changed, not travel from the old one.
    nav.OnPointer(900.0, 900.0);
    CHECK_TRUE(AllCentred(Poll(nav, 20)));
    nav.OnPointer(900.0, 900.0 + kRow);
    CHECK_EQ(PressCount(Poll(nav, 20)), 1);
  }

  BeginCase("the row is clamped, and a non-number is refused");
  {
    UiNavStepper nav;
    CHECK_TRUE(nav.row_pixels() == UiNavStepper::kDefaultRowPixels);

    nav.SetRowPixels(0.0);
    CHECK_TRUE(nav.row_pixels() == UiNavStepper::kMinRowPixels);
    nav.SetRowPixels(1.0e9);
    CHECK_TRUE(nav.row_pixels() == UiNavStepper::kMaxRowPixels);

    // A row that is not a number would divide every distance into one that is not
    // a number either, so the previous value has to survive.
    nav.SetRowPixels(std::nan(""));
    CHECK_TRUE(nav.row_pixels() == UiNavStepper::kMaxRowPixels);
    nav.SetRowPixels(std::nan(""));
    CHECK_TRUE(nav.row_pixels() == UiNavStepper::kMaxRowPixels);

    nav.SetRowPixels(20.0);
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(0.0, 20.0);
    CHECK_EQ(PressCount(Poll(nav, 20)), 1);
  }

  BeginCase("a position that is not a number is not a position");
  {
    UiNavStepper nav;
    nav.OnPointer(0.0, 0.0);
    // It is not a move to zero either: the anchor has to survive it.
    nav.OnPointer(std::nan(""), std::nan(""));
    nav.OnPointer(0.0, kRow);
    CHECK_EQ(PressCount(Poll(nav, 20)), 1);
  }

  BeginCase("a longer row needs more travel for the same step");
  {
    UiNavStepper nav;
    nav.SetRowPixels(100.0);
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(0.0, 49.0);
    CHECK_TRUE(AllCentred(Poll(nav, 10)));
    nav.OnPointer(0.0, 50.0);
    CHECK_EQ(PressCount(Poll(nav, 20)), 1);
  }

  BeginCase("motion reported as deltas feeds the same arithmetic");
  {
    // SetRelativeMouseMode freezes x/y and reports only deltas, and the driver
    // passes those here instead.
    UiNavStepper nav;
    nav.OnPointerMotion(0.0, 0.0);
    CHECK_TRUE(AllCentred(Poll(nav, 20)));
    nav.OnPointerMotion(0.0, kRow);
    CHECK_EQ(Presses(nav, 20)[0].y, -UiNavStepper::kStepDeflection);

    // A delta that is not a number is not a distance.
    nav.OnPointerMotion(std::nan(""), 5.0);
    CHECK_TRUE(AllCentred(Poll(nav, 20)));
    nav.OnPointerMotion(-kRow, 0.0);
    CHECK_EQ(Presses(nav, 20)[0].x, -UiNavStepper::kStepDeflection);
  }

  BeginCase("the pulse is settable, and a press is never shorter than a poll");
  {
    UiNavStepper nav;
    CHECK_TRUE(nav.press_polls() == kDefaultPressPolls);
    CHECK_TRUE(nav.release_polls() == kDefaultReleasePolls);

    // Ten polls of deflection and one of release: a press long enough that no
    // frame of the guest's can miss it, and the shortest gap that still ends it.
    nav.SetPulsePolls(10, 1);
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(0.0, UiNavStepper::kDefaultRowPixels);
    // One poll past the press: the gap is exactly as long as it was asked for and
    // not a poll more.
    const std::vector<Run> runs = Runs(Poll(nav, 11));
    CHECK_EQ(runs.size(), 2);
    CHECK_EQ(runs[0].polls, 10);
    CHECK_EQ(runs[0].y, -UiNavStepper::kStepDeflection);
    CHECK_EQ(runs[1].polls, 1);

    // A press of no polls would not be a press at all, so it is floored at one.
    nav.SetPulsePolls(0, -5);
    CHECK_EQ(nav.press_polls(), 1);
    CHECK_EQ(nav.release_polls(), 0);

    // With no release at all, two rows arrive as one unbroken deflection, which
    // the guest's edge-detecting menus read as a single press: two rows asked
    // for, one row moved. That is the trap the release exists to avoid.
    UiNavStepper unbroken;
    unbroken.SetPulsePolls(1, 0);
    unbroken.OnPointer(0.0, 0.0);
    unbroken.OnPointer(0.0, 2.0 * UiNavStepper::kDefaultRowPixels);
    const std::vector<Run> fused = Runs(Poll(unbroken, 4));
    CHECK_TRUE(fused[0].pressed);
    CHECK_EQ(fused[0].polls, 2);

    // A pulse longer than any stick should be held for is clamped rather than
    // trusted, whatever it is asked for.
    nav.SetPulsePolls(UiNavStepper::kMaxPulsePolls * 4, UiNavStepper::kMaxPulsePolls * 4);
    CHECK_EQ(nav.press_polls(), UiNavStepper::kMaxPulsePolls);
    CHECK_EQ(nav.release_polls(), UiNavStepper::kMaxPulsePolls);
  }

  BeginCase("HasPendingRows says when the guest has caught up");
  {
    UiNavStepper nav;
    // Nothing has happened yet, and a starting point is not travel.
    CHECK_FALSE(nav.HasPendingRows());
    nav.OnPointer(0.0, 0.0);
    CHECK_FALSE(nav.HasPendingRows());

    // Neither is a move too small to be a row: nothing is owed to the guest until
    // the remainder adds up to one.
    nav.OnPointer(0.0, 0.4 * kRow);
    CHECK_FALSE(nav.HasPendingRows());

    // A row is owed from the moment it is queued until its press and the gap
    // behind it have both been served, because until the release the guest's next
    // frame is still looking at the previous row's press.
    nav.OnPointer(0.0, kRow);
    CHECK_TRUE(nav.HasPendingRows());
    CHECK_TRUE(Drain(nav, 100) < 100);
    CHECK_FALSE(nav.HasPendingRows());

    // A wheel detent counts the same way, and so does an undelivered row from a
    // longer walk.
    nav.OnWheel(0, UiNavStepper::kScrollUnitsPerDetent);
    CHECK_TRUE(nav.HasPendingRows());
    CHECK_TRUE(Drain(nav, 100) < 100);
    CHECK_FALSE(nav.HasPendingRows());

    nav.OnPointer(0.0, kRow * 5.0);
    CHECK_TRUE(nav.HasPendingRows());
    CHECK_TRUE(Drain(nav, 100) < 100);
    CHECK_FALSE(nav.HasPendingRows());
  }

  BeginCase("a click is a press the guest's frame can see, then a gap");
  {
    UiClickPulser clicks;
    CHECK_TRUE(clicks.press_polls() == kDefaultPressPolls);
    CHECK_TRUE(clicks.release_polls() == kDefaultReleasePolls);

    clicks.OnButtonDown(kLeft);
    clicks.OnButtonUp(kLeft);
    const std::vector<uint16_t> polls = PollClicks(clicks, kStepPolls + 2, false);
    CHECK_EQ(PressRuns(polls), 1);
    for (int i = 0; i < kDefaultPressPolls; ++i) {
      CHECK_EQ(polls[i], UiClickPulser::MaskFor(kLeft));
    }
    // Up for the whole gap and afterwards: a press the guest's frame can see, and
    // a button that comes back up on its own.
    for (size_t i = kDefaultPressPolls; i < polls.size(); ++i) {
      CHECK_EQ(polls[i], 0);
    }
  }

  BeginCase("a click waits for the rows the pointer queued before it");
  {
    UiNavStepper nav;
    UiClickPulser clicks;
    nav.OnPointer(0.0, 0.0);
    nav.OnPointer(0.0, 3.0 * kRow);
    // The hand clicks the moment it stops: the click is the last thing the user
    // did, so it has to be the last thing the guest sees.
    clicks.OnButtonDown(kLeft);
    clicks.OnButtonUp(kLeft);

    int polls = 0;
    bool click_seen = false;
    while (nav.HasPendingRows() && polls < 100) {
      int16_t x = 0;
      int16_t y = 0;
      nav.Poll(&x, &y);
      const bool travel_pending = nav.HasPendingRows();
      const uint16_t buttons = clicks.Poll(travel_pending);
      if (travel_pending) {
        CHECK_EQ(buttons, 0);
      } else {
        // The row the hand stopped on is the row the click lands on.
        CHECK_EQ(buttons, UiClickPulser::MaskFor(kLeft));
        click_seen = true;
      }
      ++polls;
    }
    CHECK_TRUE(polls < 100);
    // Not lost on the way, either: it went through the moment the walk ended, and
    // it is a whole press rather than the remains of one, so the guest still has
    // the rest of the pulse to see it in.
    CHECK_TRUE(click_seen);
    CHECK_EQ(clicks.Poll(false), UiClickPulser::MaskFor(kLeft));
    CHECK_EQ(clicks.Poll(false), UiClickPulser::MaskFor(kLeft));
    CHECK_TRUE(NoButtons(PollClicks(clicks, kStepPolls + 2, false)));
  }

  BeginCase("a button held through travel stays down until the hand comes up");
  {
    UiClickPulser clicks;
    clicks.OnButtonDown(kLeft);
    // Held for the whole walk: nothing goes out while the click is owed behind it.
    CHECK_TRUE(NoButtons(PollClicks(clicks, 20, true)));

    // Once the walk is over the press starts, and a hand that is still down keeps
    // the guest's button down with it rather than firing the minimum press and
    // coming back up under a finger that never moved.
    for (uint16_t buttons : PollClicks(clicks, 20, false)) {
      CHECK_EQ(buttons, UiClickPulser::MaskFor(kLeft));
    }

    // Released: the press ends, and the gap that makes the next click a second
    // click begins.
    clicks.OnButtonUp(kLeft);
    CHECK_TRUE(NoButtons(PollClicks(clicks, 1 + kDefaultReleasePolls, false)));
  }

  BeginCase("two clicks apart are two presses, and a double click is one");
  {
    UiClickPulser clicks;
    clicks.OnButtonDown(kLeft);
    clicks.OnButtonUp(kLeft);
    const std::vector<uint16_t> first = PollClicks(clicks, kStepPolls, false);
    CHECK_EQ(PressRuns(first), 1);
    CHECK_EQ(first[0], UiClickPulser::MaskFor(kLeft));
    // Drained, gap and all: nothing is left over for the next click to hide in.
    CHECK_EQ(first.back(), 0);

    clicks.OnButtonDown(kLeft);
    clicks.OnButtonUp(kLeft);
    const std::vector<uint16_t> second = PollClicks(clicks, kStepPolls, false);
    CHECK_EQ(PressRuns(second), 1);
    CHECK_EQ(second[0], UiClickPulser::MaskFor(kLeft));

    // Closer together than one press and its gap is closer than the guest can tell
    // apart, so it is one click and not two - which is also what stops a
    // double-click from activating an item and then activating the next screen.
    UiClickPulser double_click;
    double_click.OnButtonDown(kLeft);
    double_click.OnButtonUp(kLeft);
    double_click.OnButtonDown(kLeft);
    double_click.OnButtonUp(kLeft);
    CHECK_EQ(PressRuns(PollClicks(double_click, kStepPolls + 4, false)), 1);
  }

  BeginCase("the two buttons are independent");
  {
    UiClickPulser clicks;
    clicks.SetPulsePolls(2, 2);
    clicks.OnButtonDown(kRight);
    clicks.OnButtonUp(kRight);
    clicks.OnButtonDown(kLeft);
    // Both wait for the walk, and neither becomes the other.
    CHECK_TRUE(NoButtons(PollClicks(clicks, 3, true)));

    const std::vector<uint16_t> polls = PollClicks(clicks, 3, false);
    CHECK_EQ(polls[0], UiClickPulser::MaskFor(kLeft) | UiClickPulser::MaskFor(kRight));
    CHECK_EQ(polls[1], UiClickPulser::MaskFor(kLeft) | UiClickPulser::MaskFor(kRight));
    // The right button's click is over and its gap has started; the left one is
    // still held, so the guest still sees it.
    CHECK_EQ(polls[2], UiClickPulser::MaskFor(kLeft));
    clicks.OnButtonUp(kLeft);
    CHECK_EQ(clicks.Poll(false), 0);  // the left press ends here
    CHECK_EQ(clicks.Poll(false), 0);  // its gap
    CHECK_EQ(clicks.Poll(false), 0);  // and nothing is owed
  }

  BeginCase("a click's press and gap are bounded, and a zero gap is floored");
  {
    UiClickPulser clicks;
    clicks.SetPulsePolls(0, -5);
    CHECK_EQ(clicks.press_polls(), 1);
    // Unlike a step, a click's gap is never zero: two clicks a hand apart are two
    // presses, and no gap would report them as one unbroken paste of the button.
    CHECK_EQ(clicks.release_polls(), 1);
    clicks.SetPulsePolls(UiNavStepper::kMaxPulsePolls * 4, UiNavStepper::kMaxPulsePolls * 4);
    CHECK_EQ(clicks.press_polls(), UiNavStepper::kMaxPulsePolls);
    CHECK_EQ(clicks.release_polls(), UiNavStepper::kMaxPulsePolls);

    // One poll of press is one poll of press, and the gap behind it is one poll.
    UiClickPulser one;
    one.SetPulsePolls(1, 1);
    one.OnButtonDown(kLeft);
    one.OnButtonUp(kLeft);
    const std::vector<uint16_t> polls = PollClicks(one, 3, false);
    CHECK_EQ(polls[0], UiClickPulser::MaskFor(kLeft));
    CHECK_EQ(polls[1], 0);
    CHECK_EQ(polls[2], 0);
  }

  BeginCase("a button index the driver cannot produce is ignored");
  {
    UiClickPulser clicks;
    clicks.OnButtonDown(-1);
    clicks.OnButtonDown(UiClickPulser::kButtonCount);
    clicks.OnButtonUp(UiClickPulser::kButtonCount + 3);
    CHECK_TRUE(NoButtons(PollClicks(clicks, 20, false)));
  }

  BeginCase("Reset drops a click, a press and its gap");
  {
    UiClickPulser owed;
    owed.OnButtonDown(kLeft);
    owed.Reset();
    CHECK_TRUE(NoButtons(PollClicks(owed, 20, false)));

    UiClickPulser pressing;
    pressing.OnButtonDown(kRight);
    CHECK_EQ(pressing.Poll(false), UiClickPulser::MaskFor(kRight));
    pressing.Reset();
    CHECK_TRUE(NoButtons(PollClicks(pressing, 20, false)));

    // And a reused pulser does not carry an owed gap into its next click, which
    // would be a click the guest never sees.
    UiClickPulser reused;
    reused.Reset();
    reused.OnButtonDown(kLeft);
    reused.OnButtonUp(kLeft);
    const std::vector<uint16_t> polls = PollClicks(reused, kStepPolls + 2, false);
    CHECK_EQ(polls[0], UiClickPulser::MaskFor(kLeft));
    CHECK_EQ(PressRuns(polls), 1);
  }

  return Finish();
}
