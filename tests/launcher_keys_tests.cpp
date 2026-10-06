// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's own keys (docs/plans/launcher-plan.md A5, D6): the chord grammar, the defaults
// that are the behaviour A1 hardcoded, the two rules that cannot be read off the file (a chord
// before a bare key, a move repeating and a decision not), the `[nav]` round trip, and the focus
// ring's scaling. No SDL, no ImGui, no window: nav_bindings.cpp and focus_ring.h are the two places
// these rules live, and this is what holds them.

#include "check.h"

#include "focus_ring.h"
#include "nav_bindings.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::launcher;
using namespace rb_blitz::test;

using nav_bindings::Modifiers;
using nav_bindings::Presses;
using nav_bindings::Table;
using nav_bindings::Trigger;

// check.h compares integers, so strings get their own reporter that prints both sides.
void CheckString(const char* file, int line, const std::string_view actual,
                 const std::string_view expected, const char* actual_expr,
                 const char* expected_expr) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, std::string(actual_expr) + " == " + expected_expr + " failed\n         actual   " +
                      std::string(actual) + "\n         expected " + std::string(expected));
}

#define CHECK_STR_EQ(actual, expected) \
  CheckString(__FILE__, __LINE__, (actual), (expected), #actual, #expected)

// A frame in which exactly one key was pressed, with the modifiers that were held with it.
Presses WithKey(const std::string& name, const Modifiers& modifiers = {}) {
  Presses presses;
  presses.fresh.push_back(name);
  presses.repeating.push_back(name);
  presses.modifiers = modifiers;
  return presses;
}

Modifiers Ctrl() {
  Modifiers modifiers;
  modifiers.ctrl = true;
  return modifiers;
}

Modifiers Shift() {
  Modifiers modifiers;
  modifiers.shift = true;
  return modifiers;
}

Modifiers CtrlShift() {
  Modifiers modifiers;
  modifiers.ctrl = true;
  modifiers.shift = true;
  return modifiers;
}

void TestTriggerGrammar() {
  BeginCase("the chord grammar");
  const Trigger down = *nav_bindings::ParseTrigger("DownArrow");
  CHECK_STR_EQ(down.key, "DownArrow");
  CHECK_FALSE(down.ctrl || down.shift || down.alt);
  CHECK_STR_EQ(nav_bindings::FormatTrigger(down), "DownArrow");
  // A person may write it with spaces, in either case, or with the word on the key cap.
  CHECK_TRUE(down == *nav_bindings::ParseTrigger("down arrow"));
  CHECK_TRUE(down == *nav_bindings::ParseTrigger("down_arrow"));
  CHECK_TRUE(down == *nav_bindings::ParseTrigger("  downarrow  "));
  CHECK_FALSE(*nav_bindings::ParseTrigger("DownArrow") == *nav_bindings::ParseTrigger("UpArrow"));

  const Trigger chord = *nav_bindings::ParseTrigger("shift+Tab");
  CHECK_STR_EQ(chord.key, "Tab");
  CHECK_TRUE(chord.shift);
  CHECK_FALSE(chord.ctrl);
  // Written back canonically and in one order, whatever order it was read in.
  CHECK_STR_EQ(nav_bindings::FormatTrigger(chord), "Shift+Tab");
  CHECK_TRUE(chord == *nav_bindings::ParseTrigger("Shift+Tab"));
  const Trigger both = *nav_bindings::ParseTrigger("alt+ctrl+shift+f7");
  CHECK_STR_EQ(nav_bindings::FormatTrigger(both), "Ctrl+Shift+Alt+f7");

  // Half a binding is not a binding: a modifier with no key, an empty list, and a name that is a
  // device's rather than a key's (a pad press must not be able to move the ring).
  CHECK_FALSE(nav_bindings::ParseTrigger("Ctrl").has_value());
  CHECK_FALSE(nav_bindings::ParseTrigger("").has_value());
  CHECK_FALSE(nav_bindings::ParseTrigger("Ctrl+").has_value());
  CHECK_FALSE(nav_bindings::ParseTrigger("GamepadDpadDown").has_value());
  CHECK_FALSE(nav_bindings::ParseTrigger("MouseLeft").has_value());
  // A modifier is not a key under any of the names ImGui gives it: the generic four and the
  // physical left/right pairs. A capture that accepted "LeftCtrl" would bind Ctrl+LeftCtrl when
  // the user pressed Ctrl+S, which is what this catches.
  CHECK_FALSE(nav_bindings::ParseTrigger("LeftCtrl").has_value());
  CHECK_FALSE(nav_bindings::ParseTrigger("RightShift").has_value());
  CHECK_FALSE(nav_bindings::ParseTrigger("LeftAlt").has_value());
  CHECK_FALSE(nav_bindings::IsBindableName("LeftCtrl"));
  CHECK_FALSE(nav_bindings::IsBindableName("RightShift"));
  CHECK_FALSE(nav_bindings::IsBindableName("ModCtrl"));
  CHECK_FALSE(nav_bindings::ParseTrigger("Shift").has_value());
  CHECK_TRUE(nav_bindings::IsBindableName("F7"));
  CHECK_TRUE(nav_bindings::IsBindableName("DownArrow"));
}

void TestDefaultsAreWhatTheLauncherRead() {
  BeginCase("the defaults are the keys A1 hardcoded");
  const Table table;
  CHECK_EQ(table.ChangedCount(), 0u);
  // Every action has a row of its own, so the ones below are the source's own reads: Tab and Down
  // forward, Shift+Tab and Up back, Home and End, the arrows and the page keys for the tabs, Enter
  // and Space to activate, Escape and B to cancel.
  CHECK_STR_EQ(table.BindingText(NavAction::kNext), "Tab, Down");
  CHECK_STR_EQ(table.BindingText(NavAction::kPrevious), "Shift+Tab, Up");
  CHECK_STR_EQ(table.BindingText(NavAction::kFirst), "Home");
  CHECK_STR_EQ(table.BindingText(NavAction::kLast), "End");
  CHECK_STR_EQ(table.BindingText(NavAction::kNextTab), "Right, Page Down");
  CHECK_STR_EQ(table.BindingText(NavAction::kPreviousTab), "Left, Page Up");
  CHECK_STR_EQ(table.BindingText(NavAction::kActivate), "Enter, Keypad Enter, Space");
  CHECK_STR_EQ(table.BindingText(NavAction::kCancel), "Escape, B");
  // A5 adds the four the bar's buttons and the badge owned, so a keyboard alone can do them.
  CHECK_STR_EQ(table.BindingText(NavAction::kSave), "Ctrl+S");
  CHECK_STR_EQ(table.BindingText(NavAction::kCopyCommand), "Ctrl+C");
  CHECK_STR_EQ(table.BindingText(NavAction::kLaunch), "Ctrl+Enter");
  CHECK_STR_EQ(table.BindingText(NavAction::kCopyEffectiveValue), "Ctrl+Shift+C");
  // The panel's spelling and the file's are the same key with two names for the six the key cap
  // spells differently.
  CHECK_STR_EQ(nav_bindings::KeyLabel("DownArrow"), "Down");
  CHECK_STR_EQ(nav_bindings::KeyLabel("PageUp"), "Page Up");
  CHECK_STR_EQ(nav_bindings::KeyLabel("F7"), "F7");
}

void TestChordsBeforeBareKeys() {
  BeginCase("a chord is resolved before a bare key");
  const Table table;
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Tab")) == NavAction::kNext);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Tab", Shift())) == NavAction::kPrevious);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("DownArrow")) == NavAction::kNext);
  // A bare key is the key whatever is held with it: Shift+Down has always moved the ring forward.
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("DownArrow", Shift())) == NavAction::kNext);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("UpArrow")) == NavAction::kPrevious);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("RightArrow")) == NavAction::kNextTab);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("PageUp")) == NavAction::kPreviousTab);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Space")) == NavAction::kActivate);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("KeypadEnter")) == NavAction::kActivate);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Escape")) == NavAction::kCancel);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("B")) == NavAction::kCancel);
  // The four A5 added, and the case that makes the exact-modifier rule worth having: S on its own
  // is bound to nothing, so it does not save.
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("S", Ctrl())) == NavAction::kSave);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("S")) == NavAction::kNone);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("C", Ctrl())) == NavAction::kCopyCommand);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Enter", Ctrl())) == NavAction::kLaunch);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Enter")) == NavAction::kActivate);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("C", CtrlShift())) ==
             NavAction::kCopyEffectiveValue);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("F7")) == NavAction::kNone);

  Presses two;
  two.fresh = {"DownArrow", "UpArrow"};
  // One action per frame, and the order the actions are listed in is the tie-break: a move before
  // an activation, which is what the source A1 wrote did by checking the arrows first.
  CHECK_TRUE(nav_bindings::Resolve(table, two) == NavAction::kNext);
}

