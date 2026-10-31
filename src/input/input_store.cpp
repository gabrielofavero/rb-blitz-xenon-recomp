// SPDX-License-Identifier: GPL-2.0-only
//
// Implementation of src/input/input_store.h.

#include "input/input_store.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "input/effective_input.h"
#include "input/input_capture.h"

namespace rb_blitz::input {
namespace {

using profile::gameplay_remap::Action;
using profile::gameplay_remap::TableKind;
using profile::InputRowKind;
using profile::InputRowToken;
using profile::remap::Source;
using profile::remap::SourceKind;
using profile::remap::Target;

constexpr char kKeyboardKey[] = "keyboard_support";
constexpr char kMouseKey[] = "mouse_ui_nav";

constexpr bool kKeyboardDefault = true;
constexpr bool kMouseDefault = true;

std::optional<bool> StoredBool(const profile::Profile& profile, const std::string_view key) {
  const profile::ProfileSetting* row = profile.FindSetting(key);
  if (row == nullptr) {
    return std::nullopt;
  }
  if (row->value == "true") {
    return true;
  }
  if (row->value == "false") {
    return false;
  }
  return std::nullopt;  // a value this build cannot read: the compiled default wins
}

void WriteBoolRow(profile::Profile* profile, const std::string_view key, const bool value,
                  const bool compiled_default) {
  if (value == compiled_default) {
    profile->EraseSetting(key);
  } else {
    profile->Set(key, value ? "true" : "false");
  }
}

bool SameSource(const Source& left, const Source& right) { return left == right; }

}  // namespace

InputStore::InputStore(std::filesystem::path path, profile::ProfileLoadResult load,
                       InputStoreOptions options)
    : path_(std::move(path)),
      usable_(load.usable()),
      file_present_(load.status != profile::ProfileStatus::kMissingFile),
      unreadable_(load.error),
      general_(profile::remap::Table::FromRows(load.profile.remap)),
      music_(load.profile.menu_music),
      controller_(profile::gameplay_remap::Table::FromRows(
          TableKind::kController, load.profile.gameplay_remap_controller)),
      keyboard_mouse_(profile::gameplay_remap::Table::FromRows(
          TableKind::kKeyboardMouse, load.profile.gameplay_remap_keyboard_mouse)),
      options_(std::move(options)) {
  // The switches come from the boot's effective values (the cvars after the profile was applied
  // at config rank), not from the profile again: that is what makes a command-line or
  // environment value outrank the file exactly as it does everywhere else.
  keyboard_support_ = options_.keyboard_support;
  mouse_support_ = options_.mouse_support;
  running_keyboard_ = keyboard_support_;
  running_mouse_ = mouse_support_;
}

std::unique_ptr<InputStore> InputStore::Load(const std::filesystem::path& path,
                                             const profile::ProfileLoadResult& load,
                                             InputStoreOptions options) {
  return std::unique_ptr<InputStore>(new InputStore(path, load, std::move(options)));
}

void InputStore::PublishInitial() { Publish(); }

bool InputStore::IsToken(const std::string_view token) const {
  return profile::IsInputToken(token) &&
         profile::ParseInputRowToken(token).valid();
}

std::optional<InputStore::TableSel> InputStore::SwitchTable(const InputRowKind kind) {
  switch (kind) {
    case InputRowKind::kMusicBinding:
    case InputRowKind::kResetMusic: return TableSel::kMusic;
    case InputRowKind::kGeneralBinding:
    case InputRowKind::kResetGeneral:
      return TableSel::kGeneral;
    case InputRowKind::kControllerBinding:
    case InputRowKind::kResetController:
      return TableSel::kController;
    case InputRowKind::kKeyboardMouseBinding:
    case InputRowKind::kResetKeyboardMouse:
      return TableSel::kKeyboardMouse;
    default:
      return std::nullopt;
  }
}

std::optional<InputStore::TableSel> InputStore::BindingTableOf(const InputRowKind kind) {
  switch (kind) {
    case InputRowKind::kMusicBinding: return TableSel::kMusic;
    case InputRowKind::kGeneralBinding: return TableSel::kGeneral;
    case InputRowKind::kControllerBinding: return TableSel::kController;
    case InputRowKind::kKeyboardMouseBinding: return TableSel::kKeyboardMouse;
    default: return std::nullopt;
  }
}

InputRowKind InputStore::SwitchKindOf(const TableSel table) {
  switch (table) {
    case TableSel::kMusic: return InputRowKind::kMusicBinding;
    case TableSel::kGeneral: return InputRowKind::kGeneralBinding;
    case TableSel::kController: return InputRowKind::kControllerBinding;
    case TableSel::kKeyboardMouse: return InputRowKind::kKeyboardMouseBinding;
  }
  return InputRowKind::kNone;
}

bool InputStore::CurrentSwitch(const InputRowKind kind) const {
  switch (kind) {
    case InputRowKind::kKeyboardSupport: return keyboard_support_;
    case InputRowKind::kMouseSupport: return mouse_support_;
    default: return false;
  }
}

bool InputStore::Locked(const InputRowKind kind) const {
  switch (kind) {
    case InputRowKind::kKeyboardSupport: return options_.keyboard_locked;
    case InputRowKind::kMouseSupport: return options_.mouse_locked;
    default: return false;
  }
}

bool InputStore::SetSwitch(const InputRowKind kind, const bool value, std::string* error) {
  if (Locked(kind)) {
    if (error != nullptr) {
      *error = "this row is set on the command line or in the environment, so the menu cannot "
               "change it";
    }
    return false;
  }
  if (value == CurrentSwitch(kind)) {
    return true;
  }
  // The plan's §14.4 recovery rule: an edit must never leave the menu with no usable device. A
  // connected pad is always a route; keyboard and mouse together are the other one.
  const bool next_keyboard = kind == InputRowKind::kKeyboardSupport ? value : keyboard_support_;
  const bool next_mouse = kind == InputRowKind::kMouseSupport ? value : mouse_support_;
  const bool pad = options_.pad_connected && options_.pad_connected();
  if (!next_keyboard && !next_mouse && !pad) {
    if (error != nullptr) {
      *error = "no usable input would be left: connect a controller, or keep Keyboard or Mouse on";
    }
    return false;
  }
  switch (kind) {
    case InputRowKind::kKeyboardSupport: keyboard_support_ = value; break;
    case InputRowKind::kMouseSupport: mouse_support_ = value; break;
    default: return false;
  }
  staged_switches_.insert(kind);
  dirty_ = true;
  return true;
}

const std::vector<Source>* InputStore::EffectiveSources(const InputRowToken& token) const {
  switch (token.kind) {
    case InputRowKind::kMusicBinding: return music_.Effective(token.name);
    case InputRowKind::kGeneralBinding:
      if (const auto target = profile::remap::ParseTarget(token.name); target.has_value()) {
        return general_.Effective(*target);
      }
      return nullptr;
    case InputRowKind::kControllerBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(token.name);
          action.has_value()) {
        return controller_.Effective(*action);
      }
      return nullptr;
    case InputRowKind::kKeyboardMouseBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(token.name);
          action.has_value()) {
        return keyboard_mouse_.Effective(*action);
      }
      return nullptr;
    default:
      return nullptr;
  }
}

