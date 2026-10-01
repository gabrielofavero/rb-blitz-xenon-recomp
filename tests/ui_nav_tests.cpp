// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the mouse -> left-stick translation in src/input/ui_nav.h and the
// guest-frame measurement it is aimed with in src/input/nav_detect.h, which
// src/input/mouse_ui.cpp feeds from the window's mouse events and the guest's own
// frames. No SDK, no game image, no boot: the question is pure arithmetic over
// pixel counters, poll counts and frame differences.
//
// What is pinned here is the shape of what the guest receives, because the guest
// is the thing that cannot be asked to be tolerant. Its menus move one row per
// left-stick *press* and only on the edge, so the ways this can go wrong are all
// about edges: a deflection that is too short is a row that never moves, a press
// that outlives the frame that saw it is a row the guest reads twice, and travel
// that keeps producing presses after the hand has stopped is a selection that runs
// away from the user.
//
// Two halves are held to that. The travel stepper is the fallback for when there
// are no frames to measure - mouse look, no presenter, a window with no guest
// output yet - and a row of travel is a fact about the guest's layout there, so a
// swing across several rows has to arrive as that many presses rather than being
// cut short, sub-row motion has to be carried instead of thrown away (and must not
// be invented either), and the caps that keep a flick from banking more rows than
// the guest can act on have to drop the excess rather than save it up.
//
// The other half is the aligner, which is what makes the pointer's row the
// selected row. Its inputs are a frame the guest drew and the pitch between two of
// its rows, and both are invented here rather than measured: a synthetic frame with
// a bar on it is stepped one pitch at a time, so what is being pinned is that the
// detector reads the bar's old and new positions out of a difference, that the
// aligner presses toward the row the pointer is on and stops when it is there, that
// a press is exactly as long as the frame that saw it, and that a list which has
// stopped moving is a burst that stops rather than one that keeps pressing.
//
// The pulse is counted in polls rather than in milliseconds for the stepper, and in
// frames for the aligner: turning the cvars' milliseconds into those polls needs the
// guest's poll cadence, and turning a press into frames needs the guest's frame
// rate, neither of which a test can measure. What matters to both is that they are
// told how long a press must last and that they obey it.

#include "check.h"

#include "input/nav_detect.h"
#include "input/ui_nav.h"

#include <cmath>
#include <cstdint>
#include <string>
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

// A guest that draws one bar on a plain background and moves it a row per press,
// which is what the menus do: the bar is the highlight, its pitch is the row
// spacing, and the frame it draws is what the detector has to read the move out of.
// A real screen cannot be used here (the tests may not boot the game), so this is
// the guest's whole behaviour reduced to what the aligner depends on.
// The frame a fake screen draws is small enough for a test to build hundreds of
// them, and still large enough to have the shape of one: the fractions the detector
// works in are shares of the frame, and the bar below has to be inside them.
class FakeScreen {
 public:
  static constexpr int kWidth = 640;
  static constexpr int kHeight = 360;
  static constexpr int kBarLeft = 200;
  static constexpr int kBarWidth = 240;
  // The background is a mid grey and the bar is brighter than it: the two have to
  // differ by far more than the detector's threshold, and the background has to sit
  // far enough from both ends of the range that a noise level added to it can never
  // clamp, because a picture that clamps is a picture that stops changing.
  static constexpr int kBackgroundLevel = 120;
  static constexpr int kBarLevel = 200;

  // `ramp` makes the bar brighter at its top than at its bottom, which is what most
  // beveled selection markers look like and the only way a marker at least as tall as
  // its row shows up as one band: a marker that is the same colour everywhere leaves
  // the part the two positions share unchanged, and the difference splits into the
  // two slivers either side of it. A marker whose colour changes with position
  // changes in the shared part too, and the difference is one band.
  FakeScreen(int pitch, int bar_height, int first_centre, int last_centre, int centre,
             bool ramp = false)
      : pitch_(pitch),
        bar_height_(bar_height),
        first_centre_(first_centre),
        last_centre_(last_centre),
        centre_(centre),
        ramp_(ramp) {}

  int pitch() const { return pitch_; }
  int centre() const { return centre_; }
  int presses_seen() const { return presses_; }
  // The press with this number (the first is 1) moves two rows instead of one, which
  // is what a guest does when it misses a press and the next one moves it twice.
  void SetDoublePress(int index) { double_press_ = index; }

