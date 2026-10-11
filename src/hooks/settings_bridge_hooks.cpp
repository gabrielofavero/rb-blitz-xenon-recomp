// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The guest half of the typed settings channel (P1 of docs/plans/in-game-settings-plan.md).
// src/hooks/settings_bridge.h holds the transport decision and the frame rules; this file is the
// two overridden guest builtins that carry it, plus the boot line that reports them.
//
// Why overridden guest functions and not a file hook: the title's own script builtins are ordinary
// recompiled functions, so a strong `extern "C" REX_FUNC(sub_...)` replaces every call site while
// `__imp__sub_...` stays the original - the pattern already used by src/hooks/ultimate.cpp and
// src/hooks/shell_music.cpp. It adds **no** `Nt*` definition, so the read/query hooks owned by
// src/hooks/menu_filter.cpp and src/hooks/dlc_refresh.cpp keep their single strong definitions.
//
// Measured addresses and ABI (docs/plans/in-game-settings-bridge-findings.md §2, from the pinned
// retail image and the recompiled bodies):
//
//   0x8241DFB0  file_exists(path)     -> writes {bool, type 0} into the caller's 8-byte result
//   0x8241DE48  write_file(path, data) -> writes {0, 0}
//   0x823F9538  node -> string: both take the argument's payload field address (arg i's payload is
//               at `*(u32*)r4 + 8 + 8*i`) and return a guest string pointer
//
// The rule for both hooks is the same and is the reason the channel is safe:
//
//   * a path in the exact `rbbz-settings/` namespace is **ours** - it is parsed, dispatched and
//     answered here, and the original is *not* called, because a name in our namespace is a name
//     no content file can have;
//   * every other path is forwarded to the original **exactly once**, unchanged.
//
// Faithful behaviour is therefore "no content file is named like that": with the channel idle the
// two hooks only add a prefix comparison to a call the title already makes, and the game is the
// game it was. `settings_bridge_forward_only` is the diagnostic that makes even our own namespace
// forward, so the no-op forwarded hook can be compared against the answering one in one boot.
//
// The cvar registry is read once, in Configure(), on a host thread: a guest thread must not touch
// it (project rule, docs/plans/song-cache-plan.md §7). The hooks only read the cached flags.

#include "hooks/settings_bridge.h"

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "hooks/ultimate.h"
#include "settings/ultimate_adapter.h"

// The two builtins the channel rides on and the node -> string helper the originals use. The
// originals are the __imp__ aliases; the strong definitions below replace the call sites.
REX_EXTERN(__imp__sub_8241DFB0);
REX_EXTERN(__imp__sub_8241DE48);
REX_EXTERN(__imp__sub_823F9538);

