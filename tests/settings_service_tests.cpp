// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 host tests for the settings service (src/settings/service.{h,cpp}). No SDK, no boot.
//
// The behaviours the parent plan asks P3 to prove: durable read/edit/reload, a rejected edit
// that leaves a valid manual value alone, requested/effective/pending staying separate, an
// explicit override disabling a row instead of saving something that cannot take effect, a
// provider save that is only ever reported as durable when the sink says so, and the restart
// handshake's stale/already-requested answers.

#include "check.h"

#include "settings/cvars.h"
#include "settings/service.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {

using namespace rb_blitz::settings;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

struct Scratch {
  fs::path base;
  Scratch() {
    std::error_code code;
    base = fs::temp_directory_path(code) / "rb_blitz-settings-service";
    fs::remove_all(base, code);
    fs::create_directories(base, code);
  }
  ~Scratch() {
    std::error_code code;
    fs::remove_all(base, code);
  }
};

StorePaths Paths(const Scratch& scratch) {
  StorePaths paths;
  paths.settings_file = scratch.base / "settings.toml";
  paths.paths_file = scratch.base / "paths.toml";
  paths.legacy_profile = scratch.base / "launcher.toml";
  return paths;
}

Value BoolV(bool value) {
  Value out;
  out.kind = ValueKind::kBool;
  out.boolean = value;
  return out;
}

Value EnumV(std::string token) {
  Value out;
  out.kind = ValueKind::kEnum;
  out.text = std::move(token);
  return out;
}

Value ResolutionV(int32_t width, int32_t height) {
  Value out;
  out.kind = ValueKind::kResolution;
  out.width = width;
  out.height = height;
  return out;
}

