// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_fetch.h"

#include <atomic>
#include <mutex>
#include <set>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "store/store_catalogue.h"
#include "store/store_library.h"
#include "store/store_serve.h"
#include "store/store_probe.h"
#include "store/store_settings.h"

REXCVAR_DEFINE_STRING(store_serve_dir, "", "Diagnostics",
                      "Serve the retail store's netcache records from this host directory (a file "
                      "named exactly like the requested path, e.g. "
                      "dlc_store/europe/dlc_top_xbox_eng.dta); empty uses the built-in records")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(store_fetch_deliver, false, "Diagnostics",
                    "Deliver a served store record to the panel through the retail notification "
                    "(the file cache's own `file_req_complete` call); off, the record is only put "
                    "into the manager's loaded-file container and the retail notify never runs")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(store_offer_type_symbol, false, "Diagnostics",
                    "Serve each offer's `type` as a DTA symbol (`(type song)`) instead of a string. "
                    "The symbol form is what makes the retail provider answer the browser's row "
                    "labels - measured 2026-10-21, every title is read - but the list then faults on "
                    "`read of guest 0x00000000` while drawing, so it stays off until that null is named")
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
REX_EXTERN(__imp__sub_8274F610);  // the panel's offers parser: (out, panel, message)

// Set once by InstallStoreFetch() on the host thread, before the guest exists; read-only after, so
// the guest thread may read it without a lock.
bool g_on = false;
bool g_deliver = false;
std::string g_serve_dir;

// The client the store's request registered (`sub_8235A240`'s third argument) and the parsed array
// the insert produced. Both are read before the original runs, because that call clobbers the
// context; the delivery needs them afterwards.
std::atomic<uint32_t> g_client{0};
std::atomic<uint32_t> g_record{0};

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
// 32 bytes of guest scratch for the delivery calls' return slot and message struct.
uint32_t g_scratch = 0;

// A big-endian write into guest memory; an unmapped address is simply not written.
void WriteGuestU32(uint8_t* base, uint32_t addr, uint32_t value) {
  uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, addr);
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  p[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[3] = static_cast<uint8_t>(value & 0xFF);
}

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
  // The offer record's `type` node kind (store_metadata.h): read here because the writers are
  // SDK-free and a guest thread must not touch the cvar registry.
  SetOfferTypeSymbol(REXCVAR_GET(store_offer_type_symbol));
  g_on = true;
  REXLOG_INFO("store_fetch: serving the store's records from {} (delivery {})",
              g_serve_dir.empty() ? "the built-in records" : ("'" + g_serve_dir + "' first"),
              g_deliver ? "on" : "off");
  REXLOG_INFO("store_fetch: the offer `type` is served as a {}",
              REXCVAR_GET(store_offer_type_symbol) ? "symbol" : "string (labels stay blank)");
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
  // The catalogue is fetched on its own thread, so these two calls only wake it: the front menu is
  // the store opening (the row press that needs the offers comes seconds later), a menu page is a
  // row the player actually pressed. Both are no-ops while a fetch is in flight, while the held
  // page is fresh, and for the backoff after a failure.
  if (IsFrontMenuPath(path)) {
    RequestCatalogueRefresh("the store opened");
  } else if (IsMenuRecordPath(path)) {
    RequestCatalogueRefresh("a menu page was asked for");
  }
  // The Downloaded mark's index touches every package, so it is built while the player is still on
  // the front menu rather than when a page is asked for; it is a no-op while the published index
  // still describes the library. Until it lands a page carries no mark, which is "unknown", never
  // a negative claim (plan §3.2).
  if (IsFrontMenuPath(path) || IsMenuRecordPath(path)) {
    RequestLibraryRefresh("the store asked for a page");
  }
  const ServedRecord record = RecordForRequest(path, g_serve_dir);
  if (record.bytes.empty()) {
    LogOnce(path, "no record for '" + path + "': " + record.source);
    return false;
  }
  const uint32_t manager = GuestU32(base, kFileCacheGlobal);
  if (manager == 0) {
    LogOnce(path, "no record for '" + path + "': the file cache manager is not up yet");
    return false;
  }
  const uint32_t path_chars = GuestU32(base, path_string + 4);
  // The client this request belongs to: `sub_8235A240` registers it (its `r5`) and the delivery
  // step needs it after the call returns.
  g_client.store(ctx.r5.u32, std::memory_order_relaxed);
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
  const uint32_t buffer = EnsureBuffer(ctx, base, record.bytes.size() + 1);
  if (buffer == 0) {
    LogOnce(path, "no record for '" + path + "': the guest buffer allocation failed");
    return false;
  }
  uint8_t* dest = rex::memory::GuestPtr<uint8_t*>(base, buffer);
  if (dest == nullptr) {
    LogOnce(path, "no record for '" + path + "': the guest buffer is not mapped");
    return false;
  }
  for (size_t i = 0; i < record.bytes.size(); ++i) {
    dest[i] = static_cast<uint8_t>(record.bytes[i]);
  }
  dest[record.bytes.size()] = 0;
  PPCContext call = ctx;
  call.r3.u64 = manager;
  call.r4.u64 = path_chars;
  call.r5.u64 = buffer;
  call.r6.u64 = static_cast<uint32_t>(record.bytes.size());
  __imp__sub_824685E0(call, base);
  g_record.store(call.r3.u32 != 0 ? GuestU32(base, call.r3.u32 + 68) : 0,
                 std::memory_order_relaxed);
  // The manager's readiness word is left set so `sub_8235A240`'s body - which runs after this hook
  // returns - takes its "already loaded -> notify" branch and sends the `file_req_complete` message
  // itself. That branch is the *only* notification a request gets: calling `sub_8235A0F8` here as
  // well delivered every record twice, and the second delivery re-entered the panel's screen
  // transition and faulted (boot 115: `read of guest 0x75706461`, four characters of a string read
  // as a pointer; boot 112 had survived the same double delivery, so it is a race, not a shape).
  // The write is what `store_fetch_deliver` gates: off, the word stays as the boot found it and the
  // body takes its fetch branch, exactly as retail does offline.
  if (g_deliver) {
    const uint32_t readiness = GuestU32(base, manager + 44);
    if (readiness != 1 && WriteGuestU32Protected(base, manager + 44, 1)) {
      LogOnce(path + "#ready",
              "left the file cache ready so its own request body delivers (was " +
                  std::to_string(readiness) + ")");
    }
  }
  LogOnce(path, "served '" + path + "' from " + record.source + " (" +
                    std::to_string(record.bytes.size()) + " byte(s), insert -> " +
                    std::to_string(call.r3.u32) + ")");
  if (TraceEnabled()) {
    // Names the two objects the notify involves: the client the request registered (`ctx.r5`) and
    // the loaded file the insert returned. Their vtables say which interface the callback really
    // is, which is the one thing a crash inside the notify cannot be explained without. The 72-byte
    // file object is dumped whole because `+68` is only *believed* to be the parse result.
    const uint32_t client = ctx.r5.u32;
    const uint32_t client_vt = client != 0 ? GuestU32(base, client) : 0;
    const uint32_t file = call.r3.u32;
    const uint32_t file_vt = file != 0 ? GuestU32(base, file) : 0;
    REXLOG_INFO("store_fetch: '{}' client={:#x} vt={:#x} slot5={:#x}; file={:#x} vt={:#x}", path,
                client, client_vt, client_vt != 0 ? GuestU32(base, client_vt + 20) : 0u, file,
                file_vt);
    if (file != 0) {
      REXLOG_INFO("store_fetch: file words={}", GuestWords(base, file, 18));
      REXLOG_INFO("store_fetch: file path='{}' type={} data={:#x} size={} parsed={:#x}",
                  GuestCString(base, GuestU32(base, file + 44 + 4), 240),
                  GuestU32(base, file + 52), GuestU32(base, file + 56),
                  GuestU32(base, file + 60), GuestU32(base, file + 68));
    }
  }
  return true;
}

