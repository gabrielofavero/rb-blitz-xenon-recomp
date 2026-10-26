// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "loading/load_guard.h"
#include "loading/load_policy.h"
#include <limits>

using namespace rb_blitz::loading;
using namespace rb_blitz::test;

int main() {
  BeginCase("committed policy publication is strict and attempts keep their snapshot");
  CHECK_TRUE(PublishLoadGuardPolicy("30s"));
  const auto snapshot = CurrentLoadGuardSeconds();
  CHECK_EQ(snapshot, 30u);
  CHECK_FALSE(PublishLoadGuardPolicy("15s"));
  CHECK_EQ(CurrentLoadGuardSeconds(), 30u);
  CHECK_TRUE(PublishLoadGuardPolicy("60s"));
  CHECK_EQ(CurrentLoadGuardSeconds(), 60u);
  CHECK_EQ(snapshot, 30u);
  CHECK_TRUE(PublishLoadGuardPolicy("off"));
  CHECK_EQ(CurrentLoadGuardSeconds(), 0u);
  LoadGuard guard;
  BeginCase("Off never cancels, including supported errors");
  auto gen = guard.Begin(Policy::kOff, 100);
  guard.SupportedError(gen);
  CHECK_EQ(guard.Poll(gen, 1000000, true, true), Action::kNone);

  for (auto policy : {Policy::k30Seconds, Policy::k60Seconds}) {
    const int64_t deadline = policy == Policy::k30Seconds ? 30000 : 60000;
    BeginCase("absolute boundary, safe point, completion before commitment wins");
    gen = guard.Begin(policy, 100);
    CHECK_EQ(guard.Poll(gen, deadline + 99, true, false), Action::kNone);
    CHECK_EQ(guard.Poll(gen, deadline + 100, false, false), Action::kNone);
    CHECK_EQ(guard.phase(), Phase::kAbortRequested);
    CHECK_EQ(guard.Poll(gen, deadline + 101, true, false), Action::kCancel);
    guard.Completed(gen);
    CHECK_FALSE(guard.CommitCancellation(gen, deadline + 101));
    CHECK_EQ(guard.Poll(gen, deadline + 100000, true, false), Action::kNone);

    BeginCase("completion at expiry and indefinite wait-for-A stop the timer");
    gen = guard.Begin(policy, 0);
    guard.Completed(gen);
    CHECK_EQ(guard.Poll(gen, deadline, true, false), Action::kNone);
    CHECK_EQ(guard.Poll(gen, 1000000, true, false), Action::kNone);

    BeginCase("manual exit and same-screen reentry retire all stale events");
    gen = guard.Begin(policy, 0);
    guard.ManualExit(gen);
    CHECK_EQ(guard.Poll(gen, deadline, true, false), Action::kNone);
    const auto old = gen;
    gen = guard.Begin(policy, 100000);
    CHECK_TRUE(old != gen);
    guard.Completed(old);
    guard.ManualExit(old);
    guard.SupportedError(old);
    guard.CleanedUp(old, 100000);
    guard.Rebase(old, 1000000);
    CHECK_FALSE(guard.CommitCancellation(old, 100000));
    CHECK_FALSE(guard.CommitReturn(old));
    guard.PopupShown(old);
    CHECK_EQ(guard.Poll(old, 1000000, true, true), Action::kNone);
    CHECK_EQ(guard.phase(), Phase::kWatching);
    CHECK_EQ(guard.Poll(gen, 100000 + deadline, true, false), Action::kCancel);

    BeginCase("timeout/error race latches error and native completion cannot erase it");
    guard.SupportedError(gen);
    guard.SupportedError(gen);
    guard.Completed(gen);
    CHECK_EQ(guard.reason(), Reason::kSupportedError);
    CHECK_TRUE(guard.CommitCancellation(gen, 100000 + deadline));
    CHECK_FALSE(guard.CommitCancellation(gen, 100000 + deadline));
    guard.Completed(gen);
    guard.ManualExit(gen);
    CHECK_EQ(guard.phase(), Phase::kCancelling);
    CHECK_EQ(guard.Poll(gen, 100001 + deadline, true, true), Action::kNone);
    // A main menu picture alone must not acknowledge resource/callback cleanup.
    guard.CleanedUp(gen, 100002 + deadline);
    guard.CleanedUp(gen, 100003 + deadline);
    CHECK_EQ(guard.Poll(gen, 100004 + deadline, true, false), Action::kReturnToMenu);
    CHECK_TRUE(guard.CommitReturn(gen));
    CHECK_FALSE(guard.CommitReturn(gen));
    CHECK_EQ(guard.Poll(gen, 100005 + deadline, true, false), Action::kNone);
    CHECK_EQ(guard.Poll(gen, 100006 + deadline, true, true), Action::kShowPopup);
    CHECK_EQ(guard.Poll(gen, 100006 + deadline, true, false), Action::kNone);
    CHECK_EQ(guard.Poll(gen, 100006 + deadline, true, true), Action::kShowPopup);
    guard.PopupShown(gen);
    CHECK_EQ(guard.Poll(gen, 100007 + deadline, true, true), Action::kNone);

    BeginCase("native failed load already cleaned up skips a second cancellation");
    gen = guard.Begin(policy, 0);
    guard.SupportedError(gen);
    guard.Completed(gen);
    guard.CleanedUp(gen, 1);
    CHECK_FALSE(guard.CommitCancellation(gen, 2));
    CHECK_EQ(guard.Poll(gen, 2, false, false), Action::kReturnToMenu);
    CHECK_EQ(guard.Poll(gen, 3, false, true), Action::kShowPopup);
    guard.PopupShown(gen);

    BeginCase("unresponsive cancellation reports once without a successful popup");
    gen = guard.Begin(policy, 0);
    guard.Poll(gen, deadline, true, false);
    CHECK_TRUE(guard.CommitCancellation(gen, deadline));
    CHECK_EQ(guard.Poll(gen, deadline + 9999, true, true), Action::kNone);
    CHECK_EQ(guard.Poll(gen, deadline + 10000, true, true), Action::kReportUnavailable);
    guard.CleanedUp(gen, deadline + 10001);
    CHECK_EQ(guard.Poll(gen, deadline + 10002, true, true), Action::kNone);
    CHECK_EQ(guard.phase(), Phase::kUnavailable);

    BeginCase("a menu transition has the same bounded recovery budget");
    gen = guard.Begin(policy, 0);
    guard.SupportedError(gen);
    guard.CommitCancellation(gen, 1);
    guard.CleanedUp(gen, 9000);
    guard.CommitReturn(gen);
    CHECK_EQ(guard.Poll(gen, 10001, false, false), Action::kReportUnavailable);
    CHECK_EQ(guard.Poll(gen, 10002, false, true), Action::kNone);

    BeginCase("explicit pause rebases load and recovery clocks; focus has no event");
    gen = guard.Begin(policy, 0);
    guard.Rebase(gen, -100);
    guard.Rebase(gen, 200000);
    CHECK_EQ(guard.Poll(gen, deadline + 199999, true, false), Action::kNone);
    CHECK_EQ(guard.Poll(gen, deadline + 200000, true, false), Action::kCancel);
    guard.CommitCancellation(gen, deadline + 200000);
    guard.Rebase(gen, 100000);
    CHECK_EQ(guard.Poll(gen, deadline + 309999, true, false), Action::kNone);
    CHECK_EQ(guard.Poll(gen, deadline + 310000, true, false), Action::kReportUnavailable);
  }

  BeginCase("a new attempt snapshots its policy; old requests cannot change it");
  const auto old = guard.Begin(Policy::k30Seconds, 0);
  gen = guard.Begin(Policy::k60Seconds, 0);
  CHECK_EQ(guard.policy(), Policy::k60Seconds);
  CHECK_EQ(guard.Poll(gen, 30000, true, false), Action::kNone);
  guard.SupportedError(old);
  CHECK_EQ(guard.Poll(gen, 60000, true, false), Action::kCancel);

  BeginCase("large clocks and pause durations saturate instead of overflowing");
  gen = guard.Begin(Policy::k30Seconds, std::numeric_limits<int64_t>::max() - 100);
  guard.Rebase(gen, std::numeric_limits<int64_t>::max());
  CHECK_EQ(guard.Poll(gen, std::numeric_limits<int64_t>::max() - 1, true, false), Action::kNone);
  CHECK_EQ(guard.Poll(gen, std::numeric_limits<int64_t>::max(), true, false), Action::kCancel);

  BeginCase("invalid policy is Off and a late menu does not claim recovery");
  gen = guard.Begin(static_cast<Policy>(255), 0);
  CHECK_EQ(guard.policy(), Policy::kOff);
  CHECK_EQ(guard.Poll(gen, 100000, true, true), Action::kNone);
  gen = guard.Begin(Policy::k30Seconds, 0);
  guard.SupportedError(gen);
  guard.CleanedUp(gen, 0);
  CHECK_EQ(guard.Poll(gen, 10000, true, true), Action::kReportUnavailable);
  return Finish();
}
