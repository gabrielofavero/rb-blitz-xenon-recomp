// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R7's in-game refresh, as a state machine. The refresh itself lives in
// src/hooks/dlc.cpp; what is here is the part of it that has nothing to do with the SDK:
// *one refresh at a time*, and *a finished refresh is published exactly once*.
//
// Why those two rules are the whole point. The row that asks for a refresh is a guest
// file call, and the scan it starts reads every package in the library - measured 31 s
// on a 1,402-package library, which is 31 s of frozen frame if it runs on the thread that
// asked (docs/plans/finished/2026-10-10-song-cache-plan.md §9.5). So the scan runs on a worker and the frame
// keeps drawing. That splits the work across two threads and makes both rules load-bearing:
//
//   * "one at a time" - the trigger is the title's own `file_exists`, and nothing
//     guarantees it is asked once; without this, a second ask while the first scan runs
//     would queue a second scan of the same tree.
//   * "published exactly once" - the result is handed to the content manager from a
//     *guest* thread (the file-call hooks), because ContentManager's
//     `extra_content_library_` is a plain `std::vector` with no lock of its own
//     (rexglue-sdk/include/rex/system/xam/content_manager.h) and a worker writing it
//     while the guest lists or resolves content would be a data race. Two guest threads
//     can arrive at that publish point together, so the take has to be a decision, not a
//     check.
//
// SDK-free and thread-free on purpose: the caller owns the threads (a worker runs the
// scan and calls Finish(), a guest thread calls Take()), so this is testable without
// booting the game like the rest of src/fs - tests/refresh_state_tests.cpp pins it.

#pragma once

#include <atomic>

namespace rb_blitz::fs {

class RefreshState {
 public:
  // Claims the right to run a refresh. False when one is already running, which is the
  // caller's signal to ignore the ask (and say so once) rather than queue a second scan.
  bool Begin() {
    bool expected = false;
    return running_.compare_exchange_strong(expected, true);
  }

  // The scan is done and its result is ready to be published. Called once by the worker.
  // Not running is set *after* pending, so a guest thread that arrives in between sees
  // "still running" and waits for a result that is there, rather than one that is not.
  void Finish() {
    pending_ = true;
    running_ = false;
  }

  // True when a finished refresh is waiting for a guest thread. Cheap, and only a hint:
  // two threads can both see true, which is why Take() decides.
  bool Pending() const { return pending_.load(); }

  bool Running() const { return running_.load(); }

  // Claims the pending result. Exactly one caller gets true; the payload behind it is
  // then that caller's to move out and publish.
  bool Take() {
    bool expected = true;
    return pending_.compare_exchange_strong(expected, false);
  }

 private:
  std::atomic<bool> running_{false};
  std::atomic<bool> pending_{false};
};

}  // namespace rb_blitz::fs
