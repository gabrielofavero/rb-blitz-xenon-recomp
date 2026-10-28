// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P11a host tests for the channel's real store (src/settings/bridge_sink.h). P1 proved the frames
// and the guest ABI with an in-memory sink; this proves the *other* half of the product claim - that
// the same frames, sent by the same screen, now edit `settings.toml` through P3's service - without
// an emulator, a window or a boot. The service is a real one over a temporary directory, so the
// assertions are made against bytes on disk and a second `Service` reading them back.
//
// What is deliberately checked here rather than in a boot: the text form a DTA script writes and the
// framed form a host caller writes both reach the same store; a commit is durable and
// acknowledged only when the service wrote; a value written back unchanged is *not* reported as a
// change (P9's no-op rule, which is what keeps a hot row from being re-applied); the provider's own
// rows never appear in settings.toml; the provider gate is kept locally; and an unknown setting or a
// wrong-kind value is refused instead of staged into nothing.

#include "check.h"

#include "hooks/settings_bridge.h"
#include "settings/bridge_sink.h"
#include "settings/cvars.h"
#include "settings/graphics_policy.h"
#include "settings/schema.h"
#include "settings/service.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::settings;
using namespace rb_blitz::test;

namespace fs = std::filesystem;
using bridge::Op;
using bridge::Request;
using bridge::Response;
using bridge::Status;
using bridge::WireValue;

