// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The in-game owner of the controller profile's binding rows
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md §14.2/§14.4, P7).
//
// One writer, one file. The rows the Controls menu edits are the rows Controller Settings already
// writes into `launcher.toml`:
//
//   [settings]                       keyboard_support, mouse_ui_nav
//   [remap]                          Interface Mapping (the 16 non-Guide controls)
//   [gameplay_remap_controller]      the Controller gameplay table
//   [gameplay_remap_keyboard_mouse]  the Mouse & Keyboard gameplay table
//
// The two `gameplay_mapping_*` switches are no longer written or read as routing (P2 of the
// interface/gameplay mapping restructure plan, D3/D15): custom is inferred from each table, an old
// profile key is preserved untouched, and nothing consults it.
//
// A save is the profile module's own surgical write (unknown keys, comments and unrelated tables
// survive) and it is **re-based**: the file is re-read first and only the rows this session
// changed are taken from the model, so a concurrent edit is merged rather than overwritten - the
// same rule Controller Settings' session follows. A file this build could not parse is never
// replaced; every save reports the refusal instead.
//
// After a successful save the new tables are published through src/input/effective_input.h and
// the boot's `on_published` hook bumps the input boundary: held actions are released, queued
// edges dropped, and a source must release before it can re-arm - the plan's §14.4 apply policy,
// at a menu/pause boundary rather than at the next launch.
//
// SDK-free on purpose: the boot supplies the switch values and the two platform hooks (apply a
// switch to the running process, notify after publishing). The tests drive the whole store
// against a temporary file.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "input/effective_input.h"
#include "profile/gameplay_remap.h"
#include "profile/input_rows.h"
#include "profile/profile.h"
#include "profile/remap.h"
#include "settings/input_bindings.h"

namespace rb_blitz::input {

// What the boot knows and the store cannot read for itself.
struct InputStoreOptions {
  // The effective switch values, read from the cvars after the boot applied the profile at config
  // rank. A command-line or environment value therefore arrives here already winning.
  bool keyboard_support = true;
  bool mouse_support = true;

  // True when the effective value came from the command line or the environment: the precedence
  // contract makes such a row read-only in the UI, and an edit would be a save that cannot land.
  bool keyboard_locked = false;
  bool mouse_locked = false;

  // Writes one switch to the running process (the boot performs it on a host thread and reads it
  // back). Empty means "save only, applies at the next launch" - a test or a tool.
  std::function<bool(std::string_view name, bool value, std::string* error)> apply_switch;
  // Runs after a successful publish: the boot bumps the gameplay input boundary here.
  std::function<void()> on_published;
  // True while a physical pad is connected. The recovery rule refuses an edit that would leave no
  // usable device at all (plan §14.4), so a keyboard/mouse pair cannot be turned off on a pad-less
  // machine.
  std::function<bool()> pad_connected;
};

class InputStore final : public settings::InputBindingStore {
 public:
  static std::unique_ptr<InputStore> Load(const std::filesystem::path& path,
                                          const profile::ProfileLoadResult& load,
                                          InputStoreOptions options);
  ~InputStore() override = default;

  // The first publish, before the input system exists. Also the boot log's source when it asks
  // for `Summary()`.
  void PublishInitial();

  // settings::InputBindingStore
  bool IsToken(std::string_view token) const override;
  bool Matches(std::string_view token, bool value) const override;
  bool StageBool(std::string_view token, bool value, std::string* error) override;
  bool StageReset(std::string_view token, std::string* error) override;
  bool StageResetAll(std::string_view token, std::string* error) override;
  bool Commit(std::string* error) override;
  bool BeginCapture(std::string_view token, std::string* error) override;
  void CancelCapture() override;
  bool CompleteCapture(std::string_view token, const profile::remap::Source& source,
                       std::string* error) override;
  std::string Describe(std::string_view token) const override;
  std::string Summary() const override;

  // State for the tests and the boot log.
  bool keyboard_support() const { return keyboard_support_; }
  bool mouse_support() const { return mouse_support_; }
  // "Custom" is inferred per table (D3); these are what the surface and a boot line read.
  bool controller_custom() const { return controller_.Custom(); }
  bool keyboard_mouse_custom() const { return keyboard_mouse_.Custom(); }
  // The retail preset the Controller table follows for unmodified actions (D4).
  int controller_preset() const { return ObservedControllerPreset(); }
  bool dirty() const { return dirty_; }
  bool usable() const { return usable_; }
  const std::filesystem::path& path() const { return path_; }

 private:
  enum class TableSel : uint8_t { kGeneral, kController, kKeyboardMouse, kMusic };

  InputStore(std::filesystem::path path, profile::ProfileLoadResult load,
             InputStoreOptions options);

  static std::optional<TableSel> SwitchTable(profile::InputRowKind kind);
  static std::optional<TableSel> BindingTableOf(profile::InputRowKind kind);
  static profile::InputRowKind SwitchKindOf(TableSel table);

  bool CurrentSwitch(profile::InputRowKind kind) const;
  bool Locked(profile::InputRowKind kind) const;
  bool SetSwitch(profile::InputRowKind kind, bool value, std::string* error);
  const std::vector<profile::remap::Source>* EffectiveSources(const profile::InputRowToken& token) const;
  bool IsBound(const profile::InputRowToken& token) const;
  std::size_t BoundCount(TableSel table) const;
  void ResetRow(const profile::InputRowToken& token);
  void SetRow(const profile::InputRowToken& token, std::vector<profile::remap::Source> sources);
  void ResetTable(TableSel table);
  bool Accepts(const profile::InputRowToken& token, profile::remap::SourceKind kind) const;

  // The profile-side application of the staged rows, onto a freshly read document.
  bool ApplyToProfile(profile::Profile* profile, std::string* error) const;
  void Publish();

  std::filesystem::path path_;
  bool usable_ = true;
  bool file_present_ = false;
  std::string unreadable_;

  profile::remap::Table general_;
  profile::music::Bindings music_;
  profile::gameplay_remap::Table controller_;
  profile::gameplay_remap::Table keyboard_mouse_;

  bool keyboard_support_ = true;
  bool mouse_support_ = true;

  InputStoreOptions options_;
  bool dirty_ = false;
  std::map<std::string, std::optional<std::vector<profile::remap::Source>>> staged_rows_;
  std::set<profile::InputRowKind> staged_switches_;
  std::set<TableSel> staged_reset_all_;
  // The gate values last applied to the process, so a commit only writes what moved.
  bool running_keyboard_ = true;
  bool running_mouse_ = true;
};

}  // namespace rb_blitz::input