  // What the guest's menu does with a stick: one row per press, on the edge, and
  // nothing at all once the list has ended.
  void Advance(int16_t stick_y) {
    if (stick_y != 0 && last_stick_ == 0) {
      ++presses_;
      const int rows = double_press_ == presses_ ? 2 : 1;
      const int moved = centre_ + (stick_y < 0 ? rows * pitch_ : -rows * pitch_);
      centre_ = std::max(first_centre_, std::min(last_centre_, moved));
    }
    last_stick_ = stick_y;
  }

  // A frame with the bar where it is now. `noise_step` makes the whole picture a
  // different brightness on every frame, which is what a transition - or a menu
  // whose background is a video - looks like to a difference. The caller walks it in
  // steps that are coprime with any short cycle, so that no two frames of one burst
  // are ever the same picture by accident.
  NavSampledFrame Frame(int noise_step = 0) const {
    std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4, 0);
    const int bar_top = centre_ - bar_height_ / 2;
    for (int y = 0; y < kHeight; ++y) {
      uint8_t* row = pixels.data() + static_cast<size_t>(y) * kWidth * 4;
      const bool in_bar_row = y >= centre_ - bar_height_ / 2 && y <= centre_ + bar_height_ / 2;
      const int bar_level = ramp_ ? kBarLevel - (y - bar_top) : kBarLevel;
      for (int x = 0; x < kWidth; ++x) {
        const bool in_bar = in_bar_row && x >= kBarLeft && x < kBarLeft + kBarWidth;
        const int level = (in_bar ? bar_level : kBackgroundLevel) + noise_step;
        uint8_t* pixel = row + static_cast<size_t>(x) * 4;
        pixel[0] = static_cast<uint8_t>(std::clamp(level, 0, 255));
        pixel[1] = static_cast<uint8_t>(std::clamp(level / 2, 0, 255));
        pixel[2] = static_cast<uint8_t>(std::clamp(in_bar ? level / 4 : level, 0, 255));
        pixel[3] = 255;
      }
    }
    NavFrameView view;
    view.pixels = pixels.data();
    view.width = kWidth;
    view.height = kHeight;
    view.stride = static_cast<size_t>(kWidth) * 4;
    NavSampledFrame frame;
    SampleNavFrame(view, &frame);
    return frame;
  }

 private:
  int pitch_ = 56;
  int bar_height_ = 40;
  int first_centre_ = 0;
  int last_centre_ = 0;
  int centre_ = 0;
  bool ramp_ = false;
  int16_t last_stick_ = 0;
  int presses_ = 0;
  int double_press_ = 0;
};

// What one frame of a driven aligner saw, so a case can talk about presses rather
// than about frames.
struct DrivenFrame {
  int16_t stick = 0;
  int centre = 0;
};

// Runs the aligner against the fake screen for as long as it wants frames, one guest
// frame per action, and returns what each frame was drawn with. This is the loop the
// driver runs: ask for the stick, the guest draws a frame with it, hand the frame
// back.
std::vector<DrivenFrame> DriveAlign(MenuHoverAligner* aligner, FakeScreen* screen, double frame_ms,
                                    int limit = 120, int noise_per_frame = 0,
                                    double start_ms = 20000.0) {
  std::vector<DrivenFrame> frames;
  double t = start_ms;
  bool burst_seen = false;
  for (int i = 0; i < limit; ++i) {
    const int16_t stick = aligner->PollStick(t);
    screen->Advance(stick);
    t += frame_ms;
    aligner->FeedFrame(screen->Frame(noise_per_frame != 0 ? ((i * 47) % 121) - 60 : 0), t);
    if (aligner->Busy()) {
      burst_seen = true;
    }
    frames.push_back(DrivenFrame{stick, screen->centre()});
    if (burst_seen && !aligner->Busy()) {
      break;
    }
  }
  return frames;
}

int Presses(const std::vector<DrivenFrame>& frames) {
  int presses = 0;
  int16_t last = 0;
  for (const DrivenFrame& frame : frames) {
    if (frame.stick != 0 && last == 0) {
      ++presses;
    }
    last = frame.stick;
  }
  return presses;
}

// A row pitch, a bar that fits inside one row, and the two ends of the list the
// fake screen's rows are between: 27 px a row is what the main menu measured in a
// 768-px-tall client, and 56 what the AV settings screen did
// (docs/history/bringup-log.md), so both shapes are real.
const int kPitch = 28;
const int kBarHeight = 20;
const int kListTop = 20;
const int kListBottom = 330;

