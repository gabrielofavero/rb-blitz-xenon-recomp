// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The service in src/settings/service.h. Requested, effective and pending are three separate
// values on purpose (contracts §5.2): a temporarily disconnected monitor must not erase a
// valid manual request, an explicit CLI override must show its true effective value, and an
// overridden row is refused rather than saved into something that cannot take effect.
//
// Revision rule (§6.1): `revision_` advances only when a commit becomes durable. A failed,
// partial or no-op commit does not advance it, and a transaction based on a stale revision is
// refused, so the UI re-snapshots instead of writing over a change it never saw.

#include "settings/service.h"

#include <string>
#include <utility>
#include <charconv>

#include "settings/cvars.h"
#include "settings/schema.h"
#include "fs/atomic_file.h"

namespace rb_blitz::settings {

using rb_blitz::launcher::Profile;

namespace {

std::string ResolutionToken(const Value& value) {
  if (IsAutoResolution(value)) return "auto";
  return std::to_string(value.width) + "x" + std::to_string(value.height);
}

std::string JournalValue(const Value& value) {
  switch (value.kind) {
    case ValueKind::kBool: return value.boolean ? "true" : "false";
    case ValueKind::kInt: return std::to_string(value.integer);
    case ValueKind::kResolution: return ResolutionToken(value);
    default: return value.text;
  }
}

void JournalSet(toml::Document& document, std::string_view section, SettingId id,
                const Value& value) {
  document.Set(section, std::to_string(static_cast<int>(id)),
               value.kind == ValueKind::kList ? toml::RenderStringList(value.list)
                                              : toml::RenderBasic(JournalValue(value)));
}

bool JournalRead(const toml::Document& document, std::string_view section,
                 const std::vector<Descriptor>& descriptors, std::map<SettingId, Value>* values) {
  for (const auto& key : document.Keys(section)) {
    int raw = -1;
    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), raw);
    if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size()) return false;
    const Descriptor* descriptor = nullptr;
    for (const auto& candidate : descriptors) {
      if (static_cast<int>(candidate.id) == raw) descriptor = &candidate;
    }
    // An action row carries no value at all, so it is not something the journal can hold.
    if (!descriptor || descriptor->widget == WidgetKind::kAction) return false;
    const auto item = document.Get(section, key);
    Value value;
    value.kind = descriptor->value_kind;
    if (value.kind == ValueKind::kList) {
      if (!item.list_ok) return false;
      value.list = item.list;
    } else {
      if (!item.scalar) return false;
      switch (value.kind) {
        case ValueKind::kBool:
          if (!toml::ParseBareBool(item.value, &value.boolean)) return false;
          break;
        case ValueKind::kInt: {
          int64_t number = 0;
          if (!toml::ParseBareInt(item.value, &number) || number < INT32_MIN || number > INT32_MAX)
            return false;
          value.integer = static_cast<int32_t>(number);
          break;
        }
        case ValueKind::kResolution: {
          if (item.value == "auto") break;
          const auto separator = item.value.find('x');
          int64_t width = 0, height = 0;
          if (separator == std::string::npos ||
              !toml::ParseBareInt(item.value.substr(0, separator), &width) ||
              !toml::ParseBareInt(item.value.substr(separator + 1), &height) ||
              width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX) return false;
          value.width = static_cast<int32_t>(width);
          value.height = static_cast<int32_t>(height);
          break;
        }
        default: value.text = item.value; break;
      }
    }
    (*values)[descriptor->id] = std::move(value);
  }
  return true;
}

}  // namespace

bool IsProviderFileRow(SettingId id) {
  const schema::SchemaRow* row = schema::FindRow(id);
  if (row == nullptr || row->descriptor.provider != Provider::kUltimate) {
    return false;
  }
  return !row->storage.persisted();
}