struct Scratch {
  fs::path base;
  Scratch() {
    std::error_code code;
    base = fs::temp_directory_path(code) / "rb_blitz-settings-bridge-service";
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

std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

// The text form a DTA script writes, parsed with P1's own parser - so the test exercises the same
// grammar the merged screen's literals use rather than a second spelling of it.
Request Text(const std::string& path) {
  Request request;
  std::string error;
  if (!bridge::ParseTextRequest(path, &request, &error)) {
    Fail(__FILE__, __LINE__, "the text request did not parse: " + error + " (" + path + ")");
  }
  return request;
}

// One boot of the real service, with the channel's sink over it.
struct Harness {
  Scratch scratch;
  StorePaths paths = Paths(scratch);
  Service service{paths};
  ServiceSink sink{&service};
  bridge::Dispatcher dispatcher{&sink};

  Harness() {
    MigrationResult migration;
    service.Boot(nullptr, PathEntry{}, &migration, true);
  }

  Response Send(const std::string& text) { return dispatcher.Dispatch(Text(text)); }
  Response Send(const Request& request) { return dispatcher.Dispatch(request); }
  bool Holds(const std::string& setting, const WireValue& value) const {
    Request probe;
    probe.op = Op::kSnapshot;
    probe.setting = setting;
    probe.value = value;
    return sink.Matches(probe);
  }
};

void TestTextFormEditsTheRealStore() {
  BeginCase("the text form a DTA script writes reaches settings.toml");
  Harness h;
  // Stage *the other* value of the row: its compiled default is `true` (it took the setting over
  // from Controller Settings flag, P11b) and a commit that writes the value a row already holds is
  // `kNoChanges`, so writing `b:1` here would test nothing.
  const bool before = h.service.EffectiveValue(SettingId::kHideOfflinePrompts).boolean;
  CHECK_TRUE(h.Send(std::string("rbbz-settings/set/interface.hide_offline_prompts/b:") +
                    (before ? "0" : "1"))
                 .status == Status::kOk);
  const Response commit = h.Send("rbbz-settings/commit/-/-");
  CHECK_TRUE(commit.status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_durable());
  CHECK_TRUE(h.sink.saved_revision() != 0);

  // The read half is the membership probe: exactly what a row draws its own state from.
  CHECK_TRUE(h.Holds("interface.hide_offline_prompts", WireValue::Bool(!before)));
  CHECK_TRUE(!h.Holds("interface.hide_offline_prompts", WireValue::Bool(before)));

  // The change is reported once, by its token, for the application-strategy seam.
  CHECK_TRUE(h.sink.last_commit_changes().size() == 1);
  CHECK_TRUE(h.sink.last_commit_changes().at(0) == "interface.hide_offline_prompts");

  // ...and it is on disk, under the row's storage key with its provenance.
  const std::string text = ReadText(h.paths.settings_file);
  CHECK_TRUE(text.find("hide_offline_prompts") != std::string::npos);
  CHECK_TRUE(text.find("'interface.hide_offline_prompts'") != std::string::npos);

  // A second service over the same file boots into the same value: the write really landed.
  Service again(h.paths);
  MigrationResult migration;
  again.Boot(nullptr, PathEntry{}, &migration, true);
  CHECK_TRUE(again.EffectiveValue(SettingId::kHideOfflinePrompts).boolean == !before);
  CHECK_TRUE(again.EffectiveSource(SettingId::kHideOfflinePrompts) == Source::kSettingsFile);
}

void TestFramedFormCarriesTheRequestId() {
  BeginCase("the framed form carries a request id, and only a durable write is acknowledged");
  Harness h;
  Request set = Text("rbbz-settings/set/graphics.vsync/b:0");
  set.request_id = 41;
  CHECK_TRUE(h.Send(set).status == Status::kOk);
  Request commit;
  commit.op = Op::kCommit;
  commit.request_id = 42;
  CHECK_TRUE(h.Send(commit).status == Status::kOk);
  CHECK_TRUE(h.sink.Acknowledged(42));
  CHECK_TRUE(!h.sink.Acknowledged(43));  // never sent

  // The idempotency window: the same id and op do not reach the store twice.
  Request repeated;
  repeated.op = Op::kCommit;
  repeated.request_id = 42;
  h.Send(repeated);
  CHECK_TRUE(h.dispatcher.last_outcome() == bridge::Dispatcher::Outcome::kReplayed);

  // A refused request is *not* acknowledged, so a guest polling the ack cannot mistake a rejection
  // for a completed write.
  Request bad;
  bad.op = Op::kSet;
  bad.setting = "graphics.renderer";
  bad.value = WireValue::Bool(true);  // the renderer row takes a token, not a boolean
  bad.request_id = 44;
  CHECK_TRUE(h.Send(bad).status == Status::kInvalid);
  CHECK_TRUE(!h.sink.Acknowledged(44));
}

void TestNoOpCommitReportsNothing() {
  BeginCase("a value written back unchanged is not reported as a change");
  Harness h;
  const bool current = h.service.EffectiveValue(SettingId::kVsync).boolean;
  const std::string flip = current ? "rbbz-settings/set/graphics.vsync/b:0"
                                   : "rbbz-settings/set/graphics.vsync/b:1";
  const std::string back = current ? "rbbz-settings/set/graphics.vsync/b:1"
                                   : "rbbz-settings/set/graphics.vsync/b:0";

  // The first commit really writes: the row was never persisted, so "true" on disk is new state.
  CHECK_TRUE(h.Send(flip).status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_durable());
  CHECK_TRUE(h.sink.last_commit_changes().size() == 1);

  // Committing the same value again touches the row but changes nothing, so nothing is reported and
  // the store writes no file: that is what keeps a hot row from being re-applied (P9's rule).
  CHECK_TRUE(h.Send(flip).status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_changes().empty());
  CHECK_TRUE(!h.sink.last_commit_durable());
  CHECK_TRUE(h.sink.last_commit_status() == "no_changes");

  // The change-then-revert case the plan §5.4 calls a no-op, inside one session.
  CHECK_TRUE(h.Send(back).status == Status::kOk);
  CHECK_TRUE(h.Send(flip).status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_changes().empty());
  CHECK_TRUE(!h.sink.last_commit_durable());
}

void TestProviderRowsStayOutOfTheSettingsFile() {
  BeginCase("the provider's own rows never land in settings.toml");
  Harness h;
  // `interface.hide_hud` is a row the installed Ultimate payload owns; its value's home is that
  // payload's own file, which P6's write-through persists.
  CHECK_TRUE(IsProviderFileRow(SettingId::kHideHud));
  CHECK_TRUE(h.Send("rbbz-settings/set/interface.hide_hud/b:1").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_changes().size() == 1);
  CHECK_TRUE(h.sink.last_commit_changes().at(0) == "interface.hide_hud");
  CHECK_TRUE(h.Holds("interface.hide_hud", WireValue::Bool(true)));
  WireValue got;
  CHECK_TRUE(h.sink.Get("interface.hide_hud", &got));
  CHECK_TRUE(got.kind == bridge::ValueKind::kBool && got.boolean);

  // The deliverable that matters: the player's native settings file is untouched by a provider row.
  const std::string text = ReadText(h.paths.settings_file);
  CHECK_TRUE(text.find("hide_hud") == std::string::npos);

  // `unlock_all` is the documented exception in the plan (§6.2: it should persist natively under
  // `[ultimate_overrides]`, because the payload's own saved-variable list does not carry it). P6 left
  // that write open, and this pass keeps the row in the same place as the other provider rows rather
  // than pretending: its value is held by this build and written by the provider's write-through.
  CHECK_TRUE(IsProviderFileRow(SettingId::kUnlockAll));  // the service does not claim it either
  CHECK_TRUE(h.Send("rbbz-settings/set/general.unlock_all/b:1").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.Holds("general.unlock_all", WireValue::Bool(true)));
  const std::string after = ReadText(h.paths.settings_file);
  CHECK_TRUE(after.find("unlock_all") == std::string::npos);
}

void TestProviderGateAndUnknownTokens() {
  BeginCase("the provider gate is kept locally and an unknown token is refused");
  Harness h;
  CHECK_TRUE(h.Send("rbbz-settings/set/provider.active/b:1").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.Holds("provider.active", WireValue::Bool(true)));
  const std::string text = ReadText(h.paths.settings_file);
  CHECK_TRUE(text.find("provider.active") == std::string::npos);
  // The gate is a view, not a preference: a commit that only touches it writes no file at all.
  CHECK_TRUE(!h.sink.last_commit_durable());

  // A token this build does not describe is refused rather than silently staged, so a stale screen
  // cannot stage a change that lands nowhere.
  CHECK_TRUE(h.Send("rbbz-settings/set/nonsense.row/b:1").status == Status::kInvalid);
  CHECK_TRUE(h.Send("rbbz-settings/set/interface.hide_hud/i:7").status == Status::kInvalid);
  CHECK_TRUE(h.Send("rbbz-settings/set/general.target/t:not-a-target").status == Status::kInvalid);
  // An op the protocol does not name: the text form refuses to parse it at all, and a host caller
  // that builds a frame with kUnknown gets the dispatcher's answer rather than a store write.
  Request unknown_op;
  unknown_op.op = Op::kUnknown;
  CHECK_TRUE(h.Send(unknown_op).status == Status::kUnknownOp);
  CHECK_TRUE(h.Send("rbbz-settings/invoke/general.unlock_online_achievements/-").status ==
             Status::kUnsupported);  // the achievements action is not wired (P6 §6.1)
}

void TestCancelAndRestart() {
  BeginCase("cancelling restores the field, and the restart request is recorded");
  Harness h;
  // Stage the row's *other* value: `b:1` is its compiled default and a commit of an unchanged row
  // is `kNoChanges` (see `TestTextFormEditsTheRealStore`), so the field has to move for the cancel
  // to have something to restore.
  const bool before = h.service.EffectiveValue(SettingId::kHideOfflinePrompts).boolean;
  CHECK_TRUE(h.Send(std::string("rbbz-settings/set/interface.hide_offline_prompts/b:") +
                    (before ? "0" : "1"))
                 .status == Status::kOk);
  CHECK_TRUE(h.Holds("interface.hide_offline_prompts", WireValue::Bool(!before)));
  CHECK_TRUE(h.Send("rbbz-settings/cancel-field/interface.hide_offline_prompts/-").status == Status::kOk);
  CHECK_TRUE(!h.Holds("interface.hide_offline_prompts", WireValue::Bool(!before)));
  CHECK_TRUE(h.Send("rbbz-settings/cancel/-/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_changes().empty());
  CHECK_TRUE(!fs::exists(h.paths.settings_file));  // a cancelled session writes nothing

  CHECK_TRUE(!h.sink.restart_requested());
  // Starting the child is the boot's job (§8), so the sink only records the request and reports what
  // the boot's handler did with it. With **no** handler the request is refused and reported instead
  // of recorded into nothing (`TestRestartRefusalsAreVisible` pins that), so this leg installs one.
  h.sink.SetRestartHandler(
      [](uint64_t, uint64_t, uint64_t, const std::string&, std::string*) { return true; });
  CHECK_TRUE(h.Send("rbbz-settings/restart/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.restart_requested());
}

void TestRestartPendingFollowsTheQuery() {
  BeginCase("restart.pending answers the boot's value-based query when one is installed");
  Harness h;

  // Without a query the sink's sticky bit is the answer, which is what a sink-only build (and the
  // tests above) see: nothing changed yet, so nothing is pending.
  CHECK_TRUE(!h.sink.restart_pending());
  CHECK_TRUE(h.Holds("restart.pending", WireValue::Bool(false)));

  // A restart-scoped commit sets the sticky bit. The row has to actually move for the commit to
  // save anything (see `TestTextFormEditsTheRealStore`), so stage its other value.
  const bool before = h.service.EffectiveValue(SettingId::kHideOfflinePrompts).boolean;
  CHECK_TRUE(h.Send(std::string("rbbz-settings/set/interface.hide_offline_prompts/b:") +
                    (before ? "0" : "1"))
                 .status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.restart_pending());
  CHECK_TRUE(h.Holds("restart.pending", WireValue::Bool(true)));

  // With the boot's query installed, that answer wins: the query is the sharper question (does what
  // the store says still differ from what the process runs), so a boot that says "nothing differs"
  // shows no prompt even though a restart-scoped commit happened.
  bool query_answer = false;
  h.sink.SetRestartPendingQuery([&query_answer] { return query_answer; });
  CHECK_TRUE(!h.sink.restart_pending());
  CHECK_TRUE(h.Holds("restart.pending", WireValue::Bool(false)));
  query_answer = true;
  CHECK_TRUE(h.sink.restart_pending());
  CHECK_TRUE(h.Holds("restart.pending", WireValue::Bool(true)));
}

void TestRestartRefusalsAreVisible() {
  BeginCase("every refused restart resolves restart.ok, so the confirmation screen can speak");
  Harness h;

  // The confirmation's row writes `restart/-/-` and then asks `snapshot/restart.ok/b:1`; the screen's
  // own "the game could not restart" token is printed only when that bit is false. So a refusal has
  // to clear it. Measured defect (2026-10-14, the owner's "Restart now can do nothing"): a refusal
  // taken before the boot's handler ran left the bit at its initial `true`, so the press was a
  // silent no-op. Nothing attempted yet, so the bit still reads true.
  CHECK_TRUE(h.Holds("restart.ok", WireValue::Bool(true)));
  const Response unsupported = h.Send("rbbz-settings/restart/-/-");
  CHECK_TRUE(unsupported.status == Status::kUnsupported);
  CHECK_TRUE(h.Holds("restart.ok", WireValue::Bool(false)));
  CHECK_TRUE(!h.sink.restart_requested());  // refused before anything was recorded

  // A handler that refuses (a spawn that failed, say) is answered the same way: the bit is false and
  // the handler's own reason reaches the caller.
  h.sink.SetRestartHandler([](uint64_t, uint64_t, uint64_t, const std::string&, std::string* error) {
    *error = "the child could not be started";
    return false;
  });
  const bool before = h.service.EffectiveValue(SettingId::kHideOfflinePrompts).boolean;
  CHECK_TRUE(h.Send(std::string("rbbz-settings/set/interface.hide_offline_prompts/b:") +
                    (before ? "0" : "1"))
                 .status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  const Response refused = h.Send("rbbz-settings/restart/-/-");
  CHECK_TRUE(refused.status == Status::kIoError);
  CHECK_TRUE(refused.message == "the child could not be started");
  CHECK_TRUE(h.Holds("restart.ok", WireValue::Bool(false)));
  CHECK_TRUE(h.sink.restart_requested());  // the request was recorded; the child is what failed

  // A retry after a failure must work, and the bit follows the *last* answer - which is what makes
  // the screen show the prompt again instead of a stale failure.
  h.sink.SetRestartHandler([](uint64_t saved, uint64_t current, uint64_t, const std::string&,
                              std::string*) { return saved != 0 && saved == current; });
  CHECK_TRUE(h.Send("rbbz-settings/restart/-/-").status == Status::kOk);
  CHECK_TRUE(h.Holds("restart.ok", WireValue::Bool(true)));
}

void TestCommitFailureReasons() {
  BeginCase("a failed save names the reason, and the retry the panel asks for really retries");
  Harness h;
  // A first durable save: the file has to exist before it can be made unwritable.
  const bool before = h.service.EffectiveValue(SettingId::kHideOfflinePrompts).boolean;
  const std::string other =
      std::string("rbbz-settings/set/interface.hide_offline_prompts/b:") + (before ? "0" : "1");
  const std::string back =
      std::string("rbbz-settings/set/interface.hide_offline_prompts/b:") + (before ? "1" : "0");
  CHECK_TRUE(h.Send(other).status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  // Nothing to explain after a save that worked, so the panel's own default text stands.
  CHECK_TRUE(h.sink.last_commit_failure() == ServiceSink::CommitFailure::kNone);
  CHECK_TRUE(h.Holds("commit.failed", WireValue::Text("none")));
  CHECK_TRUE(h.Holds("commit.ok", WireValue::Bool(true)));

  // The settings file cannot be replaced. This is the one failure the "check that the settings
  // folder is writable" text is true of, and the reason token says so.
  std::error_code code;
  fs::permissions(h.paths.settings_file, fs::perms::owner_read, fs::perm_options::replace, code);
  CHECK_TRUE(h.Send(back).status == Status::kOk);
  const Response failed = h.Send("rbbz-settings/commit/-/-");
  CHECK_TRUE(failed.status == Status::kIoError);
  CHECK_TRUE(failed.message.find("settings.toml") != std::string::npos);
  CHECK_TRUE(h.sink.last_commit_failure() == ServiceSink::CommitFailure::kNotWritable);
  CHECK_TRUE(h.Holds("commit.failed", WireValue::Text("not_writable")));
  CHECK_FALSE(h.Holds("commit.failed", WireValue::Text("provider")));
  CHECK_FALSE(h.Holds("commit.ok", WireValue::Bool(true)));

  // The edit is still staged, so the retry the panel asks for is a real one - and it clears the
  // reason, so the panel cannot explain the next save with this failure.
  fs::permissions(h.paths.settings_file, fs::perms::owner_all, fs::perm_options::replace, code);
  const Response retry = h.Send("rbbz-settings/commit/-/-");
  CHECK_TRUE(retry.status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_failure() == ServiceSink::CommitFailure::kNone);
  CHECK_TRUE(h.Holds("commit.ok", WireValue::Bool(true)));
  CHECK_FALSE(h.Holds("commit.failed", WireValue::Text("not_writable")));
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kHideOfflinePrompts).boolean == before);

  // A failure of the provider's own file is *not* the settings folder's problem. With no
  // service-side provider sink, a provider row is kept here (the local space, `SetProviderSaver`),
  // which is the same failure the installed provider's file produces on a commit.
  h.sink.SetProviderSaver([](const std::map<std::string, WireValue>&, std::string* error) {
    *error = "the provider's own settings file is read-only";
    return false;
  });
  CHECK_TRUE(h.Send("rbbz-settings/set/gameplay.forceplot/t:plot_bodega_a").status == Status::kOk);
  const Response provider_failed = h.Send("rbbz-settings/commit/-/-");
  CHECK_TRUE(provider_failed.status == Status::kIoError);
  CHECK_TRUE(provider_failed.message.find("read-only") != std::string::npos);
  CHECK_TRUE(h.sink.last_commit_failure() == ServiceSink::CommitFailure::kProvider);
  CHECK_TRUE(h.Holds("commit.failed", WireValue::Text("provider")));
  CHECK_FALSE(h.Holds("commit.failed", WireValue::Text("not_writable")));
  CHECK_FALSE(h.Holds("commit.ok", WireValue::Bool(true)));

  h.sink.SetProviderSaver([](const std::map<std::string, WireValue>&, std::string*) {
    return true;
  });
  const Response provider_retry = h.Send("rbbz-settings/commit/-/-");
  CHECK_TRUE(provider_retry.status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_failure() == ServiceSink::CommitFailure::kNone);
  CHECK_TRUE(h.Holds("commit.ok", WireValue::Bool(true)));
}

void TestWireModelConversions() {
  BeginCase("the wire and model value kinds meet in exactly one place");
  const schema::SchemaRow* bool_row = schema::FindRow(SettingId::kVsync);
  const schema::SchemaRow* enum_row = schema::FindRow(SettingId::kRenderer);
  const schema::SchemaRow* list_row = schema::FindRow(SettingId::kHiddenMainMenu);
  CHECK_TRUE(bool_row != nullptr && enum_row != nullptr && list_row != nullptr);

  Value model;
  CHECK_TRUE(ModelValueFromWire(bool_row->descriptor, WireValue::Bool(true), &model));
  CHECK_TRUE(model.kind == ValueKind::kBool && model.boolean);
  CHECK_TRUE(!ModelValueFromWire(bool_row->descriptor, WireValue::Int(1), &model));
  CHECK_TRUE(ModelValueFromWire(enum_row->descriptor, WireValue::Text("d3d12"), &model));
  CHECK_TRUE(model.kind == ValueKind::kEnum && model.text == "d3d12");
  CHECK_TRUE(ModelValueFromWire(list_row->descriptor, WireValue::List({"a", "b"}), &model));
  CHECK_TRUE(model.kind == ValueKind::kList && model.list.size() == 2);

  WireValue wire;
  Value resolution;
  resolution.kind = ValueKind::kResolution;
  resolution.width = 1920;
  resolution.height = 1080;
  CHECK_TRUE(WireFromModelValue(resolution, &wire));
  CHECK_TRUE(wire.kind == bridge::ValueKind::kText && wire.text == "1920x1080");
  CHECK_TRUE(!WireFromModelValue(Value{}, &wire));  // kNone has no wire spelling

  CHECK_TRUE(WireValuesEqual(WireValue::Text("a"), WireValue::Text("a")));
  CHECK_TRUE(!WireValuesEqual(WireValue::Text("1"), WireValue::Int(1)));
}

void TestRendererSelectionSurvivesCommitAndReload() {
  BeginCase("a usable inactive renderer can be selected, saved, and used on the next boot");
  Harness h;
  CapabilityInputs inputs;
  for (const std::string token : {"d3d12", "vulkan"}) {
    RendererProbe probe;
    probe.token = token;
    probe.built = probe.detected = probe.usable = true;
    probe.active = token == "d3d12";
    inputs.renderers.push_back(probe);
  }
  const auto caps = BuildCapabilitySnapshot(inputs);
  GraphicsReconciler reconciler;
  h.service.SetReconciler(&reconciler);
  h.service.SetCapabilities(caps);

  Value initial;
  initial.kind = ValueKind::kEnum;
  initial.text = "d3d12";
  h.service.SetBootValues({{SettingId::kRenderer, initial}}, "");
  CHECK_TRUE(h.Holds("graphics.renderer", WireValue::Text("d3d12")));

  CHECK_TRUE(h.Send("rbbz-settings/set/graphics.renderer/t:vulkan").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_durable());
  CHECK_TRUE(h.sink.last_commit_requires_restart());
  CHECK_TRUE(h.Holds("graphics.renderer", WireValue::Text("vulkan")));
  CHECK_FALSE(h.Holds("graphics.renderer", WireValue::Text("d3d12")));
  CHECK_TRUE(h.service.RequestedValue(SettingId::kRenderer).text == "vulkan");
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kRenderer).text == "vulkan");

  // Boot applies EffectiveValue to gpu_backend before the plugin is loaded.
  Service again(h.paths);
  again.Boot(nullptr, PathEntry{}, nullptr, true);
  again.SetReconciler(&reconciler);
  again.SetCapabilities(caps);
  CHECK_TRUE(again.EffectiveValue(SettingId::kRenderer).text == "vulkan");

  // Cycling back must persist D3D12 rather than look like a no-op against the active backend.
  CHECK_TRUE(h.Send("rbbz-settings/set/graphics.renderer/t:d3d12").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.sink.last_commit_durable());
  CHECK_TRUE(h.Holds("graphics.renderer", WireValue::Text("d3d12")));

  auto unavailable = caps;
  unavailable.renderers[1].usable = false;
  h.service.SetCapabilities(unavailable);
  CHECK_TRUE(h.Send("rbbz-settings/set/graphics.renderer/t:vulkan").status == Status::kInvalid);
}

}  // namespace

void TestMenuVisibilityBaselineAndRefresh() {
  BeginCase("visibility starts from runtime filtering and saves one refresh preference atomically");
  Harness h;
  h.sink.SeedMenuVisibility({"splash_leaderboard", "splash_achievements", "splash_dlc"}, false);
  CHECK_TRUE(h.Holds("interface.hidden_main_menu.splash_leaderboard", WireValue::Bool(true)));
  CHECK_TRUE(h.Holds("general.refresh_music_library", WireValue::Bool(false)));
  // Editing another entry must preserve the older refresh hide bit.
  CHECK_TRUE(h.Send("rbbz-settings/set/interface.hidden_main_menu.splash_leaderboard/b:0").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.Holds("interface.hidden_main_menu.splash_dlc", WireValue::Bool(true)));
  CHECK_TRUE(h.Holds("general.refresh_music_library", WireValue::Bool(false)));
  CHECK_TRUE(h.Send("rbbz-settings/set/general.refresh_music_library/b:1").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.Holds("general.refresh_music_library", WireValue::Bool(true)));
  CHECK_TRUE(h.Holds("interface.hidden_main_menu.splash_dlc", WireValue::Bool(false)));
  CHECK_TRUE(h.Holds("interface.hidden_main_menu.splash_achievements", WireValue::Bool(true)));
  Service again(h.paths);
  again.Boot(nullptr, PathEntry{}, nullptr, true);
  CHECK_TRUE(again.EffectiveValue(SettingId::kRefreshMusicLibrary).boolean);
  CHECK_TRUE(again.EffectiveValue(SettingId::kHideMainMenu).boolean);
  const auto hidden = again.EffectiveValue(SettingId::kHiddenMainMenu).list;
  CHECK_TRUE(hidden == std::vector<std::string>({"splash_achievements"}));
  // Hiding the RhythmVerse shortcut leaves store access enabled.
  CHECK_TRUE(h.Send("rbbz-settings/set/interface.hidden_main_menu.rbbz_rhythmverse/b:1").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kRhythmVerse).boolean);
  CHECK_TRUE(h.Holds("interface.hidden_main_menu.rbbz_rhythmverse", WireValue::Bool(true)));
}

void TestLinkedMusicConfirmCancelAndReenable() {
  BeginCase("linked music edits require consent, save together, and never restore children");
  Harness h;
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kMusicPlayer).boolean);
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.dlc_menu_songs/b:0").status == Status::kOk);
  CHECK_TRUE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_TRUE(h.Holds("general.dlc_menu_songs", WireValue::Bool(true)));
  CHECK_TRUE(h.Holds("interface.music_player", WireValue::Bool(true)));
  CHECK_FALSE(fs::exists(h.paths.settings_file));
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kInvalid);
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.cancel/-").status == Status::kOk);
  CHECK_FALSE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kDlcMenuSongs).boolean);
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.dlc_menu_songs/b:0").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  for (const auto id : {SettingId::kDlcMenuSongs, SettingId::kMusicPlayer,
                        SettingId::kPlayCurrentSong, SettingId::kBackgroundMusicNavigation})
    CHECK_FALSE(h.service.EffectiveValue(id).boolean);
  Service again(h.paths);
  again.Boot(nullptr, PathEntry{}, nullptr, true);
  CHECK_FALSE(again.EffectiveValue(SettingId::kMusicPlayer).boolean);
  // Enabling the parent is immediate and does not guess the previous child preferences.
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.dlc_menu_songs/b:1").status == Status::kOk);
  CHECK_FALSE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_FALSE(h.service.EffectiveValue(SettingId::kMusicPlayer).boolean);
  CHECK_FALSE(h.service.EffectiveValue(SettingId::kPlayCurrentSong).boolean);
  CHECK_FALSE(h.service.EffectiveValue(SettingId::kBackgroundMusicNavigation).boolean);
  // Enabling a child offers its parent and enables exactly that child after consent.
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.dlc_menu_songs/b:0").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.interface.music_player/b:1").status == Status::kOk);
  CHECK_TRUE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_FALSE(h.service.EffectiveValue(SettingId::kDlcMenuSongs).boolean);
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kDlcMenuSongs).boolean);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kMusicPlayer).boolean);
  CHECK_FALSE(h.service.EffectiveValue(SettingId::kBackgroundMusicNavigation).boolean);
  CHECK_FALSE(h.service.EffectiveValue(SettingId::kPlayCurrentSong).boolean);
}

