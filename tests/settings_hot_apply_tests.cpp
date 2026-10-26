// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P9's two host halves, tested without a boot:
//
//   * the §5.5 classification (src/settings/schema.cpp) and its use by the application seam
//     (src/settings/hot_apply.*), including the invariant the docs depend on - a `hot` row carries
//     the evidence that earned it, and today no row has any;
//   * the agreement between the schema's table and the provider adapter's own row table, which is
//     what keeps the two from drifting apart as rows move.
//
// The guest-side writes are not here on purpose: nothing in this build performs one yet (see the
// P9 handoff), and a test that pretended otherwise would be the "success message without a call"
// the plan forbids.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "settings/hot_apply.h"
#include "settings/schema.h"
#include "settings/ultimate_adapter.h"

// The check helpers live in rb_blitz::test; the short alias keeps the case bodies readable.
namespace test = rb_blitz::test;

namespace {

using rb_blitz::settings::ApplicationStrategy;
using rb_blitz::settings::CapabilitySnapshot;
using rb_blitz::settings::Descriptor;
using rb_blitz::settings::HotApplyOutcome;
using rb_blitz::settings::HotApplyPlan;
using rb_blitz::settings::HotApplyStatus;
using rb_blitz::settings::HotApplier;
using rb_blitz::settings::SettingId;
using rb_blitz::settings::Value;

// A test applier with one entry per behaviour, so every branch of `ApplyHotPlan` is reachable.
class ScriptedApplier : public HotApplier {
 public:
  bool Knows(SettingId id) const override { return known_.count(id) != 0; }

  bool Apply(SettingId id, const Value& value, std::string* error) override {
    (void)value;
    if (refused_.count(id) == 0) {
      // Not scripted to refuse: the write succeeds. (A row that is not `Knows` never reaches
      // here - the plan asks `Knows` first - so this is the "applied" branch.)
      return true;
    }
    // A refusal is a refusal whether or not the applier explains itself.
    *error = reasons_[id];
    return false;
  }

  void Know(SettingId id) { known_.insert(id); }
  void Refuse(SettingId id, std::string why) {
    known_.insert(id);
    refused_.insert(id);
    reasons_[id] = std::move(why);
  }

