// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_probe.h"

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "fs/payload_overlay.h"
#include "store/store_fetch.h"

REXCVAR_DEFINE_BOOL(store_trace, false, "Diagnostics",
                    "Log the store paths the guest resolves and opens under the game root "
                    "(RhythmVerse plan P2's data-layer probe)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_STRING(store_local_server_root, "", "Diagnostics",
                      "Point the retail store's netcache at a local server rooted at this "
                      "guest-relative directory (RhythmVerse plan P2's local-server spike; empty "
                      "leaves config/gen/store.dtb as shipped)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::store {
namespace {

bool IsStorePath(std::string_view path) {
  // `config/gen/store.dtb` is the netcache definition itself (`netcache_init`): whether the guest
  // opens it, and through which device, is the first question the P2 gate has to answer - a record
  // nobody reads cannot be pointed at a local server. The UI's own `ui/store/gen/store.dtb` carries
  // the same name, and the suffix test catches both without naming every directory.
  if (path.size() >= 9 && path.substr(path.size() - 9) == "store.dtb") {
    return true;
  }
  if (path.find("ui/store") != std::string_view::npos ||
      path.find("dlc_store") != std::string_view::npos ||
      path.find("dlc_top") != std::string_view::npos ||
      path.find("remote_metadata") != std::string_view::npos) {
    return true;
  }
  // The plan's two offer fragments are ordinary words, so they only count inside a store path:
  // `<root>/offers/<id>/` and `<root>/related/<id>.dta` are the templates, and neither word means
  // anything on its own.
  if (path.find("store") != std::string_view::npos &&
      (path.find("/offers") != std::string_view::npos ||
       path.find("/related") != std::string_view::npos)) {
    return true;
  }
  return false;
}

// One line per distinct (kind, path) per boot: a store screen resolves the same directory
// repeatedly, and the point of the probe is the *list* of names, not a count.
constexpr size_t kMaxDistinctPaths = 512;

// Set once on the host thread by InstallPathProbe(); the hooks run on guest threads and must not
// read the cvar registry (project rule).
std::atomic<bool> g_trace_on{false};

// A big-endian u32 out of guest memory, or 0 when the address is not mapped.
uint32_t GuestU32(uint8_t* base, const uint32_t addr) {
  const uint8_t* p = rex::memory::GuestPtr<const uint8_t*>(base, addr);
  if (p == nullptr) {
    return 0;
  }
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// A bounded, printable guest string. Anything outside the path alphabet stops the read: a wrong
// pointer is far more likely than a path with control characters, and this runs on every request.
std::string GuestCString(uint8_t* base, const uint32_t addr, const size_t max_length) {
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

void Observe(std::string_view relative_path, bool open, uint32_t desired_access, int32_t status) {
  if (!IsStorePath(relative_path)) {
    return;
  }
  static std::mutex mutex;
  static std::set<std::string> seen;
  const std::string key = std::string(open ? "open " : "resolve ") + std::string(relative_path);
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (seen.size() >= kMaxDistinctPaths) {
      return;
    }
    if (!seen.insert(key).second) {
      return;
    }
  }
  if (open) {
    REXLOG_INFO("store-probe: open {} (access {:#x}) -> {:#x}", relative_path, desired_access,
                static_cast<uint32_t>(status));
  } else {
    REXLOG_INFO("store-probe: resolve {}", relative_path);
  }
}

bool TraceEnabled() { return g_trace_on.load(std::memory_order_relaxed); }

// Big-endian write into guest memory; a bad address is simply not written.
void WriteGuestU32(uint8_t* base, const uint32_t addr, const uint32_t value) {
  uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, addr);
  if (p == nullptr) {
    return;
  }
  p[0] = uint8_t((value >> 24) & 0xFF);
  p[1] = uint8_t((value >> 16) & 0xFF);
  p[2] = uint8_t((value >> 8) & 0xFF);
  p[3] = uint8_t(value & 0xFF);
}

// The image's data section is mapped read-only (`XexModule::Load` protects code and read-only data
// with kMemoryProtectRead), so a plain store to a global there does nothing. This is the same
// unprotect-write-restore the ultimate patches use (src/hooks/ultimate.cpp).
bool WriteGuestU32Protected(uint8_t* base, const uint32_t addr, const uint32_t value) {
  auto* memory = REX_KERNEL_MEMORY();
  auto* heap = memory != nullptr ? memory->LookupHeap(addr) : nullptr;
  if (heap == nullptr) {
    return false;
  }
  uint32_t old_protect = 0;
  if (!heap->Protect(addr, sizeof(uint32_t),
                     rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite,
                     &old_protect)) {
    return false;
  }
  WriteGuestU32(base, addr, value);
  heap->Protect(addr, sizeof(uint32_t),
                old_protect != 0 ? old_protect : rex::memory::kMemoryProtectRead);
  return true;
}

// Reports the readiness word and the two vector words once per boot. `sub_82753010` returns early
// when `*(this+52) != *(this+56)` (its own "already loaded" test) *before* it reads the state word,
// so the three values together say which of the two early exits the boot took.
void LogGate(uint32_t vector_begin, uint32_t vector_end, uint32_t before, bool forced) {
  static std::once_flag once;
  std::call_once(once, [vector_begin, vector_end, before, forced] {
    REXLOG_INFO("store-probe: index request reached; this+52={:#x} this+56={:#x} state={} "
                "(forced to 2: {})",
                vector_begin, vector_end, before, forced ? "yes" : "no");
  });
}

// One line per distinct requested path per boot, like the observer above: the panel re-requests
// the same menu on every visit, and the list is the evidence.
void LogRequest(const std::string& path, const std::string& input, uint32_t flag) {
  static std::mutex mutex;
  static std::set<std::string> seen;
  const std::string key = path.empty() ? std::string("<unreadable>") : path;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (seen.size() >= kMaxDistinctPaths || !seen.insert(key).second) {
      return;
    }
  }
  REXLOG_INFO("store-probe: request '{}' (input '{}', flag {})", key, input, flag);
}

// The file-cache entry: the manager and the exact path it was handed. Distinct from the request
// line because a request can be de-duplicated or refused before the cache ever sees it.
void LogCache(uint32_t manager, const std::string& path) {
  static std::mutex mutex;
  static std::set<std::string> seen;
  const std::string key = path.empty() ? std::string("<unreadable>") : path;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (seen.size() >= kMaxDistinctPaths || !seen.insert(key).second) {
      return;
    }
  }
  REXLOG_INFO("store-probe: cache request '{}' (manager {:#x})", key, manager);
}

}  // namespace

