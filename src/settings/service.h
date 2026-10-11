// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 — the settings service (contracts §5.2/§5.3/§5.7/§5.8/§6). It owns requested/effective/
// pending state, revisioned edit transactions, the durable commit, the provider save seam and
// the restart handshake. Capability reconciliation stays behind the pure Reconciler interface
// P4 implements; the provider save stays behind ProviderSink, which P6 implements, and P3
// never reports a provider save as durable on its own word.
//
// SDK-free: the service is host logic and the tests drive it with no boot.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "launcher/profile.h"
#include "settings/model.h"
#include "settings/migration.h"
#include "settings/paths.h"
#include "settings/store.h"

namespace rb_blitz::settings {

// §5.7: whether the installed provider can be used at all, independent of the active target.
enum class ProviderReadiness : uint8_t {
  kAbsent,
  kInactiveTarget,
  kCompatible,
  kUnrecognizedShape,
};

struct ProviderSaveResult {
  bool durable = false;  // host I/O completion confirmed against the real file
  std::string error;
};

// The provider integration seam (P6). P3 holds a pointer and reports exactly what the sink
// reports — no sink means a commit that touches a provider row is a partial save, never a
// fake success.
class ProviderSink {
 public:
  virtual ~ProviderSink() = default;
  virtual ProviderReadiness Readiness() const = 0;
  // The provider's current values for its own rows (read from the installed payload).
  virtual std::map<SettingId, Value> ReadValues() const { return {}; }
  // Persist the provider rows named in `pending`. Durable only against the real file.
  virtual ProviderSaveResult Save(const std::map<SettingId, Value>& pending) = 0;
  // One immediate action (e.g. the achievements grant) with a one-shot request id.
  virtual ActionResult Invoke(SettingId action, uint64_t request_id) = 0;
};

struct Reconciliation {
  bool ok = true;
  std::string reason_key;
  Value effective;
  bool corrected = false;
  std::string correction_reason_key;
};

// P0's pure reconciliation interface. P4 replaces this with the real graphics policy; the
// default implementation only enforces the row's own kind and choice set and passes a valid
// value through unchanged.
class Reconciler {
 public:
  virtual ~Reconciler() = default;
  virtual ValidationResult Validate(SettingId id, const Descriptor& descriptor, const Value& value,
                                    const CapabilitySnapshot& caps) const;
  virtual Reconciliation Reconcile(SettingId id, const Descriptor& descriptor,
                                   const Value& requested, const CapabilitySnapshot& caps) const;
};

// §5.8. Every method runs on the caller's thread; no guest pointer crosses it.
class Service {
 public:
  explicit Service(StorePaths paths);
  ~Service();

  // --- boot ---------------------------------------------------------------
  // Loads settings.toml, migrates a legacy profile when one is supplied, and resolves the
  // document into requested/effective state. `legacy` may be null when there is none;
  // `legacy_usable` false reports an unreadable legacy file without importing from it.
  DocumentStatus Boot(const rb_blitz::launcher::Profile* legacy, const PathEntry& existing_paths,
                      MigrationResult* migration, bool legacy_usable = true);

  // The host installs the probed capabilities (P2/P4) and the provider (P6).
  void SetCapabilities(CapabilitySnapshot capabilities);
  const CapabilitySnapshot& capabilities() const { return capabilities_; }
  void SetReconciler(const Reconciler* reconciler);
  void SetProviderSink(ProviderSink* sink);
  void SetProviderValues(std::map<SettingId, Value> values);
  // Values an explicit developer config / environment / CLI won, with the source that won.
  void SetOverrides(std::map<SettingId, Value> values, std::map<SettingId, Source> sources);
  void SetMenuRows(std::vector<Choice> rows);  // P10's Hide Main Menu seam

  Store& store() { return store_; }
  const Store& store() const { return store_; }
  DocumentStatus load_status() const { return load_status_; }
  uint64_t revision() const { return revision_; }
  uint64_t capability_revision() const { return capability_revision_; }

  // --- the frozen bridge API ---------------------------------------------
  Snapshot GetSnapshot() const;
  uint64_t BeginEdit(uint64_t base_revision);
  ValidationResult Set(uint64_t txn, SettingId id, const Value& value,
                       uint64_t capability_revision);
  Value CancelField(uint64_t txn, SettingId id);
  CommitResult Commit(uint64_t txn);
  void Cancel(uint64_t txn);
  ActionResult Invoke(SettingId action, uint64_t request_id);
  RestartResult RequestRestart(uint64_t saved_revision, uint64_t request_id);

  // --- helpers the UI and the boot wiring read ---------------------------
  const Descriptor* DescriptorFor(SettingId id) const;
  Value RequestedValue(SettingId id) const;
  Value EffectiveValue(SettingId id) const;
  Source EffectiveSource(SettingId id) const;
  bool IsOverridden(SettingId id) const;
  const std::vector<Descriptor>& descriptors() const { return descriptors_; }

 private:
  void RefreshDescriptors();
  Value ReconcileEffective(const Descriptor& descriptor, const Value& requested, bool* corrected,
                           std::string* correction_reason_key) const;
  Source SourceFor(SettingId id, bool provider_row) const;
  std::filesystem::path JournalPath() const;

  StorePaths paths_;
  Store store_;
  CapabilitySnapshot capabilities_;
  std::vector<Descriptor> descriptors_;
  std::map<SettingId, Value> provider_values_;
  std::map<SettingId, Value> external_;
  std::map<SettingId, Source> external_source_;
  std::map<SettingId, Value> boot_effective_;
  DocumentStatus load_status_ = DocumentStatus::kMissing;
  Reconciler default_reconciler_;
  const Reconciler* reconciler_ = nullptr;
  ProviderSink* provider_ = nullptr;
  bool capabilities_set_ = false;
  uint64_t revision_ = 0;
  uint64_t capability_revision_ = 1;
  uint64_t txn_seq_ = 0;
  uint64_t txn_ = 0;
  std::map<SettingId, Value> pending_;
  bool restart_requested_ = false;
  uint64_t restart_request_id_ = 0;
};

// Nil-order helper: the rows whose value lives in the provider's own file rather than
// settings.toml. kUnlockAll is the exception — it persists under [ultimate_overrides].
bool IsProviderFileRow(SettingId id);

}  // namespace rb_blitz::settings
