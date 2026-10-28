// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The guest half of the typed settings channel (P1 of docs/settings/plan.md).
// src/hooks/settings_bridge.h holds the transport decision and the frame rules; this file is the
// overridden guest builtins that carry it, plus the boot line that reports them.
//
// Why overridden guest functions and not a file hook: the title's own script builtins are ordinary
// recompiled functions, so a strong `extern "C" REX_FUNC(sub_...)` replaces every call site while
// `__imp__sub_...` stays the original - the pattern already used by src/hooks/ultimate.cpp and
// src/hooks/shell_music.cpp. It adds **no** `Nt*` definition, so the read/query hooks owned by
// src/hooks/menu_filter.cpp and src/hooks/dlc_refresh.cpp keep their single strong definitions.
//
// Measured addresses and ABI (docs/settings/architecture.md (part 1, the bridge) §2, from the
// pinned retail image and the recompiled bodies):
//
//   0x8241DFB0  file_exists(path)     -> writes {bool, type 0} into the caller's 8-byte result
//   0x8241DE48  write_file(path, data) -> writes {0, 0}
//   0x8241DD48  read_file(path)       -> native type-16 binding-row array
//   0x823F9538  node -> string: takes the argument's payload field address (arg i's payload is
//               at `*(u32*)r4 + 8 + 8*i`) and returns a guest string pointer
//
// The rule for all three hooks is the same and is the reason the channel is safe:
//
//   * a path in the exact `rbbz-settings/` namespace is **ours** - it is parsed, dispatched and
//     answered here, and the original is *not* called, because a name in our namespace is a name
//     no content file can have;
//   * every other path is forwarded to the original **exactly once**, unchanged.
//
// Faithful behaviour is therefore "no content file is named like that": with the channel idle the
// hooks only add a prefix comparison to a call the title already makes, and the game is the
// game it was. `settings_bridge_forward_only` is the diagnostic that makes even our own namespace
// forward, so the no-op forwarded hook can be compared against the answering one in one boot.
//
// The cvar registry is read once, in Configure(), on a host thread: a guest thread must not touch
// it (project rule, docs/plans/finished/2026-10-10-song-cache-plan.md §7). The hooks only read the
// cached flags.

#include "hooks/settings_bridge.h"
#include "graphics/preparation_loading.h"
#include "loading/crash_recovery.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "hooks/ultimate.h"
#include "assets/texture_override.h"
#include "hooks/shell_music.h"
#include "hooks/menu_music_guest.h"
#include "hooks/hot_apply_runtime.h"
#include "input/input_store.h"
#include "input/input_icons.h"
#include "input/effective_input.h"
#include "input/native_menu.h"
#include "input/remap.h"
#include "input/title_input.h"
#include "profile/profile_apply.h"
#include "settings/boot.h"
#include "settings/bridge_sink.h"
#include "settings/hot_apply.h"
#include "settings/input_bindings.h"
#include "settings/schema.h"
#include "settings/ultimate_adapter.h"
#include "store/store_catalogue.h"
#include "store/album_art.h"
#include "store/store_download_service.h"
#include "store/store_search.h"
#include "store/store_settings.h"
#include "ui/settings_screen.h"
#include "ui/input_icon_rows.h"

// The builtins the channel rides on and the node -> string helper the originals use. The
// originals are the __imp__ aliases; the strong definitions below replace the call sites.
REX_EXTERN(__imp__sub_8241DFB0);
REX_EXTERN(__imp__sub_8241DE48);
REX_EXTERN(__imp__sub_823F9538);
REX_EXTERN(__imp__sub_8241DD48);  // read_file(path), registered at 0x82421A60
REX_EXTERN(__imp__sub_82457F50);  // size-class allocator (size, class)
REX_EXTERN(__imp__sub_823FB260);  // DataArray(this, node_count), initial refcount 1
REX_EXTERN(__imp__sub_8245C878);  // Symbol(out_slot, chars)