void TestRepeatIsTheActionsOwn() {
  BeginCase("a move repeats and a decision does not");
  CHECK_TRUE(nav_bindings::Repeats(NavAction::kNext));
  CHECK_TRUE(nav_bindings::Repeats(NavAction::kPrevious));
  CHECK_TRUE(nav_bindings::Repeats(NavAction::kNextTab));
  CHECK_TRUE(nav_bindings::Repeats(NavAction::kPreviousTab));
  CHECK_FALSE(nav_bindings::Repeats(NavAction::kActivate));
  CHECK_FALSE(nav_bindings::Repeats(NavAction::kCancel));
  CHECK_FALSE(nav_bindings::Repeats(NavAction::kSave));
  CHECK_FALSE(nav_bindings::Repeats(NavAction::kLaunch));

  const Table table;
  // Only the repeat of a held key: no fresh press anywhere.
  Presses held;
  held.repeating = {"DownArrow"};
  CHECK_TRUE(nav_bindings::Resolve(table, held) == NavAction::kNext);
  Presses held_activate;
  held_activate.repeating = {"Enter"};
  CHECK_TRUE(nav_bindings::Resolve(table, held_activate) == NavAction::kNone);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Enter")) == NavAction::kActivate);
  // Tab repeats now that the repeat is the action's rather than the key's: this is the one thing
  // A1's hardcoded source said and a rebindable table cannot say, and the README states it.
  Presses held_tab;
  held_tab.repeating = {"Tab"};
  CHECK_TRUE(nav_bindings::Resolve(table, held_tab) == NavAction::kNext);
}

