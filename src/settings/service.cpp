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

#include "settings/cvars.h"
#include "settings/schema.h"

namespace rb_blitz::settings {

using rb_blitz::launcher::Profile;

namespace {

std::string ResolutionToken(const Value& value) {
  return std::to_string(value.width) + "x" + std::to_string(value.height);
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

void Service::RefreshDescriptors() { descriptors_ = schema::BuildDescriptors(capabilities_); }

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
  }
}

void Service::SetProviderValues(std::map<SettingId, Value> values) {
  provider_values_ = std::move(values);
}

void Service::SetOverrides(std::map<SettingId, Value> values, std::map<SettingId, Source> sources) {
  external_ = std::move(values);
  external_source_ = std::move(sources);
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
  if (txn == 0 || txn != txn_) {
    result.status = CommitStatus::kInvalid;
    result.errors.push_back("settings.stale_transaction");
    return result;
  }
  if (pending_.empty()) {
    result.status = CommitStatus::kNoChanges;
    return result;
  }

  std::map<SettingId, Value> provider_rows;
  bool restart = false;
  for (const auto& [id, value] : pending_) {
    if (IsProviderFileRow(id)) {
      provider_rows[id] = value;
    } else {
      store_.MarkChanged(id, value, Origin::kManual);
    }
    const Descriptor* descriptor = DescriptorFor(id);
    if (descriptor != nullptr && descriptor->restart_scoped()) {
      restart = true;
    }
  }
  // P9: remember what this commit touched before `pending_` is cleared, so the caller can ask
  // the application-strategy seam what applies live and what waits for a restart.
  last_commit_changes_ = pending_;

  const bool native = store_.has_changes();
  const bool combined = native && !provider_rows.empty();
  const std::filesystem::path journal = JournalPath();
  if (combined && !journal.empty()) {
    std::string error;
    if (!WriteFileAtomic(journal, "state=provider_pending\n", &error)) {
      result.status = CommitStatus::kIoError;
      result.errors.push_back(error);
      return result;
    }
  }

  bool wrote_native = false;
  bool provider_written = false;
  if (native) {
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
      } else if (provider_values_.empty()) {
        SetProviderValues(provider_->ReadValues());
      }
    }
  }

  if (wrote_native || provider_written) {
    ++revision_;  // only a durable commit advances the revision
  }
  if (result.status != CommitStatus::kPartial) {
    result.status = (wrote_native || provider_written) ? CommitStatus::kSaved : CommitStatus::kNoChanges;
    if (!journal.empty()) {
      std::error_code ignored;
      std::filesystem::remove(journal, ignored);
    }
  }

  result.saved_revision = revision_;
  result.restart_required = restart && (wrote_native || provider_written);
  result.provider_written = provider_written;
  pending_.clear();
  txn_ = 0;
  return result;
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