REXCVAR_DEFINE_BOOL(
    settings_bridge, true, "Settings",
    "In-game settings channel: answer the guest's own file_exists/read_file/write_file "
    "builtins when (and only when) the path is in the rbbz-settings/ namespace, "
    "so the native Advanced Settings screen can read and write host settings. "
    "Faithful: no content file is named in that namespace, so every call is "
    "forwarded unchanged.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(settings_bridge_forward_only, false, "Settings",
                    "Diagnostic for the settings channel: forward every call, including paths in "
                    "our own namespace, so an answering hook can be compared against a pure "
                    "no-op forward in one boot. Faithful: the channel answers and the guest gets "
                    "its value.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(settings_bridge_provider_active, false, "Settings",
                    "Diagnostic for P5b's merged Help & Options root: pre-commit "
                    "`provider.active = true` in the in-memory store, so the provider gate query "
                    "answers true and a boot shows the six-row root that an installed Ultimate "
                    "provider produces, instead of the five-row base root. P6 replaces this with "
                    "the adapter's real readiness, which is why it is a diagnostic and not a "
                    "setting. Read at Configure() from `REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE`, "
                    "because the cvar registry is not finalised that early.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(settings_hot_apply_diagnostic, false, "Settings",
                    "Diagnostic for P9's live apply: put a hot row through the real commit path "
                    "once at boot (set + commit on the guest channel), so a boot log proves the "
                    "row was applied to the running process rather than only saved. It writes "
                    "the row's own cvar, so it changes this run - which is why it is a "
                    "diagnostic and off by default. Read at Configure() from "
                    "`REX_SETTINGS_HOT_APPLY_DIAGNOSTIC`.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::settings::bridge {
namespace {

// Set once in Configure() on a host thread; the hooks only read them.
bool g_enabled = true;
bool g_forward_only = false;

// P11a: the channel's store is a seam. P1's in-memory sink is the default and is what a build with
// no settings service keeps using; the boot installs the service-backed one (settings/bridge_sink.h)
// once the store is up, before the guest can send a frame. The two pointers always name the same
// object - `RequestSink` is what a request is handed to, `SinkQueries` is what the one-bit read and
// ack answers are built from - so they are installed together.
MemorySink g_memory;
RequestSink* g_sink = &g_memory;
SinkQueries* g_queries = &g_memory;
Dispatcher g_dispatcher(&g_memory);
// P7: the controller profile's binding rows, created once at Configure() and kept for the life of
// the process. The help caption and the sink both borrow it; nothing else owns it.
std::unique_ptr<::rb_blitz::input::InputStore> g_input_store;

// Rejection logs are rate-limited: a guest that asks about a malformed frame every frame must not
// fill the log, and the first one is the one that matters.
int g_rejections_logged = 0;
constexpr int kMaxRejectionLogs = 8;

// A big-endian u32 out of guest memory, or 0 when the address is not mapped.
uint32_t GuestU32(uint8_t* base, const uint32_t addr) {
  const uint8_t* p = rex::memory::GuestPtr<const uint8_t*>(base, addr);
  if (p == nullptr) {
    return 0;
  }
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// The argument array's element i payload address. `r4` points at the array; its first word is
// argument 0's node pointer, and argument i's payload field is at `node0 + 8 + 8*i` - the exact
// arithmetic the originals use.
uint32_t PayloadAddress(uint8_t* base, const uint32_t arg_array, const unsigned index) {
  const uint32_t node0 = GuestU32(base, arg_array);
  if (node0 == 0) {
    return 0;
  }
  return node0 + 8 + 8 * index;
}

// The guest string an argument names, via the engine's own helper. Bounded and validated before
// it is copied: the length is the string's own, and anything that is not a plausible path is
// treated as "no path" rather than passed on.
std::string ArgumentString(uint8_t* base, PPCContext& ctx, const uint32_t arg_array,
                           const unsigned index) {
  const uint32_t payload = PayloadAddress(base, arg_array, index);
  if (payload == 0) {
    return {};
  }
  PPCContext call(ctx);
  call.r3.u64 = payload;
  __imp__sub_823F9538(call, base);
  if (call.r3.u32 == 0) {
    return {};
  }
  const char* text = rex::memory::GuestPtr<const char*>(base, call.r3.u32);
  if (text == nullptr) {
    return {};
  }
  // A settings path is short and printable; anything else is not one of ours. Reading stops at
  // the first byte outside the path's own alphabet, which also means a missing terminator cannot
  // walk off into unrelated memory. Bytes at or above 0x80 are admitted because they are the
  // UTF-8 of a *typed* query: P9-11's search field is written by the title's own keyboard as
  // `rbbz-store/search/text/<query>` (src/store/store_search.h), and a song or artist name may
  // carry an accent. Every other namespace matches ASCII tokens only, so admitting them costs
  // those parsers nothing - their comparisons simply fail on a name they never spell.
  constexpr size_t kMaxScan = 4096;
  std::string out;
  for (size_t i = 0; i < kMaxScan; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == 0) {
      break;
    }
    if (c < 0x20 || c == 0x7F) {
      return {};
    }
    out.push_back(static_cast<char>(c));
  }
  return out;
}

// The 8-byte result cell the builtins write: `{u32 value, u32 type}` in guest byte order.
void WriteResultCell(uint8_t* base, const uint32_t cell, const uint32_t value,
                     const uint32_t type = 0) {
  uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, cell);
  if (p == nullptr) {
    return;
  }
  p[0] = uint8_t((value >> 24) & 0xFF);
  p[1] = uint8_t((value >> 16) & 0xFF);
  p[2] = uint8_t((value >> 8) & 0xFF);
  p[3] = uint8_t(value & 0xFF);
  p[4] = uint8_t(type >> 24);
  p[5] = uint8_t(type >> 16);
  p[6] = uint8_t(type >> 8);
  p[7] = uint8_t(type);
}

void LogRejection(const char* why, const std::string_view path, const std::string& error) {
  if (g_rejections_logged >= kMaxRejectionLogs) {
    return;
  }
  ++g_rejections_logged;
  REXLOG_WARN("settings_bridge: refusing a {} request ({}): {}", why, error, path.substr(0, 96));
}

const char* StatusName(const Status status) {
  switch (status) {
    case Status::kOk: return "ok";
    case Status::kUnsupported: return "unsupported";
    case Status::kBadVersion: return "bad version";
    case Status::kMalformed: return "malformed";
    case Status::kTooLarge: return "too large";
    case Status::kUnknownOp: return "unknown op";
    case Status::kStaleRequest: return "stale request";
    case Status::kConflict: return "conflict";
    case Status::kInvalid: return "invalid";
    case Status::kIoError: return "io error";
  }
  return "?";
}

const char* OpName(const Op op) {
  switch (op) {
    case Op::kUnknown: return "unknown";
    case Op::kSnapshot: return "snapshot";
    case Op::kBeginEdit: return "begin_edit";
    case Op::kSet: return "set";
    case Op::kCancelField: return "cancel_field";
    case Op::kCommit: return "commit";
    case Op::kCancelEdit: return "cancel_edit";
    case Op::kInvoke: return "invoke";
    case Op::kRequestRestart: return "request_restart";
    case Op::kHelp: return "help";
    case Op::kCapture: return "capture";
    case Op::kCaptureCancel: return "capture_cancel";
    case Op::kBindReset: return "bind_reset";
    case Op::kBindResetAll: return "bind_reset_all";
  }
  return "?";
}

// The two wire forms, in the order a path is tried: the text form a DTA script writes, then the
// checksummed frame a caller that can build bytes writes. Both land in the same Request.
bool ParseAnyRequest(const std::string_view path, Request* out, std::string* error) {
  std::string text_error;
  if (ParseTextRequest(path, out, &text_error)) {
    return true;
  }
  std::string frame_error;
  if (ParseRequestPath(path, out, &frame_error)) {
    return true;
  }
  *error = text_error + "; as a frame: " + frame_error;
  return false;
}

// A frame that arrived on the write channel: apply it. The answer is the *ack*, not a value, so
// nothing has to be built for the guest here.
//
// P6's write-through is declared here and defined below with the rest of the provider half: it is
// what a commit of a provider row does (see "the provider's two ends of the channel").
void PersistProviderRows();

// P9's live half, defined below with the rest of the application-strategy seam.
void ApplyHotRows();

void HandleRequest(const std::string_view path) {
  Request request;
  std::string error;
  if (!ParseAnyRequest(path, &request, &error)) {
    LogRejection("malformed", path, error);
    return;
  }
  // The caption event (owner request 2026-10-13) is the one request that is not an edit: it is
  // delivered to whatever is drawing the help overlay and never reaches the store, so it is routed
  // here, before the dispatcher, and answered like any other write by the caller (there is no
  // result to build). No sink means no overlay in this build; the request is still consumed.
  if (request.op == Op::kHelp) {
    if (HelpSink* sink = GetHelpSink()) {
      sink->Show(request.setting);
    }
    REXLOG_DEBUG("settings_bridge: help '{}'", request.setting.empty() ? "-" : request.setting);
    return;
  }
  const Response response = g_dispatcher.Dispatch(request);
  REXLOG_INFO("settings_bridge: {} '{}' -> {} ({}{})", OpName(request.op),
              request.setting.empty() ? "-" : request.setting, StatusName(response.status),
              response.message.empty() ? "no message" : response.message,
              g_dispatcher.last_outcome() == Dispatcher::Outcome::kReplayed ? ", replayed" : "");
  if (request.op == Op::kCommit && response.status == Status::kOk &&
      g_dispatcher.last_outcome() == Dispatcher::Outcome::kForwarded) {
    // A committed session is written through to the provider's own file, because that file is what
    // the provider's own loader reads on the next boot. A replayed commit is not written again.
    if (g_sink == static_cast<RequestSink*>(&g_memory)) PersistProviderRows();
    // P9: and every row the commit changed whose strategy is `hot` is applied to the running
    // process now; the rest keep their restart, which is what the menu's prompt covers.
    ApplyHotRows();
  }
}

// A frame that arrived on the query channel: answer one bit. `ours` is false when the path is in
// our namespace but is not a frame we can read, which the caller answers with "no such file".
bool QueryValue(const std::string_view path, bool* ours) {
  Request request;
  std::string error;
  if (!ParseAnyRequest(path, &request, &error)) {
    LogRejection("unparsable", path, error);
    *ours = false;
    return false;
  }
  *ours = true;
  if (request.op == Op::kSnapshot) {
    // The read half: "does this setting currently hold this value?"
    const bool match = g_queries->Matches(request);
    REXLOG_DEBUG("settings_bridge: read '{}' -> {}", request.setting, match ? "true" : "false");
    return match;
  }
  // Anything else is the acknowledgement: has this request id been applied durably?
  const bool acknowledged = g_queries->Acknowledged(request.request_id);
  REXLOG_DEBUG("settings_bridge: ack for request {} -> {}", request.request_id,
               acknowledged ? "true" : "false");
  return acknowledged;
}

// ------------------------------------------------------------------------------------------------
// P6: the provider's two ends of the channel
// ------------------------------------------------------------------------------------------------
// The Ultimate payload's settings live in a file the provider writes (`GAME:/ulti_settings.dta`,
// redirected into user storage by src/fs/payload_overlay.cpp) and in the DTA variables its loader
// fills from that file. Our rows are an *alternative editor* of those same settings, so the channel
// has two jobs the stub could not do:
//
//   * **seed** - a row's state query reads the store's *committed* map, so the provider's current
//     values are put there at boot. Without this every provider row would draw "off" no matter what
//     the payload holds.
//   * **write through** - a commit that names provider rows is written into the provider's own
//     file, which is what the provider's loader reads on the next boot. *This* boot does not
//     change: the provider's DTA variables were read before the first frame, which is exactly why
//     every provider row is a `restart` row (plan §5.5) and why nothing here claims otherwise.
//
// Both run on a host thread (Configure) or on the guest thread the write hook already runs on, and
// neither touches the cvar registry.

using ModelValue = ::rb_blitz::settings::Value;
using ModelValueKind = ::rb_blitz::settings::ValueKind;
using ProviderBinding = ::rb_blitz::settings::ultimate_adapter::RowBinding;

// The wire spelling of a value lives in settings/bridge_sink.h (one place, so the store and this
// file cannot disagree about a kind); the provider half below needs its own reader because a
// provider row's *kind* is the payload's, not this build's schema.
using ::rb_blitz::settings::ModelValueFromWire;
using ::rb_blitz::settings::WireFromModelValue;

// The value a row's own token means, read back out of the store on a commit. The row's *kind* is
// what decides how: the provider's `speedmod` is a slider whose tokens are percentages, its
// `camera_angle` a slider whose tokens are names, and both arrive on the wire as text.
bool ValueFromWire(const ProviderBinding& binding, const WireValue& wire, ModelValue* out) {
  switch (binding.kind) {
    case ::rb_blitz::settings::ultimate_adapter::RowKind::kBool:
      if (wire.kind != ValueKind::kBool) {
        return false;
      }
      out->kind = ModelValueKind::kBool;
      out->boolean = wire.boolean;
      return true;
    case ::rb_blitz::settings::ultimate_adapter::RowKind::kSlider:
      if (binding.id == ::rb_blitz::settings::SettingId::kCameraAngle) {
        if (wire.kind != ValueKind::kText) {
          return false;
        }
        out->kind = ModelValueKind::kEnum;
        out->text = wire.text;
        return true;
      }
      if (wire.kind != ValueKind::kText) {
        return false;
      }
      out->kind = ModelValueKind::kInt;
      out->integer = static_cast<int32_t>(std::strtol(wire.text.c_str(), nullptr, 10));
      return true;
    case ::rb_blitz::settings::ultimate_adapter::RowKind::kSelect:
      if (wire.kind != ValueKind::kText) {
        return false;
      }
      out->kind = ModelValueKind::kEnum;
      out->text = wire.text;
      return true;
    default:
      return false;
  }
}

// The provider's readiness and its current values, put into the store before the guest can ask.
void SeedProvider() {
  // The diagnostic override, kept from P5b: a boot with `REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE=1`
  // forces the gate open so the six-row root can be exercised without an installed payload. It is
  // read from the **environment** rather than through the registry because `Configure()` runs about
  // 3 ms before `cvar: initialization finalized`, where `REXCVAR_GET` still answers with the
  // compiled default. It never replaces the adapter's answer for the product path - it only adds to
  // it - and `--settings_bridge_provider_active=false` is what turns it off.
  const char* provider_env = std::getenv("REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE");
  const bool forced = provider_env != nullptr && provider_env[0] != '\0' && provider_env[0] != '0';

  const ::rb_blitz::settings::ultimate_adapter::ProviderRuntime& provider =
      ::rb_blitz::ultimate::Provider();
  const bool active = forced || provider.Active();
  // The gate is also cached for guest-thread callers (the help observer reads it every menu
  // frame); this is the one place it is set, on a host thread.
  SetProviderGateActive(active);

  int seeded = 0;
  for (const ProviderBinding& binding : ::rb_blitz::settings::ultimate_adapter::RowBindings()) {
    const ModelValue* value = provider.ValueFor(binding.id);
    if (value == nullptr) {
      continue;
    }
    WireValue wire;
    if (!WireFromModelValue(*value, &wire)) {
      continue;
    }
    Request seed;
    seed.op = Op::kSet;
    seed.setting = std::string(binding.token);
    seed.value = wire;
    if (auto* service = dynamic_cast<ServiceSink*>(g_sink)) {
      service->SeedLocal(seed.setting, seed.value);
    } else {
      g_dispatcher.Dispatch(seed);
    }
    ++seeded;
  }
  // The gate the merged root asks before it draws any provider row (plan §3): the adapter's real
  // readiness, plus the diagnostic.
  {
    Request gate;
    gate.op = Op::kSet;
    gate.setting = "provider.active";
    gate.value = WireValue::Bool(active);
    if (auto* service = dynamic_cast<ServiceSink*>(g_sink)) {
      service->SeedLocal(gate.setting, gate.value);
    } else {
      g_dispatcher.Dispatch(gate);
    }
  }
  Request commit;
  commit.op = Op::kCommit;
  const Response response = g_dispatcher.Dispatch(commit);
  REXLOG_INFO("settings_bridge: provider {} - {} row value(s) seeded from {} ({})",
              active ? "active" : "not active", seeded, provider.summary.empty() ? "no payload"
                                                                                : provider.summary,
              StatusName(response.status));
  for (const auto& [variable, choices] : provider.choices) {
    std::string joined;
    for (const std::string& choice : choices) {
      joined += (joined.empty() ? "" : ", ");
      joined += choice;
    }
    REXLOG_INFO("settings_bridge: provider choice list '{}' = {}", variable, joined);
  }
}

// A commit that touched provider rows, written into the provider's own file. The pairs the provider
// persists and this build does not draw are preserved by the adapter's own writer, which is why the
// file is read, merged and replaced rather than regenerated.
void PersistProviderRows() {
  const auto& provider = ::rb_blitz::ultimate::Provider();
  if (provider.settings_path.empty()) {
    return;
  }
  std::map<::rb_blitz::settings::SettingId, ModelValue> values;
  for (const ProviderBinding& binding : ::rb_blitz::settings::ultimate_adapter::RowBindings()) {
    WireValue wire;
    if (!g_queries->Get(binding.token, &wire)) {
      continue;
    }
    ModelValue value;
    if (ValueFromWire(binding, wire, &value)) {
      values[binding.id] = std::move(value);
    }
  }
  std::map<std::string, std::string> updates;
  std::string encode_error;
  if (!::rb_blitz::settings::ultimate_adapter::EncodeProviderValues(values, &updates,
                                                                   &encode_error)) {
    REXLOG_WARN("settings_bridge: {}", encode_error);
  }
  if (updates.empty()) {
    return;
  }
  std::string error;
  if (::rb_blitz::ultimate::SaveProvider(updates, &error)) {
    REXLOG_INFO("settings_bridge: {} provider value(s) written to the provider's own file",
                updates.size());
  } else {
    REXLOG_WARN("settings_bridge: the provider's own file was not written: {}", error);
  }
}

// ------------------------------------------------------------------------------------------------
// P9: the live half of a commit
// ------------------------------------------------------------------------------------------------
// The commit above wrote the rows durably; this is what turns a *hot* one into a change the running
// process already has. It is the only place in the project where the §5.5 strategy is acted on:
//
//   * the store reports what the commit really changed (`last_commit_changes`), so a value written
//     back unchanged is not applied again;
//   * the plan comes from the schema table, so the UI's claim and this behaviour cannot drift;
//   * a `hot` row with no applier in this build goes back into the restart set, which is why a
//     build the coordinator has not wired yet still behaves honestly.
//
// It runs on the guest thread the write hook already runs on, and it does not touch the cvar
// registry itself: the applier (src/hooks/hot_apply_runtime.cpp) performs the write on a host
// thread and waits for it, so "applied" means "written and read back".

// A committed wire value as the model's own kind: the shared reader in settings/bridge_sink.h, with
// the row's declared `value_kind` as the arbiter, so a wrong-kind value is refused rather than
// coerced. It is the same function the store's own set path uses.

void ApplyHotRows() {
  const std::vector<std::string>& tokens = g_queries->last_commit_changes();
  if (tokens.empty()) {
    return;
  }
  std::map<::rb_blitz::settings::SettingId, ModelValue> changed;
  std::map<::rb_blitz::settings::SettingId, std::string> tokens_by_id;
  for (const std::string& token : tokens) {
    const ::rb_blitz::settings::SettingId id = ::rb_blitz::settings::IdForToken(token);
    if (id == ::rb_blitz::settings::SettingId::kUnknown) {
      continue;  // a provider row: its value lives in the provider's file, not in this build
    }
    const ::rb_blitz::settings::schema::SchemaRow* row =
        ::rb_blitz::settings::schema::FindRow(id);
    WireValue wire;
    if (row == nullptr || !g_queries->Get(token, &wire)) {
      continue;
    }
    ModelValue value;
    if (!ModelValueFromWire(row->descriptor, wire, &value)) {
      REXLOG_WARN("settings_bridge: the committed value of '{}' is not the kind its row takes; it "
                  "is not applied live and the row keeps its restart",
                  token);
      continue;
    }
    changed[id] = value;
    tokens_by_id[id] = token;
  }
  if (changed.empty()) {
    return;
  }

  const ::rb_blitz::settings::HotApplyPlan plan =
      ::rb_blitz::settings::PlanHotApplyByStrategy(changed);
  if (plan.live.empty()) {
    REXLOG_DEBUG("settings_bridge: the commit changed {} row(s); every one of them needs a new "
                 "process",
                 plan.pending_restart.size());
    return;
  }

  const std::vector<::rb_blitz::settings::HotApplyOutcome> outcomes =
      ::rb_blitz::settings::ApplyHotPlan(plan, ::rb_blitz::settings::RuntimeHotApplier());
  const std::vector<::rb_blitz::settings::SettingId> restart =
      ::rb_blitz::settings::RestartSetAfterApply(plan, outcomes);
  if (!restart.empty()) {
    // The boot owns the restart prompt's answer now: it compares the store against what this
    // process is running, so a row that failed to apply live is only owed a restart while its value
    // still differs - and it is owed one even when the commit itself reported none.
    if (auto* boot = ::rb_blitz::settings::boot::SettingsBoot::Current()) {
      boot->NoteRestartRows(restart);
    }
    if (auto* service = dynamic_cast<ServiceSink*>(g_sink)) service->RequireRestartAfterApply();
  }
  size_t applied = 0;
  for (const ::rb_blitz::settings::HotApplyOutcome& outcome : outcomes) {
    const auto found = tokens_by_id.find(outcome.id);
    const std::string token =
        found == tokens_by_id.end() ? std::string(SettingIdToken(outcome.id)) : found->second;
    if (outcome.status == ::rb_blitz::settings::HotApplyStatus::kApplied) {
      ++applied;
      REXLOG_INFO("settings_bridge: applied '{}' to the running game; the row carries no pending "
                  "state",
                  token);
    } else {
      REXLOG_WARN("settings_bridge: '{}' could not be applied live ({}); it needs a restart", token,
                  outcome.error.empty()
                      ? ::rb_blitz::settings::HotApplyStatusToken(outcome.status)
                      : outcome.error.c_str());
    }
  }
  REXLOG_INFO("settings_bridge: {} of {} changed row(s) applied live; {} row(s) still need a "
              "restart",
              applied, changed.size(), restart.size());
}

// The diagnostic, gated the way P5b's provider gate is: it puts a hot row through the *real*
// channel (a set and a commit, exactly what the menu writes), so a boot log proves the live path
// and not only the tests. It writes the row's own cvar and therefore changes this run, which is why
// it is off by default and read from the environment.
void RunHotApplyDiagnostic() {
  const char* env = std::getenv("REX_SETTINGS_HOT_APPLY_DIAGNOSTIC");
  if (env == nullptr || env[0] == '\0' || env[0] == '0') {
    return;
  }
  Request set;
  set.op = Op::kSet;
  set.setting = "graphics.vsync";
  set.value = WireValue::Bool(false);
  g_dispatcher.Dispatch(set);
  Request commit;
  commit.op = Op::kCommit;
  const Response response = g_dispatcher.Dispatch(commit);
  REXLOG_INFO("settings_bridge: hot-apply diagnostic committed '{}' -> {}", set.setting,
              StatusName(response.status));
  ApplyHotRows();
}

}  // namespace