void TestLinkedStore() {
  BeginCase("store shortcuts use confirmation rules, including inverted visibility");
  Harness h;
  h.sink.SeedMenuVisibility({}, true);
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.rhythmverse/b:0").status == Status::kOk);
  CHECK_TRUE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.Holds("interface.hidden_main_menu.rbbz_rhythmverse", WireValue::Bool(true)));
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.interface.hidden_main_menu.rbbz_rhythmverse/b:0").status == Status::kOk);
  CHECK_TRUE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kRhythmVerse).boolean);
  CHECK_TRUE(h.Holds("interface.hidden_main_menu.rbbz_rhythmverse", WireValue::Bool(false)));
}

void TestLinkedFailuresSaveNothing() {
  BeginCase("a locked dependent or stale confirmation cannot save a partial linked edit");
  Harness h;
  Value forced;
  forced.kind = ValueKind::kBool;
  forced.boolean = true;
  h.service.SetOverrides({{SettingId::kMusicPlayer, forced}}, {{SettingId::kMusicPlayer, Source::kCommandLine}});
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.dlc_menu_songs/b:0").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kInvalid);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kInvalid);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kDlcMenuSongs).boolean);
  CHECK_FALSE(fs::exists(h.paths.settings_file));
  h.service.SetOverrides({}, {});
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.dlc_menu_songs/b:0").status == Status::kOk);
  CapabilitySnapshot changed_caps;
  changed_caps.revision = h.service.capability_revision() + 1;
  h.service.SetCapabilities(changed_caps);
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kStaleRequest);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kInvalid);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kDlcMenuSongs).boolean);
}

