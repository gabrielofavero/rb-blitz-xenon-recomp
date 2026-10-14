// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P9 - the application-strategy seam (parent plan §5.5, §8): given the rows a commit wrote, which
// of them can be applied to the *running* process right now, and which ones honestly need a new
// one.
//
// Why this is its own module rather than a branch inside the service: the decision is a pure
// function of two things that already exist - the descriptors' `strategy` (§5.5, materialized by
// src/settings/schema.cpp) and the set of rows a commit changed - and the *effect* is a platform
// write (a cvar by name, a guest-side call) that the service must not know about. So the service
// says what changed, this module says what applies live, and a `HotApplier` supplied by the boot
// wiring performs the write. Tests drive all three without a boot.
//
// Two rules from §5.5 are enforced here rather than trusted:
//
//   * **a hot row is applied, never merely saved**: if the row is `kHot` and an applier knows it,
//     the value goes into the running game and the row carries no pending state afterwards.
//   * **a hot row with no applier is a restart row**: a build that cannot perform the write must
//     not tell the player the value already took effect. `ApplyHotPlan` reports `kNoApplier`, and
//     `RestartSetAfterApply` puts that row back into the restart set. This is the mechanical
//     version of "the UI never claims more than it can deliver".
//
// SDK-free on purpose: the applier interface is the seam, and every rule above is a function over
// plain data.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "settings/model.h"

namespace rb_blitz::settings {

// What happened to one live application.
enum class HotApplyStatus : uint8_t {
  kApplied = 0,   // the value is in the running game
  kNoApplier,     // this build has no way to apply that row live -> it is a restart row
  kRefused,       // the applier refused the value (a stale/unsupported write, never a retry loop)
  kFailed,        // the write was attempted and failed
};

const char* HotApplyStatusToken(HotApplyStatus status);

// The host half of a live apply. One instance per boot; every call runs on the caller's thread,
// and the caller is responsible for the project rule that a *guest* thread never touches the cvar
// registry (docs/plans/finished/2026-10-10-song-cache-plan.md §7) - which is why the runtime implementation of this
// interface hands the write to a short-lived host thread instead of doing it inline.
class HotApplier {
 public:
  virtual ~HotApplier() = default;

  // True when this build knows how to apply `id` live at all. Asked before `Apply`, so the plan
  // can be classified without attempting a write.
  virtual bool Knows(SettingId id) const = 0;

  // Performs the write. False plus `error` when it did not happen; a `kHot` row whose write
  // failed must not be reported as applied.
  virtual bool Apply(SettingId id, const Value& value, std::string* error) = 0;
};

// One commit's rows, split by §5.5's strategy.
struct HotApplyPlan {
  std::map<SettingId, Value> live;         // `kHot` rows: apply now
  std::vector<SettingId> pending_restart;  // `kRestart` rows: the §8 prompt covers these

  bool empty() const { return live.empty() && pending_restart.empty(); }
  bool restart_needed() const { return !pending_restart.empty(); }
  size_t size() const { return live.size() + pending_restart.size(); }
};

// What one live application did.
struct HotApplyOutcome {
  SettingId id = SettingId::kUnknown;
  HotApplyStatus status = HotApplyStatus::kNoApplier;
  std::string error;  // non-empty for kRefused/kFailed
};

// Splits `changed` (typically Service::last_commit_changes()) by each row's strategy. A row that
// is not in `descriptors` is treated as `kRestart`: an unknown row is never applied live.
HotApplyPlan PlanHotApply(const std::map<SettingId, Value>& changed,
                          const std::vector<Descriptor>& descriptors);

// The same split asked straight from the schema table, for a caller that has the changed rows but
// not a materialized descriptor list - the guest channel's commit path is exactly that caller.
HotApplyPlan PlanHotApplyByStrategy(const std::map<SettingId, Value>& changed);

// One outcome per row in `plan.live`, in the plan's order. A null applier makes every row
// `kNoApplier`, which is exactly what a build without the wiring must report.
std::vector<HotApplyOutcome> ApplyHotPlan(const HotApplyPlan& plan, HotApplier* applier);

// The restart set after the live pass: `plan.pending_restart` plus every `kHot` row whose
// application did not happen (`kNoApplier`, `kRefused`, `kFailed`). Empty means the session is
// fully applied and no prompt is shown - §5.5's "a session whose changes are all hot never
// prompts".
std::vector<SettingId> RestartSetAfterApply(const HotApplyPlan& plan,
                                            const std::vector<HotApplyOutcome>& outcomes);

// The rows a restart is still owed for, **by value**: what the running process has (`boot_values`,
// captured when the boot finished) against what the store says now (`current_values`). A row whose
// saved value is back to the running one is not pending any more, which is what makes "change it,
// then change it back" leave Help & Options without a prompt - the sticky "a restart-scoped change
// happened" bit cannot tell those two sessions apart.
//
// `forced` holds the rows an earlier live pass found it could not apply (a `kHot` row whose write
// failed, or one this build has no applier for): those are owed a restart even though their
// strategy says `hot`, and they keep it only while their value really differs from the running one.
// Rows present in only one of the two maps are ignored - a row the boot never saw cannot be
// compared.
std::vector<SettingId> PendingRestartRows(const std::map<SettingId, Value>& boot_values,
                                          const std::map<SettingId, Value>& current_values,
                                          const std::vector<Descriptor>& descriptors,
                                          const std::vector<SettingId>& forced = {});

}  // namespace rb_blitz::settings
