// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "ui/achievement_navigation.h"
#include "ui/achievement_layout.h"
#include "ui/achievement_scroll.h"

int main() {
  using namespace rb_blitz::test;
  using namespace rb_blitz::ui::achievements;
  Navigation nav;
  BeginCase("opening and focus changes consume held controls until released");
  CHECK_TRUE(nav.Tick(kBack, true, .016f) == 0);
  CHECK_TRUE(nav.Tick(kDown, true, .016f) == 0);
  CHECK_TRUE(nav.Tick(0, true, .016f) == 0);
  CHECK_TRUE(nav.Tick(kDown, true, .016f) == kDown);
  CHECK_TRUE(nav.Tick(kDown, false, .016f) == 0);
  CHECK_TRUE(nav.Tick(kDown, true, .016f) == 0);
  nav.Tick(0, true, .016f);
  CHECK_TRUE(nav.Tick(kBack, true, .016f) == kBack);
  for (int i = 0; i < 20; ++i) CHECK_TRUE(nav.Tick(kBack, true, .1f) == 0);

  BeginCase("held directions repeat after delay");
  nav.Reset();
  nav.Tick(0, true, .016f);
  CHECK_TRUE(nav.Tick(kDown, true, .016f) == kDown);
  for (int i = 0; i < 3; ++i) CHECK_TRUE(nav.Tick(kDown, true, .1f) == 0);
  CHECK_TRUE(nav.Tick(kDown, true, .1f) == kDown);
  CHECK_TRUE(nav.Tick(kDown, true, .1f) == kDown);

  BeginCase("two-column navigation stays inside rows and reaches an odd last card");
  nav.Reset();
  nav.Move(kLeft | kUp, 5, 2); CHECK_TRUE(nav.selected == 0);
  nav.Move(kRight, 5, 2); CHECK_TRUE(nav.selected == 1);
  nav.Move(kRight, 5, 2); CHECK_TRUE(nav.selected == 1);
  nav.Move(kDown, 5, 2); CHECK_TRUE(nav.selected == 3);
  nav.Move(kDown, 5, 2); CHECK_TRUE(nav.selected == 4);
  nav.Move(kRight, 5, 2); CHECK_TRUE(nav.selected == 4);
  nav.Move(kDown, 5, 2); CHECK_TRUE(nav.selected == 4);
  nav.Move(kUp, 5, 2); CHECK_TRUE(nav.selected == 2);
  nav.Move(kLeft, 5, 2); CHECK_TRUE(nav.selected == 2);
  nav.Move(kFirst, 5, 2); CHECK_TRUE(nav.selected == 0);
  nav.Move(kLast, 5, 2); CHECK_TRUE(nav.selected == 4);
  nav.Move(kPageUp, 5, 2); CHECK_TRUE(nav.selected == 0);
  nav.Move(kPageDown, 5, 2); CHECK_TRUE(nav.selected == 4);
  nav.Move(kLast, 1, 2); CHECK_TRUE(nav.selected == 0);
  nav.Move(kRight, 4, 2); CHECK_TRUE(nav.selected == 1);
  nav.Move(kDown, 4, 2); CHECK_TRUE(nav.selected == 3);
  nav.Move(kDown, 4, 2); CHECK_TRUE(nav.selected == 3);
  nav.Move(kLeft, 4, 2); CHECK_TRUE(nav.selected == 2);
  nav.Move(kDown, 4, 2); CHECK_TRUE(nav.selected == 2);
  CHECK_TRUE(!nav.Move(kDown, 0, 2));

  BeginCase("portrait navigation visits every card and pages by single-column rows");
  nav.Reset();
  for (int i = 1; i < 5; ++i) {
    CHECK_TRUE(nav.Move(kDown, 5, 2, 1));
    CHECK_EQ(nav.selected, i);
  }
  CHECK_TRUE(!nav.Move(kDown, 5, 2, 1));
  CHECK_TRUE(!nav.Move(kLeft | kRight, 5, 2, 1));
  nav.Move(kPageUp, 5, 2, 1); CHECK_EQ(nav.selected, 2);
  nav.Move(kPageDown, 5, 2, 1); CHECK_EQ(nav.selected, 4);
  for (int i = 3; i >= 0; --i) {
    CHECK_TRUE(nav.Move(kUp, 5, 2, 1));
    CHECK_EQ(nav.selected, i);
  }
  CHECK_TRUE(!nav.Move(kUp, 5, 2, 1));
  nav.Move(kLast, 5, 2, 1); CHECK_EQ(nav.selected, 4);
  nav.Move(kFirst, 5, 2, 1); CHECK_EQ(nav.selected, 0);
  nav.Move(kRight, 5, 2, 2); CHECK_EQ(nav.selected, 1);
  nav.Move(kDown, 5, 2, 1); CHECK_EQ(nav.selected, 2);
  nav.Move(kDown, 5, 2, 2); CHECK_EQ(nav.selected, 4);

  BeginCase("portrait layout uses phone-sized type and respects device safe areas across DPI");
  for (const auto viewport : {rb_blitz::ui::PortraitViewport{320, 568},
                             {402, 874, 0, 62, 402, 778},
                             {540, 960}, {768, 1024, 20, 24, 728, 980}}) {
    for (const float dpi : {1.f, 2.f}) {
      const auto layout = LayoutBrowser(viewport.width * dpi, viewport.height * dpi, viewport);
      CHECK_TRUE(layout.portrait);
      CHECK_EQ(layout.columns, 1);
      CHECK_TRUE(18 * layout.scale >= 14 * dpi);
      CHECK_TRUE(layout.x > viewport.safe_x * dpi);
      CHECK_TRUE(layout.y > viewport.safe_y * dpi);
      const float safe_right = viewport.safe_width > 0 ?
          viewport.safe_x + viewport.safe_width : viewport.width;
      const float safe_bottom = viewport.safe_height > 0 ?
          viewport.safe_y + viewport.safe_height : viewport.height;
      CHECK_TRUE(layout.x + layout.width < safe_right * dpi);
      CHECK_TRUE(layout.y + layout.height < safe_bottom * dpi);
    }
  }
  BeginCase("landscape keeps its two-column sizing and absent safe areas fall back to the window");
  const auto landscape = LayoutBrowser(1280, 720);
  CHECK_EQ(landscape.columns, 2);
  CHECK_TRUE(landscape.scale == 1 && landscape.width == 1140);
  const auto phone = LayoutBrowser(402, 874);
  const auto empty_safe = LayoutBrowser(402, 874, {402, 874});
  CHECK_EQ(phone.width, empty_safe.width);
  CHECK_EQ(phone.height, empty_safe.height);

  BeginCase("achievement drags cover the whole viewport and preserve fractional pixel positions");
  const ScrollViewport viewport{10, 100, 390, 600, 400, 800, 400, 800};
  CardScroll scroll;
  scroll.Configure(viewport, 100.25f, 1000.75f, true);
  scroll.Observe(1, 1, ScrollPhase::kDown, .5f, .4f, 1);
  scroll.Observe(1, 1, ScrollPhase::kMove, .5f, .3f, 1.04);
  CHECK_TRUE(std::abs(scroll.Advance(1.04).value() - 180.25f) < .001f);
  scroll.Observe(1, 1, ScrollPhase::kUp, .5f, .3f, 1.05);
  CHECK_FALSE(scroll.TakeTap().has_value());
  CHECK_TRUE(scroll.Advance(1.10).value() > 180.25f);

  BeginCase("a touch catches achievement momentum without changing the selected card");
  scroll.Observe(1, 2, ScrollPhase::kDown, .5f, .4f, 1.10);
  const float caught = scroll.Advance(1.10).value();
  scroll.Observe(1, 2, ScrollPhase::kUp, .5f, .4f, 1.12);
  CHECK_FALSE(scroll.TakeTap().has_value());
  CHECK_TRUE(scroll.Advance(1.15).value() == caught);
  scroll.Observe(1, 3, ScrollPhase::kDown, .5f, .4f, 1.20);
  scroll.Observe(1, 3, ScrollPhase::kUp, .5f, .4f, 1.22);
  const auto tap = scroll.TakeTap();
  CHECK_TRUE(tap && tap->x == 200 && tap->y == 320);
  CHECK_FALSE(scroll.TakeTap().has_value());

  BeginCase("density changes preserve finger travel and eight-point tap slop");
  for (float density : {1.f, 2.f, 3.f}) {
    scroll.Reset();
    scroll.Configure({10 * density, 100 * density, 390 * density, 600 * density,
        400 * density, 800 * density, 400, 800}, 100.25f, 1000 * density, true);
    scroll.Observe(1, 1, ScrollPhase::kDown, .5f, .4f, 2);
    scroll.Observe(1, 1, ScrollPhase::kMove, .5f, .3f, 2.04);
    CHECK_TRUE(std::abs(scroll.Advance(2.04).value() - (100.25f + 80 * density)) < .001f);
    scroll.Reset();
    scroll.Configure({0, 0, 400 * density, 800 * density,
        400 * density, 800 * density, 400, 800}, 0, 1000, true);
    scroll.Observe(1, 1, ScrollPhase::kDown, .5f, .4f, 3);
    scroll.Observe(1, 1, ScrollPhase::kUp, .5f, .405f, 3.04);
    CHECK_TRUE(scroll.TakeTap().has_value());
  }

  BeginCase("touch scrolling reaches fractional boundaries and leaves the footer and scrollbar available");
  scroll.Reset();
  scroll.Configure(viewport, 10.5f, 1000.75f, true);
  scroll.Observe(1, 1, ScrollPhase::kDown, .5f, .4f, 4);
  scroll.Observe(1, 1, ScrollPhase::kMove, .5f, 1.2f, 4.04);
  CHECK_TRUE(scroll.Advance(4.04).value() == 0);
  scroll.Observe(1, 1, ScrollPhase::kMove, .5f, -2.f, 4.08);
  CHECK_TRUE(std::abs(scroll.Advance(4.08).value() - 1000.75f) < .001f);
  scroll.Observe(1, 1, ScrollPhase::kUp, .5f, -2.f, 4.09);
  CHECK_FALSE(scroll.TakeTap().has_value());
  for (const ScrollPoint outside : {ScrollPoint{.99f, .4f}, ScrollPoint{.5f, .9f}}) {
    scroll.Observe(1, 2, ScrollPhase::kDown, outside.x, outside.y, 4.2);
    CHECK_FALSE(scroll.TouchOwnsView());
    scroll.Observe(1, 2, ScrollPhase::kUp, outside.x, outside.y, 4.22);
    CHECK_FALSE(scroll.TakeTap().has_value());
  }

  BeginCase("a second finger neither changes the view nor inherits a released gesture");
  scroll.Reset();
  scroll.Configure(viewport, 100, 1000, true);
  scroll.Observe(1, 1, ScrollPhase::kDown, .5f, .4f, 5);
  scroll.Observe(1, 2, ScrollPhase::kDown, .6f, .4f, 5.01);
  scroll.Observe(1, 2, ScrollPhase::kMove, .6f, .2f, 5.02);
  CHECK_TRUE(scroll.Advance(5.02).value() == 100);
  scroll.Observe(1, 1, ScrollPhase::kUp, .5f, .4f, 5.04);
  CHECK_TRUE(scroll.TakeTap().has_value());
  scroll.Observe(1, 2, ScrollPhase::kMove, .6f, .1f, 5.06);
  scroll.Observe(1, 2, ScrollPhase::kUp, .6f, .1f, 5.08);
  CHECK_TRUE(scroll.Advance(5.08).value() == 100);
  CHECK_FALSE(scroll.TakeTap().has_value());

  BeginCase("focus, resize, navigation and closing discard achievement gestures and pending taps");
  for (int boundary = 0; boundary < 4; ++boundary) {
    scroll.Reset();
    scroll.Configure(viewport, 100, 1000, true);
    scroll.Observe(1, 1, ScrollPhase::kDown, .5f, .4f, 6);
    if (boundary == 0) scroll.Configure(viewport, 100, 1000, false);
    if (boundary == 1) { auto resized = viewport; resized.right -= 20; scroll.Configure(resized, 100, 1000, true); }
    if (boundary == 2) scroll.Cancel();
    if (boundary == 3) scroll.Reset();
    scroll.Observe(1, 1, ScrollPhase::kMove, .5f, .2f, 6.02);
    scroll.Observe(1, 1, ScrollPhase::kUp, .5f, .2f, 6.04);
    CHECK_FALSE(scroll.Advance(6.04).has_value());
    CHECK_FALSE(scroll.TakeTap().has_value());
  }
  BeginCase("focus recovery accepts a new finger even when the abandoned release never arrived");
  scroll.Reset();
  scroll.Configure(viewport, 100, 1000, true);
  scroll.Observe(1, 1, ScrollPhase::kDown, .5f, .4f, 7);
  scroll.Configure(viewport, 100, 1000, false);
  scroll.Configure(viewport, 100, 1000, true);
  scroll.Observe(1, 2, ScrollPhase::kDown, .5f, .4f, 8);
  scroll.Observe(1, 2, ScrollPhase::kUp, .5f, .4f, 8.02);
  CHECK_TRUE(scroll.TakeTap().has_value());
  return Finish();
}