void InstallPathProbe() {
  const bool on = REXCVAR_GET(store_trace);
  g_trace_on.store(on, std::memory_order_relaxed);
  fs::SetStorePathObserver(on ? &Observe : nullptr);
  REXLOG_INFO("store-probe: path trace {}; the local server root is '{}'", on ? "on" : "off",
              LocalServerRoot());
}

std::string LocalServerRoot() { return REXCVAR_GET(store_local_server_root); }

// The same switch, for the fetch hook, which must not read the cvar registry from a guest thread.
bool TraceEnabled() { return g_trace_on.load(std::memory_order_relaxed); }

// A short hex listing of the words at a guest address, for logs that have to show a layout the
// code has not been proven to use yet. Unmapped words read as 0.
std::string GuestWords(uint8_t* base, uint32_t addr, int count) {
  std::string out;
  char word[16];
  for (int i = 0; i < count; ++i) {
    std::snprintf(word, sizeof(word), "%s%08X", i == 0 ? "" : " ",
                  GuestU32(base, addr + static_cast<uint32_t>(i) * 4));
    out += word;
  }
  return out;
}

}  // namespace rb_blitz::store

// ---------------------------------------------------------------------------------------------
// P3 step 0: the request entry.
//
// 0x8274FB60 is what `{store_panel request <menu> TRUE}` reaches. It formats the metadata path
// from the provider string (`sub_8274E450` with the record's `"/%s%s"`, optionally `"?pid=%s"`),
// stores it at `this + 148`, and hands it to the file cache (`sub_8235A240(manager, &path)`).
// The dispatch that calls it is `sub_827501F0` - object `Handle(Symbol, DataArray*)`, where the
// `request` branch reads the provider string out of argument 0 and the bool out of argument 1
// (measured 2026-10-20, `out/_swap/blitz_image.bin` + the recompiled sources). The index-request
// path (`sub_82753010` -> `sub_8274E710`) also calls it, with its own generated string.
//
// The hook is a pass-through: the request still runs exactly as retail wrote it, and the probe
// only reports the path once per distinct value while `store_trace` is on. It exists because the
// file-system side cannot answer this question: the fetch performs no I/O offline (plan §5/P2
// record), so the *asked-for* name has to be read out of the panel's own state.
REX_EXTERN(__imp__sub_8274FB60);
REX_HOOK_RAW(sub_8274FB60) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t input_string = ctx.r4.u32;
  const uint32_t flag = ctx.r5.u32;
  // The argument is a `String` by value: its character buffer is at +4 (`sub_82459130` writes the
  // vtable at +0, and `sub_8235A240` reads +4 as the path). The first P3 hook read r4 as a menu
  // symbol and reported the path as unreadable; this is the same object the DTA's `request`
  // branch passes (`{sprintf menu_%s ...}` is the source id, this is the provider string).
  const std::string input = rb_blitz::store::TraceEnabled() && input_string != 0
                                ? rb_blitz::store::GuestCString(
                                      base, rb_blitz::store::GuestU32(base, input_string + 4), 240)
                                : std::string();
  __imp__sub_8274FB60(ctx, base);
  if (!rb_blitz::store::TraceEnabled()) {
    return;
  }
  // `this + 148` is the request path the function just assigned - a `String` stored inline, so its
  // character buffer is the second word.
  const std::string path = rb_blitz::store::GuestCString(
      base, rb_blitz::store::GuestU32(base, object + 148 + 4), 240);
  rb_blitz::store::LogRequest(path, input, flag);
}

