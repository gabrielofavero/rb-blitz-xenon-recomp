// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstdint>
#include <string>

#include "check.h"
#include "messages/messages_guest_model.h"
#include "messages/messages_icons.h"

using namespace rb_blitz::messages;
namespace test = rb_blitz::test;

int main() {
  test::BeginCase("built-in icon ids map to the scene's row materials");
  CHECK_EQ(kBuiltInIcons.size(), 13);
  CHECK_TRUE(IsBuiltInIcon("icon_more_stars"));
  CHECK_TRUE(RowMaterial("icon_more_stars") == "icon_more_stars.mat");
  CHECK_TRUE(RowMaterial("icons_increase_rank") == "icon_increase_rank.mat");
  CHECK_TRUE(RowMaterial("icon_misc") == "icon_misc.mat");
  CHECK_TRUE(RowMaterial("icon_challenge") == "icon_misc.mat");
  CHECK_TRUE(RowMaterial("icons_unplayed_songs") == "icon_misc.mat");
  CHECK_FALSE(IsBuiltInIcon("icon_typo"));
  CHECK_TRUE(RowMaterial("icon_typo") == "icon_misc.mat");

  test::BeginCase("provider entry matches retail's 16-byte big-endian plain-text layout");
  constexpr std::array<std::uint8_t, 16> expected = {
      0x82, 0x80, 0xA5, 0x94, 0x11, 0x22, 0x33, 0x44,
      0, 0, 0, 1, 0, 0, 0, 0};
  const auto actual = EncodeEntry(0x8280A594, 0x11223344);
  CHECK_MEM_EQ(actual.data(), expected.data(), expected.size());

  test::BeginCase("date and title form the row, body remains separate");
  Message first{"welcome", "Welcome to Rock Band Blitz 0.1.0 :)", "Body", "2026-11-06"};
  first.notification = "Welcome";
  CHECK_TRUE(SubjectLine(first) == "2026-11-06  Welcome to Rock Band Blitz 0.1.0 :)");
  CHECK_TRUE(NotificationLine(first) == "2026-11-06  Welcome");
  Message empty{"messages-empty", "No messages", "Empty body", ""};
  CHECK_TRUE(SubjectLine(empty) == "No messages");
  CHECK_TRUE(NotificationLine(empty) == "No messages");

  test::BeginCase("guest object survives rebuild and changed content gets a new object");
  GuestObjectPool pool;
  CHECK_TRUE(pool.Find(first) == nullptr);
  pool.Remember(first, {0x11110000, 0x22220000, 0x33330000, 0x44440000});
  CHECK_TRUE(pool.Find(first) != nullptr);
  CHECK_EQ(pool.Find(first)->message, 0x11110000);
  CHECK_EQ(pool.Find(first)->subject_string, 0x22220000);
  CHECK_EQ(pool.Find(first)->body_chars, 0x33330000);
  CHECK_EQ(pool.FindByAddress(0x11110000)->notification_string, 0x44440000);
  CHECK_TRUE(pool.FindByAddress(0x99990000) == nullptr);
  Message second = first;
  second.notification = "New notification";
  CHECK_TRUE(pool.Find(second) == nullptr);
  pool.Remember(second, {0x55550000, 0x66660000, 0x77770000, 0x88880000});
  CHECK_EQ(pool.size(), 2);
  CHECK_EQ(pool.Find(first)->message, 0x11110000);
  CHECK_EQ(pool.Find(second)->message, 0x55550000);
  return test::Finish();
}
