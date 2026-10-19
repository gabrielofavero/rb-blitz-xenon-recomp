// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Button remapping: the vocabulary Controller Settings' Controller tab writes and the game reads.
//
// The guest's pad state is a button field plus two analog trigger bytes, and the SDK fills it
// straight from SDL: there is no indirection to configure. A remap therefore has to be applied
// to the state the guest asked for, which is why the two halves live where they do - the
// Controller Settings owns `[remap]` in the profile and this module is the only thing that knows how to
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
//     way anyway; the eight stick directions are the one addition (`PadDirection`), because the
//     title's own presets bind them and a pad source has to be able to name them.
//   * A target with no row in `[remap]` has the binding this module ships for it (`DefaultSources`):
//     the pad's own control plus the keyboard keys the interface uses (Enter/A, Escape, X, Y,
//     WASD and the arrows, Space, Backspace), so a keyboard is a complete stand-in for the pad
//     without the profile having to say so. The shoulders and stick clicks are pad-only, and the
//     two analog triggers have no default: a keyboard cannot stand in for a trigger's travel.
//   * A target with a row binds to a comma-separated list, and an empty list means nothing presses
//     it at all.
//
// Guide is not one of the controls at all (docs/settings/contracts.md §2.3). It has
// no target, no default, no row and no source: a name this module does not know is refused rather
// than guessed at, so a `guide = pad:guide` row a person wrote once is preserved verbatim by a
// save and simply never applied - and `Apply` clears the bit out of the state the guest is handed,
// whatever set it. The platform's own Guide behaviour is not touched.
//
// SDK-free and SDL-free on purpose: the game and Controller Settings both compile this, and the tests
// exercise the whole rewrite without a pad, a window or a profile on disk (the same rule
// profile.h follows).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "profile/profile.h"

namespace rb_blitz::profile::remap {

// One control a binding can target, in the order Controller Settings' panel lists them: the face
// buttons, the shoulders and triggers, the system buttons, the sticks, then the d-pad. Guide is
// deliberately not here: it is a platform button, and the project never delivers it to the guest.
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
  kLeftThumb,
  kRightThumb,
  kDpadUp,
  kDpadDown,
  kDpadLeft,
  kDpadRight,
};

// How many controls there are, as a constant: the panel sizes its focus ring from this at
// compile time, and the tests assert that `Targets()` really has this many.
inline constexpr std::size_t kTargetCount = 16;

// Every target, in `Target`'s declaration order.
const std::vector<Target>& Targets();

// The name `[remap]` and the panel spell it as: "a", "left_shoulder", "dpad_up". These are the
// same words the SDK's own button names use, so a source and a target read alike.
std::string_view TargetName(Target target);
std::optional<Target> ParseTarget(std::string_view name);

// The name a person reads on their pad: "A", "Left bumper", "D-pad up".
std::string_view TargetLabel(Target target);

// The four directions of each analog stick, as a `pad:` *source* name. They are deliberately not
// targets: a stick direction is a person's thumb travel rather than one of the 16 controls the
// guest's button field carries, so no binding can make it a control of its own. The title's own
// controller presets bind the guest's stick-direction buttons (kPad 16-23), which is why the
// gameplay tables need to name them (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md
// §10.4, O8), and why `Apply` answers them from the pad's axes.
//
// The spelling is the direction, not the axis: `lstick_up`, `rstick_left`, and so on - the same
// words the pad reports them as.
enum class PadDirection : uint8_t {
  kLeftStickUp,
  kLeftStickDown,
  kLeftStickLeft,
  kLeftStickRight,
  kRightStickUp,
  kRightStickDown,
  kRightStickLeft,
  kRightStickRight,
};

inline constexpr std::size_t kPadDirectionCount = 8;

// Every direction, in `PadDirection`'s declaration order.
const std::vector<PadDirection>& PadDirections();

std::string_view PadDirectionName(PadDirection direction);
std::optional<PadDirection> ParsePadDirection(std::string_view name);

// True for a `[remap]` row name this build refuses on purpose - Guide, and only Guide. A row with
// this key is preserved verbatim by a save (it is not a name this build could write) but never
// applied, and the Controller Settings screen names it so a user who bound Guide once is told it
// is ignored rather than left to notice it does nothing. It is a separate question from "is this
// a control?", because "guide" is a name a person wrote in a file, not a target.
bool IsRefusedTargetName(std::string_view name);

// The refused row names present in `rows`, in file order, which is the diagnostic above.
std::vector<std::string> RefusedRowNames(const std::vector<ProfileSetting>& rows);

// The bit `target` occupies in the guest's button field, or 0 for a trigger - which has no bit,
// because the guest reads it as an analog value rather than a button.
uint16_t TargetBit(Target target);
bool TargetIsTrigger(Target target);

// The guest's pad state as the remap reads and writes it. `buttons` is X_INPUT_GAMEPAD's
// button half; the trigger bytes and the four stick axes are its analog half. The axes follow
// X_INPUT_GAMEPAD's convention - left is negative X, up is positive Y - and are only read by a
// `pad:lstick_*`/`pad:rstick_*` source; nothing in this module rewrites them.
struct PadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t left_x = 0;
  int16_t left_y = 0;
  int16_t right_x = 0;
  int16_t right_y = 0;
};

// How far a stick must travel before a direction source counts as pressed: half of full travel,
// the same point SDL's own deadzone-free games use and the one the title's preset treats as a
// direction. A direction is a direction, not a control, so there is nothing in between that a
// binding could mean - and pinning one number here is what keeps the two executables agreeing
// about when `pad:lstick_up` is down.
inline constexpr int16_t kStickThreshold = 0x4000;