// The file cache the builder hands the path to. Its first argument is the manager, the second the
// path `String`; logging here separates "the panel asked" from "the cache looked" - the question
// P3's data lane starts from. The hook also *serves* the store's own records: a request whose path
// the mod has bytes for is put into the manager's loaded-file container before the original runs,
// which is what makes the retail request take its "already loaded" branch and notify the panel
// (src/store/store_fetch.h has the measured chain).
REX_EXTERN(__imp__sub_8235A240);
REX_HOOK_RAW(sub_8235A240) {
  const uint32_t manager = ctx.r3.u32;
  const uint32_t path_string = ctx.r4.u32;
  if (rb_blitz::store::TraceEnabled() && path_string != 0) {
    const std::string path = rb_blitz::store::GuestCString(
        base, rb_blitz::store::GuestU32(base, path_string + 4), 240);
    rb_blitz::store::LogCache(manager, path);
  }
  rb_blitz::store::ServeStoreRequest(ctx, base, path_string);
  __imp__sub_8235A240(ctx, base);
  if (path_string != 0 && rb_blitz::store::TraceEnabled()) {
    const std::string path = rb_blitz::store::GuestCString(
        base, rb_blitz::store::GuestU32(base, path_string + 4), 240);
    REXLOG_INFO("store-probe: request '{}' returned to the panel", path);
  }
}

// ---------------------------------------------------------------------------------------------
// P3's data lane: what the file cache hands the panel when a record is ready.
//
// `sub_827501F0` is the panel's message dispatcher (`Handle`), and the second vtable the class
// carries (0x82102ac4) has it in slot 5 - the callback the file cache invokes from
// `sub_8235A0F8`. The served-record path crashes inside that callback, and the fault address is a
// piece of the record text, so the message layout is what has to be measured rather than inferred:
// this hook logs the four arguments and the first words of the value in r5, once per distinct
// (r4, r5) pair, and then runs the original unchanged.
REX_EXTERN(__imp__sub_827501F0);
REX_HOOK_RAW(sub_827501F0) {
  static std::atomic<int> logged{0};
  if (rb_blitz::store::TraceEnabled() && ctx.r5.u32 != 0 &&
      logged.load(std::memory_order_relaxed) < 60) {
    logged.fetch_add(1, std::memory_order_relaxed);
    const uint32_t message = ctx.r5.u32;
    const std::string words = rb_blitz::store::GuestWords(base, message, 6);
    REXLOG_INFO("store-probe: panel dispatch out={:#x} this={:#x} message={:#x}[{}] arg6={:#x}",
                ctx.r3.u32, ctx.r4.u32, message, words, ctx.r6.u32);
  }
  __imp__sub_827501F0(ctx, base);
}

