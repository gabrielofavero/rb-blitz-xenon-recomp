// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include <rex/xenos/visible_region.h>
#include <limits>
using namespace rex::gpu::draw_state;
using namespace rb_blitz::test;
int main() {
  const VisibleRegion region{true, 1280, 720, 405, 32, 0x260, 0x10530};
  const VisibleRegionPass main{0x260, 0x10530, 1280, 1280, 720, 0, 0, false, false};
  BeginCase("main-frame scissor preserves the visible aperture and filtering apron");
  VisibleRegionRect rect{0, 0, 1280, 720};
  CHECK_TRUE(ClipToVisibleRegion(region, main, rect));
  CHECK_EQ(rect.x, 405u); CHECK_EQ(rect.width, 470u);
  CHECK_EQ(rect.y, 0u); CHECK_EQ(rect.height, 720u);

  BeginCase("left and right EDRAM tiles retain identical full-frame pixels");
  for (uint32_t tile_width : {320u, 640u, 1280u}) {
    for (uint32_t origin = 0; origin < 1280; origin += tile_width) {
      auto tile = main; tile.pitch = tile_width; tile.left = -double(origin);
      rect = {0, 0, tile_width, 720};
      ClipToVisibleRegion(region, tile, rect);
      for (uint32_t x = 0; x < tile_width; ++x) {
        const bool actual = x >= rect.x && x < rect.x + rect.width;
        const bool expected = x + origin >= 405 && x + origin < 875;
        CHECK_EQ(actual, expected);
      }
    }
  }

  BeginCase("existing scissors intersect the cutoff and remain stable at every multiplier");
  for (uint32_t offset = 0; offset < 1280; offset += 17) {
    auto tile = main;
    const auto width = std::min(256u, 1280 - offset);
    rect = {offset, 0, width, 720};
    const auto original = rect;
    ClipToVisibleRegion(region, tile, rect);
    // Tiny scissors are intentionally preserved for guest helper/clear draws.
    for (uint32_t x = 0; x < 1280; ++x) {
      const bool original_inside = x >= original.x && x < original.x + original.width;
      const bool expected = original_inside && (width < 64 || (x >= 405 && x < 875));
      CHECK_EQ(x >= rect.x && x < rect.x + rect.width, expected);
    }
    for (uint32_t scale : {1u, 2u, 3u, 4u}) {
      CHECK_TRUE(uint64_t(rect.x + rect.width) * scale <= uint64_t(original.x + original.width) * scale);
    }
  }

  BeginCase("auxiliary targets and side-effecting draws retain their original scissors");
  auto unchanged = [&](VisibleRegion policy, VisibleRegionPass pass,
                       VisibleRegionRect input = {0, 0, 1280, 720}) {
    auto output = input;
    CHECK_TRUE(!ClipToVisibleRegion(policy, pass, output));
    CHECK_EQ(output.x, input.x); CHECK_EQ(output.width, input.width);
    CHECK_EQ(output.y, input.y); CHECK_EQ(output.height, input.height);
  };
  auto pass = main; pass.width = pass.height = 512; unchanged(region, pass); // shadow
  pass = main; pass.pitch = 320; pass.width = 320; pass.height = 180;
  unchanged(region, pass, {0, 0, 320, 180}); // bloom
  pass = main; pass.color_info = 0; unchanged(region, pass);
  pass = main; pass.depth_info = 0; unchanged(region, pass);
  pass = main; pass.side_effects = true; unchanged(region, pass); // memexport/query
  pass = main; pass.left = std::numeric_limits<double>::quiet_NaN(); unchanged(region, pass);
  unchanged(region, main, {0, 0, 16, 16}); // helper clear
  auto disabled = region; disabled.enabled = false; unchanged(disabled, main);
  disabled = region; disabled.visible_width = 0; unchanged(disabled, main);
  disabled = region; disabled.visible_width = 1280; unchanged(disabled, main);

  BeginCase("only full-size screen-space composition can use the main-frame aperture");
  pass = main; pass.screen_space = true;
  rect = {0, 0, 1280, 720};
  CHECK_TRUE(ClipToVisibleRegion(region, pass, rect));
  CHECK_EQ(rect.x, 405u); CHECK_EQ(rect.width, 470u);
  unchanged(region, pass, {0, 0, 320, 180});
  unchanged(region, pass, {0, 0, 16, 16});
  return Finish();
}
