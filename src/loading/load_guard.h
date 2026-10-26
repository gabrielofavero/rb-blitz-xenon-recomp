// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace rb_blitz::loading {

enum class Policy : uint8_t { kOff, k30Seconds, k60Seconds };
enum class Phase : uint8_t {
  kIdle, kWatching, kAbortRequested, kCancelling, kReturning, kPopupPending, kUnavailable
};
enum class Reason : uint8_t { kNone, kTimeout, kSupportedError };
enum class Action : uint8_t { kNone, kCancel, kReturnToMenu, kShowPopup, kReportUnavailable };

// Historical cooperative-cancellation protocol retained for research/tests.
// The shipped timeout uses external restart (song_load_monitor / load_policy).
// SDK-free protocol, owned by the guest UI thread. Times are monotonic milliseconds.
// Actions are suggestions until their explicit acknowledgement. In particular a
// screen change is NOT a cleanup acknowledgement. The adapter must prove native
// cleanup before calling CleanedUp; it must never manufacture that event.
class LoadGuard {
 public:
  uint64_t Begin(Policy policy, int64_t now);
  void Completed(uint64_t generation);
  void ManualExit(uint64_t generation);
  void SupportedError(uint64_t generation);
  Action Poll(uint64_t generation, int64_t now, bool safe_to_cancel, bool main_menu_ready);
  bool CommitCancellation(uint64_t generation, int64_t now);
  void CleanedUp(uint64_t generation, int64_t now);
  bool CommitReturn(uint64_t generation);
  void PopupShown(uint64_t generation);
  // An explicit pause/resume signal rebases both the load and recovery deadlines.
  // Focus loss alone must not call this. A negative duration is ignored.
  void Rebase(uint64_t generation, int64_t paused_ms);

  uint64_t generation() const { return generation_; }
  Phase phase() const { return phase_; }
  Reason reason() const { return reason_; }
  Policy policy() const { return policy_; }

 private:
  bool Current(uint64_t generation) const { return generation != 0 && generation == generation_; }
  uint64_t generation_ = 0;
  Phase phase_ = Phase::kIdle;
  Reason reason_ = Reason::kNone;
  Policy policy_ = Policy::kOff;
  int64_t deadline_ = 0;
  int64_t recovery_deadline_ = 0;
  bool return_committed_ = false;
};

}  // namespace rb_blitz::loading