void TestLinkedHud() {
  BeginCase("HUD parent and individual visibility use confirmations without automatic restoration");
  Harness h;
  h.sink.SeedLocal("interface.hide_hud", WireValue::Bool(false));
  h.sink.SeedLocal("interface.show_timer", WireValue::Bool(true));
  h.sink.SeedLocal("interface.show_pb", WireValue::Bool(true));
  int saves = 0;
  h.sink.SetProviderSaver([&](const std::map<std::string, WireValue>&, std::string*) {
    ++saves;
    return true;
  });
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.interface.hide_hud/b:1").status == Status::kOk);
  CHECK_TRUE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_EQ(saves, 0);
  CHECK_TRUE(h.Holds("interface.show_timer", WireValue::Bool(true)));
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_EQ(saves, 1);
  CHECK_TRUE(h.Holds("interface.hide_hud", WireValue::Bool(true)));
  CHECK_TRUE(h.Holds("interface.show_timer", WireValue::Bool(false)));
  CHECK_TRUE(h.Holds("interface.show_pb", WireValue::Bool(false)));
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.interface.show_timer/b:1").status == Status::kOk);
  CHECK_TRUE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.Holds("interface.hide_hud", WireValue::Bool(false)));
  CHECK_TRUE(h.Holds("interface.show_timer", WireValue::Bool(true)));
  CHECK_TRUE(h.Holds("interface.show_pb", WireValue::Bool(false)));
}

