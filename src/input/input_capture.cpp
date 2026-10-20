// SPDX-License-Identifier: GPL-2.0-only
//
// Implementation of src/input/input_capture.h.

#include "input/input_capture.h"

#include <chrono>
#include <mutex>
#include <utility>

#include "input/source_gates.h"

namespace rb_blitz::input::capture {
namespace {

using profile::remap::PadState;
using profile::remap::Source;
using profile::remap::SourceKind;

// A capture that has reached the listening window counts 3, 2, 1 once per second and then leaves
// the dialog (restructure plan D11). There is no other timeout and no cancel gesture: the
// countdown is the only way out, so it must not be delayed by anything a player does inside it.

struct Session {
  bool armed = false;
  CaptureTarget target;
  bool waiting_neutral = true;
  std::string message;
  // When the countdown started: the first neutral frame after `Arm`. Until then the session is
  // still waiting for the press that opened the page to be released, and no count runs.
  std::chrono::steady_clock::time_point counting_since{};
  bool have_previous = false;
  PadState previous_pad;
  uint32_t previous_mouse = 0;
};

std::mutex& Mutex() {
  static std::mutex mutex;
  return mutex;
}

Session& State() {
  static Session session;
  return session;
}

bool TriggerDown(const PadState& pad, const profile::remap::Target target) {
  switch (target) {
    case profile::remap::Target::kLeftTrigger:
      return profile::remap::TriggerPressed(pad.left_trigger);
    case profile::remap::Target::kRightTrigger:
      return profile::remap::TriggerPressed(pad.right_trigger);
    default:
      return false;
  }
}

bool PadAnyHeld(const PadState& pad) {
  if (pad.buttons != 0 || profile::remap::TriggerPressed(pad.left_trigger) ||
      profile::remap::TriggerPressed(pad.right_trigger)) {
    return true;
  }
  for (const profile::remap::PadDirection direction : profile::remap::PadDirections()) {
    if (profile::remap::DirectionPressed(pad, direction)) {
      return true;
    }
  }
  return false;
}

// Every source that became pressed between the two frames, in a deterministic order: the 16 pad
// controls, the two triggers as their own names, the eight stick directions, the mouse buttons,
// then the keys. A direction and the stick's own synthesized button are different sources on the
// wire (the button does not exist on a physical pad's report here), so no dedupe is needed
// between them; a trigger is skipped in the button pass because it has no bit.
std::vector<Source> Presses(const Observation& now, const PadState& before, const uint32_t mouse_before,
                            const std::vector<std::string>& keys) {
  std::vector<Source> out;
  for (const profile::remap::Target target : profile::remap::Targets()) {
    if (profile::remap::TargetIsTrigger(target)) {
      if (TriggerDown(now.pad, target) && !TriggerDown(before, target)) {
        out.push_back({SourceKind::kPad, std::string(profile::remap::TargetName(target))});
      }
      continue;
    }
    const uint16_t bit = profile::remap::TargetBit(target);
    if (bit != 0 && (now.pad.buttons & bit) != 0 && (before.buttons & bit) == 0) {
      out.push_back({SourceKind::kPad, std::string(profile::remap::TargetName(target))});
    }
  }
  for (const profile::remap::PadDirection direction : profile::remap::PadDirections()) {
    if (profile::remap::DirectionPressed(now.pad, direction) &&
        !profile::remap::DirectionPressed(before, direction)) {
      out.push_back({SourceKind::kPad, std::string(profile::remap::PadDirectionName(direction))});
    }
  }
  struct MouseName {
    uint32_t mask;
    const char* name;
  };
  constexpr MouseName kMouse[] = {
      {mouse_button::kLeft, "left"},   {mouse_button::kRight, "right"},
      {mouse_button::kMiddle, "middle"}, {mouse_button::kX1, "x1"},
      {mouse_button::kX2, "x2"},
  };
  for (const MouseName& button : kMouse) {
    if ((now.mouse_buttons & button.mask) != 0 && (mouse_before & button.mask) == 0) {
      out.push_back({SourceKind::kMouse, button.name});
    }
  }
  // A detent is its own press, and it can never be held: the session captures it on this frame
  // (the `still_held` oracle answers false for a wheel name, as no mask matches it).
  if (now.wheel_detents > 0) {
    out.push_back({SourceKind::kMouse, std::string(profile::remap::kMouseWheelUp)});
  } else if (now.wheel_detents < 0) {
    out.push_back({SourceKind::kMouse, std::string(profile::remap::kMouseWheelDown)});
  }
  for (const std::string& key : keys) {
    out.push_back({SourceKind::kKey, key});
  }
  return out;
}

bool Allowed(const std::vector<SourceKind>& allowed, const SourceKind kind) {
  for (const SourceKind candidate : allowed) {
    if (candidate == kind) {
      return true;
    }
  }
  return false;
}

const char* KindName(const SourceKind kind) {
  switch (kind) {
    case SourceKind::kPad: return "a pad control";
    case SourceKind::kKey: return "a keyboard key";
    case SourceKind::kMouse: return "a mouse button";
  }
  return "an input";
}

// The seconds still on the countdown, rounded up so the dialog reads 3, 2, 1 and then leaves.
int SecondsLeft(const Session& session) {
  const auto left = kCountdown - (std::chrono::steady_clock::now() - session.counting_since);
  if (left <= std::chrono::seconds::zero()) {
    return 0;
  }
  return static_cast<int>(std::chrono::ceil<std::chrono::seconds>(left).count());
}

}  // namespace

void Arm(const CaptureTarget& target) {
  std::lock_guard lock(Mutex());
  Session& session = State();
  session.armed = true;
  session.target = target;
  session.waiting_neutral = true;
  session.message.clear();
  session.counting_since = std::chrono::steady_clock::now();
  session.have_previous = false;
  session.previous_pad = {};
  session.previous_mouse = 0;
}

void Cancel() {
  std::lock_guard lock(Mutex());
  Session& session = State();
  session.armed = false;
  session.message.clear();
  session.have_previous = false;
}

bool Armed() {
  std::lock_guard lock(Mutex());
  return State().armed;
}

Tick Observe(const Observation& observation) {
  Tick tick;
  std::lock_guard lock(Mutex());
  Session& session = State();
  if (!session.armed) {
    return tick;
  }
  // A background window captures nothing: the device gates already make the inputs inert, and a
  // capture must not become the one path that listens to them. Coming back keeps waiting for a
  // neutral frame, so alt-tabbing with a key held cannot hand that key to the dialog.
  if (!observation.focused) {
    session.waiting_neutral = true;
    session.have_previous = false;
    return tick;
  }

  const PadState before = session.have_previous ? session.previous_pad : PadState{};
  const uint32_t mouse_before = session.have_previous ? session.previous_mouse : 0;
  const std::vector<Source> presses = Presses(observation, before, mouse_before,
                                              session.have_previous ? observation.new_keys
                                                                    : std::vector<std::string>{});
  session.previous_pad = observation.pad;
  session.previous_mouse = observation.mouse_buttons;
  session.have_previous = true;

  const bool any_held = observation.any_key_down || observation.mouse_buttons != 0 ||
                        PadAnyHeld(observation.pad);
  if (session.waiting_neutral) {
    if (any_held) {
      return tick;
    }
    // Everything that opened the page is released: the listening window starts now.
    session.waiting_neutral = false;
    session.counting_since = std::chrono::steady_clock::now();
    return tick;
  }
  // D11: the countdown is the dialog's only way out, so it is answered before this frame's
  // presses - a press that arrives after zero leaves the row untouched.
  if (std::chrono::steady_clock::now() - session.counting_since >= kCountdown) {
    session.armed = false;
    session.message.clear();
    tick.leave = true;
    return tick;
  }
  if (presses.empty()) {
    return tick;
  }
  // Prefer an allowed source when one arrived in the same frame as a wrong one; only a frame with
  // no allowed press is the wrong-kind feedback.
  const Source* accepted = nullptr;
  for (const Source& source : presses) {
    if (Allowed(session.target.allowed, source.kind)) {
      accepted = &source;
      break;
    }
  }
  if (accepted == nullptr) {
    // Feedback, not a binding - and deliberately not a pause: D11 keeps the count running, so the
    // dialog still closes itself on time and the player is never stuck on this page.
    session.message = std::string("That is ") + KindName(presses.front().kind) + "; this row takes " +
                      AllowedKindText(session.target.allowed) + ".";
    return tick;
  }
  // D11: a valid press is the answer. The binding decision does not wait for a release - which is
  // also why the dialog leaves through the host Back pulse in the same poll.
  tick.captured = Result{session.target.token, *accepted};
  session.armed = false;
  session.message.clear();
  return tick;
}

std::string StatusText() {
  std::lock_guard lock(Mutex());
  const Session& session = State();
  if (!session.armed) {
    return {};
  }
  if (session.waiting_neutral) {
    return "Listening: release the input you used to open this page.";
  }
  // D11: the remaining seconds are the live state, so they are drawn beside a wrong-kind sentence
  // too - the count must be readable whatever the player just pressed.
  const std::string countdown = "Closing in " + std::to_string(SecondsLeft(session)) + ".";
  if (!session.message.empty()) {
    return session.message + " " + countdown;
  }
  return "Listening for " + AllowedKindText(session.target.allowed) + ". " + countdown;
}

void PostStatusMessage(std::string message) {
  std::lock_guard lock(Mutex());
  State().message = std::move(message);
}

std::string AllowedKindText(const std::vector<SourceKind>& allowed) {
  bool pad = false, key = false, mouse = false;
  for (const SourceKind kind : allowed) {
    pad = pad || kind == SourceKind::kPad;
    key = key || kind == SourceKind::kKey;
    mouse = mouse || kind == SourceKind::kMouse;
  }
  if (pad && !key && !mouse) {
    return "a pad control";
  }
  if (!pad && (key || mouse)) {
    return mouse ? "a keyboard key or mouse button" : "a keyboard key";
  }
  return "a pad control, keyboard key, or mouse button";
}

}  // namespace rb_blitz::input::capture
