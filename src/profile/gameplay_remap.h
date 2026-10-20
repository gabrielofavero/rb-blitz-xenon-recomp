// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Gameplay mapping: the vocabulary and the two binding tables that bind physical inputs straight
// to the title's six gameplay actions
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md, D1/D6).
//
// The plan splits Gameplay Mapping into two independent tables because the two input types are
// different questions. Each table names the same six actions and accepts only its own sources:
//
//   * `[gameplay_remap_controller]` - a physical pad control or stick direction (`pad:` only).
//   * `[gameplay_remap_keyboard_mouse]` - a key or a mouse button (`key:`, `mouse:` only).
//
// A source of the wrong kind is not silently dropped: the whole row is preserved verbatim (so a
// round trip loses nothing), reported through `Rejected()`, and not applied. That is what keeps a
// hand-written `pad:a` row in the keyboard/mouse table from looking like a binding that merely
// does nothing.
//
// The rules are the remap module's rules, deliberately repeated rather than special-cased
// (src/profile/remap.h):
//
//   * a missing row uses the compiled default (`DefaultSources`), because a keyboard and a pad
//     must both be able to play without the profile being edited;
//   * an explicit empty value means "nothing fires this action" and is different from a value
//     this build cannot read, which is reported and preserved;
//   * several sources for one action are a list, not a priority, and the same source twice is one
//     press;
//   * a row this build does not know (a newer Controller Settings's action) is preserved and never
//     converted into an action.
//
// SDK-free and SDL-free on purpose: the game and Controller Settings both compile this, and the
// tests exercise every case without a pad, a window or a profile on disk. Nothing here reads a
// device or dispatches an action - P2/P3 own the device sampling and the guest dispatch; this
// module is only the configuration both of them read.
//
// The defaults are frozen from P0's evidence (plan §10.4 O3): the controller table's compiled
// default reproduces retail preset 0, which the live arms measured, and the keyboard table
// reproduces the Mouse & Keyboard defaults the restructure plan's D12 lists. They are *not* read
// back from the saved native preset at run time: a table with no custom row follows the preset
// the title's own Controller Presets page selected (P2 of
// docs/plans/in-progress/2026-10-19-interface-gameplay-mapping-restructure-plan.md), and the
// compiled default is what a build with no observed preset reports.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "profile/profile.h"
#include "profile/remap.h"

namespace rb_blitz::profile::gameplay_remap {

// One gameplay action, in the order the settings table lists them.
//
// These are the five actions the title's own controller presets produce, plus Pause - which the
// guest handles as a game-screen path rather than a `kBMAction` (plan §10.3/§10.4 O10), and which
// is still a gameplay action from the player's point of view. `Lane2`, `Swing` and
// `AwardJackpot` are defined by the guest's macros but unreachable from any shipped preset, so
// they are deliberately absent (plan §10.4 O7).
enum class Action : uint8_t {
  kFirstNote,
  kSecondNote,
  kMoveLaneRight,
  kMoveLaneLeft,
  kPowerup,
  kPause,
};

// How many actions there are, as a constant: the settings table's row count and the tests assert
// against it.
inline constexpr std::size_t kActionCount = 6;

// Every action, in the display order above.
const std::vector<Action>& Actions();

// The name the profile spells (`first_note`) and the label a person reads ("First Note").
std::string_view ActionName(Action action);
std::string_view ActionLabel(Action action);
std::optional<Action> ParseAction(std::string_view name);

// Which of the two tables a binding belongs to. The section name is the profile's table header;
// the label is the input type's own name in the settings surface.
enum class TableKind : uint8_t { kController, kKeyboardMouse };

inline constexpr std::size_t kTableKindCount = 2;

const std::vector<TableKind>& TableKinds();
std::string_view TableKindSection(TableKind kind);
std::string_view TableKindLabel(TableKind kind);
std::optional<TableKind> ParseTableKindSection(std::string_view section);

// True when `kind` may appear in this table's rows: the controller table takes `pad:` sources,
// the keyboard/mouse table takes `key:` and `mouse:` sources, and neither takes the other's.
bool AcceptsSourceKind(TableKind kind, remap::SourceKind source_kind);
bool AcceptsSource(TableKind kind, const remap::Source& source);

// What the table ships with: retail preset 0 for the controller and the Mouse & Keyboard defaults
// for the other table. Never nullptr: every action has a default in both tables.
const std::vector<remap::Source>* DefaultSources(TableKind kind, Action action);

// The title's own four controller presets (`config/gen/beatmatch_controller.dtb`, decoded
// 2026-10-17 into out/_swap/p0/defs/beatmatch_controller.txt). The controller table follows the
// preset the player selected on retail's Controller Presets page whenever it has no custom row
// (restructure plan D4); with a custom row the direct table bypasses the native preset and every
// unmodified action still uses this preset's sources.
inline constexpr int kControllerPresetCount = 4;

// What `preset` binds `action` to, in the project's `pad:` spellings. A preset outside 0..3 is
// read as 0, which is the compiled default the live arms measured.
const std::vector<remap::Source>* PresetSources(int preset, Action action);

// The name a person reads for a preset in a caption ("Preset 1" for 0).
std::string PresetLabel(int preset);

// One action's binding, as `[gameplay_remap_*]` spells it.
struct Binding {
  Action action = Action::kFirstNote;
  // Empty means nothing fires it, which is how an action is turned off.
  std::vector<remap::Source> sources;
};

// The `[gameplay_remap_controller]` or `[gameplay_remap_keyboard_mouse]` table.
class Table {
 public:
  // An empty table of `kind`: every action on its compiled default, like a profile nobody edited.
  explicit Table(TableKind kind = TableKind::kController) : kind_(kind) {}