bool InputStore::IsBound(const InputRowToken& token) const {
  switch (token.kind) {
    case InputRowKind::kMusicBinding: return music_.IsBound(token.name);
    case InputRowKind::kGeneralBinding:
      if (const auto target = profile::remap::ParseTarget(token.name); target.has_value()) {
        return general_.IsBound(*target);
      }
      return false;
    case InputRowKind::kControllerBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(token.name);
          action.has_value()) {
        return controller_.IsBound(*action);
      }
      return false;
    case InputRowKind::kKeyboardMouseBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(token.name);
          action.has_value()) {
        return keyboard_mouse_.IsBound(*action);
      }
      return false;
    default:
      return false;
  }
}

std::size_t InputStore::BoundCount(const TableSel table) const {
  switch (table) {
    case TableSel::kMusic: return music_.BoundCount();
    case TableSel::kGeneral: return general_.BoundCount();
    case TableSel::kController: return controller_.BoundCount();
    case TableSel::kKeyboardMouse: return keyboard_mouse_.BoundCount();
  }
  return 0;
}

void InputStore::ResetRow(const InputRowToken& token) {
  switch (token.kind) {
    case InputRowKind::kMusicBinding: music_.Reset(token.name); break;
    case InputRowKind::kGeneralBinding:
      if (const auto target = profile::remap::ParseTarget(token.name); target.has_value()) {
        general_.Reset(*target);
      }
      break;
    case InputRowKind::kControllerBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(token.name);
          action.has_value()) {
        controller_.Reset(*action);
      }
      break;
    case InputRowKind::kKeyboardMouseBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(token.name);
          action.has_value()) {
        keyboard_mouse_.Reset(*action);
      }
      break;
    default:
      break;
  }
}

