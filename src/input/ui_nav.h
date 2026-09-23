// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The decidable part of the mouse -> menu bridge in src/input/mouse_ui.{h,cpp}:
// turning pointer travel into the discrete left-stick presses the guest menus
// actually read.
//
// Why presses and not a held deflection. The menus are not pointer-driven: there
// is no guest cursor to move and no "activate what is under the pointer" call to
// make. What the game does read is the pad, one row per left-stick press, clamped
// at both ends and non-wrapping (docs/bringup-log.md, 2026-09-22). So the only
// faithful translation of a mouse is to emit the same kind of press a thumb
// would.
//
// Why the distance between two positions and not the position itself. Nothing the
// host can observe says which row the guest has highlighted: it is handed a
// composited frame, the guest exposes no hit test, and no pad event reports where
// the selection went. The offset between where the pointer is and which row is
// selected is therefore not just unknown but untouchable - a bridge that can only
// add the pointer's own motion to it cannot change it, and pointing at the row
// that is already highlighted moves the selection by exactly as much as pointing
// anywhere else. What is observable is how far the pointer has moved, so that is
// what this class translates: a row of travel moves the selection a row, in
// either direction, which keeps the selection under the pointer for as long as
// the pointer is travelling along the list. The offset stays a whole number of
// rows whatever the user does, so the selection keeps following the pointer even
// when it is not the row the pointer is over.
//
// Why the pixels are measured and not chosen. A row of travel is a fact about the
// guest's layout, not a feel constant: the song list's rows are 48 px apart in a
// 768-px-tall client, a sixteenth of the height (docs/bringup-log.md,
// 2026-09-22). A threshold that is merely about right makes the selection gain or
// lose a row on every gesture, which is the one thing a relative bridge cannot
// afford, so this is a fraction of the window height - and hence scaled to the
// real height by the driver - rather than a fixed number of pixels. Rounding
// rather than truncating is the same argument one row down: the nearest row is
// the one the user means, even when the pointer stopped a pixel short of it.
//
// Why a click is delivered late. Travel is a queue of presses that takes time to
// drain, and the guest only moves its highlight when one of its own frames reads
// the stick - so a click reported the moment the button goes down activates the
// row the walk happens to be passing through, not the row the hand stopped on,
// and rows still queued when the screen changes play into the next screen. The
// two halves of the bridge therefore have to be ordered rather than independent:
// UiClickPulser holds a click back until UiNavStepper::HasPendingRows is false.
//
// Kept free of any SDK dependency for the same reason src/fs/path_policy.h is -
// the interesting part is arithmetic over pixel counters and poll counts, and
// tests/ui_nav_tests.cpp has to cover it without booting the game
// (docs/rb3-references.md §7.3).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rb_blitz::input {

class UiNavStepper {
 public:
  // Full deflection, the magnitude the keyboard bindings use for the same
  // presses. The guest applies its own deadzone; nothing subtler is needed.
  static constexpr int kStepDeflection = 32767;

  // One press is the deflection held for press_polls() polls and then released
  // for release_polls().
  //
  // These are poll counts only because this class has no clock; what the guest
  // reacts to is time, and the driver converts from it (SetPulsePolls), so the
  // values below are no more than the defaults the unit tests use. Time matters
  // because the guest's two clocks are far apart: its menus advance on its frame
  // clock, while the pad is read at the device's poll rate, which the guest log
  // puts at ~420 polls a second here (docs/bringup-log.md, 2026-09-22). A press
  // counted in polls therefore shrinks as the poll rate climbs, and at 420 Hz a
  // three-poll press is 7 ms - less than one guest frame, which is why a burst
  // of steps used to arrive at the menu as a single step.
  static constexpr int kDefaultPressPolls = 3;
  static constexpr int kDefaultReleasePolls = 2;

  // No pulse is longer than this, whatever a caller or a cvar asks for: a press
  // that outlives a human's patience is a stuck stick.
  static constexpr int kMaxPulsePolls = 10000;

  // Retimes the pulse, in polls. The press is never shorter than one poll, since
  // a deflection of no polls is not a press at all, and the release may be zero
  // for callers that want back-to-back presses.
  void SetPulsePolls(int press_polls, int release_polls);
  int press_polls() const { return press_polls_; }
  int release_polls() const { return release_polls_; }

