// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's own keys (docs/plans/launcher-plan.md A5, D6).
//
// D6 gave the launcher a keyboard and A3 gave it a pad; this is what makes the keyboard
// recoverable. Which key does what is the profile's `[nav]` table, so a user whose hand is not on
// the arrow keys - or who has bound the ring to a key their keyboard does not have - can say so
// rather than being locked out of the window that would let them fix it.
//
// The shape is deliberately the remap table's (src/launcher/remap.h): a row per action, a value
// that is a comma-separated list, an unknown row kept verbatim so a newer launcher's work survives
// a round trip through this one, and defaults that are exactly the behaviour the launcher had
// before any of this existed.
//
// No ImGui and no SDL: a key is a *name* here, and what a name means is the shell's business
// (launcher/src/nav_keys.h resolves one against ImGui's own key table). That is what lets every
// rule below - the chord precedence, the repeat rule, the round trip through the file - be a test
// rather than a screenshot.
//
// Two rules are worth stating where they are implemented rather than only in the README:
//
//   * A *chord* (a trigger that names Ctrl, Shift or Alt) has to match the held modifiers exactly
//     and is resolved before any bare key. That is what keeps `Shift+Tab` meaning "back" while
//     `Tab` means "forward", and what lets `Ctrl+S` save on a keyboard where `S` is bound to
//     nothing at all. A bare key, on the other hand, is the key whatever else is held with it:
//     `Shift+Down` has always moved the ring.
//   * A *move* repeats while it is held and a discrete action does not, so the repeat is a
//     property of the action rather than of the key that happens to be bound to it. This is the
//     one thing a rebindable table cannot keep from A1's hardcoded source, which gave Tab no
//     repeat while the arrows had one; a held Tab now walks the ring like a held arrow.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "launcher/profile.h"
#include "nav.h"

namespace rb_blitz::launcher::nav_bindings {

// One key and the modifiers it is pressed with: `Ctrl+Shift+Tab`. `key` is a name, not a key -
// "Tab", "DownArrow", "F7" - and the spelling this module writes is ImGui's own (nav_keys.h has
// the reverse lookup), so the profile and a `--focus-log` line name a key the same way.
struct Trigger {
  std::string key;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};

bool operator==(const Trigger& left, const Trigger& right);
inline bool operator!=(const Trigger& left, const Trigger& right) { return !(left == right); }

// "Ctrl+Shift+Tab". Nothing for text this build cannot read as a chord - an empty key, or a
// modifier with nothing after it - which is what lets a newer launcher's binding survive a round
// trip through an older one.
std::optional<Trigger> ParseTrigger(std::string_view text);
std::string FormatTrigger(const Trigger& trigger);

// The same chord as a person reads it: "Down" rather than "DownArrow", "Page Up" rather than
// "PageUp", "Ctrl+S". Two spellings for one key on purpose - the file's is ImGui's own name, so
// one lookup resolves it, and the panel's is the words on the key cap.
std::string_view KeyLabel(std::string_view name);
std::string TriggerLabel(const Trigger& trigger);

// The modifiers a frame is holding, which is half of what a chord is matched against.
struct Modifiers {
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};

// What the keyboard saw this frame: the keys with a fresh press, the keys whose press is being
// repeated because they are held, and the modifiers that are down. Two lists rather than one
// because the repeat belongs to the action (see the header comment) and the action is not known
// until a trigger has matched.
struct Presses {
  std::vector<std::string> fresh;      // IsKeyPressed(key, repeat=false)
  std::vector<std::string> repeating;  // IsKeyPressed(key, repeat=true)
  Modifiers modifiers;
};

// True when `name` is in `keys`, comparing names the way ParseTrigger does: case, spaces and
// underscores are not part of a key's identity, so a hand-edited file may spell "down arrow",
// "DownArrow" or "down_arrow" and mean the same key.
bool Holds(const std::vector<std::string>& keys, std::string_view name);
bool NameMatches(std::string_view left, std::string_view right);

// True when a name is one a binding may use: a real key, and not a modifier (Ctrl is something
// held with a key, never a key on its own) and not one of the pad's or the mouse's ImGui keys -
// both are devices with their own source (D6), and a binding that quietly matched a pad press
// would be the pad moving the ring behind --no-gamepad's back.
bool IsBindableName(std::string_view name);

// Every action the `[nav]` table can bind, in the order a frame resolves two keys pressed
// together by: the moves, then the tab switches, then the three that end a session. `kNone` is
// not one of them: a binding says what a key does, never that it does nothing.
//
// How many that is, for a caller that has to size a focus ring before it can ask - the General
// tab's block counts its rows off this. A static_assert in the implementation is what keeps the
// number and the table from drifting apart.
const std::vector<NavAction>& BindableActions();
inline constexpr std::size_t kActionCount = 12;

// The name the profile spells an action as ("next", "next_tab", "copy_command") and the words the
// panel shows ("Next row"). ParseAction answers nothing for an action this build does not know.
std::string_view ActionName(NavAction action);
std::optional<NavAction> ParseAction(std::string_view name);
std::string_view ActionLabel(NavAction action);

// The sentence the bottom bar shows for the action's row (A2), and the keys it has out of the
// box, as a person reads them: "Tab, Down".
std::string_view ActionHelp(NavAction action);
std::string DefaultBindingText(NavAction action);

// True when a held key may repeat this action, and when the action is one the bottom bar or the
// precedence badge owns rather than one that moves the ring (A5 binds those so that a keyboard
// alone can start the game, save, read the command line, and take the value the game's own file
// decides - the four things that were mouse-only).
bool Repeats(NavAction action);
bool IsBarAction(NavAction action);

// The `[nav]` table of the profile.
class Table {
 public:
  // The defaults: every action bound to what the launcher's own source has always read.
  Table();