void InputStore::SetRow(const InputRowToken& token, std::vector<Source> sources) {
  switch (token.kind) {
    case InputRowKind::kMusicBinding: music_.Set(token.name, sources); break;
    case InputRowKind::kGeneralBinding:
      if (const auto target = profile::remap::ParseTarget(token.name); target.has_value()) {
        general_.Set(*target, std::move(sources));
      }
      break;
    case InputRowKind::kControllerBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(token.name);
          action.has_value()) {
        controller_.Set(*action, std::move(sources));
      }
      break;
    case InputRowKind::kKeyboardMouseBinding:
      if (const auto action = profile::gameplay_remap::ParseAction(token.name);
          action.has_value()) {
        keyboard_mouse_.Set(*action, std::move(sources));
      }
      break;
    default:
      break;
  }
}

void InputStore::ResetTable(const TableSel table) {
  switch (table) {
    case TableSel::kMusic: music_.ResetAll(); break;
    case TableSel::kGeneral: general_.ResetAll(); break;
    case TableSel::kController: controller_.ResetAll(); break;
    case TableSel::kKeyboardMouse: keyboard_mouse_.ResetAll(); break;
  }
}

bool InputStore::Accepts(const InputRowToken& token, const SourceKind kind) const {
  switch (token.kind) {
    case InputRowKind::kMusicBinding: return kind == SourceKind::kPad || kind == SourceKind::kKey;
    case InputRowKind::kGeneralBinding:
      return kind != SourceKind::kSdl;
    case InputRowKind::kControllerBinding:
      return kind == SourceKind::kPad || kind == SourceKind::kSdl;
    case InputRowKind::kKeyboardMouseBinding:
      return kind == SourceKind::kKey || kind == SourceKind::kMouse;
    default:
      return false;
  }
}

bool InputStore::Matches(const std::string_view token, const bool value) const {
  const InputRowToken row = profile::ParseInputRowToken(token);
  switch (row.kind) {
    case InputRowKind::kKeyboardSupport:
    case InputRowKind::kMouseSupport:
      return CurrentSwitch(row.kind) == value;
    case InputRowKind::kMusicBinding:
    case InputRowKind::kGeneralBinding:
    case InputRowKind::kControllerBinding:
    case InputRowKind::kKeyboardMouseBinding:
      return IsBound(row) == value;
    case InputRowKind::kResetGeneral:
      return BoundCount(TableSel::kGeneral) != 0;
    case InputRowKind::kResetController:
      return BoundCount(TableSel::kController) != 0;
    case InputRowKind::kResetKeyboardMouse:
      return BoundCount(TableSel::kKeyboardMouse) != 0;
    case InputRowKind::kResetMusic: return music_.BoundCount() != 0;
    case InputRowKind::kNone:
      return false;
  }
  return false;
}

