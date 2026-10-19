// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The seam between the settings channel and the in-game Controls rows
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md §14.4, P7).
//
// The store itself lives in src/input/input_store.cpp: it owns the controller profile's
// `[remap]` and both gameplay tables, writes them through the profile module's surgical save, and
// publishes the effective snapshot the poll reads. This header is the only thing
// src/settings/bridge_sink.cpp knows about it, for two reasons:
//
//   * the sink is SDK-free and compiled into the bridge tests with no emulator, so it must not
//     pull in the device-facing world to route a row;
//   * the help overlay resolves a binding row's caption (`Current: ...`) through the same seam,
//     from the guest thread, without a second copy of the tables.
//
// Every method is called on the guest thread that carries the settings channel (the write hook or
// the help write). The store serializes its own state; nothing here is a guest call.

#pragma once

#include <string>
#include <string_view>

#include "profile/remap.h"

namespace rb_blitz::settings {

class InputBindingStore {
 public:
  virtual ~InputBindingStore() = default;

  // True for every `input.*` token this build describes. An unknown input token is refused by the
  // sink rather than staged into nothing (the same rule as an unknown setting row).
  virtual bool IsToken(std::string_view token) const = 0;

  // The one-bit query: does this row currently hold `value`? A switch compares its effective
  // boolean; a binding token answers whether the row has a stored override; a reset token
  // answers whether its table has any override. This is what `populate_menu` and the Reset rows'
  // state symbols read.
  virtual bool Matches(std::string_view token, bool value) const = 0;

  // Stages a switch value. False plus `error` when the row is fixed by a higher-precedence
  // source (a command-line or environment value is read-only, per the precedence contract).
  virtual bool StageBool(std::string_view token, bool value, std::string* error) = 0;

  // Stages the removal of one row's override (the row's compiled default comes back; an explicit
  // empty row is an override and is removed the same way). False when the token is not a binding
  // row.
  virtual bool StageReset(std::string_view token, std::string* error) = 0;

  // Stages a table-wide reset. `token` is one of `input.reset.*`.
  virtual bool StageResetAll(std::string_view token, std::string* error) = 0;

  // Writes the staged rows through the shared profile save, applies the switch values to the
  // running process, and publishes the new binding snapshot. A failure leaves the previous
  // effective mapping intact and names what failed.
  virtual bool Commit(std::string* error) = 0;

  // Arms the listening session for a binding token (the row's table decides the accepted source
  // kinds). The dialog itself is the generated menu's screen; this only starts the host half.
  virtual bool BeginCapture(std::string_view token, std::string* error) = 0;
  virtual void CancelCapture() = 0;

  // The successful capture: adds the source to the effective list (deduplicated), saves, applies
  // and publishes. False plus `error` when the save failed; the old mapping stays in force.
  virtual bool CompleteCapture(std::string_view token, const profile::remap::Source& source,
                               std::string* error) = 0;

  // The caption sentence for a binding row: the row's effective sources, and whether they are a
  // stored override or the compiled default. Empty for a token this store does not describe.
  virtual std::string Describe(std::string_view token) const = 0;

  // A one-line summary for the boot log: the four switches and the three tables' override counts.
  virtual std::string Summary() const = 0;
};

// The installed store, or null in a build with no Controls rows (a test, a tool). Inline so the
// sink's test target does not need another translation unit.
inline InputBindingStore*& InputBindingsSlot() {
  static InputBindingStore* slot = nullptr;
  return slot;
}
inline InputBindingStore* InputBindings() { return InputBindingsSlot(); }
inline void SetInputBindings(InputBindingStore* store) { InputBindingsSlot() = store; }

}  // namespace rb_blitz::settings