// ---------------------------------------------------------------------------------------------
// P3's data lane: the front-menu builder chain, entered once per served record.
//
// `sub_827537C0(panel, DataArray, source)` allocates the 72-byte menu object (`sub_8275D5B8`),
// stores it on the panel and reads its entry count; `sub_8275D5B8` copies the source string and
// parses the array (`sub_8275D4E0`). The call faults on `read of guest 0x00000004`, so each of the
// three is logged at entry and exit: the last entry without its exit names the function the fault
// is in, which no stack trace does on this emulator.
REX_EXTERN(__imp__sub_827537C0);
REX_HOOK_RAW(sub_827537C0) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) REXLOG_INFO("store-probe: builder enter this={:#x} array={:#x} source={:#x}",
                         ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  __imp__sub_827537C0(ctx, base);
  if (trace) REXLOG_INFO("store-probe: builder exit");
}

REX_EXTERN(__imp__sub_8275D5B8);
REX_HOOK_RAW(sub_8275D5B8) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) REXLOG_INFO("store-probe: menu ctor enter obj={:#x} array={:#x} source={:#x}",
                         ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  __imp__sub_8275D5B8(ctx, base);
  if (trace) REXLOG_INFO("store-probe: menu ctor exit");
}

REX_EXTERN(__imp__sub_8275D4E0);
REX_HOOK_RAW(sub_8275D4E0) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) {
    REXLOG_INFO("store-probe: menu parse enter obj={:#x} array={:#x}", ctx.r3.u32, ctx.r4.u32);
  }
  __imp__sub_8275D4E0(ctx, base);
  if (trace) REXLOG_INFO("store-probe: menu parse exit");
}

// ---------------------------------------------------------------------------------------------
// P3 step 0: the readiness gate.
//
// `request_index_if_needed` is handled by 0x82753B20, whose branch calls 0x82753010, which returns
// immediately unless `*(0x828A3D20 + 44) == 2` (measured 2026-10-20 in the recompiled sources).
// That field is the panel's own state word, not the netcache definition: it stays below 2 on an
// offline boot even when the manager has been handed a local definition, which is why neither the
// file cache nor the request builder is ever reached.
//
// The hook writes 2 before the original runs - a forced state, which is exactly what a diagnostic
// is for - and reports the decision once. With `store_trace` off it is a pure pass-through.
REX_EXTERN(__imp__sub_82753010);
REX_HOOK_RAW(sub_82753010) {
  if (rb_blitz::store::TraceEnabled()) {
    // `lis r11,-32123` is 0x8285, not 0x828A: 65536 - 32123 = 33413 = 0x8285, and `addi r11,r11,
    // 15648` (0x3D20) makes the base 0x82853D20. The first P3 run forced 0x828A3D4C, which is not
    // this function's word - corrected 2026-10-20 after the generated body
    // (generated/default/rb_blitz_recomp.64.cpp, sub_82753010) was decoded again.
    constexpr uint32_t kStateField = 0x82853D20u + 44u;
    const uint32_t before = rb_blitz::store::GuestU32(base, kStateField);
    const uint32_t vector_begin =
        rb_blitz::store::GuestU32(base, ctx.r3.u32 + 52);
    const uint32_t vector_end = rb_blitz::store::GuestU32(base, ctx.r3.u32 + 56);
    const bool forced = rb_blitz::store::WriteGuestU32Protected(base, kStateField, 2);
    rb_blitz::store::LogGate(vector_begin, vector_end, before, forced);
  }
  __imp__sub_82753010(ctx, base);
}
