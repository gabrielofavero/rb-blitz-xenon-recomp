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

HotApplyPlan PlanHotApply(const std::map<SettingId, Value>& changed,
                          const std::vector<Descriptor>& descriptors) {
  HotApplyPlan plan;
  for (const auto& [id, value] : changed) {
    const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                 [id](const Descriptor& d) { return d.id == id; });
    // An id this build does not describe is never applied live: a restart is the only honest
    // answer for a row whose strategy is unknown.
    const bool hot = it != descriptors.end() && it->strategy == ApplicationStrategy::kHot;
    if (hot) {
      plan.live[id] = value;
    } else {
      plan.pending_restart.push_back(id);
    }
  }
  return plan;
}

HotApplyPlan PlanHotApplyByStrategy(const std::map<SettingId, Value>& changed) {
  HotApplyPlan plan;
  for (const auto& [id, value] : changed) {
    if (schema::StrategyOf(id) == ApplicationStrategy::kHot) {
      plan.live[id] = value;
    } else {
      plan.pending_restart.push_back(id);
    }
  }
  return plan;
}

std::vector<HotApplyOutcome> ApplyHotPlan(const HotApplyPlan& plan, HotApplier* applier) {
  std::vector<HotApplyOutcome> outcomes;
  outcomes.reserve(plan.live.size());
  for (const auto& [id, value] : plan.live) {
    HotApplyOutcome outcome;
    outcome.id = id;
    if (applier == nullptr || !applier->Knows(id)) {
      outcome.status = HotApplyStatus::kNoApplier;
      outcome.error = "no live applier for this row in this build";
    } else {
      std::string error;
      if (applier->Apply(id, value, &error)) {
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

}  // namespace rb_blitz::settings
