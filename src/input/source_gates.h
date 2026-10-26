// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The source gates and the physical snapshot every mapping evaluator reads
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md, D1/D2/D5; P2).
//
// One poll of the physical layer, classified by source kind:
//
//   * a pad source answers from the pad's own state and nothing else - never from a state another
//     device was merged into, so a `pad:` binding cannot fire because the SDK's keyboard emulation
//     synthesized the same bit;
//   * a key answers from SDL's own keyboard state, and only while Keyboard Support is on and the
//     game window has focus;
//   * a mouse button answers from SDL's own button state, and only while Mouse Support is on and
//     the window has focus. In a menu the native pointer owns the mouse; a gameplay table only
//     exists while playing, so this module never competes with the pointer.
//
// SDK-free and SDL-free on purpose: the rules are what the tests pin, and the device side (SDL
// state, the SDK's physical drivers) is supplied by src/input/remap.cpp and
// src/input/title_input.cpp as plain data.
//
// Action edges are produced here as well, because "one press is one press" is a rule rather than
// a property of any consumer: a held action stays down until every contributing source releases,
// and a boundary (focus loss, leaving gameplay, a unplugged pad, a flipped switch) drops the hold
// without inventing a release the guest should act on (D5).

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "profile/gameplay_remap.h"
#include "profile/remap.h"
#include "settings/input_bindings.h"