void TestRebinding() {
  BeginCase("rebinding an action");
  Table table = Table::FromRows({});
  table.Set(NavAction::kNext, {*nav_bindings::ParseTrigger("J")});
  CHECK_EQ(table.ChangedCount(), 1u);
  CHECK_STR_EQ(table.BindingText(NavAction::kNext), "J");
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("J")) == NavAction::kNext);
  // Tab is no longer the forward key, and Reset brings back both keys it ships with.
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Tab")) == NavAction::kNone);
  table.Reset(NavAction::kNext);
  CHECK_EQ(table.ChangedCount(), 0u);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Tab")) == NavAction::kNext);

  // Unbound is a binding: the action has a row with nothing in it, which is what "I never want
  // this key to do that" looks like.
  table.Set(NavAction::kCancel, {});
  CHECK_EQ(table.ChangedCount(), 1u);
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Escape")) == NavAction::kNone);
  table.ResetAll();
  CHECK_TRUE(nav_bindings::Resolve(table, WithKey("Escape")) == NavAction::kCancel);
}

void TestNavRowsRoundTrip() {
  BeginCase("the [nav] table");
  // A profile nobody has rebound writes no table at all, and a save that would not change a byte
  // depends on that: it is why the harness's "the run wrote nothing but the window size" holds.
  CHECK_TRUE(Table{}.ToRows().empty());

  const std::vector<ProfileSetting> rows = {
      ProfileSetting{"next", "J", ValueStyle::kBasic},
      ProfileSetting{"cancel", "", ValueStyle::kBasic},
      ProfileSetting{"from_a_newer_launcher", "Hyper+Q", ValueStyle::kBasic},
  };
  const Table table = Table::FromRows(rows);
  CHECK_STR_EQ(table.BindingText(NavAction::kNext), "J");
  CHECK_STR_EQ(table.BindingText(NavAction::kCancel), "");
  // A row this build does not know is kept verbatim rather than dropped or half-read.
  const std::vector<ProfileSetting> written = table.ToRows();
  CHECK_EQ(written.size(), 3u);
  CHECK_STR_EQ(written[0].key, "next");
  CHECK_STR_EQ(written[0].value, "J");
  CHECK_STR_EQ(written[1].key, "cancel");
  CHECK_STR_EQ(written[1].value, "");
  CHECK_STR_EQ(written[2].key, "from_a_newer_launcher");
  CHECK_STR_EQ(written[2].value, "Hyper+Q");
  // ...and read back, the two this build knows are the same bindings.
  const Table again = Table::FromRows(written);
  CHECK_STR_EQ(again.BindingText(NavAction::kNext), "J");
  CHECK_TRUE(nav_bindings::Resolve(again, WithKey("Escape")) == NavAction::kNone);
  CHECK_TRUE(nav_bindings::Resolve(again, WithKey("J")) == NavAction::kNext);

  // A value with one unreadable token is kept whole: a half-applied list would silently drop the
  // one key a newer launcher meant.
  const Table mixed = Table::FromRows({ProfileSetting{"next", "J, Hyper+Q", ValueStyle::kBasic}});
  CHECK_EQ(mixed.ChangedCount(), 0u);
  CHECK_EQ(mixed.ToRows().size(), 1u);
  CHECK_STR_EQ(mixed.ToRows()[0].value, "J, Hyper+Q");
}