REXCVAR_DEFINE_BOOL(settings_bridge, true, "Settings",
                    "In-game settings channel: answer the guest's own file_exists/write_file "
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

namespace rb_blitz::settings::bridge {
namespace {

// Set once in Configure() on a host thread; the hooks only read them.
bool g_enabled = true;
bool g_forward_only = false;

MemorySink g_store;
Dispatcher g_dispatcher(&g_store);

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
  // walk off into unrelated memory.
  constexpr size_t kMaxScan = 4096;
  std::string out;
  for (size_t i = 0; i < kMaxScan; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == 0) {
      break;
    }
    if (c < 0x20 || c > 0x7E) {
      return {};
    }
    out.push_back(static_cast<char>(c));
  }
  return out;
}

// The 8-byte result cell both builtins write: `{u32 value, u32 type}` in guest byte order.
void WriteResultCell(uint8_t* base, const uint32_t cell, const uint32_t value) {
  uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, cell);
  if (p == nullptr) {
    return;
  }
  p[0] = uint8_t((value >> 24) & 0xFF);
  p[1] = uint8_t((value >> 16) & 0xFF);
  p[2] = uint8_t((value >> 8) & 0xFF);
  p[3] = uint8_t(value & 0xFF);
  p[4] = p[5] = p[6] = p[7] = 0;  // type 0 = int/bool
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

void HandleRequest(const std::string_view path) {
  Request request;
  std::string error;
  if (!ParseAnyRequest(path, &request, &error)) {
    LogRejection("malformed", path, error);
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
    PersistProviderRows();
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
    const bool match = g_store.Matches(request);
    REXLOG_DEBUG("settings_bridge: read '{}' -> {}", request.setting, match ? "true" : "false");
    return match;
  }
  // Anything else is the acknowledgement: has this request id been applied durably?
  const bool acknowledged = g_store.Acknowledged(request.request_id);
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

// The wire spelling of one of the provider's values: the same `b:1` / `t:<token>` a row writes.
bool WireFromValue(const ModelValue& value, WireValue* out) {
  switch (value.kind) {
    case ModelValueKind::kBool:
      *out = WireValue::Bool(value.boolean);
      return true;
    case ModelValueKind::kInt:
      *out = WireValue::Int(value.integer);
      return true;
    case ModelValueKind::kEnum:
    case ModelValueKind::kString:
      if (value.text.empty()) {
        return false;
      }
      *out = WireValue::Text(value.text);
      return true;
    default:
      return false;
  }
}

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

  int seeded = 0;
  for (const ProviderBinding& binding : ::rb_blitz::settings::ultimate_adapter::RowBindings()) {
    const ModelValue* value = provider.ValueFor(binding.id);
    if (value == nullptr) {
      continue;
    }
    WireValue wire;
    if (!WireFromValue(*value, &wire)) {
      continue;
    }
    Request seed;
    seed.op = Op::kSet;
    seed.setting = std::string(binding.token);
    seed.value = wire;
    g_dispatcher.Dispatch(seed);
    ++seeded;
  }
  // The gate the merged root asks before it draws any provider row (plan §3): the adapter's real
  // readiness, plus the diagnostic.
  {
    Request gate;
    gate.op = Op::kSet;
    gate.setting = "provider.active";
    gate.value = WireValue::Bool(active);
    g_dispatcher.Dispatch(gate);
  }
  Request commit;
  commit.op = Op::kCommit;
  const Response response = g_dispatcher.Dispatch(commit);
  REXLOG_INFO("settings_bridge: provider {} - {} row value(s) seeded from {} ({})",
              active ? "active" : "not active", seeded, provider.summary.empty() ? "no payload"
                                                                                : provider.summary,
              StatusName(response.status));
  if (active && !forced) {
    // Said once, plainly: the achievements row is a one-shot *guest* action (the provider's own
    // accomplishment call on the guest thread), and this pass does not make it. Answering
    // "action taken" without calling it would be a lie the menu cannot see.
    REXLOG_WARN("settings_bridge: the achievements action is not wired yet; invoking it answers "
                "without calling the provider");
  }
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
    if (!g_store.Get(binding.token, &wire)) {
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

}  // namespace

// The two entry points the overridden builtins call, and the only names this file exports besides
// Configure(). They are named rather than anonymous because the hooks at the bottom of the file
// are at global scope (that is where a strong REX_FUNC definition belongs), and they stay out of
// the header because they take the guest register context while the header is SDK-free on purpose.
//
// Each returns true when the call was ours and the guest has already been given its answer, so the
// hook forwards only when this returns false.

bool TakeExists(PPCContext& ctx, uint8_t* base) {
  if (!g_enabled || g_forward_only) {
    return false;
  }
  const std::string path = ArgumentString(base, ctx, ctx.r4.u32, 0);
  if (!IsNamespacedPath(path)) {
    return false;
  }
  bool ours = false;
  const bool answer = QueryValue(path, &ours);
  WriteResultCell(base, ctx.r3.u32, (ours && answer) ? 1u : 0u);
  return true;
}

bool TakeWrite(PPCContext& ctx, uint8_t* base) {
  if (!g_enabled || g_forward_only) {
    return false;
  }
  const std::string path = ArgumentString(base, ctx, ctx.r4.u32, 0);
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
  SeedProvider();
  REXLOG_INFO("settings_bridge: typed guest channel {} (namespace '{}', forward-only {})",
              g_enabled ? "on" : "off", kNamespacePrefix, g_forward_only ? "on" : "off");
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