void WriteFile(const fs::path& path, const std::string& text) {
  std::error_code code;
  fs::create_directories(path.parent_path(), code);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

CapabilitySnapshot Capabilities(uint64_t revision = 1) {
  CapabilitySnapshot caps;
  caps.revision = revision;
  caps.logical_processors = 4;
  caps.supported_resolutions.push_back(DisplayMode{1280, 720});
  caps.supported_resolutions.push_back(DisplayMode{1920, 1080});
  RendererCapability renderer;
  renderer.token = "d3d12";
  renderer.built = renderer.detected = renderer.usable = renderer.active = true;
  renderer.filtering_ceiling = 5;
  renderer.native_aa_label = "2x";
  caps.renderers.push_back(renderer);
  caps.provider.installed = true;
  caps.provider.compatible = true;
  caps.provider.active = true;
  caps.provider.version = "2.11";
  return caps;
}

// A reconciler that clamps the scan-thread row, standing in for P4's graphics policy.
class ClampingReconciler : public Reconciler {
 public:
  Reconciliation Reconcile(SettingId id, const Descriptor& descriptor, const Value& requested,
                           const CapabilitySnapshot& caps) const override {
    Reconciliation result;
    result.effective = requested;
    if (id == SettingId::kDlcScanThreads) {
      const int32_t ceiling = static_cast<int32_t>(caps.logical_processors < 1 ? 1
                                                                              : caps.logical_processors);
      if (requested.integer > ceiling) {
        result.effective.integer = ceiling;
        result.corrected = true;
        result.correction_reason_key = "settings.corrected.processor_count";
      }
    }
    return result;
  }
};

struct FakeProvider : ProviderSink {
  ProviderReadiness readiness = ProviderReadiness::kCompatible;
  bool durable = true;
  int saves = 0;
  std::map<SettingId, Value> values;

  ProviderReadiness Readiness() const override { return readiness; }
  std::map<SettingId, Value> ReadValues() const override { return values; }
  ProviderSaveResult Save(const std::map<SettingId, Value>& pending) override {
    ++saves;
    return durable ? ProviderSaveResult{true, ""} : ProviderSaveResult{false, "disk full"};
  }
  ActionResult Invoke(SettingId action, uint64_t request_id) override {
    (void)action;
    (void)request_id;
    return ActionResult{true, ""};
  }
};

void TestDurableEditReload() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);

  BeginCase("service: a fresh boot has no file and shows the compiled defaults");
  Service service(paths);
  CHECK_TRUE(service.Boot(nullptr, PathEntry{}, nullptr) == DocumentStatus::kMissing);
  service.SetCapabilities(Capabilities());
  Snapshot snapshot = service.GetSnapshot();
  CHECK_EQ(snapshot.revision, 0);
  const FieldState* vsync = nullptr;
  for (const FieldState& field : snapshot.fields) {
    if (field.id == SettingId::kVsync) {
      vsync = &field;
    }
  }
  CHECK_TRUE(vsync != nullptr);
  CHECK_TRUE(vsync->requested.boolean == true);
  CHECK_TRUE(vsync->effective.boolean == true);
  CHECK_TRUE(vsync->effective_source == Source::kCompiledDefault);
  CHECK_FALSE(vsync->overridden);

  BeginCase("service: a stale base revision is refused");
  CHECK_EQ(service.BeginEdit(99), 0);

  BeginCase("service: edit, commit and a durable read back");
  const uint64_t txn = service.BeginEdit(service.revision());
  CHECK_TRUE(txn != 0);
  CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  const CommitResult committed = service.Commit(txn);
  CHECK_TRUE(committed.status == CommitStatus::kSaved);
  CHECK_EQ(committed.saved_revision, 1);
  // P9: V-Sync is the one row with measured live evidence, so a commit of it alone does not ask
  // for a restart - the application seam applies it to the running game instead (§5.5).
  CHECK_FALSE(committed.restart_required);
  CHECK_EQ(service.revision(), 1);
  CHECK_EQ(service.last_commit_changes().size(), 1u);
  CHECK_EQ(service.last_commit_changes().count(SettingId::kVsync), 1u);

  Service reloaded(paths);
  reloaded.Boot(nullptr, PathEntry{}, nullptr);
  reloaded.SetCapabilities(Capabilities());
  CHECK_TRUE(reloaded.RequestedValue(SettingId::kVsync).boolean == false);
  CHECK_TRUE(reloaded.EffectiveSource(SettingId::kVsync) == Source::kSettingsFile);

  BeginCase("service: a session with a restart row asks for the restart (P9)");
  // The hot rule is per row, not per session: one restart-strategy row in the same edit is what
  // makes the prompt appear, and the commit still reports exactly what it wrote.
  const uint64_t mixed = service.BeginEdit(service.revision());
  CHECK_TRUE(
      service.Set(mixed, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  CHECK_TRUE(
      service.Set(mixed, SettingId::kDlcMenuSongs, BoolV(true), service.capability_revision()).ok);
  const CommitResult mixed_commit = service.Commit(mixed);
  CHECK_TRUE(mixed_commit.status == CommitStatus::kSaved);
  CHECK_TRUE(mixed_commit.restart_required);
  CHECK_EQ(service.last_commit_changes().size(), 1u);  // unchanged VSync isn't written again

  BeginCase("service: a commit with nothing staged writes nothing");
  const uint64_t idle = reloaded.BeginEdit(reloaded.revision());
  const CommitResult no_changes = reloaded.Commit(idle);
  CHECK_TRUE(no_changes.status == CommitStatus::kNoChanges);
  CHECK_EQ(reloaded.revision(), 0);
}

void TestValidationAndPending() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());

  BeginCase("service: an unknown choice and a wrong kind are refused");
  const uint64_t txn = service.BeginEdit(service.revision());
  const ValidationResult bad_choice =
      service.Set(txn, SettingId::kFiltering, EnumV("3x"), service.capability_revision());
  CHECK_FALSE(bad_choice.ok);
  const ValidationResult bad_kind =
      service.Set(txn, SettingId::kVsync, EnumV("native"), service.capability_revision());
  CHECK_FALSE(bad_kind.ok);

  BeginCase("service: a stale capability revision is refused instead of clamped");
  const ValidationResult stale =
      service.Set(txn, SettingId::kResolution, ResolutionV(1280, 720), 77);
  CHECK_FALSE(stale.ok);
  CHECK_TRUE(stale.reason_key == "settings.capabilities_changed");

  BeginCase("service: pending is separate from requested and CancelField restores it");
  CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  const Snapshot pending_snapshot = service.GetSnapshot();
  for (const FieldState& field : pending_snapshot.fields) {
    if (field.id == SettingId::kVsync) {
      CHECK_TRUE(field.requested.boolean == true);
      CHECK_TRUE(field.pending.boolean == false);
    }
  }
  const Value restored = service.CancelField(txn, SettingId::kVsync);
  CHECK_TRUE(restored.boolean == true);
  service.Cancel(txn);

  BeginCase("service: Cancel discards the whole transaction");
  const uint64_t txn2 = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn2, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  service.Cancel(txn2);
  const CommitResult after_cancel = service.Commit(txn2);
  CHECK_TRUE(after_cancel.status == CommitStatus::kInvalid);
}

