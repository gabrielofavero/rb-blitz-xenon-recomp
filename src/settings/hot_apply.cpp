// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P9 - the application-strategy seam. See src/settings/hot_apply.h for why the split exists; this
// file is the whole decision, and it is deliberately small: the strategy comes from the descriptor
// (one table, src/settings/schema.cpp), the values come from the commit, and the only interesting
// rules are the two that keep the UI honest.
//
// A note on "changed": the caller passes the rows a commit actually *wrote*, not every row of a
// session. §5.4's rule is that a value edited back to what it already was does not write and does
// not prompt, and that is decided upstream by `Store::MarkChanged`/`Save` (a no-changed save
// writes nothing) - so by the time a plan is built here, what is in the map is a real change.

#include "settings/hot_apply.h"

#include <algorithm>
#include <utility>

#include "settings/schema.h"

namespace rb_blitz::settings {

const char* HotApplyStatusToken(HotApplyStatus status) {
  switch (status) {
    case HotApplyStatus::kApplied:
      return "applied";
    case HotApplyStatus::kNoApplier:
      return "no applier";
    case HotApplyStatus::kRefused:
      return "refused";
    case HotApplyStatus::kFailed:
      return "failed";
  }
  return "?";
}

void ApplicationLedger::Seed(const std::map<SettingId, Value>& running) {
  records_.clear();
  generation_ = 0;
  for (const auto& [id, value] : running) {
    ApplicationRecord record;
    record.saved = value;
    record.running = value;
    records_[id] = std::move(record);
  }
}

uint64_t ApplicationLedger::Queue(uint64_t revision, const std::map<SettingId, Value>& saved,
                                   ApplyBoundary boundary) {
  if (saved.empty()) return generation_;
  const uint64_t generation = ++generation_;
  for (const auto& [id, value] : saved) {
    auto found = records_.find(id);
    if (found == records_.end()) continue;  // no running baseline: never claim application
    ApplicationRecord& record = found->second;
    record.saved = value;
    record.revision = revision;
    record.generation = generation;
    record.boundary = boundary;
    record.reason.clear();
    record.phase = ValueEquals(record.saved, record.running) ? ApplyPhase::kApplied
                    : boundary == ApplyBoundary::kNow ? ApplyPhase::kQueued
                                                     : ApplyPhase::kDeferred;
  }
  return generation;
}

void ApplicationLedger::Complete(SettingId id, uint64_t generation, HotApplyStatus status,
                                  const std::optional<Value>& actual, std::string reason) {
  const auto found = records_.find(id);
  if (found == records_.end()) return;
  ApplicationRecord& record = found->second;
  // A late completion can have changed reality. A stale failure without an observed value has
  // changed nothing, however, and must not erase a newer failure or its restart fallback.
  if (generation != record.generation) {
    if (!actual.has_value()) return;
    record.running = *actual;
    if (ValueEquals(record.running, record.saved)) {
      record.phase = ApplyPhase::kApplied;
      record.reason.clear();
    } else if (record.phase != ApplyPhase::kFailed && record.phase != ApplyPhase::kRefused) {
      record.phase = record.boundary == ApplyBoundary::kNow ? ApplyPhase::kQueued
                                                            : ApplyPhase::kDeferred;
    }
    return;
  }
  if (actual.has_value()) record.running = *actual;
  if (status == HotApplyStatus::kApplied && actual.has_value() &&
      ValueEquals(record.saved, record.running)) {
    record.phase = ApplyPhase::kApplied;
    record.reason.clear();
  } else {
    record.phase = status == HotApplyStatus::kRefused || status == HotApplyStatus::kNoApplier
                       ? ApplyPhase::kRefused
                       : ApplyPhase::kFailed;
    record.reason = std::move(reason);
    if (record.reason.empty() && status == HotApplyStatus::kApplied)
      record.reason = actual.has_value() ? "applier reported a different running value"
                                         : "applier reported success without a running value";
  }
}

const ApplicationRecord* ApplicationLedger::Find(SettingId id) const {
  const auto found = records_.find(id);
  return found == records_.end() ? nullptr : &found->second;
}

std::map<SettingId, Value> ApplicationLedger::RunningValues() const {
  std::map<SettingId, Value> values;
  for (const auto& [id, record] : records_) values[id] = record.running;
  return values;
}

std::vector<SettingId> ApplicationLedger::PendingRestartRows() const {
  std::vector<SettingId> rows;
  for (const auto& [id, record] : records_) {
    if (ValueEquals(record.saved, record.running)) continue;
    if (record.boundary == ApplyBoundary::kProcessRestart ||
        record.phase == ApplyPhase::kRefused || record.phase == ApplyPhase::kFailed)
      rows.push_back(id);
  }
  return rows;
}

HotApplyPlan PlanHotApply(const std::map<SettingId, Value>& changed,
                          const std::vector<Descriptor>& descriptors) {
  HotApplyPlan plan;
  for (const auto& [id, value] : changed) {
    const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                 [id](const Descriptor& d) { return d.id == id; });
    // An id this build does not describe is never applied live: a restart is the only honest
    // answer for a row whose strategy is unknown.
    const ApplicationStrategy strategy = it == descriptors.end()
        ? ApplicationStrategy::kRestart : it->strategy;
    if (strategy == ApplicationStrategy::kHot) {
      plan.live[id] = value;
    } else if (strategy == ApplicationStrategy::kMenuReturn) {
      plan.deferred_menu[id] = value;
    } else if (strategy == ApplicationStrategy::kNextSong) {
      plan.deferred_song[id] = value;
    } else {
      plan.pending_restart.push_back(id);
    }
  }
  return plan;
}