ValidationResult Reconciler::Validate(SettingId id, const Descriptor& descriptor, const Value& value,
                                      const CapabilitySnapshot& caps) const {
  (void)id;
  (void)caps;
  const bool enum_like = descriptor.value_kind == ValueKind::kEnum ||
                         descriptor.value_kind == ValueKind::kString;
  const bool value_enum_like = value.kind == ValueKind::kEnum || value.kind == ValueKind::kString;
  if (value.kind != descriptor.value_kind && !(enum_like && value_enum_like)) {
    return ValidationResult{false, "settings.invalid_kind"};
  }
  if (!descriptor.choices.empty()) {
    std::string wanted;
    if (value.kind == ValueKind::kResolution) {
      wanted = ResolutionToken(value);
    } else if (value.kind == ValueKind::kInt) {
      wanted = std::to_string(value.integer);
    } else if (value_enum_like) {
      wanted = value.text;
    } else {
      return ValidationResult{true, ""};
    }
    for (const Choice& choice : descriptor.choices) {
      if (choice.token == wanted) {
        return ValidationResult{true, ""};
      }
    }
    return ValidationResult{false, "settings.invalid_choice"};
  }
  return ValidationResult{true, ""};
}

Reconciliation Reconciler::Reconcile(SettingId id, const Descriptor& descriptor,
                                     const Value& requested, const CapabilitySnapshot& caps) const {
  (void)id;
  (void)descriptor;
  (void)caps;
  Reconciliation result;
  result.effective = requested;
  return result;
}

Service::Service(StorePaths paths) : paths_(std::move(paths)), store_(paths_) {
  RefreshDescriptors();
}

Service::~Service() = default;

DocumentStatus Service::Boot(const Profile* legacy, const PathEntry& existing_paths,
                             MigrationResult* migration, bool legacy_usable) {
  load_status_ = store_.Load().status;
  if (legacy != nullptr) {
    MigrationOptions options;
    options.legacy_usable = legacy_usable;
    const MigrationResult result = Migrate(*legacy, store_, existing_paths, options);
    if (migration != nullptr) {
      *migration = result;
    }
  }
  return load_status_;
}

void Service::RefreshDescriptors() {
  descriptors_ = schema::BuildDescriptors(capabilities_);
  for (auto& descriptor : descriptors_) {
    const auto it = provider_choices_.find(descriptor.id);
    if (it != provider_choices_.end()) descriptor.choices = it->second;
  }
}

void Service::SetProviderChoices(std::map<SettingId, std::vector<Choice>> choices) {
  provider_choices_ = std::move(choices);
  RefreshDescriptors();
}

void Service::SetCapabilities(CapabilitySnapshot capabilities) {
  capabilities_ = std::move(capabilities);
  capability_revision_ = capabilities_.revision;
  RefreshDescriptors();
  if (!capabilities_set_) {
    capabilities_set_ = true;
    boot_effective_.clear();
    for (const Descriptor& descriptor : descriptors_) {
      bool corrected = false;
      std::string reason;
      boot_effective_[descriptor.id] =
          ReconcileEffective(descriptor, RequestedValue(descriptor.id), &corrected, &reason);
    }
  }
}

void Service::SetReconciler(const Reconciler* reconciler) { reconciler_ = reconciler; }

void Service::SetProviderSink(ProviderSink* sink) {
  provider_ = sink;
  if (sink != nullptr) {
    SetProviderValues(sink->ReadValues());
    RecoverCommit(&recovery_error_);
  }
}

void Service::SetProviderValues(std::map<SettingId, Value> values) {
  provider_values_ = std::move(values);
}

void Service::SetOverrides(std::map<SettingId, Value> values, std::map<SettingId, Source> sources) {
  external_ = std::move(values);
  external_source_ = std::move(sources);
}

void Service::SetBootValues(std::map<SettingId, Value> values, std::string reason) {
  boot_corrections_ = std::move(values);
  boot_correction_reason_ = std::move(reason);
  for (const auto& [id, value] : boot_corrections_) boot_effective_[id] = value;
}

void Service::SetMenuRows(std::vector<Choice> rows) {
  schema::SetMenuRows(std::move(rows));
  RefreshDescriptors();
}