bool InputStore::StageBool(const std::string_view token, const bool value, std::string* error) {
  const InputRowToken row = profile::ParseInputRowToken(token);
  if (row.kind != InputRowKind::kKeyboardSupport && row.kind != InputRowKind::kMouseSupport) {
    if (error != nullptr) {
      *error = "that is not a switch row";
    }
    return false;
  }
  return SetSwitch(row.kind, value, error);
}

bool InputStore::StageReset(const std::string_view token, std::string* error) {
  const InputRowToken row = profile::ParseInputRowToken(token);
  if (BindingTableOf(row.kind) == std::nullopt) {
    if (error != nullptr) {
      *error = "that is not a binding row";
    }
    return false;
  }
  if (staged_reset_all_.contains(*BindingTableOf(row.kind))) {
    return true;  // the table-wide reset already covers this row
  }
  if (!IsBound(row)) {
    return true;  // nothing stored: Reset is the no-op the disabled row promises
  }
  ResetRow(row);
  staged_rows_[std::string(token)] = std::nullopt;
  dirty_ = true;
  return true;
}

bool InputStore::StageResetAll(const std::string_view token, std::string* error) {
  const InputRowToken row = profile::ParseInputRowToken(token);
  std::optional<TableSel> table;
  switch (row.kind) {
    case InputRowKind::kResetMusic: table = TableSel::kMusic; break;
    case InputRowKind::kResetGeneral: table = TableSel::kGeneral; break;
    case InputRowKind::kResetController: table = TableSel::kController; break;
    case InputRowKind::kResetKeyboardMouse: table = TableSel::kKeyboardMouse; break;
    default: break;
  }
  if (!table.has_value()) {
    if (error != nullptr) {
      *error = "that is not a Reset All row";
    }
    return false;
  }
  if (BoundCount(*table) == 0) {
    return true;
  }
  ResetTable(*table);
  staged_reset_all_.insert(*table);
  // A per-row reset staged before this one is superseded; keep one operation per table.
  for (auto it = staged_rows_.begin(); it != staged_rows_.end();) {
    const InputRowToken staged = profile::ParseInputRowToken(it->first);
    const auto staged_table = BindingTableOf(staged.kind);
    it = staged_table == table ? staged_rows_.erase(it) : std::next(it);
  }
  dirty_ = true;
  return true;
}