void TestOverridesAndCorrections() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());
  const ClampingReconciler reconciler;
  service.SetReconciler(&reconciler);

  BeginCase("service: an explicit override disables the row and shows the effective value");
  std::map<SettingId, Value> overrides;
  overrides[SettingId::kVsync] = BoolV(false);
  std::map<SettingId, Source> sources;
  sources[SettingId::kVsync] = Source::kCommandLine;
  service.SetOverrides(overrides, sources);
  const uint64_t txn = service.BeginEdit(service.revision());
  const ValidationResult refused =
      service.Set(txn, SettingId::kVsync, BoolV(true), service.capability_revision());
  CHECK_FALSE(refused.ok);
  CHECK_TRUE(refused.reason_key.find("overridden") != std::string::npos);
  const Snapshot snapshot = service.GetSnapshot();
  for (const FieldState& field : snapshot.fields) {
    if (field.id == SettingId::kVsync) {
      CHECK_TRUE(field.overridden);
      CHECK_TRUE(field.effective_source == Source::kCommandLine);
      CHECK_TRUE(field.effective.boolean == false);
    }
  }
  service.Cancel(txn);
}

void TestBootCorrection() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  // The stored value was valid when it was chosen (a 32-core machine) and is not a choice on
  // the machine booting now. The request is kept; the effective value is corrected.
  WriteFile(paths.settings_file,
            "schema_version = 1\n\n[general]\ndlc_scan_threads = 16\n");
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());
  const ClampingReconciler reconciler;
  service.SetReconciler(&reconciler);

  BeginCase("service: a now-invalid manual value is corrected while the request stays");
  bool found = false;
  for (const FieldState& field : service.GetSnapshot().fields) {
    if (field.id != SettingId::kDlcScanThreads) {
      continue;
    }
    found = true;
    CHECK_TRUE(field.requested.integer == 16);
    CHECK_TRUE(field.effective.integer == 4);
    CHECK_TRUE(field.corrected);
    CHECK_TRUE(field.correction_reason_key == "settings.corrected.processor_count");
  }
  CHECK_TRUE(found);
  // The correction is not written back: the manual request survives a temporary change.
  CHECK_TRUE(service.RequestedValue(SettingId::kDlcScanThreads).integer == 16);
}

void TestProviderSeam() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());

  BeginCase("service: provider rows are hidden without a payload and disabled for base");
  {
    CapabilitySnapshot caps = Capabilities();
    caps.provider.installed = false;
    Service bare(paths);
    bare.Boot(nullptr, PathEntry{}, nullptr);
    bare.SetCapabilities(caps);
    CHECK_TRUE(bare.DescriptorFor(SettingId::kHideHud)->availability == Availability::kHidden);
  }
  {
    CapabilitySnapshot caps = Capabilities();
    caps.provider.active = false;
    Service inactive(paths);
    inactive.Boot(nullptr, PathEntry{}, nullptr);
    inactive.SetCapabilities(caps);
    const Descriptor* row = inactive.DescriptorFor(SettingId::kHideHud);
    CHECK_TRUE(row->availability == Availability::kVisibleDisabled);
    CHECK_TRUE(row->disabled_reason_key == "settings.requires_ultimate");
    const uint64_t txn = inactive.BeginEdit(inactive.revision());
    CHECK_FALSE(inactive.Set(txn, SettingId::kHideHud, BoolV(true), inactive.capability_revision()).ok);
    inactive.Cancel(txn);
  }

  BeginCase("service: a provider commit without a sink is a partial save, never a fake success");
  {
    const uint64_t txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), service.capability_revision()).ok);
    const CommitResult partial = service.Commit(txn);
    CHECK_TRUE(partial.status == CommitStatus::kPartial);
    CHECK_FALSE(partial.provider_written);
    CHECK_FALSE(partial.restart_required);
  }

  BeginCase("service: a durable provider save is reported and a failed one is not");
  FakeProvider provider;
  service.SetProviderSink(&provider);
  {
    const uint64_t txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), service.capability_revision()).ok);
    const CommitResult saved = service.Commit(txn);
    CHECK_TRUE(saved.status == CommitStatus::kSaved);
    CHECK_TRUE(saved.provider_written);
    CHECK_EQ(provider.saves, 1);
    CHECK_TRUE(provider.values.empty());
  }
  provider.durable = false;
  {
    const uint64_t txn = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(txn, SettingId::kShowTimer, BoolV(true), service.capability_revision()).ok);
    const CommitResult failed = service.Commit(txn);
    CHECK_TRUE(failed.status == CommitStatus::kPartial);
    CHECK_FALSE(failed.provider_written);
  }
}

