// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the pad's mapping, deadzone and repeat (launcher/src/pad_nav.h, A3).

#include "pad_nav.h"

namespace rb_blitz::launcher {

AxisDirection AxisDirectionFor(int value, int deadzone) {
  if (value >= deadzone) {
    return AxisDirection::kPositive;
  }
  if (value <= -deadzone) {
    return AxisDirection::kNegative;
  }
  return AxisDirection::kNeutral;
}

std::string_view FaceLabelText(FaceLabel label) {
  switch (label) {
    case FaceLabel::kA:
      return "A";
    case FaceLabel::kB:
      return "B";
    case FaceLabel::kX:
      return "X";
    case FaceLabel::kY:
      return "Y";
    case FaceLabel::kCross:
      return "Cross";
    case FaceLabel::kCircle:
      return "Circle";
    case FaceLabel::kSquare:
      return "Square";
    case FaceLabel::kTriangle:
      return "Triangle";
    case FaceLabel::kUnknown:
      break;
  }
  // A database that says nothing about a pad leaves its face buttons unnamed. The launcher still
  // has to draw something, and the 360's letters are the ones its own remap targets are spelled
  // with, so an unknown pad is drawn as the pad this project is about.
  return "A";
}

PadButtonNames ButtonNamesFor(PadFamily family, FaceLabel confirm, FaceLabel cancel) {
  PadButtonNames names;
  const FaceLabel confirm_label = confirm == FaceLabel::kUnknown ? FaceLabel::kA : confirm;
  const FaceLabel cancel_label = cancel == FaceLabel::kUnknown ? FaceLabel::kB : cancel;
  names.confirm = FaceLabelText(confirm_label);
  names.cancel = FaceLabelText(cancel_label);
  switch (family) {
    case PadFamily::kPlayStation:
      names.shoulder_left = "L1";
      names.shoulder_right = "R1";
      names.start = "Options";
      break;
    case PadFamily::kNintendo:
      names.shoulder_left = "L";
      names.shoulder_right = "R";
      names.start = "+";
      break;
    case PadFamily::kXbox:
    case PadFamily::kUnknown:
      break;
  }
  return names;
}

NavAction ActionForPadButton(PadNavButton button) {
  switch (button) {
    case PadNavButton::kUp:
      return NavAction::kPrevious;
    case PadNavButton::kDown:
      return NavAction::kNext;
    case PadNavButton::kLeft:
      return NavAction::kPreviousOption;
    case PadNavButton::kRight:
      return NavAction::kNextOption;
    case PadNavButton::kScrollUp:
      return NavAction::kScrollUp;
    case PadNavButton::kScrollDown:
      return NavAction::kScrollDown;
    case PadNavButton::kConfirm:
      return NavAction::kActivate;
    case PadNavButton::kCancel:
      return NavAction::kCancel;
    case PadNavButton::kTabForward:
      return NavAction::kNextTab;
    case PadNavButton::kTabBack:
      return NavAction::kPreviousTab;
    case PadNavButton::kLaunch:
      return NavAction::kLaunch;
    case PadNavButton::kCount:
      break;
  }
  return NavAction::kNone;
}

namespace {

// Only the directions repeat. A confirm that repeated would press the row under the ring again and
// again, which for the *Install Ultimate* row is a second download.
constexpr bool Repeats(PadNavButton button) {
  return button == PadNavButton::kUp || button == PadNavButton::kDown ||
         button == PadNavButton::kLeft || button == PadNavButton::kRight ||
         button == PadNavButton::kScrollUp || button == PadNavButton::kScrollDown;
}

}  // namespace

NavAction PadNavModel::Update(const PadNavState& state, double now_seconds) {
  // The first button in the table's order that has something to do this frame is the one that
  // fires: a direction before a face button, which is what a player pressing both would expect.
  // The loop is also where the presses are taken, so its order is the order they are served in -
  // and one that has to wait keeps its press until a frame serves it, rather than being lost.
  PadNavButton due = PadNavButton::kCount;
  for (std::size_t index = 0; index < kPadNavButtonCount; ++index) {
    const PadNavButton button = static_cast<PadNavButton>(index);
    Held& held = held_[index];
    if (!state[button]) {
      // Whatever it was doing, it is not doing it any more; the next press is a new press.
      held = Held{};
      continue;
    }
    if (!held.down) {
      held.down = true;
      held.pressed = true;
      held.due = now_seconds;
    }
    if (due == PadNavButton::kCount &&
        (held.pressed || (Repeats(button) && now_seconds >= held.due))) {
      due = button;
    }
  }
  if (due == PadNavButton::kCount) {
    return NavAction::kNone;
  }
  Held& fired = held_[static_cast<std::size_t>(due)];
  if (fired.pressed) {
    fired.pressed = false;
    fired.due = now_seconds + kRepeatDelaySeconds;
  } else {
    fired.due = now_seconds + kRepeatIntervalSeconds;
  }
  return ActionForPadButton(due);
}

}  // namespace rb_blitz::launcher
