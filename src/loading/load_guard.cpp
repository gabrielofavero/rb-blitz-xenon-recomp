// SPDX-License-Identifier: GPL-2.0-only
#include "loading/load_guard.h"

#include <limits>

namespace rb_blitz::loading {
namespace {
int64_t Add(int64_t now, int64_t duration) {
  return now > std::numeric_limits<int64_t>::max() - duration
             ? std::numeric_limits<int64_t>::max() : now + duration;
}
}

uint64_t LoadGuard::Begin(Policy policy, int64_t now) {
  if (++generation_ == 0) ++generation_;
  if (policy != Policy::k30Seconds && policy != Policy::k60Seconds) policy = Policy::kOff;
  policy_ = policy;
  reason_ = Reason::kNone;
  return_committed_ = false;
  recovery_deadline_ = 0;
  phase_ = policy == Policy::kOff ? Phase::kIdle : Phase::kWatching;
  deadline_ = Add(now, policy == Policy::k30Seconds ? 30000 : 60000);
  return generation_;
}

void LoadGuard::Completed(uint64_t generation) {
  if (!Current(generation)) return;
  if ((phase_ == Phase::kWatching || phase_ == Phase::kAbortRequested) &&
      reason_ != Reason::kSupportedError) {
    phase_ = Phase::kIdle;
    reason_ = Reason::kNone;
  }
}

void LoadGuard::ManualExit(uint64_t generation) {
  if (!Current(generation)) return;
  // Once automatic cancellation is committed, a departure is part of its
  // protocol, not evidence that its callbacks/resources have been retired.
  if (phase_ == Phase::kWatching || phase_ == Phase::kAbortRequested) {
    phase_ = Phase::kIdle;
    reason_ = Reason::kNone;
  }
}

void LoadGuard::SupportedError(uint64_t generation) {
  if (!Current(generation) || policy_ == Policy::kOff) return;
  if (phase_ == Phase::kWatching || phase_ == Phase::kAbortRequested) {
    reason_ = Reason::kSupportedError;
    phase_ = Phase::kAbortRequested;
  }
}

Action LoadGuard::Poll(uint64_t generation, int64_t now, bool safe_to_cancel,
                       bool main_menu_ready) {
  if (!Current(generation)) return Action::kNone;
  if (phase_ == Phase::kWatching && now >= deadline_) {
    reason_ = Reason::kTimeout;
    phase_ = Phase::kAbortRequested;
  }
  if (phase_ == Phase::kAbortRequested)
    return safe_to_cancel ? Action::kCancel : Action::kNone;
  if (phase_ == Phase::kPopupPending)
    return main_menu_ready ? Action::kShowPopup : Action::kNone;
  if (phase_ == Phase::kCancelling || phase_ == Phase::kReturning) {
    if (now >= recovery_deadline_) {
      phase_ = Phase::kUnavailable;
      return Action::kReportUnavailable;
    }
    if (phase_ == Phase::kReturning && main_menu_ready) {
      phase_ = Phase::kPopupPending;
      return Action::kShowPopup;
    }
    if (phase_ == Phase::kReturning && !return_committed_)
      return Action::kReturnToMenu;
  }
  return Action::kNone;
}

bool LoadGuard::CommitCancellation(uint64_t generation, int64_t now) {
  if (!Current(generation) || phase_ != Phase::kAbortRequested) return false;
  phase_ = Phase::kCancelling;
  recovery_deadline_ = Add(now, 10000);
  return true;
}

void LoadGuard::CleanedUp(uint64_t generation, int64_t now) {
  if (!Current(generation)) return;
  // Supported failures may return through the native error path before a
  // cancellation was necessary. They still owe the menu/popup outcome.
  if (phase_ == Phase::kCancelling ||
      (phase_ == Phase::kAbortRequested && reason_ == Reason::kSupportedError)) {
    if (phase_ != Phase::kCancelling) recovery_deadline_ = Add(now, 10000);
    phase_ = Phase::kReturning;
  }
}

bool LoadGuard::CommitReturn(uint64_t generation) {
  if (!Current(generation) || phase_ != Phase::kReturning || return_committed_) return false;
  return_committed_ = true;
  return true;
}

void LoadGuard::PopupShown(uint64_t generation) {
  if (Current(generation) && phase_ == Phase::kPopupPending) phase_ = Phase::kIdle;
}

void LoadGuard::Rebase(uint64_t generation, int64_t paused_ms) {
  if (!Current(generation) || paused_ms <= 0) return;
  deadline_ = Add(deadline_, paused_ms);
  recovery_deadline_ = Add(recovery_deadline_, paused_ms);
}
}  // namespace rb_blitz::loading