namespace rb_blitz::input {

// The two source gates (D2), read at poll time. `keyboard_support` and `mouse_ui_nav` are device
// gates: off means that device drives nothing at all. The two gameplay switches are routing
// selectors and live in `GameplayRouting` instead - they never disable a device.
struct SourceGates {
  bool keyboard = true;
  bool mouse = true;
  bool operator==(const SourceGates&) const = default;
};

// Which of the three device families a `remap::SourceKind` names.
enum class SourceDevice : uint8_t { kPad, kKeyboard, kMouse };
SourceDevice DeviceOf(profile::remap::SourceKind kind);

// SDL's button masks: `SDL_BUTTON_MASK(n) == 1 << (n - 1)`, which is exactly what
// `SDL_GetMouseState()` returns, so the sampler hands the call's result in without translating
// it - and the tests spell the same bits.
namespace mouse_button {
inline constexpr uint32_t kLeft = 0x01;
inline constexpr uint32_t kMiddle = 0x02;
inline constexpr uint32_t kRight = 0x04;
inline constexpr uint32_t kX1 = 0x08;
inline constexpr uint32_t kX2 = 0x10;
}  // namespace mouse_button

// The mask a `mouse:` source names, or 0 for a name this build does not know.
uint32_t MouseMaskFor(std::string_view name);

// What the physical layer reports for one poll. Raw devices only: a pad's own state, the keys SDL
// says are down (answered through `key_down`), the mouse buttons SDL says are down. Nothing here
// has been merged with another device, remapped, or synthesized.
struct PhysicalInput {
  // Both keyboards and mouse sources are gated on focus: a background window must not play.
  bool window_focused = false;
  bool pad_connected = false;
  profile::remap::PadState pad;
  // `SDL_GetMouseState()`'s mask bits (mouse_button above). Zero while unfocused.
  uint32_t mouse_buttons = 0;
  // The wheel detents seen since the previous poll: positive away from the player (`wheel_up`),
  // negative toward (`wheel_down`). A detent is an event, so a wheel source is held for exactly
  // the poll that carries it and reads as a press followed by a release.
  int32_t wheel_detents = 0;
  // Answers a key by the name the profile spells, from the sampler's own state. Absent means no
  // keyboard is supplied at all, which is also what a test with no keys passes.
  std::function<bool(std::string_view)> key_down;
  bool KeyDown(std::string_view name) const { return key_down && key_down(name); }
};

// Whether `source` is physically held, with the source gates applied. A `pad:` source answers
// from `input.pad` alone (so no synthesized bit can impersonate it); a key from `key_down` while
// the keyboard gate is open; a mouse button while the mouse gate is open.
bool SourceHeld(const PhysicalInput& input, const SourceGates& gates,
                const profile::remap::Source& source);

// The first source of `action` that is held - after its own table's kind filter
// (`gameplay_remap::AcceptsSource`) and the gates above - or nullptr. `ActionHeld` is
// `HeldSource(...) != nullptr`; the pointer itself is what the `input_source_trace` line names,
// so a trace can say *which* binding fired rather than only that something did.
const profile::remap::Source* HeldSource(const PhysicalInput& input, const SourceGates& gates,
                                         const profile::gameplay_remap::Table& table,
                                         profile::gameplay_remap::Action action);

// Whether `action` of `table` fires this poll. A source of the wrong kind for the table is
// ignored even if it reached the table by a path that did not parse it (a hand-built table, or a
// future writer): the table's own kind decides, never the source.
bool ActionHeld(const PhysicalInput& input, const SourceGates& gates,
                const profile::gameplay_remap::Table& table,
                profile::gameplay_remap::Action action);

// The two tables as routing selectors (D2, restructured by P2 of the interface/gameplay mapping
// plan): the Mouse & Keyboard table is **always** active while playing, and the Controller table
// is active only when it has a custom row - with no custom row the native preset keeps driving
// the pad (D4). `TableActive` is the one place "may this table act" is decided.
struct GameplayRouting {
  bool controller = false;    // the Controller table has a custom row
  bool keyboard_mouse = true;  // always on while playing
  bool operator==(const GameplayRouting&) const = default;
};
bool TableActive(GameplayRouting routing, profile::gameplay_remap::TableKind kind,
                 bool gameplay);

// Whether a General Mapping (`[remap]`) `key:` or `mouse:` source may act now. Key sources drive
// the menus; in gameplay the direct tables own keys and the mouse, so General Mapping stops at
// the menu (R4/D3). `kPad` is answered by the rewrite's own pad state, not here - a caller passing
// it gets false.
bool GeneralSourceAllowed(const SourceGates& gates, bool gameplay,
                          profile::remap::SourceKind kind);

// Every key or mouse source an ACTIVE gameplay table is holding this poll, plus the ones it took
// before the context changed and that are still down. General Mapping must not act on one of
// them: the press belongs to the table. Measured 2026-10-20 (P2): Escape pauses from the Mouse &
// Keyboard table, and the pause menu it opens is the menu context - so the same still-held Escape
// became Interface Mapping's Back and backed straight out of the pause it had just caused. A pad
// source is not collected: the Controller table's own pad overlay removes its bits from the
// delivered state.
//
// `carry` is the caller's per-input-filter latch, and it is what makes the rule survive the
// context change: a poll first drops everything it carries that is no longer physically held,
// then (while `gameplay`) adds what an active table holds, and answers that set through `out`.
// A source therefore stays taken until it is released - the pause panel opening between the key's
// down and its up (22 ms, re-measured in P3) can no longer leak it. A menu key no table ever took
// is never added, so the title's A press is untouched.
void CollectGameplayConsumed(const PhysicalInput& input, const SourceGates& gates,
                             GameplayRouting routing, bool gameplay,
                             const profile::gameplay_remap::Table& controller,
                             const profile::gameplay_remap::Table& keyboard_mouse,
                             std::vector<profile::remap::Source>* carry,
                             std::vector<profile::remap::Source>* out);

// One action's held state, with the edges a dispatcher consumes. `Observe` is one poll:
//   * `pressed` on the poll the action becomes held,
//   * `released` on the poll it stops being held (every contributing source released),
//   * neither on a poll that changes nothing.
// `Invalidate` is a boundary, not a release: the hold is dropped without an edge, so a focus loss
// or a context change cannot deliver a late press or a release the guest never asked for. A
// source that is still held across a boundary must also *release* before it can press again
// (plan D3's release-before-rearming rule), which is what `armed` is: a key held through an
// alt-tab cannot strike a note in the new context just because the browser stopped delivering it.
struct ActionHold {
  struct Edge {
    bool pressed = false;
    bool released = false;
    bool any() const { return pressed || released; }
  };
  bool held = false;
  // False from a boundary until the source releases; a held source cannot press across it.
  bool armed = true;
  Edge Observe(bool held_now);
  void Invalidate() {
    held = false;
    armed = false;
  }
};

// The player slot host keyboard/mouse actions belong to. The title is single player: user 0 is
// the game's own player, and a host action must never be injected into every polled slot.
inline constexpr uint32_t kGameplayUser = 0;
inline bool IsGameplayUser(uint32_t user_index) { return user_index == kGameplayUser; }

// ---------------------------------------------------------------------------------------------
// P3: the action boundary the guest dispatcher consumes (plan D4/D5).
// ---------------------------------------------------------------------------------------------

// X_INPUT_GAMEPAD_START. It is the one pad control the game reads *outside* the action boundary,
// through the game screen's own pause path (P0 measured Start resolving to `kBMAction_None` while
// still pausing, plan §10.3/§10.4 O10), so it is the one control the overlay has to raise for a
// direct Pause rather than dispatch. Kept as a bit here so this module stays SDK-free; a test
// pins it against `remap::TargetBit(Target::kStart)`.
inline constexpr uint16_t kStartButtonBit = 0x0010;

// The pad controls the Controller table's own sources name, as X_INPUT_GAMEPAD bits. Stick
// directions contribute nothing: their travel is analog and the native preset's reaction to it is
// what the boundary suppression stops. A source this build does not understand contributes
// nothing either - it cannot have been applied, so it cannot own a control.
uint16_t ControllerOwnedButtons(const profile::gameplay_remap::Table& table);

// What the state filter must do to the pad it is about to hand the guest while a gameplay table
// routes input (D3/D4). `clear_buttons` are physical controls the Controller table owns: with the
// native preset bypassed, the guest must not read them as pad input either, and Start is always
// one of them because the native pause path is part of that preset. `set_buttons` carries the
// Pause action, whose only route into the game is that same pause path.
struct GameplayPadOverlay {
  uint16_t clear_buttons = 0;
  uint16_t set_buttons = 0;
  bool operator==(const GameplayPadOverlay&) const = default;
};

// `controller_active`/`keyboard_mouse_active` are the two tables' routing; `pause_held` is the
// union's Pause action, `controller_buttons` what `ControllerOwnedButtons` answered. Start is
// delivered exactly when an active table's Pause action is held, whatever the physical pad says:
// the Controller table's own `pad:start` binding therefore still pauses, an unbound Start does
// nothing, and a keyboard/mouse Pause works with the native pad path untouched.
GameplayPadOverlay GameplayOverlay(bool controller_active, bool keyboard_mouse_active,
                                   bool pause_held, uint16_t controller_buttons);

// The dispatcher's queue (D5). The poll and the guest's frame seam are different points in time:
// a tap that begins and ends between two visits must arrive as a press *and* a release, in that
// order, or the strike it meant disappears. One press and one release are therefore remembered
// per action; a longer burst collapses to one of each, and the consumer reconciles the final held
// state, so no action can be left down. Pure: the caller owns the lock.
class ActionEdges {
 public:
  // One poll's union held state for `action`; queues the edge a change makes.
  void Observe(profile::gameplay_remap::Action action, bool held_now);
  // The queued edges in order (a press before a release when both are pending). Returns how many
  // were written; a capacity below two leaves the rest queued.
  std::size_t Drain(profile::gameplay_remap::Action action, bool* out, std::size_t capacity);
  bool Pending(profile::gameplay_remap::Action action) const;
  // The last observed held state - what the consumer reconciles against.
  bool Held(profile::gameplay_remap::Action action) const;
  // A boundary: queued edges are dropped and every action reads as released, so the next poll
  // cannot deliver the old context's press or a release nobody asked for. The consumer must
  // still release what it already dispatched.
  void Clear();