  // Pointer travel that moves the selection by one row, in physical pixels of the
  // client area. The guest's pitch is per screen - in a 768-px-tall client the
  // main menu measured 27 px a row, MOD SETTINGS 40 and the song list 96, since
  // its rows are 48 px apart but every second one is an artist heading the
  // selection skips (docs/bringup-log.md, 2026-09-22) - so no single value is
  // exactly one row on every screen. This is 32 px, the menu side of that range,
  // and src/input/mouse_ui.cpp scales it to the window it is actually looking at,
  // which is why the number here is only the default the unit tests use.
  static constexpr double kDefaultRowPixels = 32.0;
  // Bounds for the cvar. Below the minimum a row would be a few pixels of hand
  // shake, and above the maximum a whole row would be a gesture.
  static constexpr double kMinRowPixels = 8.0;
  static constexpr double kMaxRowPixels = 400.0;

  // Same unit as rex::ui::MouseEvent::scroll_x/scroll_y, duplicated rather than
  // included so this header stays SDK-free.
  static constexpr int kScrollUnitsPerDetent = 120;

  // Rows one event may queue, and detents one wheel spin may, so that a violent
  // flick or a hard spin cannot keep the menu moving for seconds after the hand
  // has stopped. The bound is per gesture and not per row: 24 rows is more than a
  // screenful at the measured pitch, so a pointer thrown across the window still
  // lands as the single jump it was meant to be.
  static constexpr int kMaxQueuedRows = 24;
  static constexpr int kMaxWheelRows = 8;

  void SetRowPixels(double pixels);
  double row_pixels() const { return row_pixels_; }

  // The pointer's position in the client area, in physical pixels - the same
  // units as rex::ui::MouseEvent::x/y and Window::GetActualPhysicalHeight.
  //
  // The first call after Reset() or ForgetPointer() only establishes a starting
  // point and queues nothing: where the pointer happens to be when the driver
  // starts looking says nothing about where the selection is, and treating it as
  // if it did would move the selection by the whole distance to that guess.
  void OnPointer(double x, double y);

  // Pointer motion for the case where x/y stop moving: SetRelativeMouseMode
  // freezes them for mouse look while the deltas keep coming. The first call only
  // establishes a starting point, like OnPointer's.
  void OnPointerMotion(double dx, double dy);

  // Wheel/scroll deltas in the same units as rex::ui::MouseEvent, +x right and
  // +y up. One detent is one step. Sub-detent deltas from high-resolution
  // wheels and touchpads accumulate here rather than being dropped, which is
  // what a detent-counting implementation would do to them.
  void OnWheel(int scroll_x, int scroll_y);

  // The left-stick value for one guest poll. Writes 0,0 when nothing is due.
  void Poll(int16_t* out_x, int16_t* out_y);

  // Whether a row the user asked for is still owed to the guest: queued travel, a
  // wheel detent, or a press that is still being served. False means the guest has
  // seen everything the pointer did, which is when a click may be delivered -
  // UiClickPulser::Poll takes this as its argument.
  bool HasPendingRows() const;

  // Drops the starting point and the part of a row that has not been reached, so
  // that the next position is only a starting point again. For a click, which
  // usually changes the screen, and for a resize or a DPI change, which change
  // the pixels rows are measured in. Rows already queued are kept: they are
  // motion the user asked for, not a guess, and a click is not a reason to
  // swallow them.
  void ForgetPointer();

  // ForgetPointer, plus queued rows and any press in flight.
  void Reset();

 private:
  bool TakeStep(int16_t* out_x, int16_t* out_y);
  void QueueRows(double dx, double dy);
  void QueueRowsOnAxis(double pixels, double* residual, double* queued);

  double row_pixels_ = kDefaultRowPixels;

  // The last position seen, in pixels, and the part of a move that was too small
  // to be a row - carried rather than dropped, so that a row walked in ten small
  // steps still arrives and a wiggle that nets to nothing still does nothing.
  bool have_pointer_ = false;
  double pointer_x_ = 0.0;
  double pointer_y_ = 0.0;
  double residual_x_ = 0.0;
  double residual_y_ = 0.0;

  // Rows not yet delivered as presses, signed the way the stick is: +x is right,
  // +y is up.
  double queued_x_ = 0.0;
  double queued_y_ = 0.0;

  // Wheel detents not yet delivered, in rows.
  double wheel_x_ = 0.0;
  double wheel_y_ = 0.0;