void TestActionNames() {
  BeginCase("the action names the profile spells");
  for (const NavAction action : nav_bindings::BindableActions()) {
    const std::string_view name = nav_bindings::ActionName(action);
    CHECK_FALSE(name.empty());
    // The profile's names are lower_snake and never one this build cannot read back.
    CHECK_TRUE(nav_bindings::ParseAction(name).has_value());
    CHECK_TRUE(*nav_bindings::ParseAction(name) == action);
    CHECK_FALSE(nav_bindings::ActionLabel(action).empty());
    CHECK_FALSE(nav_bindings::ActionHelp(action).empty());
  }
  CHECK_EQ(nav_bindings::BindableActions().size(), nav_bindings::kActionCount);
  CHECK_FALSE(nav_bindings::ParseAction("from_a_newer_launcher").has_value());
  CHECK_FALSE(nav_bindings::ParseAction("none").has_value());
}

void TestFocusRingScales() {
  BeginCase("the focus ring is the same ring at every DPI step");
  // The ring is derived from the font size, and the font size is what the display's content scale
  // multiplies - so the claim is that the outline's *proportion* to the text is the same at 100%,
  // 150%, 200% and 300%, which is what makes it neither a hairline nor a border:
  const double steps[] = {1.0, 1.5, 2.0, 3.0};
  const double base_font = 16.0;
  for (const double scale : steps) {
    const double font = base_font * scale;
    const double thickness = focus_ring::Thickness(static_cast<float>(font));
    const double padding = focus_ring::Padding(static_cast<float>(font));
    // An eighth of the face, at every step: the same shape at 2x as at 1x.
    CHECK_TRUE(std::abs(thickness - font / 8.0) < 0.001);
    CHECK_TRUE(std::abs(padding - thickness / 2.0) < 0.001);
    // ...and never thinner than a unit, so a face set very small still shows the ring.
    CHECK_TRUE(thickness >= 1.0);
  }
  // The floor is real: a 4-unit face would otherwise ask for half a unit of outline.
  CHECK_TRUE(std::abs(focus_ring::Thickness(4.0f) - 1.0f) < 0.001);
  // Rounding follows the style rather than deciding for it.
  CHECK_TRUE(std::abs(focus_ring::Rounding(3.0f) - 3.0f) < 0.001);
}

}  // namespace

// A3's pad, A2's help and A5's keys are one question asked three times - what moves the ring, what
// the bar says about where it landed, and which keys do either - so they share a test binary. Named
// rather than run from main() here, because a program has one main and the pad's checks have it.
void RunLauncherKeysChecks() {
  TestTriggerGrammar();
  TestDefaultsAreWhatTheLauncherRead();
  TestChordsBeforeBareKeys();
  TestRepeatIsTheActionsOwn();
  TestRebinding();
  TestNavRowsRoundTrip();
  TestActionNames();
  TestFocusRingScales();
}
