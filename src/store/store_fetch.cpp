// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_fetch.h"
#include "store/store_record_refresh.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "store/store_catalogue.h"
#include "store/store_catalogue_status.h"
#include "store/store_library.h"
#include "store/album_art.h"
#include "store/store_search.h"
#include "store/store_serve.h"
#include "store/store_probe.h"
#include "store/store_settings.h"

REXCVAR_DEFINE_STRING(store_serve_dir, "", "Diagnostics",
                      "Serve the retail store's netcache records from this host directory (a file "
                      "named exactly like the requested path, e.g. "
                      "dlc_store/europe/dlc_top_xbox_eng.dta); empty uses the built-in records")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(store_fetch_deliver, true, "Diagnostics",
                    "Deliver a served store record to the panel through the retail notification "
                    "(the file cache's own `file_req_complete` call). On by default because the store "
                    "has nothing to show otherwise: offline the cache's request body never notifies. "
                    "Only a record this module actually serves is affected, and serving itself is "
                    "gated on the RhythmVerse master switch, so a stock boot is untouched")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(store_offer_type_symbol, true, "Diagnostics",
                    "Serve each offer's `type` as a DTA symbol (`(type song)`) instead of a string. "
                    "The symbol form is what makes the retail provider answer the browser's row "
                    "labels (measured 2026-10-21); the null read that used to follow the labels was "
                    "the details panel's `(rank <part>)` path, now unrolled onto `rank_<part>` keys "
                    "by the payload (store_payload.h). Off is the pre-P3 blank-row behaviour.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(store_front_menu_row, "no-count", "Diagnostics",
                      "How a served front-menu row spells its count (store_metadata.h's "
                      "FrontMenuRowForm): `no-count` marks the row with retail's own "
                      "`random_song` fourth element, which is the only form whose count label is "
                      "never fed a number; `count` is the pre-P9-5 `(path label TRUE)` that drew "
                      "`1`, `false` and `zero` are the other spellings of the same count")
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
REX_EXTERN(__imp__sub_82474E08);  // lookup returning a retained file, not a boolean
REX_EXTERN(__imp__sub_82468120);  // release the lookup's file reference
REX_EXTERN(__imp__sub_82468138);  // parse the file's owned text buffer
REX_EXTERN(__imp__sub_82190510);  // release a DataArray (LoadedFile's destructor uses it)
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

// The guest buffer an art file is copied into: **one per served path per boot**, never one shared
// across records. The insert does not copy what it is handed - `sub_82468248` stores the pointer
// itself at `file + 56` (`stw r28,56(r30)`) and parses it in place - so the bytes have to outlive
// the request object, which the file cache keeps for the whole boot. Measured 2026-10-22: the
// store's second visit re-requested the first offer's cover, the engine refused the duplicate
// insert (`sub_824685E0` answers 0 for a path it already holds), and the completion then decoded
// the **file object from the first visit** - whose `+56` was the one shared buffer the rest of the
// boot had since overwritten with a menu page and other covers - and read guest 0x4
// (`rb_blitz_243.log`; the same decode call with the same arguments decodes cleanly on the first
// visit). Art files retain their raw bytes, so repeat art serves reuse that path's allocation.
// Text metadata uses fresh owned buffers instead: the native parser consumes and frees them.
std::mutex g_buffer_mutex;
std::map<std::string, std::pair<uint32_t, uint32_t>> g_buffers;  // path -> (address, capacity)
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

