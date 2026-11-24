// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "ui/achievement_navigation.h"
#include "ui/achievement_layout.h"

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
  return Finish();
}