// The entry points the overridden builtins call, and the only names this file exports besides
// Configure(). They are named rather than anonymous because the hooks at the bottom of the file
// are at global scope (that is where a strong REX_FUNC definition belongs), and they stay out of
// the header because they take the guest register context while the header is SDK-free on purpose.
//
// Each returns true when the call was ours and the guest has already been given its answer, so the
// hook forwards only when this returns false.

bool TakeExists(PPCContext& ctx, uint8_t* base) {
  const std::string path = ArgumentString(base, ctx, ctx.r4.u32, 0);
  bool recovery_answer = false;
  if (::rb_blitz::loading::CrashRecoveryQuery(path, &recovery_answer)) {
    WriteResultCell(base, ctx.r3.u32, recovery_answer ? 1u : 0u);
    return true;
  }
  bool header_answer = false;
  if (::rb_blitz::input::AnswerMenuHeaderQuery(path, &header_answer)) {
    WriteResultCell(base, ctx.r3.u32, header_answer ? 1u : 0u);
    return true;
  }
  uint32_t graphics_answer = 0;
  if (::rb_blitz::graphics::LoadingQuery(path, &graphics_answer)) {
    WriteResultCell(base, ctx.r3.u32, graphics_answer);
    return true;
  }
  // The "Play Current Song" probes (owner feature, 2026-10-14) ride the same builtin but are not part
  // of the settings channel: they are the main menu's only way to ask the host which pooled DLC song
  // is playing, so they are answered even on a boot that turns the settings channel off or makes it
  // forward-only. The whole `rbbz-play/` namespace is ours (src/hooks/menu_music_pool.h).
  bool play_answer = false;
  if (::rb_blitz::shell_music::AnswerPlayQuery(path, &play_answer)) {
    WriteResultCell(base, ctx.r3.u32, play_answer ? 1u : 0u);
    return true;
  }
  // The RhythmVerse store's own one-bit queries (plan P1) ride this builtin too, and are answered
  // even on a boot that turns the settings channel off: they ask the host store rather than the
  // settings store, and the main menu's entry depends on them (src/store/store_settings.h).
  bool store_answer = false;
  if (::rb_blitz::store::AnswerStoreQuery(path, &store_answer)) {
    WriteResultCell(base, ctx.r3.u32, store_answer ? 1u : 0u);
    return true;
  }
  // P9-10's press gate (`rbbz-store/catalogue/failed`): the catalogue holds no offers and its
  // last walk failed. Answered by the catalogue worker rather than the settings snapshot, and
  // before the settings gate for the same reason (the main menu's store row depends on it).
  bool catalogue_answer = false;
  if (::rb_blitz::store::AnswerCatalogueQuery(path, &catalogue_answer)) {
    WriteResultCell(base, ctx.r3.u32, catalogue_answer ? 1u : 0u);
    return true;
  }
  // P5's download state predicates (`rbbz-store/state/<job>/<state>`,
  // `progress/<job>/at-least/<bucket>`, `error/<job>/<token>`) are the same one-bit read. They
  // are answered here for the same reason: the store owns them, not the settings channel.
  bool download_answer = false;
  if (::rb_blitz::store::AnswerDownloadQuery(path, &download_answer)) {
    WriteResultCell(base, ctx.r3.u32, download_answer ? 1u : 0u);
    return true;
  }
  // P4's search bits (`rbbz-store/search/show|seed`) are the store record's read half - the same
  // one-bit channel again, answered with the settings channel off for the same reason. P9-11
  // retired the key pages' own probes (`open`, `ready`, `state/<token>`, `count/at-least/<n>`)
  // along with the pages.
  bool search_answer = false;
  if (::rb_blitz::store::AnswerSearchQuery(path, &search_answer)) {
    WriteResultCell(base, ctx.r3.u32, search_answer ? 1u : 0u);
    return true;
  }
  bool art_answer = false;
  if (::rb_blitz::store::AnswerArtQuery(path, &art_answer)) {
    WriteResultCell(base, ctx.r3.u32,
                    art_answer && ::rb_blitz::store::Settings()->enabled ? 1u : 0u);
    return true;
  }
  if (!g_enabled || g_forward_only) {
    return false;
  }
  if (!IsNamespacedPath(path)) {
    return false;
  }
  bool ours = false;
  const bool answer = QueryValue(path, &ours);
  WriteResultCell(base, ctx.r3.u32, (ours && answer) ? 1u : 0u);
  return true;
}