// True when `direction` is held on the pad state.
inline bool DirectionPressed(const PadState& state, PadDirection direction) {
  const int threshold = kStickThreshold;
  switch (direction) {
    case PadDirection::kLeftStickUp:
      return state.left_y > threshold;
    case PadDirection::kLeftStickDown:
      return state.left_y < -threshold;
    case PadDirection::kLeftStickLeft:
      return state.left_x < -threshold;
    case PadDirection::kLeftStickRight:
      return state.left_x > threshold;
    case PadDirection::kRightStickUp:
      return state.right_y > threshold;
    case PadDirection::kRightStickDown:
      return state.right_y < -threshold;
    case PadDirection::kRightStickLeft:
      return state.right_x < -threshold;
    case PadDirection::kRightStickRight:
      return state.right_x > threshold;
  }
  return false;
}

// The bit Guide occupies in the guest's button field (X_INPUT_GAMEPAD_GUIDE). No binding can name
// it any more and the project never delivers it: the SDK's own `guide_button` and `keybind_guide`
// cvars can still raise it, and this is the bit that is taken back out.
inline constexpr uint16_t kGuideButtonBit = 0x0400;

// The pad state with every bit the project refuses to deliver cleared. `Apply` does this itself,
// so a caller that installs a table does not need it; a caller that installs no table at all - a
// machine with no profile to read - uses it on its own, which is what makes the prohibition hold
// on every input path rather than only on the ones that go through a binding.
inline PadState WithoutProhibitedButtons(PadState state) {
  state.buttons = static_cast<uint16_t>(state.buttons & ~kGuideButtonBit);
  return state;
}

// The value above which the SDK treats a trigger as pressed for its own digital view of it
// (`HID_SDL_TRIGG_THRES` in the SDK's SDL input driver, which compares the byte the same way).
// A trigger source uses the same threshold, so a binding and the SDK agree about what "the
// trigger is pressed" means.
inline constexpr uint8_t kTriggerThreshold = 0x1F;

// True when the trigger byte counts as pressed.
inline bool TriggerPressed(uint8_t value) { return value > kTriggerThreshold; }

// Whether a `pad:` source's own name is held on `state`: one of the 16 controls (answered from
// the button bit or the trigger byte) or a stick direction (answered from the axes). False for a
// name that is neither. Public because the physical snapshot the gameplay tables read asks the
// same question outside a `[remap]` rewrite (src/input/source_gates.h), and the two answers must
// never disagree about when `pad:a` is down.
bool PadSourceHeld(const PadState& state, std::string_view name);

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

// "pad:a", "pad:lstick_up", "key:space", "mouse:left". Nothing for text this build does not
// understand, which is what lets a newer Controller Settings's binding survive a round trip
// through an older one.
std::optional<Source> ParseSource(std::string_view text);
std::string FormatSource(const Source& source);

// The comma-separated list a `[remap]` or `[gameplay_remap_*]` value spells. Nothing when any
// token is not a source this build knows, which is what makes a value either wholly understood or
// kept verbatim - a half-applied list would silently drop the one source a newer Controller
// Settings meant. Exposed so the gameplay tables read the same grammar rather than a second copy
// of it (src/profile/gameplay_remap.h).
std::optional<std::vector<Source>> ParseSourceList(std::string_view text);
std::string FormatSourceList(const std::vector<Source>& sources);

// One target's binding, as `[remap]` spells it.
struct Binding {
  Target target = Target::kA;
  // Empty means nothing presses it, which is how a control is turned off.
  std::vector<Source> sources;
};

// The binding a control has when the profile says nothing about it: the pad's own control plus
// the keyboard keys the interface uses (2026-10-19). Gameplay Mapping's Keyboard + Mouse table is
// on by default, so General Mapping's keys are the menu's: Enter/A confirm, Escape backs out, X
// and Y keep their letters, Space is Start, Backspace is Back, and the D-pad answers to WASD and
// the arrow keys. The shoulders and stick clicks are pad-only; the analog triggers are empty -
// nullptr - and left to the pad. The sources read `state` before any rewriting, so a default is
// the identity for a pad user.
const std::vector<Source>* DefaultSources(Target target);

// The `[remap]` table of the profile.
class Table {
 public:
  // From the profile's rows. A row whose key or value this build does not understand is kept
  // verbatim, so a save neither drops a newer Controller Settings's work nor rewrites what it cannot read.
  static Table FromRows(const std::vector<ProfileSetting>& rows);

  // The rows a save would write: the bindings in `Targets()` order, then whatever was kept
  // verbatim. A table with nothing bound renders nothing at all.
  std::vector<ProfileSetting> ToRows() const;

  // The target's sources, or nullptr when the target has no row - which means the pad's own
  // reporting is left alone. An empty list is a real binding: it means the control is off.
  const std::vector<Source>* Bindings(Target target) const;

  // What actually presses `target`: the row the user bound, or the binding this module ships when
  // the profile says nothing. nullptr only for a control with no default and no row - the analog
  // triggers - which keeps the pad's own reporting. This is the one the game applies and the panel
  // shows, so a keyboard works without the profile having to be edited.
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
// one - so a `pad:` source reads `state` (a control's bit or trigger byte, or a stick direction's
// travel), and every binding sees the same originals no matter what another binding does. Writes
// the result into `*out`, which may be the same object as `state` for in-place use. The stick
// axes are copied through untouched: no binding writes them, and the guest reads its own axes.
//
// The result never carries a refused bit (`kGuideButtonBit`), whatever `state` held.
void Apply(const Table& table, const PadState& state, DownFn down, void* context, PadState* out);

}  // namespace rb_blitz::profile::remap