// Poles the stepper until it owes the guest nothing, or until the limit is reached,
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

    // And so does an undelivered row from a longer walk.
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

  BeginCase("a difference of one press is the highlight twice, one pitch apart");
  {
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 120);
    const NavSampledFrame before = screen.Frame();
    screen.Advance(-MenuHoverAligner::kDeflection);
    const NavSampledFrame after = screen.Frame();

    NavDetectConfig config;
    std::vector<NavRowDiff> rows;
    CHECK_TRUE(DiffNavFrames(before, after, config, &rows));
    CHECK_FALSE(LooksLikeScreenChange(after, rows, config));

    NavBand bands[kMaxDetectBands];
    const int count = FindChangedBands(after, rows, config, bands);
    // Two rectangles, not one: where the bar was and where it went.
    CHECK_EQ(count, 2);
    for (int i = 0; i < count; ++i) {
      CHECK_EQ(bands[i].Height(), kBarHeight + 1);
      CHECK_EQ(bands[i].Width(), FakeScreen::kBarWidth);
    }
    CHECK_EQ(bands[1].top - bands[0].top, kPitch);

    const NavStep step = FindNavStep(bands, count, config, 0, -1.0);
    CHECK_TRUE(step.found);
    CHECK_TRUE(step.separated);
    CHECK_EQ(step.pitch, kPitch);
    // Pressing down moves the selection down the screen, so the lower rectangle is
    // where the highlight went - and only the caller's direction can say that.
    CHECK_EQ(static_cast<int>(NewHighlightCentre(step, 1)), 120 + kPitch);
    CHECK_EQ(static_cast<int>(NewHighlightCentre(step, -1)), 120);
  }

  BeginCase("a frame that has not changed is not a step");
  {
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 120);
    const NavSampledFrame frame = screen.Frame();
    NavDetectConfig config;
    std::vector<NavRowDiff> rows;
    CHECK_TRUE(DiffNavFrames(frame, frame, config, &rows));
    CHECK_FALSE(LooksLikeScreenChange(frame, rows, config));
    NavBand bands[kMaxDetectBands];
    CHECK_EQ(FindChangedBands(frame, rows, config, bands), 0);
    CHECK_FALSE(FindNavStep(bands, 0, config, 0, -1.0).found);
  }

  BeginCase("what a band is not");
  {
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 120);
    const NavSampledFrame base = screen.Frame();
    NavDetectConfig config;
    std::vector<NavRowDiff> rows;
    NavBand bands[kMaxDetectBands];

    // A few samples changing in a row is under the share a row has to change by: a
    // stray pixel, or animation that only touches part of a row, is not a marker.
    NavSampledFrame speckle = base;
    for (int y = 40; y < 80; ++y) {
      for (int sx = 0; sx < 6; ++sx) {
        speckle.rgb[(static_cast<size_t>(y) * speckle.sample_width + sx) * 3] = 255;
      }
    }
    CHECK_TRUE(DiffNavFrames(base, speckle, config, &rows));
    CHECK_EQ(FindChangedBands(speckle, rows, config, bands), 0);

    // A whole row changing is too wide to be a selection marker, which is what keeps
    // a scrolling list or a screen transition from being read as one.
    NavSampledFrame wide = base;
    for (int y = 40; y < 80; ++y) {
      for (int sx = 0; sx < wide.sample_width; ++sx) {
        wide.rgb[(static_cast<size_t>(y) * wide.sample_width + sx) * 3] = 255;
      }
    }
    CHECK_TRUE(DiffNavFrames(base, wide, config, &rows));
    CHECK_EQ(FindChangedBands(wide, rows, config, bands), 0);

    // A single changed row is too thin to be one either.
    NavSampledFrame line = base;
    for (int sx = 40; sx < 120; ++sx) {
      line.rgb[(static_cast<size_t>(40) * line.sample_width + sx) * 3] = 255;
    }
    CHECK_TRUE(DiffNavFrames(base, line, config, &rows));
    CHECK_EQ(FindChangedBands(line, rows, config, bands), 0);

    // And a screen that is replaced all over is reported as one.
    NavSampledFrame replaced = base;
    for (int y = 0; y < replaced.sample_height; ++y) {
      for (int sx = 0; sx < replaced.sample_width; ++sx) {
        replaced.rgb[(static_cast<size_t>(y) * replaced.sample_width + sx) * 3] = 255;
      }
    }
    CHECK_TRUE(DiffNavFrames(base, replaced, config, &rows));
    CHECK_TRUE(LooksLikeScreenChange(replaced, rows, config));
  }

  BeginCase("a bar as tall as its row is measured the same way");
  {
    // The bar and the pitch the same size: the difference sits between the two
    // positions' own rows, so it still arrives as two rectangles a pitch apart -
    // measured to the row boundary, which is one pixel more than the pitch.
    FakeScreen screen(kPitch, kPitch, kListTop, kListBottom, 120);
    const NavSampledFrame before = screen.Frame();
    screen.Advance(-MenuHoverAligner::kDeflection);
    const NavSampledFrame after = screen.Frame();

    NavDetectConfig config;
    std::vector<NavRowDiff> rows;
    CHECK_TRUE(DiffNavFrames(before, after, config, &rows));
    NavBand bands[kMaxDetectBands];
    const int count = FindChangedBands(after, rows, config, bands);
    CHECK_EQ(count, 2);
    const NavStep step = FindNavStep(bands, count, config, 0, -1.0);
    CHECK_TRUE(step.found);
    CHECK_TRUE(step.separated);
    CHECK_EQ(step.pitch, kPitch + 1);
    CHECK_TRUE(std::fabs(NewHighlightCentre(step, 1) - (120 + kPitch)) < 1.5);
    CHECK_TRUE(std::fabs(NewHighlightCentre(step, -1) - 120) < 1.5);
  }

  BeginCase("a beveled marker taller than its row is one band, and one band is not a step");
  {
    // A marker brighter at its top: the part the old and the new positions share
    // changes as well, so the difference cannot separate them and arrives as a single
    // band. That is the shape a menu with a big beveled bar - the settings and
    // Ultimate-style lists the mouse has to drive - puts in the difference, and a
    // single band is not a step until something says how tall one row is.
    FakeScreen screen(kPitch, kPitch + 16, kListTop, kListBottom, 120, /*ramp=*/true);
    const NavSampledFrame before = screen.Frame();
    screen.Advance(-MenuHoverAligner::kDeflection);
    const NavSampledFrame after = screen.Frame();

    NavDetectConfig config;
    std::vector<NavRowDiff> rows;
    CHECK_TRUE(DiffNavFrames(before, after, config, &rows));
    CHECK_FALSE(LooksLikeScreenChange(after, rows, config));
    NavBand bands[kMaxDetectBands];
    const int count = FindChangedBands(after, rows, config, bands);
    CHECK_EQ(count, 1);
    // With no pitch there is nothing to split the band with, so the separated path
    // cannot pair it and the merged path cannot place it. The aligner is what has to
    // get the pitch, from a second press rather than from this one.
    CHECK_FALSE(FindNavStep(bands, count, config, 0, -1.0).found);
  }

  BeginCase("one rectangle spanning more than a pitch is placed by the pitch");
  {
    // A bar taller than its row is the shape where the two positions really do
    // overlap: there is nothing in the difference to tell them apart, and the pitch
    // already measured says where the centres are inside the one rectangle.
    NavBand bands[1];
    bands[0].top = 100;
    bands[0].bottom = 179;
    bands[0].left = 200;
    bands[0].right = 439;
    NavDetectConfig config;

    // With no pitch measured, a tall rectangle is only a tall rectangle: guessing
    // where the selection is inside it would be guessing where it is at all.
    CHECK_FALSE(FindNavStep(bands, 1, config, 0, -1.0).found);

    const NavStep step = FindNavStep(bands, 1, config, 40, 120.0);
    CHECK_TRUE(step.found);
    CHECK_TRUE(step.merged);
    CHECK_EQ(step.pitch, 40);
    CHECK_TRUE(std::fabs(NewHighlightCentre(step, 1) - 159.5) < 1e-9);
    CHECK_TRUE(std::fabs(NewHighlightCentre(step, -1) - 119.5) < 1e-9);

    // And a rectangle that does not contain the highlight that was last seen is not
    // this step, however tall it is.
    CHECK_FALSE(FindNavStep(bands, 1, config, 40, 260.0).found);
  }

  BeginCase("two pairs are told apart by the pitch that is known");
  {
    NavBand bands[3];
    bands[0].top = 100;
    bands[0].bottom = 139;
    bands[1].top = 128;
    bands[1].bottom = 167;
    bands[2].top = 200;
    bands[2].bottom = 239;
    for (NavBand& band : bands) {
      band.left = 400;
      band.right = 879;
    }
    // The third band is the same height as the other two but slightly narrower: it
    // has to be distinguishable, since a pair that fits as tidily as another would
    // win only by being found first.
    bands[2].right = 859;
    NavDetectConfig config;

    // Without a pitch, the pair that is the same bar in two places - identical in
    // height and edges - is the one believed.
    const NavStep unknown = FindNavStep(bands, 3, config, 0, -1.0);
    CHECK_TRUE(unknown.found);
    CHECK_EQ(unknown.pitch, 28);
    CHECK_TRUE(std::fabs(unknown.lower_centre - 147.5) < 1e-9);

    // With one, the pair that matches it wins even though it fits less tidily.
    const NavStep known = FindNavStep(bands, 3, config, 100, -1.0);
    CHECK_TRUE(known.found);
    CHECK_EQ(known.pitch, 100);
    CHECK_TRUE(std::fabs(known.lower_centre - 219.5) < 1e-9);
  }

  BeginCase("the pointer's pixels are the guest's pixels");
  {
    // A 1360x768 window with a 1280x720 guest: the presenter fits by width, centres
    // the shortfall vertically, and the mapping has to say the same thing.
    const GuestImageMapping window = ComputeGuestImageMapping(1360, 768, 1280, 720, true);
    CHECK_TRUE(window.valid);
    CHECK_TRUE(std::fabs(window.scale - 1.0625) < 1e-9);
    CHECK_TRUE(std::fabs(window.offset_x) < 1e-9);
    CHECK_TRUE(std::fabs(window.offset_y - 1.0) < 1e-9);
    CHECK_TRUE(std::fabs(window.ClientToGuestY(1.0)) < 1e-9);
    CHECK_TRUE(std::fabs(window.GuestToClientY(720.0) - 766.0) < 1e-9);

    // A window of exactly the guest's size needs no mapping at all.
    const GuestImageMapping same = ComputeGuestImageMapping(1280, 720, 1280, 720, true);
    CHECK_TRUE(std::fabs(same.scale - 1.0) < 1e-9);
    CHECK_TRUE(std::fabs(same.offset_y) < 1e-9);

    // A window taller than the guest's aspect ratio letterboxes top and bottom.
    const GuestImageMapping tall = ComputeGuestImageMapping(1280, 1000, 1280, 720, true);
    CHECK_TRUE(std::fabs(tall.scale - 1.0) < 1e-9);
    CHECK_TRUE(std::fabs(tall.offset_y - 140.0) < 1e-9);

    // With letterboxing switched off the image fills the window instead, which is a
    // different map, and a pointer mapped through the wrong one is an offset.
    const GuestImageMapping stretched = ComputeGuestImageMapping(1360, 768, 1280, 720, false);
    CHECK_TRUE(std::fabs(stretched.scale - 768.0 / 720.0) < 1e-9);
    CHECK_TRUE(std::fabs(stretched.offset_y) < 1e-9);

    // Nothing to map with is not a mapping.
    CHECK_FALSE(ComputeGuestImageMapping(0, 768, 1280, 720, true).valid);
    CHECK_FALSE(ComputeGuestImageMapping(1360, 768, 0, 0, true).valid);
  }

  BeginCase("a hover one row down is one press, and lands on the row");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 120);
    aligner.FeedFrame(screen.Frame(), 0.0);
    // The pointer is put on the row below the one that is selected - the row under
    // the bar's lower edge, which is inside the next row and not this one.
    aligner.SetTargetY(120 + 0.7 * kPitch, 1000.0);

    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7);
    CHECK_EQ(Presses(frames), 1);
    CHECK_EQ(screen.centre(), 120 + kPitch);
    // The press is held for a couple of the guest's frames - long enough that no
    // frame can miss it - and the guest moves one row from it rather than one row per
    // deflected frame, which is what the edge-triggered guest below models and what
    // the real menus do.
    int deflected = 0;
    for (const DrivenFrame& frame : frames) {
      if (frame.stick != 0) {
        ++deflected;
        CHECK_EQ(frame.stick, -MenuHoverAligner::kDeflection);
      }
    }
    CHECK_TRUE(deflected >= 2);
    CHECK_TRUE(deflected <= 5);
    CHECK_TRUE(aligner.HaveGeometry());
    CHECK_EQ(aligner.pitch(), kPitch);
    CHECK_FALSE(aligner.Busy());

    // And a pointer moved within the same row is the same hover: no more presses.
    aligner.SetTargetY(120 + kPitch + 0.1 * kPitch, 9000.0);
    CHECK_EQ(Presses(DriveAlign(&aligner, &screen, 16.7, 40)), 0);
    CHECK_EQ(screen.centre(), 120 + kPitch);
  }

  BeginCase("a hover six rows down arrives as six presses, in order");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 60);
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(60 + 6.0 * kPitch, 1000.0);

    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7);
    // The aligner has to measure the screen on the way in, and what it measures is
    // the row it just crossed - so the presses are the rows between, the first one
    // gives the pitch, and the pointer's row is where it stops.
    CHECK_EQ(Presses(frames), 6);
    CHECK_EQ(screen.centre(), 60 + 6 * kPitch);
    CHECK_EQ(screen.presses_seen(), 6);
    // Every press is the same direction, because the pointer never moved.
    for (const DrivenFrame& frame : frames) {
      CHECK_TRUE(frame.stick == 0 || frame.stick == -MenuHoverAligner::kDeflection);
    }
  }

  BeginCase("a hover that has to go up presses up");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 60 + 4 * kPitch);
    // Two presses' worth of measurement first: on an untouched screen the first
    // press is a guess, so the case gives the aligner a screen it has already
    // measured by hovering the row below.
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(60 + 5.0 * kPitch, 1000.0);
    DriveAlign(&aligner, &screen, 16.7);
    CHECK_TRUE(aligner.HaveGeometry());

    // Now the pointer goes up, and the presses have to follow it.
    aligner.SetTargetY(60 + 1.0 * kPitch, 9000.0);
    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7);
    CHECK_EQ(screen.centre(), 60 + 1 * kPitch);
    CHECK_TRUE(Presses(frames) >= 3);
    for (const DrivenFrame& frame : frames) {
      CHECK_TRUE(frame.stick == 0 || frame.stick == MenuHoverAligner::kDeflection);
    }
  }

  BeginCase("the first hover on a screen measures it, and the next one costs nothing");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 120);
    aligner.FeedFrame(screen.Frame(), 0.0);
    // Nothing has been measured yet, so the only way to find the highlight is to
    // press, and the pointer happens to be on the row that is already selected: down
    // once to find the pitch, then back up once the measurement says so.
    aligner.SetTargetY(120.0, 1000.0);
    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7);
    CHECK_EQ(Presses(frames), 2);
    CHECK_EQ(screen.centre(), 120);
    CHECK_TRUE(aligner.HaveGeometry());
  }

  BeginCase("the hover waits for the pointer to rest");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 120);
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(120 + 3.0 * kPitch, 1000.0);
    // Sweeping across a list must not press once per row crossed: nothing happens
    // until the pointer has been still for longer than a sweep's own gaps.
    for (double t = 1000.0; t < 1000.0 + aligner.hover_delay_ms(); t += 5.0) {
      CHECK_EQ(aligner.PollStick(t), 0);
    }
    CHECK_FALSE(aligner.Busy());
    // The frame a press is measured against has to be one read while the pointer was
    // resting, and the one this screen was last read at is a second old: the aligner
    // asks for a frame instead of pressing, and the driver's frame thread is what
    // answers that.
    const double rest_ms = 1000.0 + aligner.hover_delay_ms();
    CHECK_TRUE(aligner.WantsFrames(rest_ms));
    CHECK_EQ(aligner.PollStick(rest_ms), 0);
    CHECK_FALSE(aligner.Busy());
    // Given one, it presses on the next poll.
    aligner.FeedFrame(screen.Frame(), rest_ms + 10.0);
    CHECK_TRUE(aligner.PollStick(rest_ms + 20.0) != 0);
    CHECK_TRUE(aligner.Busy());
  }

  BeginCase("a list that has ended stops the burst instead of pressing into it");
  {
    MenuHoverAligner aligner;
    // Two rows, and the pointer well below the last of them.
    FakeScreen screen(kPitch, kBarHeight, 60, 60 + kPitch, 60);
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(60 + 6.0 * kPitch, 1000.0);

    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7);
    CHECK_EQ(screen.centre(), 60 + kPitch);
    // A press that moves nothing is retried with a longer gap, and then given up on
    // rather than kept up: this is a list at its end, not a pulse that was too
    // short, and the guest must not be held down in the hope that it changes.
    CHECK_TRUE(Presses(frames) >= 2);
    CHECK_TRUE(Presses(frames) <= 6);
    CHECK_FALSE(aligner.Busy());
    // And the same hover does not press again: only a new pointer position is a new
    // question.
    CHECK_EQ(Presses(DriveAlign(&aligner, &screen, 16.7, 60)), 0);
    CHECK_EQ(screen.presses_seen(), Presses(frames));
  }

  BeginCase("a press that crossed two rows does not become the pitch");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 60);
    aligner.FeedFrame(screen.Frame(), 0.0);
    // The guest's second press moves it two rows - what a guest does when the press
    // before it landed while it was not looking - so that step arrives as a pair two
    // rows apart. Read as the pitch, that would double the tolerance the pointer's
    // row is judged by, and the hover would stop a row short of the pointer with the
    // log insisting it had arrived.
    screen.SetDoublePress(2);
    aligner.SetTargetY(60 + 4.0 * kPitch, 1000.0);
    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7);
    CHECK_EQ(screen.centre(), 60 + 4 * kPitch);
    CHECK_EQ(aligner.pitch(), kPitch);
    CHECK_TRUE(aligner.HaveGeometry());
    CHECK_FALSE(aligner.Busy());
    // Three presses rather than four, because the guest's second one crossed two rows.
    CHECK_EQ(Presses(frames), 3);
  }

  BeginCase("a marker at least as tall as its row is measured across two presses");
  {
    // The bar overlaps its own next position and the ramp makes the overlap change,
    // so every press arrives as one band and the pitch cannot be read from any single
    // difference. Two presses give it: the band is rigid, so it has moved by exactly
    // one row. Before this the burst ran out of presses and gave up on the screen.
    const int tall_pitch = 40;
    const int tall_bar = 46;
    MenuHoverAligner aligner;
    FakeScreen screen(tall_pitch, tall_bar, kListTop, kListBottom, 120, /*ramp=*/true);
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(120 + 3.0 * tall_pitch, 1000.0);

    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7);
    CHECK_TRUE(aligner.HaveGeometry());
    CHECK_EQ(aligner.pitch(), tall_pitch);
    CHECK_EQ(screen.centre(), 120 + 3 * tall_pitch);
    // One press to move the band, one to learn the pitch from how far it moved, and
    // then the presses the distance actually needs.
    CHECK_EQ(Presses(frames), 3);
    CHECK_FALSE(aligner.Busy());

    MenuHoverAligner::Report report;
    CHECK_TRUE(aligner.TakeReport(&report));
    CHECK_TRUE(std::string(report.outcome) == "aligned");
    CHECK_TRUE(report.have_highlight);
    CHECK_EQ(report.pitch, tall_pitch);
  }

  BeginCase("a tall marker that cannot move does not invent a pitch");
  {
    // A list of one row: the press is clamped away, the picture does not change, and
    // there is no band - let alone two of them to learn a pitch from. Two presses
    // that look like one is not enough to measure anything, so the burst still ends
    // by giving up rather than by pressing on.
    const int tall_pitch = 40;
    MenuHoverAligner aligner;
    FakeScreen screen(tall_pitch, 46, 120, 120, 120, /*ramp=*/true);
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(120 + 4.0 * tall_pitch, 1000.0);

    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7);
    CHECK_FALSE(aligner.HaveGeometry());
    CHECK_FALSE(aligner.Busy());
    CHECK_TRUE(Presses(frames) >= 2);
    CHECK_TRUE(Presses(frames) <= MenuHoverAligner::kAttempts + 1);
  }

  BeginCase("losing the focus does not lose the row the highlight is on");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 120);
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(120 + 2.0 * kPitch, 1000.0);
    DriveAlign(&aligner, &screen, 16.7);
    CHECK_TRUE(aligner.HaveGeometry());

    // The window loses the focus: the gesture is over, the screen is not.
    aligner.ForgetBurst();
    CHECK_TRUE(aligner.HaveGeometry());
    CHECK_FALSE(aligner.Busy());
    // And the pointer's row is still the selected row, so nothing has to move - a
    // press to prove it would be a flick of the selection for nothing.
    aligner.SetTargetY(120 + 2.0 * kPitch, 30000.0);
    CHECK_EQ(Presses(DriveAlign(&aligner, &screen, 16.7, 60)), 0);

    // Another window or another screen is another matter: nothing measured in the
    // old one's pixels means anything.
    aligner.Forget();
    CHECK_FALSE(aligner.HaveGeometry());
  }

  BeginCase("a screen that changes under a still pointer is not stepped through");
  {
    MenuHoverAligner aligner;
    FakeScreen menu(kPitch, kBarHeight, kListTop, kListBottom, 120);
    aligner.FeedFrame(menu.Frame(), 0.0);
    aligner.SetTargetY(120 + 2.0 * kPitch, 1000.0);
    DriveAlign(&aligner, &menu, 16.7);
    CHECK_TRUE(aligner.HaveGeometry());

    // A click opens another menu, and the pointer is still where it was: the row it is
    // over is a row of the screen that has gone, and the selection of the new one is
    // not the pointer's to move until the pointer does.
    FakeScreen other(kPitch, kBarHeight, 60, 60 + 8 * kPitch, 60);
    aligner.FeedFrame(menu.Frame(), 40000.0);
    aligner.FeedFrame(other.Frame(50), 40016.0);
    CHECK_FALSE(aligner.HaveGeometry());
    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &other, 16.7, 60, 0, 40032.0);
    CHECK_EQ(Presses(frames), 0);
    CHECK_EQ(other.presses_seen(), 0);

    // And the pointer moving again is the next hover, on the new screen.
    aligner.SetTargetY(60 + 2.0 * kPitch, 41000.0);
    const std::vector<DrivenFrame> again = DriveAlign(&aligner, &other, 16.7, 60, 0, 41000.0);
    CHECK_EQ(other.centre(), 60 + 2 * kPitch);
    CHECK_TRUE(Presses(again) >= 1);
  }

  BeginCase("a screen that keeps changing ends the burst");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 120);
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(120 + 4.0 * kPitch, 1000.0);
    // Every frame is a different picture, as a transition or a menu background that
    // is a video would be: nothing about the highlight can be read, so the burst
    // ends rather than pressing into a moving screen. The noise cycles rather than
    // rising for ever, since a level that saturates is a picture that stops changing.
    const std::vector<DrivenFrame> frames = DriveAlign(&aligner, &screen, 16.7, 200, 1);
    CHECK_TRUE(Presses(frames) <= MenuHoverAligner::kMaxScreenChanges + 1);
    CHECK_FALSE(aligner.Busy());
    CHECK_FALSE(aligner.HaveGeometry());
  }

  BeginCase("a pointer that moves again mid-burst re-aims it");
  {
    MenuHoverAligner aligner;
    FakeScreen screen(kPitch, kBarHeight, kListTop, kListBottom, 60);
    aligner.FeedFrame(screen.Frame(), 0.0);
    aligner.SetTargetY(60 + 8.0 * kPitch, 1000.0);
    double t = 20000.0;
    for (int i = 0; i < 60; ++i) {
      const int16_t stick = aligner.PollStick(t);
      screen.Advance(stick);
      t += 16.7;
      aligner.FeedFrame(screen.Frame(), t);
      if (i == 10) {
        // The hand keeps moving while the selection is still travelling.
        aligner.SetTargetY(60 + 2.0 * kPitch, t);
      }
      if (i > 12 && !aligner.Busy()) {
        break;
      }
    }
    // It follows the pointer rather than finishing the journey it started: the row
    // the pointer ended on is the row that is selected.
    CHECK_EQ(screen.centre(), 60 + 2 * kPitch);
    CHECK_FALSE(aligner.Busy());
  }

  BeginCase("no frames is no hover, and no hover is no press");
  {
    MenuHoverAligner aligner;
    aligner.SetTargetY(400.0, 1000.0);
    // Without a frame there is nothing to aim at, so the driver leaves the selection
    // to the travel fallback and the aligner stays silent.
    for (double t = 1000.0; t < 2000.0; t += 5.0) {
      CHECK_EQ(aligner.PollStick(t), 0);
    }
    CHECK_FALSE(aligner.Busy());
    // It is asking for frames rather than for nothing: without one there is nothing to
    // press from, and reading one is what the driver's frame thread does for it.
    CHECK_TRUE(aligner.WantsFrames(2000.0));
  }

  return Finish();
}
