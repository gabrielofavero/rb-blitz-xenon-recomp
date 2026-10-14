// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "ui/content_progress.h"

using namespace rb_blitz::content_progress;
using namespace rb_blitz::test;

int main() {
  BeginCase("empty, single, structured, mixed and large enumerators use their actual sizes");
  for (uint32_t total : {0u, 1u, 21u, 1423u, 100000u}) {
    Model model;
    for (uint32_t i = 0; i < total; ++i) {
      model.Enumerated(4, total, kSuccess);
      CHECK_EQ(model.Get().current, i + 1);
      CHECK_EQ(model.Get().total, total);
      CHECK_EQ(model.Get().determinate(), i + 1 < total);
    }
    model.Enumerated(4, total, kNoMoreFiles);
    CHECK_EQ(model.Get().successful, total);
    CHECK_TRUE(model.Get().active());
    CHECK_FALSE(model.Get().determinate());
    model.Finalizing(); // metadata parsing/cache persistence are still pending
    CHECK_TRUE(model.Get().active());
    CHECK_TRUE(model.MenuReady());
    CHECK_FALSE(model.MenuReady());
    CHECK_FALSE(model.Get().active());
  }
  BeginCase("failed and pending calls never count as successful packages");
  Model model;
  model.Enumerated(4, 3, kPending);
  model.Enumerated(4, 3, 0x80070057);
  CHECK_EQ(model.Get().current, 0);
  CHECK_EQ(model.Get().successful, 0);
  CHECK_EQ(model.Get().failures, 1);
  model.Enumerated(4, 3, kSuccess);
  CHECK_EQ(model.Get().current, 1);
  model.Enumerated(4, 3, kNoMoreFiles);
  CHECK_EQ(model.Get().current, 1);
  CHECK_FALSE(model.Get().determinate());
  BeginCase("each enumerator is a separate phase, including a reused handle");
  model.Enumerated(4, 2, kSuccess);
  CHECK_EQ(model.Get().batch, 2);
  CHECK_EQ(model.Get().current, 1);
  model.Enumerated(5, 100, kSuccess);
  CHECK_EQ(model.Get().batch, 3);
  CHECK_EQ(model.Get().current, 1);
  model.Enumerated(4, 2, kSuccess);
  CHECK_EQ(model.Get().batch, 2);
  CHECK_EQ(model.Get().current, 2);
  model.Enumerated(4, 2, kNoMoreFiles);
  CHECK_EQ(model.Get().current, 2);
  model.Enumerated(4, 2, kNoMoreFiles);
  CHECK_EQ(model.Get().batch, 2);
  model.MenuReady();
  BeginCase("a new attempt resets every count");
  model.Begin();
  CHECK_EQ(model.Get().attempt, 2);
  CHECK_EQ(model.Get().batch, 0);
  CHECK_EQ(model.Get().successful, 0);
  CHECK_EQ(model.Get().failures, 0);
  BeginCase("unknown totals never invent a percentage");
  model.Enumerated(6, 0, kSuccess);
  CHECK_EQ(model.Get().successful, 1);
  CHECK_FALSE(model.Get().determinate());
  return Finish();
}
