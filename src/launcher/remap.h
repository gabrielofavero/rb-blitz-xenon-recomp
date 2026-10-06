// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Button remapping: the vocabulary the launcher's Controller tab writes and the game reads.
//
// The guest's pad state is a button field plus two analog trigger bytes, and the SDK fills it
// straight from SDL: there is no indirection to configure. A remap therefore has to be applied
// to the state the guest asked for, which is why the two halves live where they do - the
// launcher owns `[remap]` in the profile and this module is the only thing that knows how to
// spell it, so the two processes cannot disagree about what a binding means.
//
// The rules are deliberately small and total:
//
//   * A *target* is one of the 17 controls a 360 pad has. The 15 digital ones are bits in the
//     guest's button field; the two triggers are analog, so binding one drives its byte to full
//     rather than pretending it is a bit.
//   * A *source* is a pad control (`pad:`), a keyboard key (`key:`) or a mouse button
//     (`mouse:`). The pad's own controls are named with the target vocabulary, because the
//     SDK's pad mapping is one physical control per bit and the pad's buttons are labelled that
//     way anyway.
//   * A target with no row in `[remap]` has the binding this module ships for it (`DefaultSources`):
//     the pad's own control plus a keyboard key, so a keyboard is a complete stand-in for the
//     pad without the profile having to say so. The two analog triggers and Guide have no default:
//     a keyboard cannot stand in for a trigger's travel, and Guide belongs to the platform.
//   * A target with a row binds to a comma-separated list, and an empty list means nothing presses
//     it at all.
//
// SDK-free and SDL-free on purpose: the game and the launcher both compile this, and the tests
// exercise the whole rewrite without a pad, a window or a profile on disk (the same rule
// profile.h follows).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "launcher/profile.h"

namespace rb_blitz::launcher::remap {

// One control a binding can target, in the order the launcher's panel lists them: the face
// buttons, the shoulders and triggers, the system buttons, the sticks, then the d-pad.
enum class Target : uint8_t {
  kA,
  kB,
  kX,
  kY,
  kLeftShoulder,
  kRightShoulder,
  kLeftTrigger,
  kRightTrigger,
  kBack,
  kStart,
  kGuide,
  kLeftThumb,
  kRightThumb,
  kDpadUp,
  kDpadDown,
  kDpadLeft,
  kDpadRight,
};

// Every target, in `Target`'s declaration order.
const std::vector<Target>& Targets();

// The name `[remap]` and the panel spell it as: "a", "left_shoulder", "dpad_up". These are the
// same words the SDK's own button names use, so a source and a target read alike.
std::string_view TargetName(Target target);
std::optional<Target> ParseTarget(std::string_view name);

// The name a person reads on their pad: "A", "Left bumper", "D-pad up".
std::string_view TargetLabel(Target target);

// The bit `target` occupies in the guest's button field, or 0 for a trigger - which has no bit,
// because the guest reads it as an analog value rather than a button.
uint16_t TargetBit(Target target);
bool TargetIsTrigger(Target target);

// The guest's pad state as the remap reads and writes it. `buttons` is X_INPUT_GAMEPAD's
// button half; the trigger bytes are its analog half.
struct PadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
};

// The value above which the SDK treats a trigger as pressed for its own digital view of it
// (`HID_SDL_TRIGG_THRES` in the SDK's SDL input driver, which compares the byte the same way).
// A trigger source uses the same threshold, so a binding and the SDK agree about what "the
// trigger is pressed" means.
inline constexpr uint8_t kTriggerThreshold = 0x1F;

// True when the trigger byte counts as pressed.
inline bool TriggerPressed(uint8_t value) { return value > kTriggerThreshold; }

// One physical input a binding can come from.
enum class SourceKind { kPad, kKey, kMouse };

struct Source {
  SourceKind kind = SourceKind::kPad;
  // `kPad`: a target name - the pad control, not a guest button. `kKey`: an SDL scancode name
  // ("space", "f1", "w"). `kMouse`: one of left, right, middle, x1, x2.
  std::string name;
};

bool operator==(const Source& left, const Source& right);
inline bool operator!=(const Source& left, const Source& right) { return !(left == right); }

// "pad:a", "key:space", "mouse:left". Nothing for text this build does not understand, which is
// what lets a newer launcher's binding survive a round trip through an older one.
std::optional<Source> ParseSource(std::string_view text);
std::string FormatSource(const Source& source);

// One target's binding, as `[remap]` spells it.
struct Binding {
  Target target = Target::kA;
  // Empty means nothing presses it, which is how a control is turned off.
  std::vector<Source> sources;
};

// The binding a control has when the profile says nothing about it: the pad's own control plus a
// keyboard key chosen to mirror the pad on a keyboard for this game (the left hand on the D-pad's
// WASD, the right hand on the face buttons' IJKL diamond, and Q/E, Z/C, V/N and Backspace/Return
// around them). Empty - nullptr - for the analog triggers and Guide, which are left to the pad.
// The sources read `state` before any rewriting, so a default is the identity for a pad user.
const std::vector<Source>* DefaultSources(Target target);

// The `[remap]` table of the profile.
class Table {
 public:
  // From the profile's rows. A row whose key or value this build does not understand is kept
  // verbatim, so a save neither drops a newer launcher's work nor rewrites what it cannot read.
  static Table FromRows(const std::vector<ProfileSetting>& rows);

  // The rows a save would write: the bindings in `Targets()` order, then whatever was kept
  // verbatim. A table with nothing bound renders nothing at all.
  std::vector<ProfileSetting> ToRows() const;

  // The target's sources, or nullptr when the target has no row - which means the pad's own
  // reporting is left alone. An empty list is a real binding: it means the control is off.
  const std::vector<Source>* Bindings(Target target) const;

  // What actually presses `target`: the row the user bound, or the binding this module ships when
  // the profile says nothing. nullptr only for a control with no default and no row - the analog
  // triggers and Guide - which keeps the pad's own reporting. This is the one the game applies and
  // the panel shows, so a keyboard works without the profile having to be edited.
  const std::vector<Source>* Effective(Target target) const;

  bool IsBound(Target target) const { return Bindings(target) != nullptr; }
  // True when the target is bound to nothing.
  bool IsDisabled(Target target) const;

  void Set(Target target, std::vector<Source> sources);
  // Back to stock: the row goes, and the pad's own reporting comes back.
  void Reset(Target target);
  void ResetAll();

  // How many targets have a row, which is what the panel says it is showing.
  std::size_t BoundCount() const;
  // True when any target the profile names has a source. A table of empty rows is not "done":
  // it turns a pad into a brick, and the panel refuses to save one by accident.
  bool AnySourceSet() const;

 private:
  std::vector<Binding> bindings_;
  std::vector<ProfileSetting> unknown_;
};

// Answers "is this source down?" for the rewrite below. A function pointer rather than a
// std::function because the rewrite runs on every poll, and this is the only thing that has to
// know about a real device.
using DownFn = bool (*)(void* context, const Source& source);

// The rewrite. `state` is what the SDK reported, which is also the pad's own controls one for
// one - so a `pad:` source reads `state`, and every binding sees the same originals no matter
// what another binding does. Writes the result into `*out`, which may be the same object as
// `state` for in-place use.
void Apply(const Table& table, const PadState& state, DownFn down, void* context, PadState* out);

}  // namespace rb_blitz::launcher::remap