uint32_t RecordBuffer(PPCContext& ctx, uint8_t* base, const std::string& path, size_t size) {
  std::lock_guard<std::mutex> lock(g_buffer_mutex);
  const auto it = g_buffers.find(path);
  if (it != g_buffers.end() && it->second.second >= size) {
    return it->second.first;
  }
  const uint32_t allocated = Allocate(ctx, base, size);
  if (allocated == 0) {
    return 0;
  }
  g_buffers[path] = {allocated, static_cast<uint32_t>((size + 255) & ~size_t{255})};
  return allocated;
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
  // The front-menu row's count form, for the same reason (store_metadata.h's FrontMenuRowForm).
  FrontMenuRowForm form = FrontMenuRow();
  const std::string row_form = REXCVAR_GET(store_front_menu_row);
  if (ParseFrontMenuRowForm(row_form, &form)) {
    SetFrontMenuRowForm(form);
  } else {
    REXLOG_WARN("store_fetch: `store_front_menu_row` = '{}' is not one of count/false/zero/"
                "no-count; serving '{}'",
                row_form, FrontMenuRowFormToken(form));
  }
  g_on = true;
  REXLOG_INFO("store_fetch: serving the store's records from {} (delivery {})",
              g_serve_dir.empty() ? "the built-in records" : ("'" + g_serve_dir + "' first"),
              g_deliver ? "on" : "off");
  REXLOG_INFO("store_fetch: the offer `type` is served as a {}",
              REXCVAR_GET(store_offer_type_symbol) ? "symbol" : "string (labels stay blank)");
  REXLOG_INFO("store_fetch: a front-menu row's count is served as '{}'",
              FrontMenuRowFormToken(FrontMenuRow()));
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
  // page is fresh, and for the backoff after a failure. The same two moments are the store's own
  // *surface* notes (P7): its loading and empty/offline sentences are drawn only between a surface
  // being served and `kCatalogueSurfaceWindow` later.
  if (IsFrontMenuPath(path)) {
    NoteStoreSurface(StoreSurface::kFrontMenu);
    RequestCatalogueRefresh("the store opened");
  } else if (IsMenuRecordPath(path)) {
    NoteStoreSurface(StoreSurface::kMenuPage);
    RequestCatalogueRefresh("a menu page was asked for");
  }
  // The Downloaded mark's index touches every package, so it is built while the player is still on
  // the front menu rather than when a page is asked for; it is a no-op while the published index
  // still describes the library. Until it lands a page carries no mark, which is "unknown", never
  // a negative claim (plan §3.2).
  if (IsFrontMenuPath(path) || IsMenuRecordPath(path)) {
    RequestLibraryRefresh("the store asked for a page");
  }
  // Search is requested only after the loading modal observes a terminal answer.
  // Never stall the guest here: drawing and cancellation must keep running.
  const ServedRecord record = RecordForRequest(path, g_serve_dir);
  if (record.bytes.empty()) {
    LogOnce(path, "no record for '" + path + "': " + record.source);
    return false;
  }
  if (TraceEnabled() && IsMenuRecordPath(path)) {
    // The Downloaded mark is decided by `BuiltinRecord`'s predicate, so the served text is the only
    // place to see how it landed. One page of offers is small; a plain substring count keeps this
    // out of the writer (which is SDK-free and cannot log).
    const std::string_view text(reinterpret_cast<const char*>(record.bytes.data()),
                                record.bytes.size());
    std::size_t marked = 0;
  for (std::size_t at = text.find("(is_purchased 1)"); at != std::string_view::npos;
       at = text.find("(is_purchased 1)", at + 1)) {
      ++marked;
    }
    std::size_t offers = 0;
    for (std::size_t at = text.find("(name \""); at != std::string_view::npos;
         at = text.find("(name \"", at + 1)) {
      ++offers;
    }
    REXLOG_INFO("store_fetch: the served page carries {} offer(s), {} of them marked Downloaded",
                offers, marked);
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
  // The text parser owns and frees this buffer. Never reuse the address held
  // for an earlier parsed page; art files, in contrast, retain their raw bytes.
  const uint32_t buffer = Allocate(ctx, base, record.bytes.size() + 1);
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
  call.r3.u64 = manager + 76;
  call.r4.u64 = path_chars;
  __imp__sub_82474E08(call, base);
  const uint32_t cached_file = call.r3.u32;
  if (cached_file != 0) {
    const bool refreshed = RefreshCachedTextRecord(
        manager, cached_file, buffer, static_cast<uint32_t>(record.bytes.size()),
        [&](uint32_t address) { return GuestU32(base, address); },
        [&](uint32_t address, uint32_t value) { WriteGuestU32(base, address, value); },
        [&](uint32_t file) {
          PPCContext parse = ctx;
          parse.r3.u64 = file;
          __imp__sub_82468138(parse, base);
        },
        [&](uint32_t array) {
          PPCContext release = ctx;
          release.r3.u64 = array;
          __imp__sub_82190510(release, base);
        });
    PPCContext release = ctx;
    release.r3.u64 = cached_file;
    __imp__sub_82468120(release, base);
    if (!refreshed) {
      REXLOG_WARN("store_fetch: could not refresh cached record '{}'", path);
      return false;
    }
    call.r3.u64 = cached_file;
    REXLOG_INFO("store_fetch: refreshed cached record '{}' ({} byte(s))", path, record.bytes.size());
  } else {
    call = ctx;
    call.r3.u64 = manager;
    call.r4.u64 = path_chars;
    call.r5.u64 = buffer;
    call.r6.u64 = static_cast<uint32_t>(record.bytes.size());
    __imp__sub_824685E0(call, base);
  }
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
  // BISECT: dump the exact text served, so a crash in the guest's parser can be read offline.
  if (const char* dump_dir = std::getenv("RBBZ_DUMP_SERVED")) {
    std::error_code ec;
    std::filesystem::create_directories(dump_dir, ec);
    std::string name = path;
    for (char& c : name) {
      if (c == '/') {
        c = '_';
      }
    }
    std::ofstream dump(std::filesystem::path(dump_dir) / (name + ".txt"), std::ios::binary);
    dump.write(reinterpret_cast<const char*>(record.bytes.data()),
               static_cast<std::streamsize>(record.bytes.size()));
  }
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

// The album-art request (`sub_82469C28`): the path arrives composed (`BandStorePanel::load_art`
// prepends the panel's request prefix to the offer's `preview_art`) and as a **C string**, unlike
// the metadata request's `String` object - measured 2026-10-21 at all four call sites of the entry
// (`sub_824456A8` passes `*(String + 4)`, and the others load the chars word the same way). Reading
// it as a `String` dereferences the path's own characters (`dlc_store/` + 4 = `0x73746F72`, "stor")
// and faults, so this must stay a plain `GuestCString(path_string)`.
//
// The rule itself is positional from the end (`.../offers/<key>/cover_art.png_xbox`), so nothing
// depends on the prefix. Unlike the metadata request there is no readiness word and no delivery step
// of ours: this entry performs the loaded-file lookup itself, so an insert is all a served texture
// needs. A miss asks the art worker for the key and answers false, which leaves the retail loader
// exactly as it is offline - the panel keeps the placeholder art it already set.
bool ServeArtRequest(PPCContext& ctx, uint8_t* base, uint32_t path_string) {
  if (!g_on || path_string == 0) {
    return false;
  }
  const std::string path = GuestCString(base, path_string, 240);
  if (!IsArtRequestPath(path)) {
    // A texture request that is not in our shape is worth one line while tracing: the panel's
    // prefix is the one part of the composed path we do not control, and a wrong guess here would
    // otherwise look exactly like "the art never loads".
    if (TraceEnabled() && path.find(".png_xbox") != std::string::npos) {
      LogOnce(path, "an art request this build does not answer '" + path + "'");
    }
    return false;
  }
  if (!Settings()->enabled) {
    LogOnce(path, "not serving art for '" + path + "': RhythmVerse is off");
    return false;
  }
  const std::string key = ArtKeyFromPath(path);
  const std::string bytes = ArtTextureForKey(key);
  if (bytes.empty()) {
    // The panel asks once per selection and does not retry a path it has already set, so the fetch
    // is started here as a last resort; the catalogue and the page writer normally got there first.
    LogOnce(path, "no texture for '" + path + "' yet; asked the art worker");
    RequestArtTexture(key);
    return false;
  }
  const uint32_t manager = ctx.r3.u32;  // the netcache manager object the request was made on
  if (manager == 0) {
    LogOnce(path, "no texture for '" + path + "': the netcache manager is not up yet");
    return false;
  }
  const uint32_t path_chars = path_string;
  const uint32_t buffer = RecordBuffer(ctx, base, path, bytes.size() + 1);
  if (buffer == 0) {
    LogOnce(path, "no texture for '" + path + "': the guest buffer allocation failed");
    return false;
  }
  uint8_t* dest = rex::memory::GuestPtr<uint8_t*>(base, buffer);
  if (dest == nullptr) {
    LogOnce(path, "no texture for '" + path + "': the guest buffer is not mapped");
    return false;
  }
  for (size_t i = 0; i < bytes.size(); ++i) {
    dest[i] = static_cast<uint8_t>(bytes[i]);
  }
  dest[bytes.size()] = 0;
  PPCContext call = ctx;
  call.r3.u64 = manager;
  call.r4.u64 = path_chars;
  call.r5.u64 = buffer;
  call.r6.u64 = static_cast<uint32_t>(bytes.size());
  __imp__sub_824685E0(call, base);
  LogOnce(path, "served art '" + path + "' (" + std::to_string(bytes.size()) +
                    " byte(s), insert -> " + std::to_string(call.r3.u32) + ")");
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