bool TakeRead(PPCContext& ctx, uint8_t* base) {
  const std::string path = ArgumentString(base, ctx, ctx.r4.u32, 0);
  // Independent of the settings channel, like the rbbz-play/begin query. Use
  // the same native Symbol constructor as binding text below, preserving the
  // exact metadata spelling that SongMgr expects (DataNode type 5).
  if (path == "rbbz-play/song") {
    const auto key = ::rb_blitz::shell_music::SelectedSongKey();
    ::rb_blitz::shell_music::ReturnSongSymbol(ctx, base, key);
    return true;
  }
  if (!g_enabled || g_forward_only) return false;
  if (!IsNamespacedPath(path)) return false;
  constexpr std::string_view prefix = "rbbz-settings/text/";
  const auto snapshot = ::rb_blitz::input::CurrentInput();
  std::vector<std::string> lines;
  if (path.starts_with(prefix)) {
    const std::string_view panel = std::string_view(path).substr(prefix.size());
    lines = ::rb_blitz::ui::screen::PanelBindingText(panel, *snapshot);
    if (!lines.empty() && ::rb_blitz::assets::InputIconsAvailable()) {
      auto view = ::rb_blitz::input::icons::CaptureActive();
      view.input = snapshot;
      view.revision = snapshot->revision;
      auto page = ::rb_blitz::ui::input_icon_rows::Build(
          ::rb_blitz::ui::screen::PanelRowSymbols(panel), lines, view);
      if (page.overflow) {
        REXLOG_WARN("input_icons: '{}' needs more than 30 image slots; full text retained", panel);
      } else if (!page.icons.empty()) {
        const std::string signature = std::string(panel) + ":" +
            std::to_string(view.revision) + ":" + view.controller_scheme;
        ::rb_blitz::assets::PublishInputIcons(signature, std::move(page.icons));
        lines = std::move(page.lines);
      }
    }
  }
  if (lines.empty()) {
    WriteResultCell(base, ctx.r3.u32, 0);
    return true;
  }

  // Use the title's own allocator/constructor pair: DataArray is a 16-byte header with a
  // separately allocated node buffer and a u16 reference count at +10. A raw node buffer is
  // not an array. Transfer the constructor's one reference to the returned DataNode; normal
  // variable/list ownership then releases it, just as the original read_file does.
  size_t longest = 0;
  for (const auto& line : lines) longest = std::max(longest, line.size());
  rex::CallFrame frame(ctx);
  auto& call = frame.ctx;
  call.r1.u32 -= static_cast<uint32_t>((longest + 1 + 0x100 + 15) & ~size_t{15});
  const uint32_t chars = call.r1.u32 + 0x80;
  call.r3.u32 = 16;
  call.r4.u32 = 16;
  __imp__sub_82457F50(call, base);
  const uint32_t array = call.r3.u32;
  if (array == 0) {
    WriteResultCell(base, ctx.r3.u32, 0);
    return true;
  }
  call.r3.u32 = array;
  call.r4.u32 = static_cast<uint32_t>(lines.size());
  __imp__sub_823FB260(call, base);
  const uint32_t nodes = rex::memory::load_and_swap<uint32_t>(base + array);
  for (size_t i = 0; i < lines.size(); ++i) {
    std::memcpy(base + chars, lines[i].c_str(), lines[i].size() + 1);
    const uint32_t node = nodes + static_cast<uint32_t>(i * 8);
    call.r3.u32 = node;
    call.r4.u32 = chars;
    __imp__sub_8245C878(call, base);
    const uint32_t symbol = rex::memory::load_and_swap<uint32_t>(base + node);
    WriteResultCell(base, node, symbol, 5);
  }
  WriteResultCell(base, ctx.r3.u32, array, 16);
  std::string logged;
  for (const auto& line : lines) {
    if (!logged.empty()) logged += " | ";
    logged += line;
  }
  REXLOG_INFO("settings_bridge: binding text {}: {}", path.substr(prefix.size()), logged);
  return true;
}

