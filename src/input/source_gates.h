// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The source gates and the physical snapshot every mapping evaluator reads
// (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md, D1/D2/D5; P2).
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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "profile/gameplay_remap.h"
#include "profile/remap.h"

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

// The two gameplay switches as routing selectors (D2): off stops that table's direct actions and
// nothing else. `TableActive` is the one place "may this table act" is decided - a table acts only
// while playing and only while its own switch is on.
struct GameplayRouting {
  bool controller = false;
  bool keyboard_mouse = true;
  bool operator==(const GameplayRouting&) const = default;
};
bool TableActive(GameplayRouting routing, profile::gameplay_remap::TableKind kind,
                 bool gameplay);

// Whether a General Mapping (`[remap]`) `key:` or `mouse:` source may act now. Key sources drive
// the menus, and play too while both gameplay switches are off (the native preset's keyboard
// path); mouse sources are gameplay-only, because the native pointer owns the mouse in every
// menu. While either gameplay switch is on, gameplay is the direct tables' business and General
// Mapping's key/mouse sources stop at the menu (R4). `kPad` is answered by the rewrite's own pad
// state, not here - a caller passing it gets false.
bool GeneralSourceAllowed(const SourceGates& gates, bool gameplay, bool keyboard_mouse_mapping,
                          bool controller_mapping, profile::remap::SourceKind kind);

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

// The kernel of the synthetic host device: it enumerates while either host gate is open, so a
// keyboard-only or mouse-only configuration stays connected without a pad (D2), and it
// contributes no pad state of its own.
inline bool HostDeviceConnected(const SourceGates& gates) { return gates.keyboard || gates.mouse; }

// Whether the OS cursor belongs on screen: only while a native menu can use it, or while an
// ImGui overlay asked for pointer capture. A live song shows no cursor (D3), which is also the
// reason a gameplay mouse press cannot reach the native-menu queue - the queue only accepts
// input while a menu is available.
inline bool CursorVisible(bool mouse_support, bool menu_available, bool imgui_wants_capture) {
  return mouse_support && (menu_available || imgui_wants_capture);
}

}  // namespace rb_blitz::input