  // From the profile's rows. A row whose action or value this build does not understand is kept
  // verbatim (so a save neither drops a newer Controller Settings's work nor rewrites it); a row
  // this build *does* understand but this table refuses - a source of the wrong kind, or a value
  // that is not a source list - is kept verbatim too, and reported through `Rejected()`.
  static Table FromRows(TableKind kind, const std::vector<ProfileSetting>& rows);

  // The rows a save would write: the bindings in `Actions()` order, then whatever was kept
  // verbatim. A table with nothing bound and nothing preserved renders nothing at all.
  std::vector<ProfileSetting> ToRows() const;

  TableKind kind() const { return kind_; }

  // The action's sources, or nullptr when the profile has no row for it - which means the
  // effective default below. An empty list is a real binding: it means the action is off.
  const std::vector<remap::Source>* Bindings(Action action) const;

  // What actually fires `action`: the row the user bound; otherwise the selected controller
  // preset for the Controller table, or the Mouse & Keyboard compiled default for the other.
  // Never nullptr.
  const std::vector<remap::Source>* Effective(Action action) const;

  // The retail controller preset this table follows for actions without a custom row. Only the
  // Controller table has one; the other table ignores it. Out-of-range input is read as 0.
  void SetControllerPreset(int preset);
  int Preset() const { return preset_; }

  // True when the table has any custom row, which is what the settings surface calls "custom"
  // (restructure plan D3: inferred, never a switch of its own).
  bool Custom() const { return BoundCount() > 0; }

  bool IsBound(Action action) const { return Bindings(action) != nullptr; }
  // True when the action has a row that binds nothing.
  bool IsDisabled(Action action) const;

  void Set(Action action, std::vector<remap::Source> sources);
  // Back to the compiled default: the row goes.
  void Reset(Action action);
  void ResetAll();

  // How many actions have a row - what the settings surface says it is showing.
  std::size_t BoundCount() const;
  // How many actions actually fire something, counting the defaults - what a boot reports, and
  // what tells an all-empty table (a scheme that can play nothing) from an untouched one.
  std::size_t EffectiveBoundCount() const;
  // True when any action's *effective* binding has a source. A table of explicit empty rows is not
  // "done": it is a scheme that can play nothing, and a boot says so.
  bool AnySourceSet() const;

  // A row this table keeps but does not apply, and the sentence that says why. The settings
  // surface and the boot log both name these rather than leaving a wrong-kind row looking like a
  // binding that silently does nothing.
  struct RejectedRow {
    ProfileSetting row;
    std::string reason;
  };
  const std::vector<RejectedRow>& Rejected() const { return rejected_; }

 private:
  TableKind kind_ = TableKind::kController;
  // The retail preset the Controller table follows while an action has no custom row.
  int preset_ = 0;
  std::vector<Binding> bindings_;
  // Kept verbatim, never applied: a row this build does not understand at all.
  std::vector<ProfileSetting> unknown_;
  std::vector<RejectedRow> rejected_;
};

}  // namespace rb_blitz::profile::gameplay_remap
