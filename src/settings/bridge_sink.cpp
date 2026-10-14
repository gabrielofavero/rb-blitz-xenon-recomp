// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The implementation of src/settings/bridge_sink.h - see that file for why the channel has two
// stores and which rows go to which.

#include "settings/bridge_sink.h"

#include <cstdio>
#include <algorithm>
#include <string>
#include <utility>

#include "settings/schema.h"

namespace rb_blitz::settings {
namespace {

using bridge::Status;
using bridge::WireValue;

// NB: `ValueKind` is deliberately *not* pulled in here: the two vocabularies both spell it, and the
// whole point of this file is that they meet in exactly one place. `ValueKind::kX` below is the
// settings model's; the wire's is always written `bridge::ValueKind::kX`.

std::string ResolutionText(const Value& value) {
  if (IsAutoResolution(value)) return "auto";
  return std::to_string(value.width) + "x" + std::to_string(value.height);
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// wire <-> model
// ------------------------------------------------------------------------------------------------

bool WireFromModelValue(const Value& value, WireValue* out) {
  switch (value.kind) {
    case ValueKind::kBool:
      *out = WireValue::Bool(value.boolean);
      return true;
    case ValueKind::kInt:
      *out = WireValue::Int(value.integer);
      return true;
    case ValueKind::kEnum:
    case ValueKind::kString:
      if (value.text.empty()) {
        return false;
      }
      *out = WireValue::Text(value.text);
      return true;
    case ValueKind::kResolution:
      *out = WireValue::Text(ResolutionText(value));
      return true;
    case ValueKind::kList:
      *out = WireValue::List(value.list);
      return true;
    default:
      return false;
  }
}

bool ModelValueFromWire(const Descriptor& descriptor, const WireValue& wire, Value* out) {
  switch (descriptor.value_kind) {
    case ValueKind::kBool:
      if (wire.kind != bridge::ValueKind::kBool) {
        return false;
      }
      out->kind = ValueKind::kBool;
      out->boolean = wire.boolean;
      return true;
    case ValueKind::kInt:
      if (wire.kind != bridge::ValueKind::kInt || wire.integer < INT32_MIN || wire.integer > INT32_MAX) {
        return false;
      }
      out->kind = ValueKind::kInt;
      out->integer = static_cast<int32_t>(wire.integer);
      return true;
    case ValueKind::kEnum:
    case ValueKind::kString:
      if (wire.kind != bridge::ValueKind::kText) {
        return false;
      }
      // The row's own choice set decides: a token it does not describe is refused rather than
      // stored, which is the same rule `Service::Set` applies to an out-of-list enum.
      return schema::ParseTokenToValue(descriptor, wire.text, out);
    case ValueKind::kResolution:
      if (wire.kind != bridge::ValueKind::kText) {
        return false;
      }
      return schema::ParseTokenToValue(descriptor, wire.text, out);
    case ValueKind::kList:
      if (wire.kind != bridge::ValueKind::kList) {
        return false;
      }
      out->kind = ValueKind::kList;
      out->list = wire.list;
      return true;
    default:
      return false;
  }
}

bool WireValuesEqual(const WireValue& lhs, const WireValue& rhs) {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  switch (lhs.kind) {
    case bridge::ValueKind::kNone:
      return true;
    case bridge::ValueKind::kBool:
      return lhs.boolean == rhs.boolean;
    case bridge::ValueKind::kInt:
      return lhs.integer == rhs.integer;
    case bridge::ValueKind::kText:
      return lhs.text == rhs.text;
    case bridge::ValueKind::kList:
      return lhs.list == rhs.list;
  }
  return false;
}

// ------------------------------------------------------------------------------------------------
// the sink
// ------------------------------------------------------------------------------------------------

bool ServiceSink::IsLocalToken(const std::string_view token) const {
  if (token == kProviderGateToken) {
    return true;  // P5b's gate: a view of the adapter's readiness, not a row
  }
  const SettingId id = schema::IdForToken(token);
  if (id == SettingId::kUnknown) {
    return false;  // not a row of this build: refused rather than staged into nothing
  }
  if (IsAction(id)) {
    return true;
  }
  // The provider's own rows: its file is their source of truth and P6's write-through persists
  // them, so the service must not also claim to own them.
  return IsProviderFileRow(id) && (!service_ || !service_->has_provider_sink());
}

Value ServiceSink::CurrentValue(const SettingId id) const {
  const auto staged = staged_.find(id);
  if (staged != staged_.end()) {
    return staged->second;
  }
  if (!service_) return Value{};
  // Keep Auto selected in the menu while boot uses its resolved pixel dimensions.
  if (id == SettingId::kResolution && !service_->IsOverridden(id) &&
      IsAutoResolution(service_->RequestedValue(id))) return service_->RequestedValue(id);
  return service_->EffectiveValue(id);
}

bool ServiceSink::EnsureTransaction(bridge::Response* response) {
  if (txn_ != 0) {
    return true;
  }
  if (service_ == nullptr) {
    response->status = Status::kUnsupported;
    response->message = "no settings service is installed";
    return false;
  }
  txn_ = service_->BeginEdit(service_->revision());
  if (txn_ == 0) {
    response->status = Status::kStaleRequest;
    response->message = "the settings service refused a transaction at this revision";
    return false;
  }
  return true;
}

bridge::Response ServiceSink::Handle(const bridge::Request& request) {
  bridge::Response response;
  response.version = bridge::kProtocolVersion;
  response.request_id = request.request_id;
  Status status = Status::kOk;

  switch (request.op) {
    case bridge::Op::kSnapshot: {
      // The one-bit channel cannot carry a snapshot; the read half is the membership probe
      // (`Matches`). This answers with the size of what the sink holds and says which store it is.
      const size_t rows = service_ != nullptr ? service_->descriptors().size() : 0;
      response.value = WireValue::Int(static_cast<int64_t>(rows + local_.size()));
      response.message = service_ != nullptr ? "native settings service" : "no service";
      break;
    }
    case bridge::Op::kBeginEdit: {
      edit_failed_ = false;
      if (service_ == nullptr) {
        status = Status::kUnsupported;
        response.message = "no settings service is installed";
        break;
      }
      // The guest may name a base revision (`i:<n>`); without one the service's current revision
      // is the base, and a revision the service has moved past is refused by `BeginEdit` itself.
      const uint64_t base = request.value.kind == bridge::ValueKind::kInt
                                ? static_cast<uint64_t>(request.value.integer)
                                : service_->revision();
      txn_ = service_->BeginEdit(base);
      if (txn_ == 0) {
        status = Status::kStaleRequest;
        response.message = "the base revision is not the current one";
      } else {
        staged_.clear();
        staged_local_.clear();
        staged_base_.clear();
        staged_token_.clear();
        response.message = "edit begun";
      }
      break;
    }
    case bridge::Op::kSet: {
      if (request.setting.empty()) {
        status = Status::kInvalid;
        response.message = "a set needs a setting token";
        break;
      }
      if (IsLocalToken(request.setting)) {
        if (request.setting != kProviderGateToken) {
          // A row the provider owns still validates its *kind* here: the descriptor is ours even
          // when the value's home is the provider's file, so a wrong-kind write is refused now
          // rather than by the provider's writer later.
          const schema::SchemaRow* local_row =
              schema::FindRow(schema::IdForToken(request.setting));
          Value parsed;
          if (local_row == nullptr ||
              !ModelValueFromWire(local_row->descriptor, request.value, &parsed)) {
            status = Status::kInvalid;
            response.message = "the value is not the kind this row takes";
            break;
          }
        }
        staged_local_[request.setting] = request.value;
        response.message = request.setting == kProviderGateToken
                               ? "staged (this build keeps the gate itself)"
                               : "staged (kept by this build, not by settings.toml)";
        break;
      }
      constexpr std::string_view member_prefix = "interface.hidden_main_menu.";
      const bool member = request.setting.starts_with(member_prefix);
      const SettingId id = member ? SettingId::kHiddenMainMenu : schema::IdForToken(request.setting);
      const schema::SchemaRow* row = schema::FindRow(id);
      if (row == nullptr) {
        status = Status::kInvalid;
        response.message = "this build does not describe that setting";
        break;
      }
      if (IsAction(id)) {
        status = Status::kInvalid;
        response.message = "that row is an action; invoke it instead of setting it";
        break;
      }
      Value value;
      if (member && (request.value.kind != bridge::ValueKind::kBool ||
                     request.setting.size() == member_prefix.size())) {
        status = Status::kInvalid;
        response.message = "a menu member needs a token and a boolean";
        break;
      }
      if (member) {
        value = CurrentValue(id);
        value.kind = ValueKind::kList;
        const std::string token = request.setting.substr(member_prefix.size());
        std::erase(value.list, token);
        if (request.value.boolean) value.list.push_back(token);
      } else if (!ModelValueFromWire(row->descriptor, request.value, &value)) {
        status = Status::kInvalid;
        response.message = "the value is not the kind this row takes";
        break;
      }
      if (!EnsureTransaction(&response)) {
        status = response.status;
        break;
      }
      const ValidationResult valid =
          service_->Set(txn_, id, value, service_->capability_revision());
      if (!valid.ok) {
        status = Status::kInvalid;
        response.message = valid.reason_key.empty() ? "refused" : valid.reason_key;
        break;
      }
      // What the row held *before* this session touched it, so the commit can tell a real change
      // from a value written back unchanged (P9's no-op rule). The service reports what the
      // transaction touched; the values are ours to compare. The *token spelling the guest used* is
      // kept too, so the log line and the hot-apply lookup see the screen's own name
      // ("general.dlc_menu_songs") rather than a second spelling of it.
      staged_base_.insert({id, service_->EffectiveValue(id)});
      staged_token_[id] = member ? "interface.hidden_main_menu" : request.setting;
      staged_[id] = std::move(value);
      response.message = "staged";
      break;
    }
    case bridge::Op::kCancelField: {
      const SettingId id = schema::IdForToken(request.setting);
      if (IsLocalToken(request.setting)) {
        staged_local_.erase(request.setting);
        response.message = "field restored";
        break;
      }
      if (service_ == nullptr || txn_ == 0 || id == SettingId::kUnknown) {
        response.message = "field restored";
        break;
      }
      staged_.erase(id);
      const Value back = service_->CancelField(txn_, id);
      WireValue wire;
      if (WireFromModelValue(back, &wire)) {
        response.value = wire;
      }
      response.message = "field restored";
      break;
    }
    case bridge::Op::kCancelEdit: {
      edit_failed_ = false;
      staged_.clear();
      staged_local_.clear();
      staged_base_.clear();
      staged_token_.clear();
      if (service_ != nullptr && txn_ != 0) {
        service_->Cancel(txn_);
      }
      txn_ = 0;
      response.message = "edit cancelled";
      break;
    }
    case bridge::Op::kCommit: {
      last_commit_changes_.clear();
      if (edit_failed_) {
        if (service_ && txn_) service_->Cancel(txn_);
        txn_ = 0;
        staged_.clear(); staged_base_.clear(); staged_token_.clear(); staged_local_.clear();
        edit_failed_ = false;
        commit_ok_ = false;
        last_commit_durable_ = false;
        last_commit_requires_restart_ = false;
        status = Status::kInvalid;
        response.message = "the edit was refused; nothing saved";
        break;
      }
      CommitResult result;
      if (service_ != nullptr && txn_ != 0) {
        result = service_->Commit(txn_);
      }
      if (result.status == CommitStatus::kSaved || result.status == CommitStatus::kNoChanges) txn_ = 0;
      last_commit_status_ = result.status;
      saved_revision_ = result.status == CommitStatus::kNoChanges && service_
                            ? service_->revision() : result.saved_revision;
      last_commit_durable_ = result.status == CommitStatus::kSaved;
      last_commit_requires_restart_ = result.restart_required;
      bool local_saved = true;
      std::map<std::string, WireValue> provider_changes;
      for (const auto& [token, value] : staged_local_) {
        if (token == kProviderGateToken) continue;
        const auto old = local_.find(token);
        if (old == local_.end() || !WireValuesEqual(old->second, value))
          provider_changes[token] = value;
      }
      if (provider_saver_ && !provider_changes.empty() &&
          (result.status == CommitStatus::kSaved || result.status == CommitStatus::kNoChanges)) {
        std::string error;
        local_saved = provider_saver_(provider_changes, &error);
        if (!local_saved) {
          result.status = last_commit_durable_ ? CommitStatus::kPartial : CommitStatus::kIoError;
          result.errors.push_back(error);
        } else {
          last_commit_durable_ = true;
          last_commit_requires_restart_ = true;
          saved_revision_ = service_ ? service_->revision() : 0;
        }
      }
      // What the service really wrote, filtered by P9's no-op rule: a row whose value did not move
      // is not reported, so a commit cannot re-apply a hot row for a session that changed nothing
      // (the test that matters is the change-then-revert case the plan §5.4 calls a no-op). Then the
      // provider's own rows, whose commit is the write-through the caller performs next
      // (`PersistProviderRows`) - that is what makes the provider's portion durable.
      //
      // The session's own record is cleared only *after* it is read: `staged_base_` is the value
      // each row held before the edit, and it is what tells a real change from a write-back.
      if (service_ != nullptr && (result.status == CommitStatus::kSaved || result.status == CommitStatus::kPartial)) {
        for (const auto& entry : service_->last_commit_changes()) {
          const auto base = staged_base_.find(entry.first);
          if (base != staged_base_.end() && ValueEquals(base->second, entry.second)) {
            continue;
          }
          const auto token = staged_token_.find(entry.first);
          last_commit_changes_.push_back(token != staged_token_.end()
                                             ? token->second
                                             : std::string(SettingIdToken(entry.first)));
        }
      }
      if (result.status == CommitStatus::kSaved || result.status == CommitStatus::kNoChanges) {
        staged_.clear();
        staged_base_.clear();
        staged_token_.clear();
      }
      for (auto& entry : staged_local_) {
        if (!local_saved || (result.status != CommitStatus::kSaved &&
                             result.status != CommitStatus::kNoChanges)) continue;
        const auto before = local_.find(entry.first);
        const bool changed = before == local_.end() || !WireValuesEqual(before->second, entry.second);
        local_[entry.first] = entry.second;
        if (changed) {
          last_commit_changes_.push_back(entry.first);
        }
      }
      if (result.status == CommitStatus::kSaved || result.status == CommitStatus::kNoChanges)
        staged_local_.clear();
      switch (result.status) {
        case CommitStatus::kSaved:
        case CommitStatus::kNoChanges:
          status = Status::kOk;
          break;
        case CommitStatus::kConflict:
          status = Status::kConflict;
          break;
        case CommitStatus::kInvalid:
          status = Status::kInvalid;
          break;
        case CommitStatus::kPartial:
          // One part is durable and one is not. The guest cannot express "partly", and saying
          // "saved" would be a lie the menu cannot see, so it is an error whose message names the
          // state (plan §6.3: never a blanket "nothing was saved").
          status = Status::kIoError;
          for (const std::string& error : result.errors) {
            if (response.message.empty()) {
              response.message = error;
            } else {
              response.message += "; " + error;
            }
          }
          if (response.message.empty()) {
            response.message = "part of the commit is durable and part is not";
          }
          break;
        case CommitStatus::kIoError:
          status = Status::kIoError;
          for (const std::string& error : result.errors) {
            if (!response.message.empty()) {
              response.message += "; ";
            }
            response.message += error;
          }
          break;
      }
      if (status == Status::kOk && response.message.empty()) {
        response.value = WireValue::Bool(true);
        response.message = result.status == CommitStatus::kNoChanges ? "nothing to commit"
                                                                    : "committed";
      }
      commit_ok_ = status == Status::kOk;
      last_commit_status_ = result.status;
      // Sticky for the session (owner decision, 2026-10-13): a durable commit that needs a
      // restart sets the prompt's state once, and it stays set until the process restarts.
      if (commit_ok_ && last_commit_requires_restart_) restart_pending_ = true;
      break;
    }
    case bridge::Op::kInvoke: {
      const SettingId id = schema::IdForToken(request.setting);
      if (service_ == nullptr || id == SettingId::kUnknown) {
        status = Status::kUnsupported;
        response.message = "this build has no such action";
        break;
      }
      const ActionResult action = service_->Invoke(id, request.request_id);
      if (!action.ok) {
        status = Status::kUnsupported;
        response.message = action.error.empty() ? "the action was refused" : action.error;
      } else {
        response.message = "action taken";
      }
      break;
    }
    case bridge::Op::kHelp:
      // A caption event is not an edit. In the running game the write channel hands it to the
      // overlay before the dispatcher is reached (settings_bridge_hooks.cpp `HandleRequest`), so
      // one that gets this far is a request this sink has nothing to do with - a non-edit tool or a
      // test driving the dispatcher directly. Answered, not refused, so a caption event can never
      // read as a broken row.
      response.message = "caption event (no settings change)";
      break;
    case bridge::Op::kRequestRestart: {
      // The confirmation screen decides what to draw from `snapshot/restart.ok` alone (the merged
      // screen's `Kind::kRestart` case): it writes `restart/-/-` and, when that bit is false, prints
      // its "could not restart" token into the panel's own message surface. So the bit has to be the
      // answer to *this* press - cleared first, and set only by a request that really reached the
      // coordinator. Measured defect (2026-10-14): a refusal taken before the handler ran left the
      // bit at its initial `true`, so "Restart now" did nothing **and said nothing**.
      restart_ok_ = false;
      if (service_ == nullptr || restart_handler_ == nullptr) {
        // No boot-provided handler means nothing can start a child. Refusing by name is the honest
        // answer, and it is the same bit the screen reads.
        status = Status::kUnsupported;
        response.message = "this build has no way to restart the game";
        break;
      }
      if (!commit_ok_) {
        status = Status::kInvalid;
        response.message = "settings have not been saved";
        break;
      }
      // Recorded here, acted on by the boot: the child must be started while this process is still
      // alive and with its own identity in the handoff token (§8), which is not the sink's business.
      restart_requested_ = true;
      restart_request_id_ = request.request_id;
      restart_ok_ = restart_handler_(saved_revision_, service_->revision(), request.request_id,
                                    service_->EffectiveValue(SettingId::kTarget).text,
                                    &response.message);
      if (!restart_ok_) {
        status = Status::kIoError;
        break;
      }
      response.message = "restart requested";
      break;
    }
    case bridge::Op::kUnknown:
      status = Status::kUnknownOp;
      response.message = "unknown operation";
      break;
  }

  if (request.op == bridge::Op::kSet && status != Status::kOk) edit_failed_ = true;
  response.status = status;
  // Only a request this sink actually applied is acknowledged, so a guest polling the ack cannot
  // mistake a rejection for a completed write. An id-less text request is never recorded.
  if (request.request_id != 0) {
    handled_[request.request_id] = status;
  }
  return response;
}

bool ServiceSink::Matches(const bridge::Request& request) const {
  if (request.setting == "restart.ok") return WireValuesEqual(WireValue::Bool(restart_ok_), request.value);
  if (request.setting == "commit.ok") return WireValuesEqual(WireValue::Bool(commit_ok_), request.value);
  if (request.setting == "restart.pending")
    return WireValuesEqual(WireValue::Bool(restart_pending()), request.value);
  if (request.setting == "commit.restart_required")
    return WireValuesEqual(WireValue::Bool(commit_ok_ && last_commit_requires_restart_), request.value);
  constexpr std::string_view prefix = "interface.hidden_main_menu.";
  if (request.setting.starts_with(prefix)) {
    const Value value = CurrentValue(SettingId::kHiddenMainMenu);
    const std::string token = request.setting.substr(prefix.size());
    return WireValuesEqual(WireValue::Bool(std::find(value.list.begin(), value.list.end(), token) !=
                                          value.list.end()), request.value);
  }
  if (IsLocalToken(request.setting)) {
    const auto staged = staged_local_.find(request.setting);
    if (staged != staged_local_.end()) {
      return WireValuesEqual(staged->second, request.value);
    }
    const auto found = local_.find(request.setting);
    return found != local_.end() && WireValuesEqual(found->second, request.value);
  }
  const SettingId id = schema::IdForToken(request.setting);
  if (id == SettingId::kUnknown) {
    return false;
  }
  WireValue current;
  if (!WireFromModelValue(CurrentValue(id), &current)) {
    return false;
  }
  return WireValuesEqual(current, request.value);
}
bool ServiceSink::Acknowledged(const uint64_t request_id) const {
  const auto found = handled_.find(request_id);
  return found != handled_.end() && found->second == Status::kOk;
}

bool ServiceSink::Get(const std::string_view setting, WireValue* out) const {
  if (IsLocalToken(setting)) {
    return GetLocal(setting, out);
  }
  const SettingId id = schema::IdForToken(setting);
  if (id == SettingId::kUnknown) {
    return false;
  }
  return WireFromModelValue(CurrentValue(id), out);
}

void ServiceSink::SeedLocal(std::string token, WireValue value) {
  local_[std::move(token)] = std::move(value);
}

bool ServiceSink::GetLocal(const std::string_view token, WireValue* out) const {
  const auto staged = staged_local_.find(std::string(token));
  if (staged != staged_local_.end()) {
    if (out != nullptr) {
      *out = staged->second;
    }
    return true;
  }
  const auto found = local_.find(std::string(token));
  if (found == local_.end()) {
    return false;
  }
  if (out != nullptr) {
    *out = found->second;
  }
  return true;
}

void ServiceSink::Reset() {
  if (service_ && txn_) service_->Cancel(txn_);
  edit_failed_ = false;
  commit_ok_ = true;
  restart_ok_ = true;
  local_.clear();
  staged_local_.clear();
  staged_.clear();
  staged_base_.clear();
  staged_token_.clear();
  handled_.clear();
  last_commit_changes_.clear();
  txn_ = 0;
  restart_requested_ = false;
  restart_request_id_ = 0;
  last_commit_durable_ = false;
  saved_revision_ = 0;
  last_commit_requires_restart_ = false;
  restart_pending_ = false;
  last_commit_status_ = CommitStatus::kNoChanges;
}

}  // namespace rb_blitz::settings
