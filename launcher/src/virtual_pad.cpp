// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the scripted virtual pad (launcher/src/virtual_pad.h, A3).

#include "virtual_pad.h"

#include <cstdlib>
#include <string>
#include <utility>

namespace rb_blitz::launcher {
namespace {

// How long after --test-pad starts before the pad arrives. Long enough that the launcher is
// clearly already running, which is the point: a pad that is there before the window is a pad the
// registry found on its first frame, and "plugged in later" is the case worth testing.
constexpr double kArrivalDelaySeconds = 1.2;
// The gap between one step and the next: long enough for a press to be seen by several frames and
// for the ring's own repeat not to run them together.
constexpr double kStepGapSeconds = 0.4;
// How long a step holds when it does not say.
constexpr double kDefaultHoldSeconds = 0.12;

// Where each named control sits on the virtual pad. These are the pad's *raw* inputs, and the
// mapping installed below is what binds exactly these: the pad is the one device whose mapping
// this project chooses, which is why the harness can name a control and know what pressing it
// does. The numbers are SDL's own button numbering, so a pad built like a real one is described
// like a real one.
struct ControlStep {
  std::string_view name;
  int index;
  bool hat;
};

constexpr ControlStep kControls[] = {
    {"a", 0, false},     {"b", 1, false},      {"x", 2, false},    {"y", 3, false},
    {"lb", 4, false},    {"rb", 5, false},     {"back", 6, false}, {"start", 7, false},
    {"up", 0, true},     {"down", 0, true},    {"left", 0, true},  {"right", 0, true},
};

const ControlStep* FindControl(std::string_view name) {
  for (const ControlStep& control : kControls) {
    if (control.name == name) {
      return &control;
    }
  }
  return nullptr;
}

int HatFor(std::string_view name) {
  if (name == "up") {
    return SDL_HAT_UP;
  }
  if (name == "down") {
    return SDL_HAT_DOWN;
  }
  if (name == "left") {
    return SDL_HAT_LEFT;
  }
  return SDL_HAT_RIGHT;
}

// The bindings the pad brings with it. A virtual pad's hardware id is not in SDL's database, so
// without this it would be read through the database's "default" mapping - which is this same
// layout, but only by luck of the database's contents. Stating it here is what makes the harness
// independent of that, and the `family=` suffix is what makes a pad that claims to be something
// else possible.
constexpr std::string_view kPadBindings =
    "a:b0,b:b1,x:b2,y:b3,back:b6,start:b7,leftshoulder:b4,rightshoulder:b5,"
    "leftstick:b8,rightstick:b9,"
    "dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,"
    "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,";

}  // namespace

VirtualPad::~VirtualPad() { Stop(); }

bool VirtualPad::Start(std::string_view script, std::string* error) {
  const auto fail = [error](std::string reason) {
    if (error != nullptr) {
      *error = std::move(reason);
    }
    return false;
  };

  // The pad arrives first unless the script says otherwise, because every later step needs one to
  // press: a script that opens with `detach` is how "it goes away" is written.
  steps_.push_back(Step{Step::Kind::kAttach, 0, 0, 0.0});

  std::size_t start = 0;
  while (start <= script.size()) {
    const std::size_t semicolon = script.find(';', start);
    const std::size_t end = semicolon == std::string_view::npos ? script.size() : semicolon;
    std::string_view token = script.substr(start, end - start);
    if (!token.empty()) {
      std::string_view name = token;
      double seconds = kDefaultHoldSeconds;
      if (const std::size_t colon = token.find(':'); colon != std::string_view::npos) {
        name = token.substr(0, colon);
        // SDL_atoi rather than std::stoi: a malformed number is a step nobody can play, and
        // refusing it here says so instead of throwing out of the frame loop.
        const int millis = SDL_atoi(std::string(token.substr(colon + 1)).c_str());
        if (millis <= 0) {
          return fail("--test-pad: '" + std::string(token) + "' is not a length in milliseconds");
        }
        seconds = static_cast<double>(millis) / 1000.0;
      }

      if (name == "attach") {
        steps_.push_back(Step{Step::Kind::kAttach, 0, 0, 0.0});
      } else if (name == "detach") {
        steps_.push_back(Step{Step::Kind::kDetach, 0, 0, 0.0});
      } else if (name == "family=xbox" || name == "family=sony") {
        // Handled by Attach, which is where the mapping that carries the family is installed.
        family_ = name == "family=sony" ? "type:ps5,face:sony," : "type:xboxone,face:abxy,";
      } else if (name.starts_with("lstick=")) {
        Step step;
        step.kind = Step::Kind::kStick;
        step.seconds = 0.0;  // a stick stays where it was put until another step moves it
        const std::string_view pair = name.substr(7);
        const std::size_t comma = pair.find(',');
        if (comma == std::string_view::npos) {
          return fail("--test-pad: '" + std::string(name) + "' wants the stick as lstick=X,Y");
        }
        step.index = SDL_atoi(std::string(pair.substr(0, comma)).c_str());
        step.value = SDL_atoi(std::string(pair.substr(comma + 1)).c_str());
        steps_.push_back(step);
      } else if (const ControlStep* control = FindControl(name)) {
        Step step;
        step.kind = control->hat ? Step::Kind::kHat : Step::Kind::kButton;
        step.index = control->index;
        step.value = control->hat ? HatFor(name) : 0;
        step.seconds = seconds;
        steps_.push_back(step);
      } else {
        return fail("--test-pad: '" + std::string(name) + "' is not a step this knows");
      }
    }
    if (semicolon == std::string_view::npos) {
      break;
    }
    start = semicolon + 1;
  }

  Attach();
  if (instance_ == 0) {
    return fail("--test-pad: SDL would not attach a virtual pad");
  }
  return true;
}

void VirtualPad::Attach() {
  if (instance_ != 0) {
    return;
  }
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  // Every axis and button a gamepad has, so a press can be placed wherever the mapping says it
  // is, and one hat for the D-pad - which is how the database binds a D-pad as well.
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.nhats = 1;
  desc.name = "Virtual Pad (--test-pad)";
  instance_ = SDL_AttachVirtualJoystick(&desc);
  if (instance_ == 0) {
    return;
  }

  // The mapping has to be in place *before* the pad is opened: SDL reads an open gamepad's raw
  // inputs through the mapping it had when it was opened, and a mapping added afterwards only
  // reaches it through a reload that would drop the mapping again.
  char guid_text[33] = {};
  SDL_GUIDToString(SDL_GetJoystickGUIDForID(instance_), guid_text, sizeof(guid_text));
  const std::string mapping = std::string(guid_text) + ",Virtual Pad," + std::string(kPadBindings) +
                              family_;
  SDL_AddGamepadMapping(mapping.c_str());

  // Opened here and not by the registry, because a virtual joystick's state can only be set on an
  // opened one. The registry opens the same instance for the gamepad side; SDL keeps one joystick
  // object per instance, so the two share it.
  joystick_ = SDL_OpenJoystick(instance_);
}

void VirtualPad::Stop() {
  if (joystick_ != nullptr) {
    SDL_CloseJoystick(joystick_);
    joystick_ = nullptr;
  }
  if (instance_ != 0) {
    SDL_DetachVirtualJoystick(instance_);
    instance_ = 0;
  }
}

void VirtualPad::Apply(const Step& step, bool down) {
  if (joystick_ == nullptr) {
    return;
  }
  switch (step.kind) {
    case Step::Kind::kButton:
      SDL_SetJoystickVirtualButton(joystick_, step.index, down);
      return;
    case Step::Kind::kHat:
      SDL_SetJoystickVirtualHat(joystick_, step.index,
                                static_cast<Uint8>(down ? step.value : SDL_HAT_CENTERED));
      return;
    case Step::Kind::kStick:
      SDL_SetJoystickVirtualAxis(joystick_, step.index, static_cast<Sint16>(step.value));
      return;
    case Step::Kind::kAttach:
    case Step::Kind::kDetach:
      return;
  }
}

void VirtualPad::Update(double now_seconds) {
  if (steps_.empty()) {
    return;
  }
  if (next_at_ == 0.0) {
    next_at_ = now_seconds + kArrivalDelaySeconds;
  }
  // Let go of what the last step was holding before starting the next one, so a press is a press
  // and not a state the rest of the script inherits. This runs after the last step too, which is
  // why finished() asks about the release and not only about the list.
  if (holding_ && now_seconds >= release_at_) {
    Apply(steps_[next_ - 1], false);
    holding_ = false;
  }
  if (next_ >= steps_.size() || now_seconds < next_at_) {
    return;
  }
  const Step& step = steps_[next_];
  ++next_;
  switch (step.kind) {
    case Step::Kind::kAttach:
      Attach();
      next_at_ = now_seconds + kStepGapSeconds;
      return;
    case Step::Kind::kDetach:
      Stop();
      next_at_ = now_seconds + kStepGapSeconds;
      return;
    case Step::Kind::kStick:
      // Held until another step moves it, so there is nothing to release and nothing to wait for.
      Apply(step, true);
      next_at_ = now_seconds + kStepGapSeconds;
      return;
    case Step::Kind::kButton:
    case Step::Kind::kHat:
      Apply(step, true);
      holding_ = true;
      release_at_ = now_seconds + step.seconds;
      next_at_ = release_at_ + kStepGapSeconds;
      return;
  }
}

}  // namespace rb_blitz::launcher