const Descriptor* Service::DescriptorFor(SettingId id) const {
  for (const Descriptor& descriptor : descriptors_) {
    if (descriptor.id == id) {
      return &descriptor;
    }
  }
  return nullptr;
}

Value Service::RequestedValue(SettingId id) const {
  const Descriptor* descriptor = DescriptorFor(id);
  if (descriptor == nullptr) {
    return Value{};
  }
  if (const auto it = store_.document().values.find(id);
      it != store_.document().values.end()) {
    return it->second;
  }
  if (descriptor->provider == Provider::kUltimate) {
    if (const auto it = provider_values_.find(id); it != provider_values_.end()) {
      return it->second;
    }
  }
  return descriptor->compiled_default;
}

Source Service::SourceFor(SettingId id, bool provider_row) const {
  if (external_source_.count(id) != 0) {
    return external_source_.at(id);
  }
  if (store_.document().values.count(id) != 0) {
    const auto origin = store_.document().origins.find(id);
    return origin != store_.document().origins.end() && origin->second == Origin::kAutomatic
               ? Source::kRecommendedFirstRun : Source::kSettingsFile;
  }
  if (provider_row) {
    return Source::kProvider;
  }
  if (const auto origin = store_.document().origins.find(id);
      origin != store_.document().origins.end() && origin->second == Origin::kAutomatic) {
    return Source::kRecommendedFirstRun;
  }
  return Source::kCompiledDefault;
}

Value Service::ReconcileEffective(const Descriptor& descriptor, const Value& requested,
                                  bool* corrected, std::string* correction_reason_key) const {
  if (const auto it = boot_corrections_.find(descriptor.id); it != boot_corrections_.end()) {
    if (corrected) *corrected = !ValueEquals(it->second, requested);
    if (correction_reason_key) *correction_reason_key = boot_correction_reason_;
    return it->second;
  }
  const Reconciler* reconciler = reconciler_ != nullptr ? reconciler_ : &default_reconciler_;
  const Reconciliation result =
      reconciler->Reconcile(descriptor.id, descriptor, requested, capabilities_);
  if (corrected != nullptr) {
    *corrected = result.corrected;
  }
  if (correction_reason_key != nullptr) {
    *correction_reason_key = result.correction_reason_key;
  }
  return result.ok ? result.effective : descriptor.compiled_default;
}

Value Service::EffectiveValue(SettingId id) const {
  const Descriptor* descriptor = DescriptorFor(id);
  if (descriptor == nullptr) {
    return Value{};
  }
  if (const auto it = external_.find(id); it != external_.end()) {
    return it->second;
  }
  bool corrected = false;
  std::string reason;
  return ReconcileEffective(*descriptor, RequestedValue(id), &corrected, &reason);
}

Source Service::EffectiveSource(SettingId id) const {
  const Descriptor* descriptor = DescriptorFor(id);
  const bool provider_row = descriptor != nullptr && descriptor->provider == Provider::kUltimate;
  if (external_.count(id) != 0) {
    return external_source_.count(id) != 0 ? external_source_.at(id) : Source::kCommandLine;
  }
  return SourceFor(id, provider_row);
}

bool Service::IsOverridden(SettingId id) const { return external_.count(id) != 0; }