bool InputStore::ApplyToProfile(profile::Profile* profile, std::string* error) const {
  if (profile == nullptr) {
    if (error != nullptr) *error = "no profile document";
    return false;
  }
  profile::music::Bindings music(profile->menu_music);
  profile::remap::Table general = profile::remap::Table::FromRows(profile->remap);
  profile::gameplay_remap::Table controller = profile::gameplay_remap::Table::FromRows(
      TableKind::kController, profile->gameplay_remap_controller);
  profile::gameplay_remap::Table keyboard_mouse = profile::gameplay_remap::Table::FromRows(
      TableKind::kKeyboardMouse, profile->gameplay_remap_keyboard_mouse);

  for (const TableSel table : staged_reset_all_) {
    switch (table) {
      case TableSel::kMusic: music.ResetAll(); break;
      case TableSel::kGeneral: general.ResetAll(); break;
      case TableSel::kController: controller.ResetAll(); break;
      case TableSel::kKeyboardMouse: keyboard_mouse.ResetAll(); break;
    }
  }
  for (const auto& [token, sources] : staged_rows_) {
    const InputRowToken row = profile::ParseInputRowToken(token);
    const auto table = BindingTableOf(row.kind);
    if (!table.has_value() || staged_reset_all_.contains(*table)) {
      continue;
    }
    const std::vector<Source> list = sources.value_or(std::vector<Source>{});
    switch (row.kind) {
      case InputRowKind::kMusicBinding:
        if (sources) music.Set(row.name, list); else music.Reset(row.name);
        break;
      case InputRowKind::kGeneralBinding:
        if (const auto target = profile::remap::ParseTarget(row.name); target.has_value()) {
          if (sources.has_value()) {
            general.Set(*target, list);
          } else {
            general.Reset(*target);
          }
        }
        break;
      case InputRowKind::kControllerBinding:
        if (const auto action = profile::gameplay_remap::ParseAction(row.name);
            action.has_value()) {
          if (sources.has_value()) {
            controller.Set(*action, list);
          } else {
            controller.Reset(*action);
          }
        }
        break;
      case InputRowKind::kKeyboardMouseBinding:
        if (const auto action = profile::gameplay_remap::ParseAction(row.name);
            action.has_value()) {
          if (sources.has_value()) {
            keyboard_mouse.Set(*action, list);
          } else {
            keyboard_mouse.Reset(*action);
          }
        }
        break;
      default:
        break;
    }
  }

  for (const InputRowKind kind : staged_switches_) {
    switch (kind) {
      case InputRowKind::kKeyboardSupport:
        WriteBoolRow(profile, kKeyboardKey, keyboard_support_, kKeyboardDefault);
        break;
      case InputRowKind::kMouseSupport:
        WriteBoolRow(profile, kMouseKey, mouse_support_, kMouseDefault);
        break;
      default:
        break;
    }
  }

  profile->menu_music = music.ToRows();
  profile->remap = general.ToRows();
  profile->gameplay_remap_controller = controller.ToRows();
  profile->gameplay_remap_keyboard_mouse = keyboard_mouse.ToRows();
  return true;
}

bool InputStore::Commit(std::string* error) {
  if (!usable_) {
    if (error != nullptr) {
      *error = "the profile could not be read, so it is left untouched: " + unreadable_;
    }
    return false;
  }
  if (!dirty_) {
    return true;
  }
  if (path_.empty()) {
    if (error != nullptr) {
      *error = "this process has nowhere to keep the profile";
    }
    return false;
  }
  // Re-base: the document saved into is the one on disk now, and only the rows this session
  // staged are taken from the model. A concurrent edit to another row is merged, not overwritten.
  const profile::ProfileLoadResult disk = profile::LoadProfile(path_);
  if (!disk.usable()) {
    usable_ = false;
    unreadable_ = disk.error;
    if (error != nullptr) {
      *error = path_.string() + " could not be read, so it was not replaced: " + disk.error;
    }
    return false;
  }
  profile::Profile document = disk.profile;
  if (!ApplyToProfile(&document, error)) {
    return false;
  }
  if (!profile::SaveProfile(path_, document, error)) {
    return false;
  }
  usable_ = true;
  file_present_ = true;
  music_ = profile::music::Bindings(document.menu_music);
  general_ = profile::remap::Table::FromRows(document.remap);
  controller_ = profile::gameplay_remap::Table::FromRows(TableKind::kController,
                                                         document.gameplay_remap_controller);
  keyboard_mouse_ = profile::gameplay_remap::Table::FromRows(
      TableKind::kKeyboardMouse, document.gameplay_remap_keyboard_mouse);

  // Apply the switches to the running process before publishing the tables: the next poll reads
  // the gates from their cvars, so the boundary the publish bumps must already see the new
  // values. A refused write keeps its honest restart debt and is reported.
  bool applied = true;
  std::string apply_error;
  const auto apply = [&](const char* name, const bool value, bool& running) {
    if (options_.apply_switch && value != running) {
      std::string why;
      if (!options_.apply_switch(name, value, &why)) {
        applied = false;
        if (apply_error.empty()) {
          apply_error = std::string(name) + ": " + why;
        }
      } else {
        running = value;
      }
    }
  };
  apply(kKeyboardKey, keyboard_support_, running_keyboard_);
  apply(kMouseKey, mouse_support_, running_mouse_);

  staged_rows_.clear();
  staged_switches_.clear();
  staged_reset_all_.clear();
  dirty_ = false;
  Publish();
  if (!applied) {
    if (error != nullptr) {
      *error = "the mapping was saved, but applying it to this game failed (" + apply_error +
               "); it takes effect the next time the game starts";
    }
    return false;
  }
  return true;
}