void TestCombinedCommitAndRestart() {
  Scratch scratch;
  const StorePaths paths = Paths(scratch);
  Service service(paths);
  service.Boot(nullptr, PathEntry{}, nullptr);
  service.SetCapabilities(Capabilities());
  FakeProvider provider;
  service.SetProviderSink(&provider);

  BeginCase("service: a native+provider commit completes and clears its journal");
  const uint64_t txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), service.capability_revision()).ok);
  CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), service.capability_revision()).ok);
  const CommitResult combined = service.Commit(txn);
  CHECK_TRUE(combined.status == CommitStatus::kSaved);
  CHECK_TRUE(combined.provider_written);
  CHECK_TRUE(combined.restart_required);
  CHECK_EQ(combined.saved_revision, 1);
  fs::path journal = paths.settings_file;
  journal += ".commit-journal";
  CHECK_FALSE(fs::exists(journal));
  CHECK_TRUE(LoadSettingsDocument(paths.settings_file).document.values.at(SettingId::kVsync).boolean ==
             false);

  BeginCase("service: the restart handshake refuses a stale or repeated request");
  CHECK_TRUE(service.RequestRestart(999, 1).status == RestartStatus::kStaleRevision);
  CHECK_TRUE(service.RequestRestart(service.revision(), 1).status == RestartStatus::kOk);
  CHECK_TRUE(service.RequestRestart(service.revision(), 2).status == RestartStatus::kAlreadyRequested);
}

void TestInterruptedCommitRecovery() {
  BeginCase("combined save: partial failure blocks restart and is recoverable after process exit");
  Scratch scratch;
  const auto paths = Paths(scratch);
  fs::path journal = paths.settings_file;
  journal += ".commit-journal";
  {
    Service service(paths);
    service.Boot(nullptr, {}, nullptr);
    service.SetCapabilities(Capabilities());
    FakeProvider provider;
    provider.durable = false;
    service.SetProviderSink(&provider);
    const auto txn = service.BeginEdit(0);
    CHECK_TRUE(service.Set(txn, SettingId::kVsync, BoolV(false), 1).ok);
    CHECK_TRUE(service.Set(txn, SettingId::kHideHud, BoolV(true), 1).ok);
    const auto result = service.Commit(txn);
    CHECK_TRUE(result.status == CommitStatus::kPartial);
    CHECK_EQ(service.revision(), 0u);
    CHECK_FALSE(result.restart_required);
    CHECK_TRUE(fs::exists(journal));
    CHECK_FALSE(LoadSettingsDocument(paths.settings_file).document.values.at(SettingId::kVsync).boolean);
    CHECK_TRUE(service.RequestRestart(0, 1).status == RestartStatus::kNotSaved);
  }
  {
    Service service(paths);
    service.Boot(nullptr, {}, nullptr);
    service.SetCapabilities(Capabilities());
    FakeProvider provider;
    service.SetProviderSink(&provider);
    CHECK_EQ(provider.saves, 1);
    CHECK_TRUE(service.RequestedValue(SettingId::kHideHud).boolean);
    CHECK_FALSE(fs::exists(journal));
    CHECK_EQ(service.revision(), 1u);
    const auto no_op = service.BeginEdit(service.revision());
    CHECK_TRUE(service.Set(no_op, SettingId::kHideHud, BoolV(true), 1).ok);
    CHECK_TRUE(service.Commit(no_op).status == CommitStatus::kNoChanges);
    CHECK_EQ(provider.saves, 1);
  }

  BeginCase("Auto survives a durable save and reload as intent, without fixing a pixel size");
  Service service(paths);
  service.Boot(nullptr, {}, nullptr);
  service.SetCapabilities(Capabilities());
  const auto txn = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(txn, SettingId::kResolution, ResolutionV(1920, 1080), 1).ok);
  CHECK_TRUE(service.Commit(txn).status == CommitStatus::kSaved);
  const auto automatic = service.BeginEdit(service.revision());
  CHECK_TRUE(service.Set(automatic, SettingId::kResolution, ResolutionV(0, 0), 1).ok);
  CHECK_TRUE(service.Commit(automatic).status == CommitStatus::kSaved);
  CHECK_TRUE(IsAutoResolution(LoadSettingsDocument(paths.settings_file).document.values.at(SettingId::kResolution)));
}