HotApplyPlan PlanHotApplyByStrategy(const std::map<SettingId, Value>& changed) {
  HotApplyPlan plan;
  for (const auto& [id, value] : changed) {
    const ApplicationStrategy strategy = schema::StrategyOf(id);
    if (strategy == ApplicationStrategy::kHot) {
      plan.live[id] = value;
    } else if (strategy == ApplicationStrategy::kMenuReturn) {
      plan.deferred_menu[id] = value;
    } else if (strategy == ApplicationStrategy::kNextSong) {
      plan.deferred_song[id] = value;
    } else {
      plan.pending_restart.push_back(id);
    }
  }
  return plan;
}

std::vector<HotApplyOutcome> ApplyHotPlan(const HotApplyPlan& plan, HotApplier* applier) {
  if (applier != nullptr) {
    // Domain handlers may complete a group with one outcome per row. Never let a
    // missing or duplicate acknowledgement turn a saved value into an apparent
    // success: the restart fallback must see every requested row exactly once.
    const std::vector<HotApplyOutcome> reported = applier->ApplyBatch(plan.live);
    std::map<SettingId, HotApplyOutcome> by_id;
    for (const HotApplyOutcome& outcome : reported) {
      if (!plan.live.contains(outcome.id)) continue;
      const auto [it, inserted] = by_id.emplace(outcome.id, outcome);
      if (!inserted) {
        it->second = {outcome.id, HotApplyStatus::kFailed,
                      "domain handler reported a duplicate completion"};
      }
    }
    std::vector<HotApplyOutcome> normalized;
    normalized.reserve(plan.live.size());
    for (const auto& [id, value] : plan.live) {
      (void)value;
      const auto found = by_id.find(id);
      normalized.push_back(found == by_id.end()
          ? HotApplyOutcome{id, HotApplyStatus::kNoApplier,
                            "domain handler did not acknowledge this row"}
          : std::move(found->second));
    }
    return normalized;
  }
  std::vector<HotApplyOutcome> outcomes;
  outcomes.reserve(plan.live.size());
  for (const auto& [id, value] : plan.live) {
    (void)value;
    HotApplyOutcome outcome;
    outcome.id = id;
    outcome.status = HotApplyStatus::kNoApplier;
    outcome.error = "no live applier for this row in this build";
    outcomes.push_back(std::move(outcome));
  }
  return outcomes;
}

std::vector<HotApplyOutcome> HotApplier::ApplyBatch(
    const std::map<SettingId, Value>& values) {
  std::vector<HotApplyOutcome> outcomes;
  outcomes.reserve(values.size());
  for (const auto& [id, value] : values) {
    HotApplyOutcome outcome;
    outcome.id = id;
    if (!Knows(id)) {
      outcome.status = HotApplyStatus::kNoApplier;
      outcome.error = "no live applier for this row in this build";
    } else {
      std::string error;
      if (Apply(id, value, &error)) {
        outcome.status = HotApplyStatus::kApplied;
      } else {
        outcome.status = error.empty() ? HotApplyStatus::kFailed : HotApplyStatus::kRefused;
        outcome.error = std::move(error);
      }
    }
    outcomes.push_back(std::move(outcome));
  }
  return outcomes;
}

std::vector<SettingId> RestartSetAfterApply(const HotApplyPlan& plan,
                                            const std::vector<HotApplyOutcome>& outcomes) {
  std::vector<SettingId> pending = plan.pending_restart;
  for (const HotApplyOutcome& outcome : outcomes) {
    if (outcome.status == HotApplyStatus::kApplied) {
      continue;
    }
    // The row is `kHot` in the schema but did not take effect in this build: it belongs in the
    // restart set, not in the "already applied" story the menu tells.
    if (std::find(pending.begin(), pending.end(), outcome.id) == pending.end()) {
      pending.push_back(outcome.id);
    }
  }
  return pending;
}

void AcknowledgeRunningValues(std::map<SettingId, Value>* running_values,
                              std::vector<SettingId>* forced_restart,
                              const std::map<SettingId, Value>& requested,
                              const std::vector<HotApplyOutcome>& outcomes) {
  if (running_values == nullptr || forced_restart == nullptr) return;
  for (const HotApplyOutcome& outcome : outcomes) {
    if (outcome.status != HotApplyStatus::kApplied) continue;
    const auto value = requested.find(outcome.id);
    if (value == requested.end()) continue;
    (*running_values)[outcome.id] = value->second;
    forced_restart->erase(std::remove(forced_restart->begin(), forced_restart->end(), outcome.id),
                          forced_restart->end());
  }
}

std::vector<SettingId> PendingRestartRows(const std::map<SettingId, Value>& running_values,
                                          const std::map<SettingId, Value>& current_values,
                                          const std::vector<Descriptor>& descriptors,
                                          const std::vector<SettingId>& forced) {
  // What really differs from what this process is running. A row the boot never saw (a descriptor
  // added by a capability change, say) cannot be compared and is skipped rather than guessed at.
  std::map<SettingId, Value> changed;
  for (const auto& [id, current] : current_values) {
    const auto running = running_values.find(id);
    if (running == running_values.end()) {
      continue;
    }
    if (!ValueEquals(current, running->second)) {
      changed[id] = current;
    }
  }
  if (changed.empty()) {
    return {};
  }
  const HotApplyPlan plan = PlanHotApply(changed, descriptors);
  std::vector<SettingId> pending = plan.pending_restart;
  // A row the live pass already failed on keeps its restart, but only while its value is still the
  // one the running process does not have: reverting it is the whole point of this function.
  for (const SettingId id : forced) {
    if (!changed.contains(id)) {
      continue;
    }
    if (std::find(pending.begin(), pending.end(), id) == pending.end()) {
      pending.push_back(id);
    }
  }
  return pending;
}

}  // namespace rb_blitz::settings