bool TakeWrite(PPCContext& ctx, uint8_t* base) {
  const std::string path = ArgumentString(base, ctx, ctx.r4.u32, 0);
  if (::rb_blitz::store::HandleArtWrite(path, ::rb_blitz::store::Settings()->enabled)) {
    WriteResultCell(base, ctx.r3.u32, 0);
    return true;
  }
  // P4's search keys (`rbbz-store/search/open|done|cancel|key/<token>|text/<hex>`) come first
  // because the download handler below claims the *whole* `rbbz-store/` prefix: it answers `true`
  // for anything it cannot parse as a job command ("refusing a malformed write"), which would
  // otherwise swallow every key press. This parser answers `false` for a `download/...` path, so
  // P5's commands still reach it unchanged.
  std::string search_why;
  if (::rb_blitz::store::HandleStoreSearchWrite(path, &search_why)) {
    // One line per key the guest sends: the measurement's evidence that a press reached the host,
    // and the only way to tell a dropped press from a refused one.
    if (search_why.empty()) {
      REXLOG_INFO("store_search: '{}'", path);
    } else {
      REXLOG_INFO("store_search: '{}' {}", path, search_why);
    }
    WriteResultCell(base, ctx.r3.u32, 0);
    return true;
  }
  // P9-10's retry (`rbbz-store/catalogue/retry`) has to be parsed **before** the download handler,
  // which claims the whole `rbbz-store/` prefix and answers `true` for anything it cannot parse as
  // a job command ("refusing a malformed write") - the same reason P4's search keys come first.
  if (::rb_blitz::store::HandleCatalogueWrite(path)) {
    REXLOG_INFO("store_catalogue: '{}' asked for a fresh walk", path);
    WriteResultCell(base, ctx.r3.u32, 0);
    return true;
  }
  // P5's download commands (`rbbz-store/download/start|cancel|again/<job>`) are the store's, not
  // the settings channel's, so they are consumed before the settings gate - the same ordering the
  // store's read queries use above.
  if (::rb_blitz::store::HandleDownloadWrite(path)) {
    WriteResultCell(base, ctx.r3.u32, 0);
    return true;
  }
  if (!g_enabled || g_forward_only) {
    return false;
  }
  if (!IsNamespacedPath(path)) {
    return false;
  }
  HandleRequest(path);
  WriteResultCell(base, ctx.r3.u32, 0);
  return true;
}