Snapshot Service::GetSnapshot() const {
  Snapshot snapshot;
  snapshot.schema_version = kSnapshotSchemaVersion;
  snapshot.revision = revision_;
  snapshot.capabilities = capabilities_;
  snapshot.descriptors = descriptors_;
  snapshot.fields.reserve(descriptors_.size());
  for (const Descriptor& descriptor : descriptors_) {
    FieldState field;
    field.id = descriptor.id;
    field.requested = RequestedValue(descriptor.id);
    if (const auto it = pending_.find(descriptor.id); it != pending_.end()) {
      field.pending = it->second;
    } else {
      field.pending = field.requested;
    }

    const bool provider_row = descriptor.provider == Provider::kUltimate;
    if (const auto it = external_.find(descriptor.id); it != external_.end()) {
      field.effective = it->second;
      field.overridden = true;
      field.effective_source = external_source_.count(descriptor.id) != 0
                                   ? external_source_.at(descriptor.id)
                                   : Source::kCommandLine;
    } else {
      field.effective =
          ReconcileEffective(descriptor, field.requested, &field.corrected,
                             &field.correction_reason_key);
      field.effective_source = SourceFor(descriptor.id, provider_row);
    }

    if (const auto origin = store_.document().origins.find(descriptor.id);
        origin != store_.document().origins.end()) {
      field.origin = origin->second;
    }
    if (const auto boot = boot_effective_.find(descriptor.id); boot != boot_effective_.end()) {
      field.restart_required =
          descriptor.restart_scoped() && !ValueEquals(field.requested, boot->second);
    }
    snapshot.fields.push_back(std::move(field));
  }
  return snapshot;
}

uint64_t Service::BeginEdit(uint64_t base_revision) {
  if (base_revision != revision_) {
    return 0;  // stale: the caller re-reads GetSnapshot() and starts again
  }
  pending_.clear();
  txn_ = ++txn_seq_;
  return txn_;
}

ValidationResult Service::Set(uint64_t txn, SettingId id, const Value& value,
                             uint64_t capability_revision) {
  if (txn == 0 || txn != txn_) {
    return ValidationResult{false, "settings.stale_transaction"};
  }
  const Descriptor* descriptor = DescriptorFor(id);
  if (descriptor == nullptr) {
    return ValidationResult{false, "settings.unknown_row"};
  }
  if (IsAction(id)) {
    return ValidationResult{false, "settings.action_not_settable"};
  }
  if (descriptor->availability == Availability::kHidden) {
    return ValidationResult{false, "settings.row_hidden"};
  }
  if (descriptor->availability == Availability::kVisibleDisabled) {
    return ValidationResult{false, descriptor->disabled_reason_key.empty()
                                       ? std::string("settings.row_disabled")
                                       : descriptor->disabled_reason_key};
  }
  if (external_.count(id) != 0) {
    const Source source = external_source_.count(id) != 0 ? external_source_.at(id)
                                                         : Source::kCommandLine;
    return ValidationResult{false, OverrideExplanation(source, SettingIdToken(id))};
  }
  if (capability_revision != capability_revision_) {
    // The choices this edit was based on are stale; refuse rather than clamp by index.
    return ValidationResult{false, "settings.capabilities_changed"};
  }
  const Reconciler* reconciler = reconciler_ != nullptr ? reconciler_ : &default_reconciler_;
  const ValidationResult validation = reconciler->Validate(id, *descriptor, value, capabilities_);
  if (!validation.ok) {
    return validation;
  }
  pending_[id] = value;
  return validation;
}

Value Service::CancelField(uint64_t txn, SettingId id) {
  if (txn == 0 || txn != txn_) {
    return Value{};
  }
  const Value previous = RequestedValue(id);
  pending_.erase(id);
  return previous;
}

std::filesystem::path Service::JournalPath() const {
  std::filesystem::path journal = store_.paths().settings_file;
  if (!journal.empty()) {
    journal += ".commit-journal";
  }
  return journal;
}