// The offers parser, called the way the panel's own dispatcher calls it (`sub_827501F0`, branch
// `loc_8275088C`): it builds `msg = {vt = 0x82102DC4, array}` - **the message array**, not the
// record - and calls `sub_8274F610(&out, client - 204, &msg)`, whose prologue reads the record out
// of node 2 of that array.
//
// The array is the engine's own, captured from the front menu's `file_req_complete` (see
// `RememberMessageArray`); only node 2 is swapped for the page's parsed record and put back
// afterwards. Building the array from scratch was wrong twice over: the header carries engine fields
// (`word1`, the refs word) that a writer would have to invent.
//
void DeliverStoreRecord(PPCContext& ctx, uint8_t* base, uint32_t cache, uint32_t path_string) {
  (void)ctx;
  if (!g_deliver || cache == 0 || path_string == 0) {
    return;
  }
  const std::string path = GuestCString(base, GuestU32(base, path_string + 4), 240);
  if (path.empty()) {
    return;
  }
  // Nothing to send: `sub_8235A240`'s own body delivered this record a moment ago (see the readiness
  // word in `ServeStoreRequest`), which is the behaviour boot 112's browser and boot 115's fault
  // between them proved - the delivery must happen exactly once, and the engine's own call is the one
  // that survives. This hook is the observation point only.
  if (TraceEnabled()) {
    const uint32_t panel = Panel();
    const uint32_t begin = panel != 0 ? GuestU32(base, panel + 52) : 0;
    REXLOG_INFO("store_fetch: the cache's own body delivered '{}' (record {:#x}); primary panel "
                "{:#x} menus[{:#x},{:#x},{:#x}]",
                path, g_record.load(std::memory_order_relaxed), panel, begin,
                panel != 0 ? GuestU32(base, panel + 56) : 0u,
                panel != 0 ? GuestU32(base, panel + 60) : 0u);
  }
  LogOnce(path + "#delivered", "delivered the record for '" + path + "'");
}

}  // namespace rb_blitz::store
