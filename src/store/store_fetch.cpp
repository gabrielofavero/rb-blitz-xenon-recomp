// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_fetch.h"

#include <mutex>
#include <set>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "store/store_serve.h"
#include "store/store_probe.h"
#include "store/store_settings.h"

REXCVAR_DEFINE_STRING(store_serve_dir, "", "Diagnostics",
                      "Serve the retail store's netcache records from this host directory (a file "
                      "named exactly like the requested path, e.g. "
                      "dlc_store/europe/dlc_top_xbox_eng.dta); empty uses the built-in records")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(store_fetch_deliver, false, "Diagnostics",
                    "Hand a served store record straight to the panel's own front-menu builder "
                    "(RhythmVerse plan P3's delivery spike); off, the record is only put into the "
                    "manager's loaded-file container and the retail notify runs unchanged")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::store {
namespace {

// The manager object `sub_8235A240` itself loads (`lis r11,-32103` = 0x8299, `lwz r3,-9964(r11)`
// = 0x8298D914): the file cache whose loaded-file container the requests are answered from.
constexpr uint32_t kFileCacheGlobal = 0x8298D914u;

REX_EXTERN(__imp__sub_823E0C00);  // the guest allocator (160-byte request entries use it too)
REX_EXTERN(__imp__sub_824685E0);  // insert a loaded file: (manager, path, data, size)
REX_EXTERN(__imp__sub_82468518);  // "is this path already loaded?" (the request's branch test)
REX_EXTERN(__imp__sub_82474E68);  // the loaded-file lookup behind it, on `manager + 76`
REX_EXTERN(__imp__sub_827537C0);  // the panel's own front-menu builder: (panel, DataArray, source)

// Set once by InstallStoreFetch() on the host thread, before the guest exists; read-only after, so
// the guest thread may read it without a lock.
bool g_on = false;
bool g_deliver = false;
std::string g_serve_dir;

uint32_t GuestU32(uint8_t* base, uint32_t addr) {
  const uint8_t* p = rex::memory::GuestPtr<const uint8_t*>(base, addr);
  if (p == nullptr) {
    return 0;
  }
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

std::string GuestCString(uint8_t* base, uint32_t addr, size_t max_length) {
  const char* text = rex::memory::GuestPtr<const char*>(base, addr);
  if (text == nullptr) {
    return {};
  }
  std::string out;
  for (size_t i = 0; i < max_length; ++i) {
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

// The guest buffer the record is copied into. One per boot, grown when a larger record arrives:
// the insert copies what it is handed (the download path hands it a buffer it owns and frees its
// own source), so the buffer only has to outlive the call - but reusing one allocation is cheaper
// than allocating per request and leaves no freed pointer behind a panic.
std::mutex g_buffer_mutex;
uint32_t g_buffer = 0;
uint32_t g_buffer_size = 0;

uint32_t Allocate(PPCContext& ctx, uint8_t* base, size_t size) {
  const uint32_t want = static_cast<uint32_t>((size + 255) & ~size_t{255});
  PPCContext call = ctx;
  call.r3.u64 = want;
  __imp__sub_823E0C00(call, base);
  return call.r3.u32;
}

uint32_t EnsureBuffer(PPCContext& ctx, uint8_t* base, size_t size) {
  std::lock_guard<std::mutex> lock(g_buffer_mutex);
  if (g_buffer != 0 && g_buffer_size >= size) {
    return g_buffer;
  }
  const uint32_t allocated = Allocate(ctx, base, size);
  if (allocated == 0) {
    return 0;
  }
  g_buffer = allocated;
  g_buffer_size = static_cast<uint32_t>((size + 255) & ~size_t{255});
  return g_buffer;
}

// One line per distinct path per boot: a store screen re-requests the same menu on every visit.
std::mutex g_log_mutex;
std::set<std::string> g_logged;

void LogOnce(const std::string& path, const std::string& line) {
  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_logged.size() >= 128 || !g_logged.insert(path).second) {
      return;
    }
  }
  REXLOG_INFO("store_fetch: {}", line);
}

}  // namespace

void InstallStoreFetch() {
  g_serve_dir = REXCVAR_GET(store_serve_dir);
  g_deliver = REXCVAR_GET(store_fetch_deliver);
  g_on = true;
  REXLOG_INFO("store_fetch: serving the store's records from {} (delivery spike {})",
              g_serve_dir.empty() ? "the built-in records" : ("'" + g_serve_dir + "' first"),
              g_deliver ? "on" : "off");
}

bool ServeStoreRequest(PPCContext& ctx, uint8_t* base, uint32_t path_string) {
  if (!g_on || path_string == 0) {
    return false;
  }
  const std::string path = GuestCString(base, GuestU32(base, path_string + 4), 240);
  if (!IsStoreRequestPath(path)) {
    return false;
  }
  // Master off means no store work at all (plan §1.6); the row's gate already refuses entry, and a
  // stale screen left over from a commit must not be fed either.
  if (!Settings()->enabled) {
    LogOnce(path, "not serving '" + path + "': RhythmVerse is off");
    return false;
  }
  const ServedRecord record = RecordForRequest(path, g_serve_dir);
  if (record.text.empty()) {
    LogOnce(path, "no record for '" + path + "': " + record.source);
    return false;
  }
  const uint32_t manager = GuestU32(base, kFileCacheGlobal);
  if (manager == 0) {
    LogOnce(path, "no record for '" + path + "': the file cache manager is not up yet");
    return false;
  }
  const uint32_t path_chars = GuestU32(base, path_string + 4);
  // The request's own branch test, called here before anything is inserted: `sub_8235A240` takes
  // its "already loaded -> notify" branch exactly when this answers true, and the three words it
  // guards on are logged with it because a false answer with a loaded file is the one result that
  // cannot be explained from the outside (measured 2026-10-20: the insert landed, the request still
  // submitted).
  if (TraceEnabled()) {
    PPCContext probe = ctx;
    probe.r3.u64 = manager;
    probe.r4.u64 = path_chars;
    __imp__sub_82468518(probe, base);
    const bool loaded_before = probe.r3.u32 != 0;
    PPCContext lookup = ctx;
    lookup.r3.u64 = manager + 76;
    lookup.r4.u64 = path_chars;
    __imp__sub_82474E68(lookup, base);
    REXLOG_INFO("store_fetch: '{}' before insert: loaded={} lookup={:#x} guard(44={},48={},104={})",
                path, loaded_before ? "yes" : "no", lookup.r3.u32,
                GuestU32(base, manager + 44), GuestU32(base, manager + 48),
                GuestU32(base, manager + 104));
  }
  const uint32_t buffer = EnsureBuffer(ctx, base, record.text.size() + 1);
  if (buffer == 0) {
    LogOnce(path, "no record for '" + path + "': the guest buffer allocation failed");
    return false;
  }
  uint8_t* dest = rex::memory::GuestPtr<uint8_t*>(base, buffer);
  if (dest == nullptr) {
    LogOnce(path, "no record for '" + path + "': the guest buffer is not mapped");
    return false;
  }
  for (size_t i = 0; i < record.text.size(); ++i) {
    dest[i] = static_cast<uint8_t>(record.text[i]);
  }
  dest[record.text.size()] = 0;
  PPCContext call = ctx;
  call.r3.u64 = manager;
  call.r4.u64 = path_chars;
  call.r5.u64 = buffer;
  call.r6.u64 = static_cast<uint32_t>(record.text.size());
  __imp__sub_824685E0(call, base);
  LogOnce(path, "served '" + path + "' from " + record.source + " (" +
                    std::to_string(record.text.size()) + " byte(s), insert -> " +
                    std::to_string(call.r3.u32) + ")");
  if (TraceEnabled()) {
    // Names the two objects the notify involves: the client the request registered (`ctx.r5`) and
    // the loaded file the insert returned. Their vtables say which interface the callback really
    // is, which is the one thing a crash inside the notify cannot be explained without.
    const uint32_t client = ctx.r5.u32;
    const uint32_t client_vt = client != 0 ? GuestU32(base, client) : 0;
    const uint32_t file = call.r3.u32;
    const uint32_t file_vt = file != 0 ? GuestU32(base, file) : 0;
    REXLOG_INFO("store_fetch: '{}' client={:#x} vt={:#x} slot5={:#x}; file={:#x} vt={:#x}", path,
                client, client_vt, client_vt != 0 ? GuestU32(base, client_vt + 20) : 0u, file,
                file_vt);
  }
  if (!g_deliver) {
    // The record stays in the container; the retail request runs unchanged and takes its
    // "nothing loaded" branch (the manager's readiness word is 0 on a boot that never reached a
    // server). Nothing is delivered yet - see the plan's P3 record.
    return false;
  }
  // Delivery spike: hand the record straight to the panel's own front-menu builder
  // (`sub_827537C0(panel, DataArray, source)`). The insert already parsed the record
  // (`sub_82468138` stores the DataArray at `file + 68` for a `.dta`), so this is the panel work
  // that the file-cache notification would have triggered - without the notification, whose
  // message shape is still unproven (the `sub_8235A0F8` route crashes inside the client callback).
  const uint32_t file = call.r3.u32;
  const uint32_t array = file != 0 ? GuestU32(base, file + 68) : 0;
  // The client the request registered is the panel's secondary subobject, and the dispatcher's
  // `this` is `client - *(client - 4)` (the thunk at the callback's vtable slot). That value is
  // exactly what the panel's own top-menu handler passes to the builder: `sub_827538B8`'s
  // prologue takes its second argument - the dispatcher's `this` - and calls
  // `sub_827537C0(this, array, source)` with it. Verified before use: the result must carry the
  // secondary vtable resolved from the image's RTTI (out/_swap/rtti_vtables.py).
  const uint32_t client = ctx.r5.u32;
  const uint32_t panel = client != 0 ? client - GuestU32(base, client - 4) : 0;
  const uint32_t panel_vt = panel != 0 ? GuestU32(base, panel) : 0;
  constexpr uint32_t kPanelSecondaryVtable = 0x82102AC4u;
  if (array == 0 || panel == 0 || panel_vt != kPanelSecondaryVtable) {
    LogOnce(path + "#deliver", "not delivered '" + path + "': array=" +
                                   std::to_string(array) + " panel=" + std::to_string(panel) +
                                   " vtable=" + std::to_string(panel_vt));
    return false;
  }
  // The builder's third argument is a `char*`, not a `String*`: `sub_8275D5B8` copies it with
  // `sub_82459130`, which is `String::String(const char*)` (the loaded-file ctor calls it the same
  // way with the path characters). Passing a String here is what made the first attempt fault on
  // `read of guest 0x00000004`.
  PPCContext deliver = ctx;
  deliver.r3.u64 = panel;
  deliver.r4.u64 = array;
  deliver.r5.u64 = path_chars;
  __imp__sub_827537C0(deliver, base);
  LogOnce(path + "#delivered",
          "delivered '" + path + "' to the panel (array " + std::to_string(array) + ", panel " +
              std::to_string(panel) + ")");
  return true;
}

}  // namespace rb_blitz::store
