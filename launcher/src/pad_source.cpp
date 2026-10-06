// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the pad's SDL half (launcher/src/pad_source.h, A3).

#include "pad_source.h"

#include <utility>

namespace rb_blitz::launcher {
namespace {

PadFamily FamilyFor(SDL_GamepadType type) {
  switch (type) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
      return PadFamily::kXbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
      return PadFamily::kPlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
      return PadFamily::kNintendo;
    default:
      // SDL_GAMEPAD_TYPE_STANDARD, which is what a mapping database entry with no family says,
      // and anything newer than this build knows about. The letters are the right answer for
      // both: they are what the pad's own mapping calls its buttons.
      return PadFamily::kUnknown;
  }
}

FaceLabel LabelFor(SDL_GamepadButtonLabel label) {
  switch (label) {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
      return FaceLabel::kA;
    case SDL_GAMEPAD_BUTTON_LABEL_B:
      return FaceLabel::kB;
    case SDL_GAMEPAD_BUTTON_LABEL_X:
      return FaceLabel::kX;
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
      return FaceLabel::kY;
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
      return FaceLabel::kCross;
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
      return FaceLabel::kCircle;
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
      return FaceLabel::kSquare;
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
      return FaceLabel::kTriangle;
    case SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN:
      break;
  }
  return FaceLabel::kUnknown;
}

// A direction is a D-pad press or a stick pushed past the deadzone, and the launcher is not
// interested in which: D6 asks for both, and the ring's own behaviour is the same either way.
bool Pushed(SDL_Gamepad* pad, SDL_GamepadButton button, SDL_GamepadAxis axis, bool positive) {
  if (SDL_GetGamepadButton(pad, button)) {
    return true;
  }
  const AxisDirection direction =
      AxisDirectionFor(SDL_GetGamepadAxis(pad, axis), kStickDeadzone);
  return direction == (positive ? AxisDirection::kPositive : AxisDirection::kNegative);
}

}  // namespace

PadNavState ReadPad(SDL_Gamepad* pad) {
  PadNavState state;
  state[PadNavButton::kConfirm] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
  // Back is the pad's other way out, next to Start. D6 gives it a "reload profile / quit" menu;
  // that menu is not built, and A5 owns the recovery it was for, so the half of it that exists is
  // the cancel - which is also what B does, and what Escape does.
  state[PadNavButton::kCancel] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) ||
                                 SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
  state[PadNavButton::kTabBack] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  state[PadNavButton::kTabForward] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  state[PadNavButton::kLaunch] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
  state[PadNavButton::kUp] = Pushed(pad, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_AXIS_LEFTY,
                                    /*positive=*/false);
  state[PadNavButton::kDown] = Pushed(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_AXIS_LEFTY,
                                      /*positive=*/true);
  state[PadNavButton::kLeft] = Pushed(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_AXIS_LEFTX,
                                      /*positive=*/false);
  state[PadNavButton::kRight] = Pushed(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_AXIS_LEFTX,
                                       /*positive=*/true);
  // The right stick is the only control with no D-pad twin: it scrolls the tab body's scrollbar.
  // A wider deadzone than the left stick's is deliberate - the scroll is a glance, not a command,
  // and a stick resting off-centre must not walk the list on its own.
  const AxisDirection scroll =
      AxisDirectionFor(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY), kScrollStickDeadzone);
  state[PadNavButton::kScrollUp] = scroll == AxisDirection::kNegative;
  state[PadNavButton::kScrollDown] = scroll == AxisDirection::kPositive;
  return state;
}

PadRegistry::~PadRegistry() {
  for (SDL_Gamepad* pad : pads_) {
    SDL_CloseGamepad(pad);
  }
}

void PadRegistry::Refresh() {
  int connected = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&connected);
  std::vector<SDL_Gamepad*> wanted;
  if (ids != nullptr) {
    wanted.reserve(static_cast<std::size_t>(connected));
    for (int index = 0; index < connected; ++index) {
      // Already open: keep the handle we have, because closing and reopening would drop the
      // state of a pad that is in the middle of being used.
      for (SDL_Gamepad* open : pads_) {
        if (SDL_GetGamepadID(open) == ids[index]) {
          wanted.push_back(open);
          break;
        }
      }
    }
    // What is left are the pads that have appeared. A pad that will not open is skipped rather
    // than reported: SDL has already decided it is a gamepad, and a pad that fails here is not
    // something the user can act on.
    for (int index = 0; index < connected; ++index) {
      bool known = false;
      for (SDL_Gamepad* open : wanted) {
        if (SDL_GetGamepadID(open) == ids[index]) {
          known = true;
          break;
        }
      }
      if (known) {
        continue;
      }
      if (SDL_Gamepad* opened = SDL_OpenGamepad(ids[index])) {
        wanted.push_back(opened);
      }
    }
    SDL_free(ids);
  }

  // Whatever is no longer in `wanted` has gone: close it, or a pad unplugged and plugged back in
  // would leak a handle every time.
  for (SDL_Gamepad* open : pads_) {
    bool kept = false;
    for (SDL_Gamepad* wanted_pad : wanted) {
      if (wanted_pad == open) {
        kept = true;
        break;
      }
    }
    if (!kept) {
      SDL_CloseGamepad(open);
    }
  }
  pads_ = std::move(wanted);
}

GamepadNavSource::GamepadNavSource(PadRegistry& pads) : pads_(pads) {}

NavAction GamepadNavSource::Poll() {
  const std::vector<SDL_Gamepad*>& pads = pads_.pads();
  PadNavState state;
  for (std::size_t index = 0; index < pads.size(); ++index) {
    const PadNavState from_pad = ReadPad(pads[index]);
    if (from_pad.any()) {
      // The labels describe the pad the user is holding, so they follow the last pad that was
      // *used*. With two attached - which is the case a launcher should not have to ask the user
      // about (D17) - naming the first one's buttons would tell a player holding a DualSense to
      // press A.
      label_index_ = index;
    }
    for (std::size_t button = 0; button < kPadNavButtonCount; ++button) {
      state.down[button] = state.down[button] || from_pad.down[button];
    }
  }
  if (label_index_ >= pads.size()) {
    label_index_ = 0;
  }

  // With no pad open the labels are the 360's letters, so the bar can be drawn without asking
  // whether a pad is there.
  names_ = PadButtonNames{};
  name_.clear();
  if (!pads.empty()) {
    SDL_Gamepad* pad = pads[label_index_];
    const PadFamily family = FamilyFor(SDL_GetGamepadType(pad));
    names_ = ButtonNamesFor(family,
                            LabelFor(SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_SOUTH)),
                            LabelFor(SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_EAST)));
    if (const char* name = SDL_GetGamepadName(pad)) {
      name_ = name;
    }
  }

  saw_input_ = state.any();
  // SDL's own millisecond clock: it is monotonic and already a double's worth of precision at the
  // repeat times that matter, and it needs nothing passed in - the seam's signature is Poll(),
  // which is what keeps the ring from having to know how the pad tells the time.
  const double now_seconds = static_cast<double>(SDL_GetTicks()) / 1000.0;
  return model_.Update(state, now_seconds);
}

}  // namespace rb_blitz::launcher