void TestCvarVocabulary() {
  BeginCase("cvars: the controller allowlist excludes Guide");
  CHECK_TRUE(IsControllerCvar("mouse_ui_nav"));
  CHECK_TRUE(IsControllerCvar("input_backend"));
  CHECK_FALSE(IsControllerCvar("guide_button"));
  CHECK_FALSE(IsControllerCvar("keybind_guide"));

  BeginCase("cvars: the allowlist keeps the controller rows and retires the rest");
  {
    // What a legacy launcher profile carries: two controller rows, the retired Guide pair, and the
    // native rows that are about to become the game's own settings file's business (P8).
    rb_blitz::launcher::ProfileSetting rows[6];
    rows[0].key = "mouse_ui_nav";
    rows[0].value = "true";
    rows[1].key = "input_backend";
    rows[1].value = "xinput";
    rows[2].key = "guide_button";
    rows[2].value = "true";
    rows[3].key = "keybind_guide";
    rows[3].value = "F1";
    rows[4].key = "user_data_root";
    rows[4].value = "D:\\Saves";
    rows[5].key = "dlc_root";
    rows[5].value = "D:\\DLC";
    const std::vector<rb_blitz::launcher::ProfileSetting> all(
        std::begin(rows), std::end(rows));

    const std::vector<rb_blitz::launcher::ProfileSetting> kept = ControllerProfileRows(all);
    CHECK_EQ(kept.size(), 2u);
    if (kept.size() == 2) {
      CHECK_TRUE(kept[0].key == "mouse_ui_nav" && kept[0].value == "true");
      CHECK_TRUE(kept[1].key == "input_backend" && kept[1].value == "xinput");
    }
    // Nothing to keep is not an error, and a table of only retired rows keeps nothing.
    CHECK_EQ(ControllerProfileRows({}).size(), 0u);
    const std::vector<rb_blitz::launcher::ProfileSetting> retired(rows + 2, rows + 4);
    CHECK_EQ(ControllerProfileRows(retired).size(), 0u);
  }

  BeginCase("cvars: the AA enum maps to the backend's two mechanisms");
  const std::vector<CvarAssignment> native = AntiAliasingToCvars("native");
  CHECK_EQ(native.size(), 2);
  CHECK_TRUE(native[0].name == "native_2x_msaa" && native[0].value == "true");
  CHECK_TRUE(native[1].name == "swap_post_effect" && native[1].value == "none");
  const std::vector<CvarAssignment> fxaa = AntiAliasingToCvars("fxaa");
  CHECK_TRUE(fxaa[0].value == "false" && fxaa[1].value == "fxaa");

  BeginCase("cvars: filtering never writes the label as the value");
  int32_t value = 0;
  CHECK_TRUE(FilteringToCvarValue("16x", &value));
  CHECK_EQ(value, 5);
  CHECK_TRUE(FilteringToCvarValue("native", &value));
  CHECK_EQ(value, -1);

  BeginCase("cvars: target translates to ultimate_mode and license_mask");
  LicensePolicy policy;
  policy.purchased_mask = 0xFF;
  policy.trial_mask = 0x01;
  const std::vector<CvarAssignment> ultimate =
      TargetToCvars(rb_blitz::launcher::LaunchTarget::kUltimate, policy);
  CHECK_TRUE(ultimate[0].name == "ultimate_mode" && ultimate[0].value == "1");
  CHECK_TRUE(ultimate[1].name == "license_mask" && ultimate[1].value == "255");
  const std::vector<CvarAssignment> demo =
      TargetToCvars(rb_blitz::launcher::LaunchTarget::kDemo, policy);
  CHECK_TRUE(demo[0].value == "0" && demo[1].value == "1");

  BeginCase("cvars: precedence ranks put explicit sources above the settings file");
  CHECK_TRUE(SourceRank(Source::kSettingsFile) > SourceRank(Source::kCompiledDefault));
  CHECK_TRUE(SourceRank(Source::kCommandLine) > SourceRank(Source::kSettingsFile));
  CHECK_TRUE(SourceRank(Source::kEnvironment) > SourceRank(Source::kDeveloperConfig));
}

}  // namespace

int main() {
  TestDurableEditReload();
  TestValidationAndPending();
  TestOverridesAndCorrections();
  TestBootCorrection();
  TestProviderSeam();
  TestCombinedCommitAndRestart();
  TestInterruptedCommitRecovery();
  TestCvarVocabulary();
  return Finish();
}
