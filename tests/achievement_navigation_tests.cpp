// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "ui/achievement_navigation.h"

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
  return Finish();
}