CommitResult Service::Commit(uint64_t txn) {
  CommitResult result;
  result.saved_revision = revision_;
  last_commit_changes_.clear();
  if (txn == 0 || txn != txn_) {
    result.status = CommitStatus::kInvalid;
    result.errors.push_back("settings.stale_transaction");
    return result;
  }
  if (pending_.empty()) {
    result.status = CommitStatus::kNoChanges;
    txn_ = 0;
    return result;
  }

  rb_blitz::fs::FileLock commit_lock;
  std::string error;
  if (!commit_lock.Acquire(JournalPath(), &error) || !RecoverCommit(&error)) {
    result.status = CommitStatus::kIoError;
    result.errors.push_back(error);
    return result;
  }

  std::map<SettingId, Value> provider_rows;
  std::map<SettingId, Value> native_rows;
  bool restart = false;
  for (const auto& [id, value] : pending_) {
    if (ValueEquals(RequestedValue(id), value)) continue;
    if (IsProviderFileRow(id)) {
      provider_rows[id] = value;
    } else {
      native_rows[id] = value;
    }
    last_commit_changes_[id] = value;
    const Descriptor* descriptor = DescriptorFor(id);
    if (descriptor != nullptr && descriptor->restart_scoped()) {
      restart = true;
    }
  }
  // P9: remember what this commit touched before `pending_` is cleared, so the caller can ask
  // the application-strategy seam what applies live and what waits for a restart.
  const bool native = !native_rows.empty();
  const bool combined = native && !provider_rows.empty();
  const std::filesystem::path journal = JournalPath();
  if (!provider_rows.empty() &&
      (!provider_ || provider_->Readiness() != ProviderReadiness::kCompatible)) {
    result.status = CommitStatus::kPartial;
    result.errors.push_back("settings.provider_unavailable");
    return result;
  }
  if (combined && !journal.empty()) {
    toml::Document recovery;
    recovery.Set("", "schema_version", "1");
    for (const auto& [id, value] : native_rows) {
      JournalSet(recovery, "native", id, value);
      const auto previous = store_.document().values.find(id);
      if (previous != store_.document().values.end()) JournalSet(recovery, "base", id, previous->second);
    }
    for (const auto& [id, value] : provider_rows) JournalSet(recovery, "provider", id, value);
    if (!WriteFileAtomic(journal, recovery.Render(), &error)) {
      result.status = CommitStatus::kIoError;
      result.errors.push_back(error);
      return result;
    }
  }

  bool wrote_native = false;
  bool provider_written = false;
  if (native) {
    for (const auto& [id, value] : native_rows) store_.MarkChanged(id, value, Origin::kManual);
    const SaveOutcome outcome = store_.Save();
    switch (outcome.status) {
      case CommitStatus::kConflict:
        result.status = CommitStatus::kConflict;
        result.conflicts = outcome.conflicts;
        result.errors = outcome.errors;
        break;
      case CommitStatus::kInvalid:
      case CommitStatus::kIoError:
        result.status = outcome.status;
        result.errors = outcome.errors;
        break;
      default:
        wrote_native = outcome.wrote;
        break;
    }
    if (result.status == CommitStatus::kConflict || result.status == CommitStatus::kInvalid ||
        result.status == CommitStatus::kIoError) {
      if (!journal.empty()) {
        std::error_code ignored;
        std::filesystem::remove(journal, ignored);
      }
      store_.Load();  // Restore requested state; pending edits remain available for retry.
      return result;
    }
  }

  if (!provider_rows.empty()) {
    if (provider_ == nullptr) {
      result.status = CommitStatus::kPartial;
      result.errors.push_back("settings.provider_unavailable");
    } else {
      const ProviderSaveResult saved = provider_->Save(provider_rows);
      provider_written = saved.durable;
      if (!saved.durable) {
        result.status = CommitStatus::kPartial;
        if (!saved.error.empty()) {
          result.errors.push_back(saved.error);
        }
      } else {
        for (const auto& [id, value] : provider_rows) provider_values_[id] = value;
      }
    }
  }

  if (result.status != CommitStatus::kPartial && (wrote_native || provider_written)) {
    ++revision_;  // only a durable commit advances the revision
  }
  if (result.status != CommitStatus::kPartial) {
    result.status = (wrote_native || provider_written) ? CommitStatus::kSaved : CommitStatus::kNoChanges;
    if (!journal.empty()) {
      std::error_code ignored;
      std::filesystem::remove(journal, ignored);
    }
    for (const auto& [id, value] : pending_) boot_corrections_.erase(id);
  }

  result.saved_revision = revision_;
  result.restart_required = result.status != CommitStatus::kPartial && restart && (wrote_native || provider_written);
  result.provider_written = provider_written;
  if (result.status != CommitStatus::kPartial) {
    pending_.clear();
    txn_ = 0;
  }
  return result;
}