void Configure() {
  g_enabled = REXCVAR_GET(settings_bridge);
  g_forward_only = REXCVAR_GET(settings_bridge_forward_only);

  // P7: one writer for the controller profile. The two device gates are read from the cvars here
  // - the boot applied the profile at config rank before this point, so an explicit command-line
  // or environment value has already won - and a row whose value came from either of those stays
  // read-only in the menu. The store publishes the initial binding snapshot before the input
  // system is created, so the first poll reads the profile's tables rather than a default. The
  // old `gameplay_mapping_*` switches are not read at all (restructure plan D3): custom is
  // inferred from the tables.
  {
    const auto effective_bool = [](const char* name, const bool compiled_default) {
      const std::string text = rex::cvar::GetFlagByName(name);
      if (text.empty()) return compiled_default;
      return text != "false";
    };
    const auto locked = [](const char* name) {
      const rex::cvar::Source source = rex::cvar::GetFlagSource(name);
      return source == rex::cvar::Source::kCommandLine ||
             source == rex::cvar::Source::kEnvironment;
    };
    ::rb_blitz::input::InputStoreOptions options;
    options.keyboard_support = effective_bool("keyboard_support", true);
    options.mouse_support = effective_bool("mouse_ui_nav", true);
    options.keyboard_locked = locked("keyboard_support");
    options.mouse_locked = locked("mouse_ui_nav");
    options.apply_switch = [](std::string_view name, const bool value, std::string* error) {
      const std::string text = value ? "true" : "false";
      bool ok = false;
      std::string why;
      // The project rule: a guest thread must not touch the cvar registry. This runs from the
      // settings write hook, so the write goes to a short-lived host thread and is read back.
      std::thread worker([&name, &text, &ok, &why] {
        if (!rex::cvar::SetFlagByName(std::string(name), text)) {
          why = "the cvar refused the value";
          return;
        }
        const std::string after = rex::cvar::GetFlagByName(std::string(name));
        if (after != text) {
          why = "the cvar still reads '" + after + "'";
          return;
        }
        ok = true;
      });
      worker.join();
      if (!ok && error != nullptr) {
        *error = why;
      }
      return ok;
    };
    options.on_published = [] { ::rb_blitz::input::InvalidateGameplayInput(); };
    options.pad_connected = [] {
      rex::input::X_INPUT_STATE state{};
      return ::rb_blitz::input::SamplePhysicalPad(&state);
    };

    const std::filesystem::path path = ::rb_blitz::profile::CurrentProfilePath();
    ::rb_blitz::profile::ProfileLoadResult load;
    if (!path.empty()) {
      load = ::rb_blitz::profile::LoadProfile(path);
    }
    g_input_store = ::rb_blitz::input::InputStore::Load(path, load, options);
    g_input_store->PublishInitial();
    ::rb_blitz::settings::SetInputBindings(g_input_store.get());
    if (auto* sink = dynamic_cast<ServiceSink*>(g_sink)) {
      sink->SetInputBindingStore(g_input_store.get());
    }
    if (path.empty()) {
      REXLOG_WARN("settings_bridge: Controls store has nowhere to keep the profile; edits will "
                  "not be saved");
    } else {
      REXLOG_INFO("settings_bridge: Controls store '{}'; {}", path.string(),
                  g_input_store->Summary());
    }
  }

  SeedProvider();
  REXLOG_INFO("settings_bridge: typed guest channel {} (namespace '{}', forward-only {})",
              g_enabled ? "on" : "off", kNamespacePrefix, g_forward_only ? "on" : "off");
  // P11a: name the store the channel is editing, so a boot says whether the menu's writes are
  // going to settings.toml (the product path) or to the in-memory stand-in (a build with no
  // settings service, which is what the tests and an unwired tree keep).
  REXLOG_INFO("settings_bridge: store {}", g_sink == static_cast<RequestSink*>(&g_memory)
                                              ? "in-memory (no settings service installed)"
                                              : "the native settings service (settings.toml)");
  // P9: name the live set once, so a boot says what a commit can apply without a restart.
  REXLOG_INFO("settings_bridge: live applier: {}", ::rb_blitz::settings::HotApplierSummary());
  RunHotApplyDiagnostic();
}

void InstallSink(RequestSink* sink, SinkQueries* queries) {
  g_sink = sink != nullptr ? sink : static_cast<RequestSink*>(&g_memory);
  g_queries = queries != nullptr ? queries : static_cast<SinkQueries*>(&g_memory);
  g_dispatcher.SetSink(g_sink);
}

}  // namespace rb_blitz::settings::bridge

// 0x8241DFB0 file_exists(path): answer the query channel, forward everything else exactly once.
extern "C" REX_FUNC(sub_8241DFB0) {
  if (rb_blitz::settings::bridge::TakeExists(ctx, base)) {
    return;
  }
  __imp__sub_8241DFB0(ctx, base);
}

// 0x8241DE48 write_file(path, data): take the request channel, forward everything else once.
extern "C" REX_FUNC(sub_8241DE48) {
  if (rb_blitz::settings::bridge::TakeWrite(ctx, base)) {
    return;
  }
  __imp__sub_8241DE48(ctx, base);
}

extern "C" REX_FUNC(sub_8241DD48) {
  if (rb_blitz::settings::bridge::TakeRead(ctx, base)) return;
  __imp__sub_8241DD48(ctx, base);
}