  int press_polls_ = kDefaultPressPolls;
  int release_polls_ = kDefaultReleasePolls;
  int press_polls_left_ = 0;
  int release_polls_left_ = 0;
  int16_t press_x_ = 0;
  int16_t press_y_ = 0;
};

// The other half of the bridge: turning mouse clicks into the short A and B
// presses the guest reads, in an order the guest's frames can follow.
//
// Why a click waits for the walk. A click and the travel that led to it arrive as
// separate events and are served on different polls, but the guest only moves its
// highlight when one of its own frames reads the stick - so a click let through
// while rows are still queued activates whichever row the walk has reached rather
// than the one the hand was on, and rows still queued when the screen changes are
// played into whatever menu comes next. Both halves of that are what "sometimes
// there is an offset, sometimes it does not work" was. Waiting until the stepper
// has nothing left to deliver costs the tail of the last row's press - tens of
// milliseconds - and nothing at all when the hand is already still, which is when
// a click is made.
//
// Why a click is owed rather than passed through. A press shorter than the guest's
// frame is a press the guest never sees, and deferring a click widens that window
// rather than narrowing it: a five-millisecond click, released long before the
// deferred press could have been emitted, would otherwise vanish. So the press the
// user made is remembered until it has been emitted, however brief the physical
// one was, and a button released early is a click that is delivered late rather
// than one that is lost.
//
// Two clicks closer together than one press and its gap are one click here, not
// two: that is finer than the guest can tell apart anyway, and a double-click that
// arrived as two presses would activate a menu item and then whatever the next
// screen put under the selection.
class UiClickPulser {
 public:
  // One button each, in the driver's numbering: index 0 is the left button and the
  // guest's A, 1 the right and the guest's B. A poll reports them as a bit per
  // index, which is the driver's to map onto whatever the guest calls them.
  static constexpr int kButtonCount = 2;
  static constexpr uint16_t MaskFor(int index) {
    return static_cast<uint16_t>(1u << index);
  }

  // As UiNavStepper::SetPulsePolls, except that the release is at least one poll:
  // two clicks a hand apart are two presses, and no release at all would report
  // them as one unbroken one, which the guest's edge-detecting menus read as a
  // single click.
  void SetPulsePolls(int press_polls, int release_polls);
  int press_polls() const { return press_polls_; }
  int release_polls() const { return release_polls_; }

  // Buttons outside the two are ignored: the driver has no third button, so an
  // index it did not produce is a bug rather than input.
  void OnButtonDown(int index);
  void OnButtonUp(int index);

  // The buttons to report for one guest poll, one bit per button index, as
  // MaskFor. travel_pending is UiNavStepper::HasPendingRows(): while it is true, a
  // click that has not been emitted yet stays owed.
  uint16_t Poll(bool travel_pending);

  // ForgetPointer's counterpart for clicks: nothing is owed, nothing is pressed
  // and nothing is owed a gap. For a focused-out window, whose releases are never
  // delivered, and for a screen change the driver did not ask for.
  void Reset();

 private:
  struct Button {
    // The physical button, which is what decides how long a press lasts once it
    // has started.
    bool held = false;
    // A click the user made and the guest has not been given yet. Cleared when it
    // is emitted, and deliberately untouched by the release.
    bool pending = false;
    // Reporting this button as pressed: either serving the click's press, or
    // holding the guest's button down for as long as the hand is still on it,
    // which is what holding a pad button does.
    bool pressing = false;
    // Polls of press still owed before the press counts as one the guest has seen.
    int press_polls_left = 0;
    // Polls of release owed before this button may be pressed again.
    int gap_polls_left = 0;
  };

  Button buttons_[kButtonCount];
  int press_polls_ = UiNavStepper::kDefaultPressPolls;
  int release_polls_ = UiNavStepper::kDefaultReleasePolls;
};

// Moves 'value' one step of 'magnitude' toward zero and stops there.
inline double Consume(double value, double magnitude) {
  if (value >= magnitude) {
    return value - magnitude;
  }
  if (value <= -magnitude) {
    return value + magnitude;
  }
  return 0.0;
}

inline void UiNavStepper::SetRowPixels(double pixels) {
  // A row that is not a number would make every comparison below false and so
  // step on every single poll: keep the previous value instead.
  if (!std::isfinite(pixels)) {
    return;
  }
  row_pixels_ = std::clamp(pixels, kMinRowPixels, kMaxRowPixels);
}

