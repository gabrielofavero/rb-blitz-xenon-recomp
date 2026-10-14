// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <map>

namespace rb_blitz::content_progress {

// A batch is one real XAM aggregate enumerator, including every source the SDK
// put in it. Its size is not the number of packages in the host's flat cache.
// Finishing a batch does not finish discovery: the guest may mount/parse/save
// afterwards, or start another batch. Only the usable menu ends the attempt.
enum class Phase { kIdle, kEnumerating, kFinalizing, kComplete };
inline constexpr uint32_t kSuccess = 0;
inline constexpr uint32_t kNoMoreFiles = 0x80070012;
inline constexpr uint32_t kPending = 997;

struct Progress {
  Phase phase = Phase::kIdle;
  uint32_t attempt = 0;
  uint32_t batch = 0;
  uint32_t current = 0;
  uint32_t total = 0;
  uint64_t successful = 0;
  uint64_t failures = 0;

  bool active() const { return phase != Phase::kIdle && phase != Phase::kComplete; }
  bool determinate() const {
    return phase == Phase::kEnumerating && total != 0 && current < total;
  }
};

class Model {
 public:
  const Progress& Get() const { return progress_; }
  void Begin() {
    const auto attempt = progress_.attempt + 1;
    progress_ = {};
    progress_.attempt = attempt;
    progress_.phase = Phase::kFinalizing;
    batches_.clear();
    next_batch_ = 0;
  }

  void Enumerated(uint32_t handle, uint32_t total, uint32_t result) {
    if (!progress_.active()) Begin();
    auto& batch = batches_[handle];
    if (batch.number == 0 || (batch.ended && result == kSuccess)) {
      batch = {++next_batch_, 0, total, false};
    }
    progress_.batch = batch.number;
    progress_.current = batch.current;
    progress_.total = batch.total;
    if (result == kPending) return;  // a submission is not a completion
    if (result == kNoMoreFiles) {
      batch.ended = true;
      progress_.phase = Phase::kFinalizing;
      return;
    }
    if (result != kSuccess) {
      ++progress_.failures;
      progress_.phase = Phase::kFinalizing;
      return;
    }
    ++progress_.successful;
    if (batch.current < batch.total) ++batch.current;
    progress_.current = batch.current;
    progress_.phase = progress_.current < progress_.total ? Phase::kEnumerating
                                                        : Phase::kFinalizing;
  }

  void Finalizing() {
    if (progress_.active()) progress_.phase = Phase::kFinalizing;
  }
  bool MenuReady() {
    if (!progress_.active()) return false;
    progress_.phase = Phase::kComplete;
    return true;
  }

 private:
  Progress progress_;
  struct Batch { uint32_t number = 0, current = 0, total = 0; bool ended = false; };
  std::map<uint32_t, Batch> batches_;
  uint32_t next_batch_ = 0;
};

}  // namespace rb_blitz::content_progress
