// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P11a — the guest channel's real store: P3's `Service` behind P1's `RequestSink`.
//
// P1 built the transport with an in-memory stand-in (`MemorySink`) because P3's store did not exist
// yet, and P9's live-apply note (docs/plans/in-game-settings-hot-apply-restart.md §6.3) named the
// step this file performs: "when the coordinator wires the real service, the same three calls move
// onto Service::last_commit_changes() (P9 added it) and the seam is unchanged". So this is not a
// second channel - it is the same frames, the same ops and the same one-bit answers, with the menu
// now editing `settings.toml` through the service's transactions instead of a map in the process.
//
// SDK-free on purpose, and that is the point: the sink can be driven by a real `Service` over a
// temporary directory with no emulator, no window and no boot, so tests/settings_bridge_service_tests.cpp
// proves a full edit -> set -> commit -> read-back -> restart cycle (and the ack, and the no-op rule)
// against actual bytes on disk.
//
// ---------------------------------------------------------------------------------------------
// Which rows go where, and why the split is not an accident
// ---------------------------------------------------------------------------------------------
//
// Two kinds of row reach the channel, and they have different owners:
//
//   * **native rows** (target, DLC, graphics, interface, `unlock_all`) are *our* preferences and
//     they live in `settings.toml`. They go to the `Service`: a transaction, per-row validation
//     against the live capability snapshot, a durable commit, `[provenance]`, and the journal.
//   * **the provider's own rows** (`gameplay.*`, the HUD set, `letterbox`, plots/skyboxes/gems) are
//     the installed Ultimate payload's values, and its own file is their only source of truth
//     (P6). The service models them (`IsProviderFileRow`) but persisting them is the provider's
//     writer, which the channel already does after a commit (`PersistProviderRows` in
//     src/hooks/settings_bridge_hooks.cpp). So the sink keeps their values in its own small space
//     and the existing write-through stays exactly as P6 measured it - seeding included, which must
//     not look like a change and must not rewrite the player's provider file on every boot.
//   * **tokens with no row at all** (`provider.active`, the merged root's gate that P5b reads to
//     decide whether it draws the Gameplay row) are also kept locally. They are a *view* onto
//     something else's state, not a preference, and the service stores preferences.
//
// That is why `IsLocalToken` is a classification rather than a fallback: a token that is not a row
// of ours, or is a row the provider owns, is answered from the local space; everything else is the
// service's.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "hooks/settings_bridge.h"
#include "settings/model.h"
#include "settings/service.h"

namespace rb_blitz::settings {

// --- wire <-> model -------------------------------------------------------
//
// The bridge carries `WireValue` and the store carries `Value`; these two functions are the only
// place the two vocabularies meet, so a wrong kind is refused once rather than coerced in three
// call sites. `descriptor` is the arbiter of the kind, exactly as `Service::Set` validates it.
bool WireFromModelValue(const Value& value, bridge::WireValue* out);
bool ModelValueFromWire(const Descriptor& descriptor, const bridge::WireValue& wire, Value* out);

// Kind-and-content equality, so a probe compares like with like (a text token is never equal to an
// integer that happens to share its digits).
bool WireValuesEqual(const bridge::WireValue& lhs, const bridge::WireValue& rhs);

// --- the sink -------------------------------------------------------------

// The one token that is not a row of ours: P5b's provider gate, which the merged root asks before it
// draws any provider row (`rbbz-settings/snapshot/provider.active/b:1`). It is a *view* onto the
// adapter's readiness, not a preference, so it lives in the sink's local space. Any *other* token
// this build does not describe is refused rather than silently accepted: a stale screen must not be
// able to stage a change that lands nowhere.
inline constexpr std::string_view kProviderGateToken = "provider.active";

class ServiceSink final : public bridge::RequestSink, public bridge::SinkQueries {
 public:
  // `service` is borrowed and must outlive this sink.
  explicit ServiceSink(Service* service) : service_(service) {}

  bridge::Response Handle(const bridge::Request& request) override;
  bool Matches(const bridge::Request& request) const override;
  bool Acknowledged(uint64_t request_id) const override;
  bool Get(std::string_view setting, bridge::WireValue* out) const override;
  const std::vector<std::string>& last_commit_changes() const override {
    return last_commit_changes_;
  }

  // The local space: a token with no row of ours, or a row the installed provider owns. Called by
  // the boot's seeding path (through the channel, so it is the same code the menu exercises).
  void SeedLocal(std::string token, bridge::WireValue value);
  bool GetLocal(std::string_view token, bridge::WireValue* out) const;

  // --- what the boot asks after a commit ----------------------------------
  // The guest's own restart request (`rbbz-settings/restart/-/-`), for P9's coordinator. The sink
  // records it; starting the child is the boot's job, because that is where the process identity
  // and the launch context live (§8).
  bool restart_requested() const { return restart_requested_; }
  uint64_t restart_request_id() const { return restart_request_id_; }
  // The last commit's outcome, in the terms the restart handshake needs: a durable revision, and
  // whether any `restart`-strategy row really changed.
  bool last_commit_durable() const { return last_commit_durable_; }
  uint64_t saved_revision() const { return saved_revision_; }
  bool last_commit_requires_restart() const { return last_commit_requires_restart_; }
  // "kSaved" / "kNoChanges" / ... for the log line.
  std::string_view last_commit_status() const { return CommitStatusToken(last_commit_status_); }

  size_t local_count() const { return local_.size(); }
  size_t handled_count() const { return handled_.size(); }

  void Reset();
  using ProviderSaver = std::function<bool(const std::map<std::string, bridge::WireValue>&,
                                           std::string*)>;
  void SetProviderSaver(ProviderSaver saver) { provider_saver_ = std::move(saver); }
  using RestartHandler = std::function<bool(uint64_t, uint64_t, uint64_t,
                                            const std::string&, std::string*)>;
  void SetRestartHandler(RestartHandler handler) { restart_handler_ = std::move(handler); }

 private:
  // True when the token's value is kept here rather than in the service (see the file header).
  bool IsLocalToken(std::string_view token) const;
  // The value a row currently shows: what this session staged for it, else the service's effective
  // value (§7.3: the effective value is what the row reports, with a correction explained in the
  // log, because that is what the game is actually running).
  Value CurrentValue(SettingId id) const;
  // Starts a transaction when there is none, so the guest's text form (which has no `begin` step
  // it must remember) still edits through a real transaction.
  bool EnsureTransaction(bridge::Response* response);

  Service* service_ = nullptr;
  std::map<std::string, bridge::WireValue> local_;
  std::map<std::string, bridge::WireValue> staged_local_;
  std::map<SettingId, Value> staged_;
  // What each staged row held before this session touched it: the commit uses it to tell a real
  // change from a value written back unchanged (P9's no-op rule).
  std::map<SettingId, Value> staged_base_;
  // The token spelling the guest used for each staged row, so the reported change names the row the
  // way the screen's own table does.
  std::map<SettingId, std::string> staged_token_;
  std::map<uint64_t, bridge::Status> handled_;
  std::vector<std::string> last_commit_changes_;
  uint64_t txn_ = 0;
  bool restart_requested_ = false;
  uint64_t restart_request_id_ = 0;
  bool last_commit_durable_ = false;
  uint64_t saved_revision_ = 0;
  bool last_commit_requires_restart_ = false;
  CommitStatus last_commit_status_ = CommitStatus::kNoChanges;
  ProviderSaver provider_saver_;
  bool edit_failed_ = false;
  bool commit_ok_ = true;
  bool restart_ok_ = true;
  RestartHandler restart_handler_;
};

}  // namespace rb_blitz::settings
