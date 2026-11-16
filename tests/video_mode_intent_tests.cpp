// SPDX-License-Identifier: GPL-2.0-only
// Exercise the production dimension predicate with the real cvar registry.
#include "check.h"

#include <rex/graphics/video_mode_util.h>

REXCVAR_DEFINE_INT32(video_mode_width, 1280, "Test", "Guest width");
REXCVAR_DEFINE_INT32(video_mode_height, 720, "Test", "Guest height");

int main() {
  using namespace rb_blitz::test;
  using rex::graphics::video_mode_util::HasConfiguredVideoModeDimension;
  BeginCase("unset video dimensions retain the window/preset fallback");
  rex::cvar::ResetAllToDefaults();
  CHECK_FALSE(HasConfiguredVideoModeDimension("video_mode_width"));
  CHECK_FALSE(HasConfiguredVideoModeDimension("video_mode_height"));

  BeginCase("explicit native dimensions outrank a Retina logical window size");
  CHECK_TRUE(rex::cvar::SetFlagFromConfig("video_mode_width", "1280"));
  CHECK_TRUE(rex::cvar::SetFlagFromConfig("video_mode_height", "720"));
  CHECK_FALSE(rex::cvar::HasNonDefaultValue("video_mode_width"));
  CHECK_FALSE(rex::cvar::HasNonDefaultValue("video_mode_height"));
  CHECK_TRUE(HasConfiguredVideoModeDimension("video_mode_width"));
  CHECK_TRUE(HasConfiguredVideoModeDimension("video_mode_height"));

  BeginCase("16:10 native mode keeps both explicit dimensions authoritative");
  CHECK_TRUE(rex::cvar::SetFlagFromConfig("video_mode_width", "1152"));
  CHECK_TRUE(HasConfiguredVideoModeDimension("video_mode_width"));
  CHECK_TRUE(HasConfiguredVideoModeDimension("video_mode_height"));
  CHECK_EQ(rex::cvar::Query<int32_t>("video_mode_width"), 1152);
  CHECK_EQ(rex::cvar::Query<int32_t>("video_mode_height"), 720);

  BeginCase("reset returns dimensions to the unconfigured fallback");
  rex::cvar::ResetAllToDefaults();
  CHECK_FALSE(HasConfiguredVideoModeDimension("video_mode_width"));
  CHECK_FALSE(HasConfiguredVideoModeDimension("video_mode_height"));
  return Finish();
}