  // From the profile's rows. A row whose key or value this build does not understand is kept
  // verbatim, so a save neither drops a newer launcher's work nor rewrites what it cannot read.
  static Table FromRows(const std::vector<ProfileSetting>& rows);

  // The rows a save would write: one per action that differs from the default, in
  // `BindableActions()` order, then whatever was kept verbatim. An action left at its default has
  // no row at all, so a profile nobody has rebound has no `[nav]` table.
  std::vector<ProfileSetting> ToRows() const;

  // The action's triggers, which is never null: an action with no row has its defaults, and an
  // action whose row is empty has no keys at all - which is a real setting, and what "unbind this"
  // means.
  const std::vector<Trigger>& Triggers(NavAction action) const;
  // The triggers as the panel and the bottom bar spell them: "Tab, Down", or empty when the action
  // has no keys.
  std::string BindingText(NavAction action) const;

  // True when the action's keys are the ones it ships with.
  bool IsDefault(NavAction action) const;
  // How many actions differ from their defaults, which is what the block says it is showing and
  // what decides whether *Reset every key* has anything to do.
  std::size_t ChangedCount() const;

  // Binds `triggers` to the action, replacing what it had: assigning a key is this action's key
  // being the one that was pressed, and a user who wants Tab *and* Down adds them in one capture
  // list rather than by assigning twice. An empty list unbinds the action.
  void Set(NavAction action, std::vector<Trigger> triggers);
  void Reset(NavAction action);
  void ResetAll();

 private:
  struct Row {
    NavAction action = NavAction::kNone;
    std::vector<Trigger> triggers;
  };
  std::vector<Row> rows_;                // one per BindableActions(), in that order
  std::vector<ProfileSetting> unknown_;  // rows this build does not know, kept verbatim
};

// The one action this frame's presses ask for, or kNone.
NavAction Resolve(const Table& table, const Presses& presses);

}  // namespace rb_blitz::launcher::nav_bindings
