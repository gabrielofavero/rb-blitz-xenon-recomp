// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for src/fs/refresh_state.h. The refresh's scan is host I/O and its publish
// needs a content manager, so neither is here - what is here are the two rules the two
// threads of an in-game refresh depend on, and the orderings that make them hold. No SDK,
// no boot.
//
// The concrete bug each case stands for:
//   * "one at a time" - without it a trigger that fires twice starts two 31-second scans
//     of the same tree (docs/plans/cancelled/2026-10-10-song-cache-plan.md §9.5).
//   * "published exactly once" - two guest threads reaching the publish point together
//     must not both hand the content manager a freshly moved-from vector.

#include "check.h"

#include "fs/refresh_state.h"

namespace {

using namespace rb_blitz::fs;
using namespace rb_blitz::test;

void TestOneAtATime() {
  BeginCase("a second ask while one runs is refused, and the key frees on Finish");

  RefreshState state;
  CHECK_FALSE(state.Running());
  CHECK_FALSE(state.Pending());

  CHECK_TRUE(state.Begin());
  CHECK_TRUE(state.Running());

  // The second ask, whatever thread it comes from, is refused: this is what stops a
  // second scan being queued while the first is still reading the tree.
  CHECK_FALSE(state.Begin());
  CHECK_FALSE(state.Begin());

  state.Finish();
  CHECK_FALSE(state.Running());
  CHECK_TRUE(state.Pending());

  // And a new refresh may be asked for as soon as the previous one finished, even before
  // its result has been published - the two are independent.
  CHECK_TRUE(state.Begin());
  CHECK_TRUE(state.Pending());
}

void TestPublishExactlyOnce() {
  BeginCase("a pending result is taken once, and only once");

  RefreshState state;
  // Nothing to publish yet: a guest thread that arrives before the worker finished must
  // not be able to claim anything.
  CHECK_FALSE(state.Pending());
  CHECK_FALSE(state.Take());

  CHECK_TRUE(state.Begin());
  // Still nothing while the scan runs.
  CHECK_FALSE(state.Pending());
  CHECK_FALSE(state.Take());

  state.Finish();
  CHECK_TRUE(state.Pending());
  CHECK_TRUE(state.Take());
  // The second thread at the same point gets nothing: the payload is already spoken for.
  CHECK_FALSE(state.Take());
  CHECK_FALSE(state.Pending());
  CHECK_FALSE(state.Take());
}

void TestFinishOrdering() {
  BeginCase("running clears after pending is set, so no result is published early");

  RefreshState state;
  CHECK_TRUE(state.Begin());

  // The invariant Finish() must keep: until it returns, a guest thread sees "running" and
  // not "pending" - it cannot publish a payload the worker has not stored yet. Finish()
  // is a single call here, so what is pinned is that the resulting state is (not running,
  // pending) and never (not running, not pending), which is the state that would let a
  // publish find an empty payload.
  state.Finish();
  CHECK_FALSE(state.Running());
  CHECK_TRUE(state.Pending());
  CHECK_FALSE(state.Running() || !state.Pending());

  // A taken result leaves the machine idle: a later Finish() cannot resurrect it.
  CHECK_TRUE(state.Take());
  CHECK_FALSE(state.Take());
  CHECK_FALSE(state.Pending());
}

}  // namespace

int main() {
  TestOneAtATime();
  TestPublishExactlyOnce();
  TestFinishOrdering();
  return Finish();
}