 private:
  std::set<SettingId> known_;
  std::set<SettingId> refused_;
  std::map<SettingId, std::string> reasons_;
};

Value Bool(bool value) {
  Value out;
  out.kind = rb_blitz::settings::ValueKind::kBool;
  out.boolean = value;
  return out;
}

Value Enum(std::string token) {
  Value out;
  out.kind = rb_blitz::settings::ValueKind::kEnum;
  out.text = std::move(token);
  return out;
}

// The rows the parent plan's §3 names as the ones people will ask about by name, so a future
// session cannot quietly promote one of them without the evidence column changing too. `kVsync` is
// deliberately NOT here: it is the one row with measured live evidence (R7's frame pacing,
// docs/guides/dlc.md), and its promotion is asserted below. `kRhythmVerse` (the store's master)
// joined on 2026-10-22: the owner pass's P9-2 made it `restart` because the surfaces the switch
// owns are drawn at boot, so re-promoting it needs a measurement of both of them changing live.
constexpr SettingId kRowsThatMustStayRestart[] = {
    SettingId::kBackgroundMusicNavigation,
    SettingId::kTarget,     SettingId::kHideHud,     SettingId::kCacheDlcLibrary,
    SettingId::kResolution, SettingId::kRenderer,   SettingId::kAntiAliasing,
    SettingId::kFiltering,  SettingId::kRhythmVerse, SettingId::kShowMessages, SettingId::kShowRecommendations,
};

// The hot set, by name, so a promotion is a deliberate edit to this list and not a side effect.
// The five RhythmVerse **filter** leaves joined it on 2026-10-20 (RhythmVerse plan P6): the store's
// applier replaces the in-process settings snapshot its workers read before every request, which
// is a live effect with its own evidence in `src/settings/schema.cpp`. The master is NOT here
// since the owner pass of 2026-10-22 (plan §9/P9-2): it became `restart` because the switch also
// owns what the boot drew - the main-menu row and the store's heading - which a live value would
// leave lying until the next start.
constexpr SettingId kRowsThatMustBeHot[] = {
    SettingId::kVsync,             SettingId::kSettingsLabels,  SettingId::kRhythmVerseGuitar,
    SettingId::kRhythmVerseBass,   SettingId::kRhythmVerseDrums, SettingId::kRhythmVerseVocals,
    SettingId::kRhythmVerseKeys,
};

void StrategyTableIsComplete() {
  test::BeginCase("strategy: every row carries a §5.5 classification");

  const std::vector<rb_blitz::settings::schema::SchemaRow>& rows =
      rb_blitz::settings::schema::Rows();
  CHECK_TRUE(rows.size() >= 30);
  int hot = 0;
  for (const rb_blitz::settings::schema::SchemaRow& row : rows) {
    const Descriptor& descriptor = row.descriptor;
    CHECK_TRUE(descriptor.id != SettingId::kUnknown);
    const bool valid = descriptor.strategy == ApplicationStrategy::kHot ||
                       descriptor.strategy == ApplicationStrategy::kRestart;
    CHECK_TRUE(valid);
    if (descriptor.strategy == ApplicationStrategy::kHot) {
      ++hot;
      // The one rule the docs rest on: `hot` is evidence, never a wish.
      CHECK_FALSE(descriptor.strategy_evidence.empty());
    } else {
      // A restart row with evidence would be a row whose measurement was recorded but whose
      // classification was not moved - which is exactly the drift this table exists to prevent.
      CHECK_TRUE(descriptor.strategy_evidence.empty());
    }
    // `restart_scoped()` is the convenience view and must agree with the source of truth.
    CHECK_EQ(descriptor.restart_scoped(),
             descriptor.strategy == ApplicationStrategy::kRestart);
  }

  // Exactly seven rows are hot today, and each is hot because something measured the effect, not
  // because its cvar was tagged reloadable (see src/settings/schema.cpp). A future session that
  // promotes another row must also record its evidence, and this expectation moves with it.
  CHECK_EQ(hot, 7);
  for (const SettingId id : kRowsThatMustBeHot) {
    const rb_blitz::settings::schema::SchemaRow* row = rb_blitz::settings::schema::FindRow(id);
    CHECK_TRUE(row != nullptr);
    if (row != nullptr) {
      CHECK_EQ(row->descriptor.strategy == ApplicationStrategy::kHot, true);
    }
  }
  // Each hot row's evidence names where its measurement is recorded, so the claim is checkable
  // rather than asserted: V-Sync's is the frame-pacing measurement in the DLC guide, Settings
  // Labels' is the boot that flipped the row and watched the caption go (docs/settings/status.md).
  {
    const auto* vsync = rb_blitz::settings::schema::FindRow(SettingId::kVsync);
    CHECK_TRUE(vsync != nullptr &&
               vsync->descriptor.strategy_evidence.find("docs/guides/dlc.md") !=
                   std::string::npos);
    const auto* labels = rb_blitz::settings::schema::FindRow(SettingId::kSettingsLabels);
    CHECK_TRUE(labels != nullptr &&
               labels->descriptor.strategy_evidence.find("docs/settings/status.md") !=
                   std::string::npos);
  }

  for (const SettingId id : kRowsThatMustStayRestart) {
    const rb_blitz::settings::schema::SchemaRow* row = rb_blitz::settings::schema::FindRow(id);
    CHECK_TRUE(row != nullptr);
    if (row != nullptr) {
      CHECK_EQ(row->descriptor.strategy == ApplicationStrategy::kRestart, true);
    }
  }
}

void SchemaAndAdapterAgree() {
  test::BeginCase("strategy: the schema table and the provider table agree");

  int compared = 0;
  for (const rb_blitz::settings::ultimate_adapter::RowBinding& binding :
       rb_blitz::settings::ultimate_adapter::RowBindings()) {
    const rb_blitz::settings::schema::SchemaRow* row =
        rb_blitz::settings::schema::FindRow(binding.id);
    CHECK_TRUE(row != nullptr);
    if (row == nullptr) {
      continue;
    }
    const ApplicationStrategy from_schema = row->descriptor.strategy;
    const ApplicationStrategy from_adapter =
        binding.strategy == rb_blitz::settings::ultimate_adapter::Strategy::kHot
            ? ApplicationStrategy::kHot
            : ApplicationStrategy::kRestart;
    CHECK_EQ(from_schema == from_adapter, true);
    // The two evidence strings are the same claim in two files; a divergence is a drift bug.
    CHECK_TRUE(std::string(binding.evidence) == row->descriptor.strategy_evidence);
    ++compared;
  }
  CHECK_EQ(compared, 17);  // the provider's bindings, per P6's table
}

void PlanSplitsByStrategy() {
  test::BeginCase("plan: a commit is split by each row's strategy");

  CapabilitySnapshot caps;
  const std::vector<Descriptor> descriptors =
      rb_blitz::settings::schema::BuildDescriptors(caps);

  // A commit of restart rows applies nothing now and prompts once.
  std::map<SettingId, Value> changed;
  changed[SettingId::kHideHud] = Bool(true);
  changed[SettingId::kResolution] = Bool(true);
  HotApplyPlan plan = rb_blitz::settings::PlanHotApply(changed, descriptors);
  CHECK_EQ(plan.size(), 2);
  CHECK_TRUE(plan.live.empty());
  CHECK_TRUE(plan.restart_needed());
  CHECK_FALSE(plan.empty());

  // The same session with V-Sync added: the one hot row is applied now and the other two wait.
  changed[SettingId::kVsync] = Bool(false);
  plan = rb_blitz::settings::PlanHotApply(changed, descriptors);
  CHECK_EQ(plan.size(), 3);
  CHECK_EQ(plan.live.size(), 1);
  CHECK_EQ(plan.pending_restart.size(), 2);
  CHECK_EQ(plan.live.count(SettingId::kVsync), 1);

  // A row this build does not describe is never applied live.
  std::map<SettingId, Value> unknown;
  unknown[SettingId::kShowSteak] = Bool(true);
  const std::vector<Descriptor> empty_descriptors;
  const HotApplyPlan unknown_plan =
      rb_blitz::settings::PlanHotApply(unknown, empty_descriptors);
  CHECK_TRUE(unknown_plan.live.empty());
  CHECK_EQ(unknown_plan.pending_restart.size(), 1);

  // No changes at all is not a session: nothing live, nothing pending, no prompt.
  const HotApplyPlan empty = rb_blitz::settings::PlanHotApply({}, descriptors);
  CHECK_TRUE(empty.empty());
  CHECK_FALSE(empty.restart_needed());
}

void ApplyReportsWhatHappened() {
  test::BeginCase("apply: applied, refused, failed and no-applier are all reported");

  HotApplyPlan plan;
  plan.live[SettingId::kVsync] = Bool(false);       // applied
  plan.live[SettingId::kHideHud] = Bool(true);      // the applier refuses the value
  plan.live[SettingId::kCacheDlcLibrary] = Bool(true); // the write fails
  plan.live[SettingId::kFiltering] = Bool(true);    // no applier at all
  plan.pending_restart.push_back(SettingId::kTarget);

  ScriptedApplier applier;
  applier.Know(SettingId::kVsync);
  applier.Refuse(SettingId::kHideHud, "settings.not_reloadable");
  applier.Refuse(SettingId::kCacheDlcLibrary, "");  // an empty reason is still a failure

  const std::vector<HotApplyOutcome> outcomes = rb_blitz::settings::ApplyHotPlan(plan, &applier);
  CHECK_EQ(outcomes.size(), 4);
  std::map<SettingId, HotApplyStatus> status;
  for (const HotApplyOutcome& outcome : outcomes) {
    status[outcome.id] = outcome.status;
  }
  CHECK_EQ(status[SettingId::kVsync] == HotApplyStatus::kApplied, true);
  CHECK_EQ(status[SettingId::kHideHud] == HotApplyStatus::kRefused, true);
  CHECK_EQ(status[SettingId::kCacheDlcLibrary] == HotApplyStatus::kFailed, true);
  CHECK_EQ(status[SettingId::kFiltering] == HotApplyStatus::kNoApplier, true);

  // Only the applied row leaves the restart set. Everything else - including the three hot rows
  // that did not take effect - keeps the restart the row honestly needs.
  const std::vector<SettingId> restart =
      rb_blitz::settings::RestartSetAfterApply(plan, outcomes);
  CHECK_EQ(restart.size(), 4);
  const auto contains = [&restart](SettingId id) {
    for (const SettingId candidate : restart) {
      if (candidate == id) {
        return true;
      }
    }
    return false;
  };
  CHECK_EQ(contains(SettingId::kVsync), false);
  CHECK_TRUE(contains(SettingId::kHideHud));
  CHECK_TRUE(contains(SettingId::kCacheDlcLibrary));
  CHECK_TRUE(contains(SettingId::kFiltering));
  CHECK_TRUE(contains(SettingId::kTarget));

  // A build with no applier wired at all: every hot row is reported, not silently applied.
  const std::vector<HotApplyOutcome> none =
      rb_blitz::settings::ApplyHotPlan(plan, nullptr);
  CHECK_EQ(none.size(), 4);
  for (const HotApplyOutcome& outcome : none) {
    CHECK_EQ(outcome.status == HotApplyStatus::kNoApplier, true);
  }
  CHECK_EQ(rb_blitz::settings::RestartSetAfterApply(plan, none).size(), 5);

  // The tokens are the two words the plan and the docs use.
  CHECK_TRUE(std::string(rb_blitz::settings::HotApplyStatusToken(HotApplyStatus::kApplied)) ==
             "applied");
  CHECK_TRUE(std::string(rb_blitz::settings::HotApplyStatusToken(HotApplyStatus::kNoApplier)) ==
             "no applier");
}

void PendingRestartIsValueBased() {
  test::BeginCase("pending: the prompt follows the values, not the session's history");

  CapabilitySnapshot caps;
  const std::vector<Descriptor> descriptors =
      rb_blitz::settings::schema::BuildDescriptors(caps);

  // What the boot is running, and what the store says now.
  std::map<SettingId, Value> boot;
  boot[SettingId::kHideHud] = Bool(false);
  boot[SettingId::kVsync] = Bool(true);
  boot[SettingId::kTarget] = Enum("common");
  std::map<SettingId, Value> now = boot;

  // Nothing changed: no prompt.
  CHECK_EQ(rb_blitz::settings::PendingRestartRows(boot, now, descriptors).size(), 0u);

  // A restart row changed: one restart owed.
  now[SettingId::kHideHud] = Bool(true);
  std::vector<SettingId> pending =
      rb_blitz::settings::PendingRestartRows(boot, now, descriptors);
  CHECK_EQ(pending.size(), 1u);
  if (pending.size() == 1) CHECK_TRUE(pending[0] == SettingId::kHideHud);

  // The same value put back the way the boot had it: nothing is owed any more. This is the case the
  // sticky "a restart-scoped change happened" bit could not tell apart from the one above.
  now[SettingId::kHideHud] = Bool(false);
  CHECK_EQ(rb_blitz::settings::PendingRestartRows(boot, now, descriptors).size(), 0u);

  // A hot row (V-Sync) never asks for a restart, however often it moves.
  now[SettingId::kVsync] = Bool(false);
  CHECK_EQ(rb_blitz::settings::PendingRestartRows(boot, now, descriptors).size(), 0u);
  now[SettingId::kVsync] = Bool(true);

  // The same for the second hot row, and it is the one the owner's request turns on: flipping
  // Settings Labels must not ask for a restart, because the caption is hidden on the next draw.
  boot[SettingId::kSettingsLabels] = Bool(true);
  now[SettingId::kSettingsLabels] = Bool(true);
  CHECK_EQ(rb_blitz::settings::PendingRestartRows(boot, now, descriptors).size(), 0u);
  now[SettingId::kSettingsLabels] = Bool(false);
  CHECK_EQ(rb_blitz::settings::PendingRestartRows(boot, now, descriptors).size(), 0u);
  {
    std::map<SettingId, Value> changed_labels;
    changed_labels[SettingId::kSettingsLabels] = Bool(false);
    const HotApplyPlan labels_plan =
        rb_blitz::settings::PlanHotApplyByStrategy(changed_labels);
    CHECK_EQ(labels_plan.live.count(SettingId::kSettingsLabels), 1);
    CHECK_FALSE(labels_plan.restart_needed());
  }

  // ...unless a live pass failed on it: then it is owed one while its value differs.
  now[SettingId::kVsync] = Bool(false);
  pending = rb_blitz::settings::PendingRestartRows(boot, now, descriptors,
                                                   {SettingId::kVsync});
  CHECK_EQ(pending.size(), 1u);
  if (pending.size() == 1) CHECK_TRUE(pending[0] == SettingId::kVsync);
  // And again: reverting clears even a failed live apply.
  now[SettingId::kVsync] = Bool(true);
  CHECK_EQ(rb_blitz::settings::PendingRestartRows(boot, now, descriptors, {SettingId::kVsync})
               .size(),
           0u);

  // A row the boot never saw cannot be compared, so it is not a pending restart.
  std::map<SettingId, Value> unseen;
  unseen[SettingId::kShowSteak] = Bool(true);
  CHECK_EQ(rb_blitz::settings::PendingRestartRows(boot, unseen, descriptors).size(), 0u);
}

void PlanByStrategyMatchesTheDescriptorPlan() {
  test::BeginCase("plan: the schema-driven split agrees with the descriptor-driven one");

  CapabilitySnapshot caps;
  const std::vector<Descriptor> descriptors = rb_blitz::settings::schema::BuildDescriptors(caps);
  std::map<SettingId, Value> changed;
  changed[SettingId::kVsync] = Bool(false);       // hot
  changed[SettingId::kHideHud] = Bool(true);      // restart
  changed[SettingId::kResolution] = Bool(true);   // restart

  const HotApplyPlan by_schema = rb_blitz::settings::PlanHotApplyByStrategy(changed);
  const HotApplyPlan by_descriptors =
      rb_blitz::settings::PlanHotApply(changed, descriptors);
  CHECK_EQ(by_schema.live.size(), by_descriptors.live.size());
  CHECK_EQ(by_schema.pending_restart.size(), by_descriptors.pending_restart.size());
  CHECK_EQ(by_schema.live.count(SettingId::kVsync), 1);
  CHECK_EQ(by_schema.pending_restart.size(), 2);
  CHECK_TRUE(by_schema.restart_needed());

  // An id this build does not describe is a restart, not a live apply.
  std::map<SettingId, Value> unknown;
  unknown[SettingId::kUnknown] = Bool(true);
  CHECK_TRUE(rb_blitz::settings::PlanHotApplyByStrategy(unknown).live.empty());

  // The strategy the runtime commit path reads is the same answer the table gives.
  CHECK_EQ(rb_blitz::settings::schema::StrategyOf(SettingId::kVsync) ==
               ApplicationStrategy::kHot,
           true);
  CHECK_EQ(rb_blitz::settings::schema::StrategyOf(SettingId::kTarget) ==
               ApplicationStrategy::kRestart,
           true);
  CHECK_EQ(rb_blitz::settings::schema::StrategyOf(SettingId::kUnknown) ==
               ApplicationStrategy::kRestart,
           true);
}

void StrategyTokensAreFrozen() {
  test::BeginCase("strategy: the two tokens are the plan's own words");
  CHECK_TRUE(std::string(rb_blitz::settings::ApplicationStrategyToken(
                 ApplicationStrategy::kHot)) == "hot");
  CHECK_TRUE(std::string(rb_blitz::settings::ApplicationStrategyToken(
                 ApplicationStrategy::kRestart)) == "restart");
  // The bridge's per-row strategy reader (P11's cutover) has one vocabulary to compare against.
  CHECK_TRUE(std::string(rb_blitz::settings::schema::FindRow(SettingId::kTarget)
                             ->descriptor.strategy_evidence)
                 .empty());
}

}  // namespace

int main() {
  StrategyTableIsComplete();
  SchemaAndAdapterAgree();
  PlanSplitsByStrategy();
  PlanByStrategyMatchesTheDescriptorPlan();
  ApplyReportsWhatHappened();
  PendingRestartIsValueBased();
  StrategyTokensAreFrozen();
  return test::Finish();
}