void TestRefreshEnableClearsLegacyHide() {
  BeginCase("enabling Refresh Songs clears a legacy hide bit without changing unrelated rows");
  Harness h;
  CHECK_TRUE(h.Send("rbbz-settings/set/general.refresh_music_library/b:0").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  h.sink.SeedMenuVisibility({"splash_dlc", "splash_achievements"}, false);
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.refresh_music_library/b:1").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kRefreshMusicLibrary).boolean);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kHiddenMainMenu).list ==
             std::vector<std::string>({"splash_achievements"}));
}

void TestLinkedSaveFailureRetry() {
  BeginCase("a failed linked save can be reviewed and retried without an extra rejected edit");
  Harness h;
  CHECK_TRUE(h.Send("rbbz-settings/set/general.load_guard/t:30s").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  const auto before = ReadText(h.paths.settings_file);
  std::error_code ec;
  fs::permissions(h.paths.settings_file, fs::perms::owner_read, fs::perm_options::replace, ec);
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.dlc_menu_songs/b:0").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kIoError);
  CHECK_TRUE(ReadText(h.paths.settings_file) == before);
  CHECK_TRUE(h.service.EffectiveValue(SettingId::kMusicPlayer).boolean);
  fs::permissions(h.paths.settings_file, fs::perms::owner_all, fs::perm_options::replace, ec);
  CHECK_TRUE(h.Send("rbbz-settings/set/linked.general.dlc_menu_songs/b:0").status == Status::kOk);
  CHECK_TRUE(h.Holds("linked.pending", WireValue::Bool(true)));
  CHECK_TRUE(h.Send("rbbz-settings/invoke/linked.confirm/-").status == Status::kOk);
  CHECK_TRUE(h.Send("rbbz-settings/commit/-/-").status == Status::kOk);
  CHECK_FALSE(h.service.EffectiveValue(SettingId::kDlcMenuSongs).boolean);
  CHECK_FALSE(h.service.EffectiveValue(SettingId::kMusicPlayer).boolean);
}

int main() {
  TestLinkedMusicConfirmCancelAndReenable();
  TestLinkedStore();
  TestLinkedFailuresSaveNothing();
  TestLinkedHud();
  TestRefreshEnableClearsLegacyHide();
  TestLinkedSaveFailureRetry();
  TestMenuVisibilityBaselineAndRefresh();
  TestTextFormEditsTheRealStore();
  TestFramedFormCarriesTheRequestId();
  TestNoOpCommitReportsNothing();
  TestProviderRowsStayOutOfTheSettingsFile();
  TestProviderGateAndUnknownTokens();
  TestCancelAndRestart();
  TestRestartPendingFollowsTheQuery();
  TestRestartRefusalsAreVisible();
  TestCommitFailureReasons();
  TestWireModelConversions();
  TestRendererSelectionSurvivesCommitAndReload();
  return Finish();
}