 private:
  struct Slot {
    bool press = false;
    bool release = false;
  };
  static constexpr std::size_t kIndex(profile::gameplay_remap::Action action) {
    return static_cast<std::size_t>(action);
  }
  std::array<Slot, profile::gameplay_remap::kActionCount> slots_{};
  std::array<bool, profile::gameplay_remap::kActionCount> held_{};
};

// The kernel of the synthetic host device: it enumerates while either host gate is open, so a
// keyboard-only or mouse-only configuration stays connected without a pad (D2), and it
// contributes no pad state of its own.
inline bool HostDeviceConnected(const SourceGates& gates) { return gates.keyboard || gates.mouse; }

// Whether the OS cursor belongs on screen: only while a native menu can use it, or while an
// host player owns the pointer, or an ImGui dialog asked for capture. A live song shows no cursor (D3), which is also the
// reason a gameplay mouse press cannot reach the native-menu queue - the queue only accepts
// input while a menu is available.
inline bool CursorVisible(bool mouse_support, bool menu_available, bool imgui_wants_capture,
                          bool player_pointer = false) {
  return mouse_support && (menu_available || imgui_wants_capture || player_pointer);
}

// ---------------------------------------------------------------------------------------------
// P8: the Controls recovery path (plan §14.4).
// ---------------------------------------------------------------------------------------------
//
// A saved profile can leave the menu with no usable input at all: both host gates closed and no
// pad connected. Ordinary input is inert then, so the player cannot reach the row that would fix
// it - which is the recovery surface R9 provided outside the game until P8 retired that app. The
// in-game path is deliberately narrow: while (and only while) no route exists, one fixed key held
// for `kRecoveryHold` reopens the keyboard gate. It never runs otherwise, it changes exactly one
// stored switch, and it says what it did (src/ui/help_overlay.cpp). That is the whole exception
// to R1: menus and gameplay still follow the saved gates everywhere else.
inline constexpr std::chrono::milliseconds kRecoveryHold{2000};
// F10 is not a default binding and no retail page uses it; the name is the profile's spelling.
inline constexpr std::string_view kRecoveryKey = "f10";

// True when nothing can drive the menus: both host gates closed, no pad. A pad is a route even
// though it is not a host gate, so its presence always disables the recovery path.
bool RecoveryNeeded(const SourceGates& gates, bool pad_connected);

// Watches the recovery key. `Observe` is one poll and returns true exactly once per complete
// hold; any release, or a poll where recovery is not needed, starts over - a key that was already
// down when the profile was read cannot trigger it, and a failed recovery needs a fresh hold.
// The hold starts on a *rising* edge (the key was observed up, then down), and `was_down_`
// starts true so the very first poll can never read as one: a key physically held while the
// profile loads must be released and pressed again.
class RecoveryHold {
 public:
  bool Observe(bool needed, bool key_down, std::chrono::steady_clock::time_point now);
  bool Holding() const { return holding_; }

 private:
  bool holding_ = false;
  bool done_ = false;
  bool was_down_ = true;
  std::chrono::steady_clock::time_point since_{};
};

// What the recovery path's one action did.
enum class RecoveryOutcome : uint8_t {
  kRearmed,    // staged and committed: keyboard_support is on and the save landed
  kNoStore,    // no Controls store in this build (a tool, or a boot that never installed one)
  kRefused,    // the store refused the switch (a command-line/environment lock, say)
  kSaveFailed  // the switch was staged but the profile could not be saved or applied
};

// The recovery path's one action: reopen the keyboard gate through the installed store. It
// stages exactly one switch (`input.keyboard_support = true`), commits once, and touches nothing
// else - the profile is not reset, the mouse switch is not touched, and a refusal is named in
// `why` rather than reported as success. SDK-free: the store interface is all this calls.
RecoveryOutcome RecoverKeyboard(settings::InputBindingStore* store, std::string* why);

}  // namespace rb_blitz::input
