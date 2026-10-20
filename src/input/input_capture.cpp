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

// A capture that has not seen an input for this long leaves the dialog rather than leaving the
// game with no navigation. Twenty seconds is longer than a person needs to pick a button and
// short enough that a forgotten dialog cannot absorb the next menu press.
constexpr auto kTimeout = std::chrono::seconds(20);

struct Session {
  bool armed = false;
  CaptureTarget target;
  bool waiting_neutral = true;
  std::string message;
  std::chrono::steady_clock::time_point armed_at{};
  bool have_previous = false;
  PadState previous_pad;
  uint32_t previous_mouse = 0;
  // The candidate press, waiting for its release. A hold past `kHoldToCancel` turns it into the
  // dialog's cancel gesture instead of a binding - including a press of the wrong kind, so the
  // gesture is available even when nothing on this page can accept the device.
  bool have_pending = false;
  bool pending_allowed = true;
  Source pending;
  std::chrono::steady_clock::time_point pending_since{};
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

}  // namespace

void Arm(const CaptureTarget& target) {
  std::lock_guard lock(Mutex());
  Session& session = State();
  session.armed = true;
  session.target = target;
  session.waiting_neutral = true;
  session.message.clear();
  session.armed_at = std::chrono::steady_clock::now();
  session.have_previous = false;
  session.previous_pad = {};
  session.previous_mouse = 0;
  session.have_pending = false;
  session.pending = {};
}

void Cancel() {
  std::lock_guard lock(Mutex());
  Session& session = State();
  session.armed = false;
  session.message.clear();
  session.have_previous = false;
  session.have_pending = false;
  session.pending_allowed = true;
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
  if (std::chrono::steady_clock::now() - session.armed_at > kTimeout) {
    session.armed = false;
    session.message.clear();
    tick.leave = true;
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

  // A pending candidate answers first: its release binds it (or confirms the wrong-kind feedback),
  // a long hold is the cancel gesture. This runs before the new-press pass so a candidate's own
  // release frame (which carries no new press) still completes it.
  if (session.have_pending) {
    const auto held_for = std::chrono::steady_clock::now() - session.pending_since;
    if (observation.still_held && observation.still_held(session.pending) &&
        held_for >= kHoldToCancel) {
      session.armed = false;
      session.have_pending = false;
      session.message.clear();
      tick.leave = true;
      tick.cancelled = true;
      return tick;
    }
    const bool released = !observation.still_held || !observation.still_held(session.pending);
    if (released) {
      session.have_pending = false;
      if (session.pending_allowed) {
        tick.captured = Result{session.target.token, session.pending};
        session.armed = false;
        session.message.clear();
      } else {
        // A wrong-kind tap is feedback, not a binding: the sentence stays up and the session waits
        // for everything to be released before it listens again.
        session.waiting_neutral = true;
      }
    }
    return tick;
  }

  const bool any_held = observation.any_key_down || observation.mouse_buttons != 0 ||
                        PadAnyHeld(observation.pad);
  if (session.waiting_neutral) {
    if (!any_held) {
      session.waiting_neutral = false;
    }
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
    session.message = std::string("That is ") + KindName(presses.front().kind) + "; this row takes " +
                      AllowedKindText(session.target.allowed) + ".";
    // The press is still a gesture: holding it cancels the dialog, releasing it keeps listening.
    // That is what makes the cancel available even when every device is the wrong kind here.
    if (observation.still_held) {
      session.pending = presses.front();
      session.pending_since = std::chrono::steady_clock::now();
      session.have_pending = true;
      session.pending_allowed = false;
      return tick;
    }
    session.waiting_neutral = true;
    return tick;
  }
  // With device levels the capture waits for the release, so a hold can still cancel; without
  // them the press is the answer (the caller can only report edges).
  if (observation.still_held) {
    session.pending = *accepted;
    session.pending_since = std::chrono::steady_clock::now();
    session.have_pending = true;
    session.pending_allowed = true;
    return tick;
  }
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
  if (session.have_pending) {
    if (!session.pending_allowed) {
      return session.message + " Hold it to cancel.";
    }
    return std::string("Release to bind it; hold it for a moment to cancel.");
  }
  if (!session.message.empty()) {
    return session.message;
  }
  if (session.waiting_neutral) {
    return "Listening: release the input you used to open this page.";
  }
  return "Listening for " + AllowedKindText(session.target.allowed) +
         ". Tap an input to bind it, hold it to cancel.";
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

bool PadSourceHeld(const PadState& pad, const std::string_view name) {
  for (const profile::remap::Target target : profile::remap::Targets()) {
    if (profile::remap::TargetName(target) != name) {
      continue;
    }
    if (profile::remap::TargetIsTrigger(target)) {
      return TriggerDown(pad, target);
    }
    const uint16_t bit = profile::remap::TargetBit(target);
    return bit != 0 && (pad.buttons & bit) != 0;
  }
  for (const profile::remap::PadDirection direction : profile::remap::PadDirections()) {
    if (profile::remap::PadDirectionName(direction) == name) {
      return profile::remap::DirectionPressed(pad, direction);
    }
  }
  return false;
}

bool MouseSourceHeld(const uint32_t mouse_buttons, const std::string_view name) {
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
    if (name == button.name) {
      return (mouse_buttons & button.mask) != 0;
    }
  }
  return false;
}

}  // namespace rb_blitz::input::capture