bool InputStore::BeginCapture(const std::string_view token, std::string* error) {
  capture_replace_ = false;
  const InputRowToken row = profile::ParseInputRowToken(token);
  if (BindingTableOf(row.kind) == std::nullopt) {
    if (error != nullptr) {
      *error = "that row is not a binding";
    }
    return false;
  }
  capture::CaptureTarget target;
  target.token = std::string(token);
  switch (row.kind) {
    case InputRowKind::kMusicBinding:
      target.allowed = {SourceKind::kPad, SourceKind::kKey};
      break;
    case InputRowKind::kGeneralBinding:
      target.allowed = {SourceKind::kPad, SourceKind::kKey, SourceKind::kMouse};
      break;
    case InputRowKind::kControllerBinding:
      target.allowed = {SourceKind::kPad, SourceKind::kSdl};
      break;
    case InputRowKind::kKeyboardMouseBinding:
      target.allowed = {SourceKind::kPad, SourceKind::kKey, SourceKind::kMouse};
      break;
    default:
      break;
  }
  capture::Arm(target);
  return true;
}

bool InputStore::BeginCaptureReplace(const std::string_view token, std::string* error) {
  if (!BeginCapture(token, error)) return false;
  capture_replace_ = true;
  return true;
}

void InputStore::CancelCapture() { capture::Cancel(); }

bool InputStore::HasPendingControllerCapture() const {
  return pending_controller_capture_.has_value();
}

bool InputStore::ResolvePendingControllerCapture(const bool accept, std::string* error) {
  if (!pending_controller_capture_) return !accept;
  const auto pending = std::move(*pending_controller_capture_);
  pending_controller_capture_.reset();
  return !accept || SaveCapturedSource(pending.token, pending.source, pending.replace, error);
}

bool InputStore::CompleteCapture(const std::string_view token, const Source& source,
                                 std::string* error) {
  const InputRowToken row = profile::ParseInputRowToken(token);
  if (BindingTableOf(row.kind) == std::nullopt) {
    if (error != nullptr) {
      *error = "that row is not a binding";
    }
    return false;
  }
  if ((!(row.kind == InputRowKind::kKeyboardMouseBinding && source.kind == SourceKind::kPad) &&
       !Accepts(row, source.kind)) ||
      (row.kind == InputRowKind::kMusicBinding && !profile::music::Bindings::Accepts(source))) {
    if (error != nullptr) {
      *error = std::string("this row does not take ") + profile::remap::FormatSource(source);
    }
    return false;
  }
  if (row.kind == InputRowKind::kKeyboardMouseBinding && source.kind == SourceKind::kPad) {
    const auto action = profile::gameplay_remap::ParseAction(row.name);
    const std::string controller_token = profile::GameplayBindingToken(TableKind::kController, *action);
    if (!controller_.Custom()) {
      pending_controller_capture_ = PendingControllerCapture{controller_token, source, capture_replace_};
      return true;
    }
    return SaveCapturedSource(controller_token, source, capture_replace_, error);
  }
  return SaveCapturedSource(token, source, capture_replace_, error);
}

