// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P6 of the RhythmVerse store plan (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md
// §1.6, P6): the store's settings as its workers see them.
//
// Three rules shape this file:
//
//   * **Workers never read the settings store or a cvar registry** (project rule). They read an
//     immutable snapshot handed to them, so a commit can only ever affect the *next* request.
//   * **A commit is live.** `ApplyCommittedRow` replaces the snapshot in-process, which is why the
//     six rows are `hot` in the schema: the effect is the next catalogue request, not the next boot.
//   * **Master off means no requests.** The workers ask `enabled` before they touch the network; the
//     filter's five leaves stay meaningful (and editable) while the master is off.
//
// SDK-free, so `tests/store_client_tests.cpp` can walk the row -> snapshot mapping without a boot.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "settings/model.h"
#include "store/rhythmverse_client.h"

namespace rb_blitz::store {

// The committed RhythmVerse settings. `enabled` is the master switch (default off: the store makes
// no network request until the player asks for it); `instruments` is the filter the catalogue query
// and the local guard both use (On = the chart is required, Off = no condition).
struct StoreSettings {
  bool enabled = false;
  InstrumentFilter instruments;
};

// Read by the store's workers. Opaque on purpose: the type stays immutable, so a worker holding a
// snapshot cannot be surprised mid-request by a commit on the guest thread.
using SettingsSnapshot = std::shared_ptr<const StoreSettings>;

// The current snapshot and its generation. A snapshot is always available (the compiled defaults
// are generation 1), so a worker never has to branch on "nothing published yet".
SettingsSnapshot Settings();
std::uint64_t SettingsGeneration();

// Replaces the snapshot and returns the generation that now holds. Publishing settings equal to the
// current ones is a no-op and keeps the generation, so a repeated commit cannot invalidate caches.
std::uint64_t PublishSettings(const StoreSettings& settings);

// True when `id` is one of the six RhythmVerse rows.
bool IsStoreSetting(settings::SettingId id);

// Maps one row onto `settings`. Returns false (and leaves `settings` untouched) when the id is not a
// store row or the value is not a boolean.
bool ApplyRowToSettings(settings::SettingId id, const settings::Value& value, StoreSettings* settings);

// The hot applier's single entry point: load, apply, publish. `error` explains a refusal.
bool ApplyCommittedRow(settings::SettingId id, const settings::Value& value, std::string* error);

}  // namespace rb_blitz::store