inline void UiNavStepper::SetPulsePolls(int press_polls, int release_polls) {
  press_polls_ = std::clamp(press_polls, 1, kMaxPulsePolls);
  release_polls_ = std::clamp(release_polls, 0, kMaxPulsePolls);
}

inline void UiNavStepper::OnPointer(double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y)) {
    return;
  }
  if (!have_pointer_) {
    have_pointer_ = true;
    pointer_x_ = x;
    pointer_y_ = y;
    return;
  }
  const double dx = x - pointer_x_;
  const double dy = y - pointer_y_;
  pointer_x_ = x;
  pointer_y_ = y;
  QueueRows(dx, dy);
}

inline void UiNavStepper::OnPointerMotion(double dx, double dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy)) {
    return;
  }
  // Under the pointer lock the first delta is whatever the hand did while the
  // pointer was still free, so it starts the gesture rather than joining it.
  if (!have_pointer_) {
    have_pointer_ = true;
    return;
  }
  pointer_x_ += dx;
  pointer_y_ += dy;
  QueueRows(dx, dy);
}

inline void UiNavStepper::QueueRows(double dx, double dy) {
  QueueRowsOnAxis(dx, &residual_x_, &queued_x_);
  // Screen down is stick down: the list moves the way the pointer moves. This is
  // the only place the vertical sign changes.
  QueueRowsOnAxis(-dy, &residual_y_, &queued_y_);
}

inline void UiNavStepper::QueueRowsOnAxis(double pixels, double* residual, double* queued) {
  const double rows = *residual + pixels / row_pixels_;
  if (!std::isfinite(rows) || std::fabs(rows) > 1.0e9) {
    return;
  }
  long long whole = std::llround(rows);
  if (whole > kMaxQueuedRows || whole < -kMaxQueuedRows) {
    // Past the cap this is no longer a gesture the guest can follow row by row,
    // and the rows beyond it are dropped rather than banked: rows that arrive
    // after the hand has stopped are a selection running away from the user.
    whole = whole > 0 ? kMaxQueuedRows : -kMaxQueuedRows;
    *residual = 0.0;
  } else {
    *residual = rows - static_cast<double>(whole);
  }
  const double cap = static_cast<double>(kMaxQueuedRows);
  *queued = std::clamp(*queued + static_cast<double>(whole), -cap, cap);
}

inline void UiNavStepper::OnWheel(int scroll_x, int scroll_y) {
  const double detents_x = static_cast<double>(scroll_x) / kScrollUnitsPerDetent;
  const double detents_y = static_cast<double>(scroll_y) / kScrollUnitsPerDetent;
  const double cap = static_cast<double>(kMaxWheelRows);
  wheel_x_ = std::clamp(wheel_x_ + detents_x, -cap, cap);
  wheel_y_ = std::clamp(wheel_y_ + detents_y, -cap, cap);
}

inline void UiNavStepper::ForgetPointer() {
  have_pointer_ = false;
  pointer_x_ = 0.0;
  pointer_y_ = 0.0;
  residual_x_ = 0.0;
  residual_y_ = 0.0;
}

inline void UiNavStepper::Reset() {
  ForgetPointer();
  queued_x_ = 0.0;
  queued_y_ = 0.0;
  wheel_x_ = 0.0;
  wheel_y_ = 0.0;
  press_polls_left_ = 0;
  release_polls_left_ = 0;
  press_x_ = 0;
  press_y_ = 0;
}

