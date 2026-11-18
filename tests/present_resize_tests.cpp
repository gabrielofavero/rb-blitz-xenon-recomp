// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "gpu/xenos/present_resize.h"

using rex::gpu::PresentResizeTracker;
using namespace rb_blitz::test;

int main() {
  BeginCase("present resize: first valid frame and stable frames retain caches");
  PresentResizeTracker tracker;
  CHECK_FALSE(tracker.Observe(1280, 540, true));
  CHECK_FALSE(tracker.Observe(1280, 540, true));
  CHECK_EQ(tracker.width(), 1280);
  CHECK_EQ(tracker.height(), 540);

  BeginCase("present resize: invalid dimensions cannot replace the last valid frame");
  CHECK_FALSE(tracker.Observe(0, 360, true));
  CHECK_FALSE(tracker.Observe(640, 0, true));
  CHECK_FALSE(tracker.Observe(0, 0, true));
  CHECK_EQ(tracker.width(), 1280);
  CHECK_EQ(tracker.height(), 540);
  CHECK_TRUE(tracker.Observe(640, 360, true));

  BeginCase("present resize: disabled cleanup still observes the current valid dimensions");
  CHECK_FALSE(tracker.Observe(960, 540, false));
  CHECK_FALSE(tracker.Observe(960, 540, true));
  CHECK_TRUE(tracker.Observe(1280, 540, true));
  CHECK_FALSE(tracker.Observe(1280, 540, true));

  BeginCase("present resize: each interface/gameplay transition queues cleanup once");
  for (int transition = 0; transition < 4; ++transition) {
    CHECK_TRUE(tracker.Observe(640, 360, true));
    CHECK_FALSE(tracker.Observe(640, 360, true));
    CHECK_TRUE(tracker.Observe(1280, 540, true));
    CHECK_FALSE(tracker.Observe(1280, 540, true));
  }

  BeginCase("present resize: command processor instances keep independent history");
  PresentResizeTracker other;
  CHECK_FALSE(other.Observe(640, 360, true));
  CHECK_FALSE(tracker.Observe(1280, 540, true));
  CHECK_TRUE(other.Observe(640, 480, true));
  CHECK_FALSE(tracker.Observe(1280, 540, true));

  BeginCase("present resize: invalid first frames do not arm cleanup");
  PresentResizeTracker empty;
  CHECK_FALSE(empty.Observe(0, 540, true));
  CHECK_FALSE(empty.Observe(1280, 0, false));
  CHECK_FALSE(empty.Observe(1280, 540, true));
  return Finish();
}