bool InputStore::SaveCapturedSource(const std::string_view token, const Source& source,
                                    const bool replace, std::string* error) {
  const InputRowToken row = profile::ParseInputRowToken(token);
  const bool was_custom = IsBound(row);
  std::optional<std::vector<Source>> previous;
  if (was_custom) {
    if (const std::vector<Source>* stored = EffectiveSources(row); stored != nullptr) {
      previous = *stored;
    }
  }
  std::vector<Source> next = replace || (row.kind == InputRowKind::kControllerBinding &&
                                         !controller_.Custom())
      ? std::vector<Source>{} : previous.value_or(*EffectiveSources(row));
  if (std::none_of(next.begin(), next.end(),
                   [&source](const Source& candidate) { return SameSource(candidate, source); })) {
    next.push_back(source);
  }
  SetRow(row, next);
  staged_rows_[std::string(token)] = next;
  dirty_ = true;
  std::string why;
  if (!Commit(&why)) {
    // The save did not land: put the row back so the next commit cannot publish a binding the
    // profile does not hold.
    if (previous.has_value()) {
      SetRow(row, *previous);
    } else {
      ResetRow(row);
    }
    staged_rows_.erase(std::string(token));
    if (error != nullptr) {
      *error = why;
    }
    return false;
  }
  return true;
}

std::string InputStore::Describe(const std::string_view token) const {
  InputRowToken row = profile::ParseInputRowToken(token);
  if (!row.valid()) {
    // The caption event carries the row's canonical menu symbol, not its token.
    row = profile::ParseInputRowSymbol(token);
  }
  if (BindingTableOf(row.kind) == std::nullopt) {
    return {};
  }
  const std::string label = profile::BindingRowLabel(row);
  const std::vector<Source>* sources = EffectiveSources(row);
  std::string text = label + ": ";
  if (sources == nullptr || sources->empty()) {
    text += "(no inputs)";
  } else {
    text += profile::BindingListText(*sources);
  }
  text += ". ";
  // The action page offers Add Key, Replace, and Reset.
  if (IsBound(row)) {
    text += "Custom binding. Choose Add Key or Replace.";
  } else if (row.kind == InputRowKind::kControllerBinding) {
    text += "Default binding from " +
            profile::gameplay_remap::PresetLabel(ObservedControllerPreset()) +
            ". Choose Add Key or Replace.";
  } else {
    text += "Default binding. Choose Add Key or Replace.";
  }
  return text;
}

std::string InputStore::Summary() const {
  std::string out;
  out += "keyboard_support=";
  out += keyboard_support_ ? "on" : "off";
  out += " mouse_ui_nav=";
  out += mouse_support_ ? "on" : "off";
  out += "; controller preset=";
  const int preset = ObservedControllerPreset();
  if (preset < 0) {
    out += "not observed yet (preset 1 until a song starts)";
  } else {
    out += profile::gameplay_remap::PresetLabel(preset);
  }
  out += "; [menu_music] " + std::to_string(music_.BoundCount()) + " row(s)";
  out += "; [remap] ";
  out += std::to_string(general_.BoundCount());
  out += " row(s); [gameplay_remap_controller] ";
  out += std::to_string(controller_.BoundCount());
  out += " row(s) (";
  out += controller_.Custom() ? "custom: the direct table drives the pad" :
                                "no custom row: the native preset drives";
  out += "); [gameplay_remap_keyboard_mouse] ";
  out += std::to_string(keyboard_mouse_.BoundCount());
  out += " row(s) (always on while playing)";
  return out;
}

void InputStore::Publish() {
  auto snapshot = std::make_shared<EffectiveInput>();
  snapshot->general = general_;
  snapshot->music = music_;
  snapshot->controller = controller_;
  // D4: a table without a custom row follows the preset the title's own preset page selected.
  // The observed value is boot-wide (src/input/effective_input.h), so a later publish must stamp
  // it too or a commit would silently fall back to preset 0.
  snapshot->controller.SetControllerPreset(ObservedControllerPreset());
  snapshot->keyboard_mouse = keyboard_mouse_;
  snapshot->profile_present = file_present_ || usable_;
  PublishInput(std::move(snapshot));
  if (options_.on_published) {
    options_.on_published();
  }
}

}  // namespace rb_blitz::input