// Consumes one row of whatever is due, or returns false when nothing is.
inline bool UiNavStepper::TakeStep(int16_t* out_x, int16_t* out_y) {
  // A wheel detent is a deliberate gesture on its own, so it goes ahead of
  // motion that is still only being walked off.
  if (std::fabs(wheel_x_) >= 1.0 || std::fabs(wheel_y_) >= 1.0) {
    const bool vertical = std::fabs(wheel_y_) >= std::fabs(wheel_x_);
    double& wheel = vertical ? wheel_y_ : wheel_x_;
    const double sign = wheel > 0.0 ? 1.0 : -1.0;
    wheel -= sign;
    const int16_t deflection = static_cast<int16_t>(sign * kStepDeflection);
    if (vertical) {
      *out_y = deflection;
    } else {
      *out_x = deflection;
    }
    return true;
  }

  const double rows_x = std::fabs(queued_x_);
  const double rows_y = std::fabs(queued_y_);
  if (rows_x < 1.0 && rows_y < 1.0) {
    return false;
  }

  // The axis that is owed more rows goes first, and pays for the step in rows
  // rather than in the motion that produced them: what a press costs is one row,
  // whatever the pointer did to ask for it. Ties go to the vertical, which is
  // the axis menus are lists of.
  if (rows_y >= rows_x) {
    *out_y = static_cast<int16_t>(queued_y_ > 0.0 ? kStepDeflection : -kStepDeflection);
    queued_y_ = Consume(queued_y_, 1.0);
  } else {
    *out_x = static_cast<int16_t>(queued_x_ > 0.0 ? kStepDeflection : -kStepDeflection);
    queued_x_ = Consume(queued_x_, 1.0);
  }
  return true;
}

inline void UiNavStepper::Poll(int16_t* out_x, int16_t* out_y) {
  int16_t x = 0;
  int16_t y = 0;

  if (press_polls_left_ > 0) {
    --press_polls_left_;
    x = press_x_;
    y = press_y_;
    if (press_polls_left_ == 0) {
      release_polls_left_ = release_polls_;
    }
  } else if (release_polls_left_ > 0) {
    --release_polls_left_;
  } else if (TakeStep(&x, &y)) {
    // This poll is the first of the press, so only the rest of it is counted.
    press_polls_left_ = press_polls_ - 1;
    press_x_ = x;
    press_y_ = y;
  }

  if (out_x) {
    *out_x = x;
  }
  if (out_y) {
    *out_y = y;
  }
}

inline bool UiNavStepper::HasPendingRows() const {
  // The residual fractions of a row are not owed to anyone: the queue and the
  // wheel hold whole rows only, and a press in flight or a release owed is a row
  // the guest has not finished seeing.
  return std::fabs(wheel_x_) >= 1.0 || std::fabs(wheel_y_) >= 1.0 ||
         std::fabs(queued_x_) >= 1.0 || std::fabs(queued_y_) >= 1.0 || press_polls_left_ > 0 ||
         release_polls_left_ > 0;
}

inline void UiClickPulser::SetPulsePolls(int press_polls, int release_polls) {
  press_polls_ = std::clamp(press_polls, 1, UiNavStepper::kMaxPulsePolls);
  release_polls_ = std::clamp(release_polls, 1, UiNavStepper::kMaxPulsePolls);
}

inline void UiClickPulser::OnButtonDown(int index) {
  if (index < 0 || index >= kButtonCount) {
    return;
  }
  Button& button = buttons_[index];
  button.held = true;
  button.pending = true;
}

inline void UiClickPulser::OnButtonUp(int index) {
  if (index < 0 || index >= kButtonCount) {
    return;
  }
  // Not pending: whether the press has been emitted yet is Poll's business, and a
  // click released during travel is still a click.
  buttons_[index].held = false;
}

inline uint16_t UiClickPulser::Poll(bool travel_pending) {
  uint16_t buttons = 0;
  for (int i = 0; i < kButtonCount; ++i) {
    Button& button = buttons_[i];
    if (button.gap_polls_left > 0) {
      // The press has just ended, and a button that never comes up is not a click.
      --button.gap_polls_left;
      continue;
    }
    if (!button.pressing) {
      if (!button.pending || travel_pending) {
        continue;
      }
      button.pending = false;
      button.pressing = true;
      button.press_polls_left = press_polls_;
    }
    if (button.press_polls_left > 0) {
      // A press shorter than this is one the guest's frame can miss, so the count
      // starts at the whole pulse rather than at all but its first poll.
      buttons |= MaskFor(i);
      --button.press_polls_left;
      continue;
    }
    if (button.held) {
      // The hand is still on the button, and holding a pad button holds it.
      buttons |= MaskFor(i);
      continue;
    }
    // Served, and the hand has come up: the press ends here, on the poll after
    // the release, so that what the guest sees is the release the user made. This
    // poll is the first of the gap rather than one in front of it, so the gap is
    // the length it was asked for.
    button.pressing = false;
    button.gap_polls_left = release_polls_ - 1;
  }
  return buttons;
}

inline void UiClickPulser::Reset() {
  for (Button& button : buttons_) {
    button = Button{};
  }
}

}  // namespace rb_blitz::input