bool Service::RecoverCommit(std::string* error) {
  std::string text;
  bool exists = false;
  if (!ReadFileText(JournalPath(), &text, &exists)) {
    *error = "Cannot read settings commit journal";
    return false;
  }
  if (!exists) { recovery_error_.clear(); return true; }
  bool parsed = false;
  const auto journal = toml::Document::Parse(text, &parsed, error);
  std::map<SettingId, Value> native, base, provider;
  if (!parsed || journal.Get("", "schema_version").value != "1" ||
      !JournalRead(journal, "native", descriptors_, &native) ||
      !JournalRead(journal, "base", descriptors_, &base) ||
      !JournalRead(journal, "provider", descriptors_, &provider) || provider.empty() || native.empty()) {
    *error = "Unrecognized settings commit journal; original files retained";
    return false;
  }
  const auto disk = LoadSettingsDocument(paths_.settings_file);
  if (!disk.usable()) { *error = disk.error; return false; }
  bool committed = true, unchanged = true;
  for (const auto& [id, value] : native) {
    if (IsProviderFileRow(id)) { *error = "Invalid native journal row"; return false; }
    const auto current = disk.document.values.find(id);
    const auto previous = base.find(id);
    committed &= current != disk.document.values.end() && ValueEquals(current->second, value);
    unchanged &= previous == base.end() ? current == disk.document.values.end()
        : current != disk.document.values.end() && ValueEquals(current->second, previous->second);
  }
  if (!committed && !unchanged) {
    *error = "Settings changed since the interrupted save; recovery journal retained";
    return false;
  }
  if (committed) {
    if (!provider_ || provider_->Readiness() != ProviderReadiness::kCompatible) {
      *error = "Settings recovery needs the compatible Ultimate provider";
      return false;
    }
    for (const auto& [id, value] : provider) {
      const auto* descriptor = DescriptorFor(id);
      if (!IsProviderFileRow(id) || !descriptor ||
          !default_reconciler_.Validate(id, *descriptor, value, capabilities_).ok) {
        *error = "Interrupted provider settings are incompatible with this installation";
        return false;
      }
    }
    const auto saved = provider_->Save(provider);
    if (!saved.durable) { *error = saved.error; return false; }
    for (const auto& [id, value] : provider) provider_values_[id] = value;
    store_.Load();
    ++revision_;
  }
  std::error_code ec;
  std::filesystem::remove(JournalPath(), ec);
  if (ec) { *error = "Cannot finish settings recovery: " + ec.message(); return false; }
  recovery_error_.clear();
  return true;
}

void Service::Cancel(uint64_t txn) {
  if (txn == txn_) {
    pending_.clear();
    txn_ = 0;
  }
}

ActionResult Service::Invoke(SettingId action, uint64_t request_id) {
  if (action != SettingId::kUnlockOnlineAchievements) {
    return ActionResult{false, "settings.unknown_action"};
  }
  if (provider_ == nullptr) {
    return ActionResult{false, "settings.provider_unavailable"};
  }
  return provider_->Invoke(action, request_id);
}

RestartResult Service::RequestRestart(uint64_t saved_revision, uint64_t request_id) {
  RestartResult result;
  std::error_code ec;
  if (std::filesystem::exists(JournalPath(), ec) || ec) {
    result.status = RestartStatus::kNotSaved;
    result.error = "settings.partial_save";
    return result;
  }
  if (saved_revision != revision_) {
    result.status = RestartStatus::kStaleRevision;
    result.error = "settings.stale_revision";
    return result;
  }
  if (restart_requested_) {
    result.status = RestartStatus::kAlreadyRequested;
    return result;
  }
  restart_requested_ = true;
  restart_request_id_ = request_id;
  result.status = RestartStatus::kOk;
  return result;
}

}  // namespace rb_blitz::settings
