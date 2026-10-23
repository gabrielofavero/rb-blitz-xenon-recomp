// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_probe.h"

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <windows.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "fs/payload_overlay.h"
#include "store/store_fetch.h"
#include "store/store_settings.h"

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

// The store panel `sub_8274FB60` was last called on. Written by that hook, read by the delivery
// step in store_fetch.cpp, both on guest threads.
std::atomic<uint32_t> g_panel{0};

// `rex::memory::GuestPtr` does no validation - it is base + address - so a null pointer, a small
// integer or a host pointer handed to a log line faults the guest instead of reporting it. Every
// read below is gated on the address landing in a *committed* region of a guest heap.
//
// Measured 2026-10-21: `LookupHeap` alone is not enough. The system heap is registered as a
// virtual range starting at 0, so `LookupHeap(5)` answers and the read then faults; `QuerySize`
// is what actually says whether pages back the address.
bool GuestMapped(const uint32_t addr, const uint32_t length = 4) {
  if (addr < 0x10000) {
    return false;
  }
  auto* memory = REX_KERNEL_MEMORY();
  auto* heap = memory != nullptr ? memory->LookupHeap(addr) : nullptr;
  if (heap == nullptr) {
    return false;
  }
  uint32_t size = 0;
  return heap->QuerySize(addr, &size) && size >= length;
}

// How many bytes from `addr` stay inside its committed region; 0 when unbacked.
uint32_t GuestSpan(const uint32_t addr) {
  if (addr < 0x10000) {
    return 0;
  }
  auto* memory = REX_KERNEL_MEMORY();
  auto* heap = memory != nullptr ? memory->LookupHeap(addr) : nullptr;
  if (heap == nullptr) {
    return 0;
  }
  uint32_t base = addr;
  uint32_t size = 0;
  if (!heap->QuerySize(addr, &size) || size == 0) {
    return 0;
  }
  return size > addr - base ? size - (addr - base) : size;
}

// A big-endian u32 out of guest memory, or 0 when the address is not mapped.
uint32_t GuestU32(uint8_t* base, const uint32_t addr) {
  if (!GuestMapped(addr, 4)) {
    return 0;
  }
  const uint8_t* p = rex::memory::GuestPtr<const uint8_t*>(base, addr);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// A bounded, printable guest string. Anything outside the path alphabet stops the read: a wrong
// pointer is far more likely than a path with control characters, and this runs on every request.
std::string GuestCString(uint8_t* base, const uint32_t addr, const size_t max_length) {
  const uint32_t span = GuestSpan(addr);
  if (span == 0) {
    return {};
  }
  const char* text = rex::memory::GuestPtr<const char*>(base, addr);
  std::string out;
  for (size_t i = 0; i < max_length && i < span; ++i) {
    // The heap is mapped in chunks, so a negative-looking byte is the only warning available for a
    // pointer that walked out of its object; stop there rather than reading further.
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

// The first-chance log of a guest fault (installed by InstallPathProbe while `store_trace` is on).
// It never handles the exception - the SDK's own handler still decides what happens, this only
// records where the guest was. The host addresses are resolved against `rb_blitz.map` (see
// out/_swap/map_lookup.py), which is how a bare `read of guest 0x00000000` becomes a function name.
//
// Measured 2026-10-21: this is what the store's two open P3 faults needed. The SDK's line names the
// address and the thread and nothing else, and both faults are null reads inside UI code that runs
// several guest frames below the last hook that can log.
LONG CALLBACK LogFault(EXCEPTION_POINTERS* info) {
  static std::atomic<int> seen{0};
  static std::atomic<int> entries{0};
  if (!g_trace_on.load(std::memory_order_relaxed) || info == nullptr ||
      info->ExceptionRecord == nullptr ||
      info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (entries.fetch_add(1, std::memory_order_relaxed) < 3) {
    REXLOG_INFO("store-probe: vector handler entered, access violation of {:#x}",
                static_cast<unsigned long long>(
                    static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1])));
  }
  const EXCEPTION_RECORD* record = info->ExceptionRecord;
  // The reported address is a *host* address: the guest's zero page is `virtual_membase + 0`, which
  // is why the first version of this handler saw 0x100000000 and filtered the fault out. The SDK's
  // own line translates it (`HostToGuestVirtual`), and so does this.
  auto* memory = REX_KERNEL_MEMORY();
  uintptr_t address = static_cast<uintptr_t>(record->ExceptionInformation[1]);
  if (memory != nullptr && address >= reinterpret_cast<uintptr_t>(memory->virtual_membase()) &&
      address < reinterpret_cast<uintptr_t>(memory->physical_membase())) {
    address = memory->HostToGuestVirtual(reinterpret_cast<const void*>(address));
  }
  // Only guests: a new thread's stack probe writes its guard page, and that is a host write, not a
  // guest fault. A guest object read past its own null pointer lands near zero (the store's faults
  // are `0x0`, `0x5` and `0x44`), so the near-null range is the whole of what this looks for.
  const bool guest = address < 0x100000u;
  if (!guest || seen.fetch_add(1, std::memory_order_relaxed) >= 64) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const bool write = record->ExceptionInformation[0] != 0;
  std::string frames;
  void* stack[24] = {};
  const USHORT count = RtlCaptureStackBackTrace(1, 24, stack, nullptr);
  for (USHORT i = 0; i < count; ++i) {
    char text[24];
    std::snprintf(text, sizeof(text), " %#llx", static_cast<unsigned long long>(
                                                  reinterpret_cast<uintptr_t>(stack[i])));
    frames += text;
  }
  REXLOG_ERROR("store-probe: fault {} of {:#010x} at rip {:#x}, frames:{}", write ? "write" : "read",
               static_cast<unsigned>(address),
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(record->ExceptionAddress)),
               frames);
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallPathProbe() {
  const bool on = REXCVAR_GET(store_trace);
  g_trace_on.store(on, std::memory_order_relaxed);
  fs::SetStorePathObserver(on ? &Observe : nullptr);
  if (on) {
    // A guest null read ends the boot with one line that names the address and nothing else. The
    // handler turns it into a host backtrace, which `out/_swap/map_lookup.py` resolves against
    // `rb_blitz.map` to the recompiled `sub_XXXXXXXX` that faulted - the store's remaining P3
    // faults both surfaced as `read of guest 0x00000000` (docs/reference/known-issues.md).
    const PVOID handle = AddVectoredExceptionHandler(1, &LogFault);
    auto* memory = REX_KERNEL_MEMORY();
    REXLOG_INFO("store-probe: fault logger {}; module base {:#x}; guest window [{:#x}, {:#x})",
                handle != nullptr ? "installed" : "NOT installed",
                reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                memory != nullptr ? reinterpret_cast<uintptr_t>(memory->virtual_membase()) : 0,
                memory != nullptr ? reinterpret_cast<uintptr_t>(memory->physical_membase()) : 0);
  }
  REXLOG_INFO("store-probe: path trace {}; the local server root is '{}'", on ? "on" : "off",
              LocalServerRoot());
}

std::string LocalServerRoot() { return REXCVAR_GET(store_local_server_root); }

// The same switch, for the fetch hook, which must not read the cvar registry from a guest thread.
bool TraceEnabled() { return g_trace_on.load(std::memory_order_relaxed); }

void RememberPanel(uint32_t panel) {
  if (panel != 0) {
    g_panel.store(panel, std::memory_order_relaxed);
  }
}

uint32_t Panel() { return g_panel.load(std::memory_order_relaxed); }

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
  // The panel itself: the front-menu builder is a method of this same object (`this + 52` is its
  // menu list, `this + 148` the request path this function writes). Recorded unconditionally
  // because the delivery step needs it whatever the trace switch says.
  rb_blitz::store::RememberPanel(object);
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
  const bool served = rb_blitz::store::ServeStoreRequest(ctx, base, path_string);
  __imp__sub_8235A240(ctx, base);
  if (served) {
    // The request has registered its client by now, which is the state the retail notify needs:
    // `sub_8235A240`'s loaded branch appends the client and immediately calls `sub_8235A0F8`.
    rb_blitz::store::DeliverStoreRecord(ctx, base, manager, path_string);
  }
  if (path_string != 0 && rb_blitz::store::TraceEnabled()) {
    const std::string path = rb_blitz::store::GuestCString(
        base, rb_blitz::store::GuestU32(base, path_string + 4), 240);
    REXLOG_INFO("store-probe: request '{}' returned to the panel", path);
  }
}

// ---------------------------------------------------------------------------------------------
// P3's album art: the request entry `BandStorePanel::load_art` reaches.
//
// `sub_8274E760` (the panel's `load_art` handler) builds `request prefix + preview_art` and calls
// `sub_824456A8(panel, path, callback)`, which hands the path to the netcache manager through
// `sub_82469C28(manager, path, client, 1)`. The path is a **C string** there, not a `String` object
// (measured 2026-10-21: `sub_824456A8` passes the chars word, and reading it as a `String` faults on
// the path's own characters). That entry looks the path up in the manager's loaded-file container
// itself (`sub_82474E08(manager + 76)`, the same container `sub_824685E0` inserts into) and, when it
// is there, wraps it and calls the client - so a texture is served with one insert and no readiness
// word, unlike the metadata request (store_fetch.h's ServeArtRequest).
REX_EXTERN(__imp__sub_82469C28);
REX_HOOK_RAW(sub_82469C28) {
  const uint32_t path_string = ctx.r4.u32;
  if (rb_blitz::store::TraceEnabled() && path_string != 0) {
    const std::string path = rb_blitz::store::GuestCString(base, path_string, 240);
    if (path.find("dlc_store") != std::string::npos ||
        path.find(".png_xbox") != std::string::npos) {
      REXLOG_INFO("store-probe: art request manager={:#x} client={:#x} path='{}'", ctx.r3.u32,
                  ctx.r5.u32, path);
    }
  }
  const bool served = rb_blitz::store::ServeArtRequest(ctx, base, path_string);
  __imp__sub_82469C28(ctx, base);
  if (served && path_string != 0 && rb_blitz::store::TraceEnabled()) {
    const std::string path = rb_blitz::store::GuestCString(base, path_string, 240);
    REXLOG_INFO("store-probe: art '{}' returned to the panel", path);
  }
}

// The art completion (`sub_82445CD8`): the client callback `sub_82469C28` fires once the file is in
// the container. It decodes the bytes (`RndBitmap::Load`), copies the bitmap into the panel's
// texture (`panel + 100`) and only then messages `art_loaded` to the DTA object the request wrapped
// (`panel + 104`), but *only* while that wrapper's state word (`+28`) is 1. Every one of those steps
// can be a silent no-op, so a trace boot reports the state before the call, the texture's fields
// after it, and whether the material's texture was ever the one we served. The wrapper the request
// built (`ctx.r5`) is logged too: its second word is the file object whose bytes the decode reads,
// which is what named the 2026-10-22 second-visit fault.
REX_EXTERN(__imp__sub_82445CD8);
REX_HOOK_RAW(sub_82445CD8) {
  const uint32_t panel = ctx.r4.u32;
  const uint32_t wrapper = ctx.r5.u32;
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace && panel != 0) {
    const uint32_t callback = rb_blitz::store::GuestU32(base, panel + 104);
    const uint32_t texture = rb_blitz::store::GuestU32(base, panel + 100);
    REXLOG_INFO("store-probe: art completion enter panel={:#x} texture={:#x} callback={:#x} "
                "callback[28]={} wrapper={:#x}[{}]",
                panel, texture, callback,
                callback != 0 ? rb_blitz::store::GuestU32(base, callback + 28) : 0u, wrapper,
                wrapper != 0 ? rb_blitz::store::GuestWords(base, wrapper, 3) : std::string());
  }
  __imp__sub_82445CD8(ctx, base);
  if (trace && panel != 0) {
    // `RndTex`'s view (the object `panel + 100` points at): +84 the "has a bitmap" word, +88/+92 the
    // dimensions, +96 the bpp code and +108 the number of extra levels `sub_821FC858` counted -
    // exactly what `sub_8220CB98` writes when the decode succeeded. Nothing here says the pixels are
    // right: a wrong cover is measured by dumping the served file (`store_art_dump_dir`) and decoding
    // it offline.
    const uint32_t texture = rb_blitz::store::GuestU32(base, panel + 100);
    REXLOG_INFO("store-probe: art completion exit panel={:#x} texture={:#x} "
                "has_bitmap={} width={} height={} bpp={} levels={}",
                panel, texture,
                texture != 0 ? rb_blitz::store::GuestU32(base, texture + 84) : 0u,
                texture != 0 ? rb_blitz::store::GuestU32(base, texture + 88) : 0u,
                texture != 0 ? rb_blitz::store::GuestU32(base, texture + 92) : 0u,
                texture != 0 ? rb_blitz::store::GuestU32(base, texture + 96) : 0u,
                texture != 0 ? rb_blitz::store::GuestU32(base, texture + 108) : 0u);
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
// `sub_827537C0(panel, const DataArray* record, const char* source)` allocates the 72-byte menu
// object (`sub_8275D5B8`), pushes it into the panel's menu list (`sub_821F73B8` on `panel + 52`, a
// `std::vector<FrontMenu*>` whose begin/end/cap are `+52/+56/+60`) and finishes with the panel's
// own update (`sub_82753288`). The second argument is the insert's **parsed array**
// (`file + 68`): `sub_8275D5B8` stores it at `menu + 60` and `sub_8275D4E0` looks `submenus` up in
// it (`sub_823FAE00`), which a raw text pointer cannot answer - handing it text faults inside the
// parse (`read of 0x28737566`, `"(suf"` of `(submenus` read as a pointer).
//
// The four calls are logged at entry and exit: the last entry without its exit names the function
// a fault is in, which no stack trace does on this emulator.
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

// The retail `file_req_complete` helper, the caller the delivery has to satisfy. `sub_827538B8(out,
// panel, message)` walks the message's record array and bails at four named checks before it calls
// the builder: the node at +24 must convert, +16 must be an array, that array must carry
// `submenus`, and the panel's menu list must be empty. Logging the nodes at entry is what turns
// "the panel drew its own rows" into "which of the four refused our record".
REX_EXTERN(__imp__sub_827538B8);
REX_HOOK_RAW(sub_827538B8) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) {
    const uint32_t message = ctx.r5.u32;
    const uint32_t array = message != 0 ? rb_blitz::store::GuestU32(base, message + 4) : 0;
    REXLOG_INFO("store-probe: file-complete enter out={:#x} panel={:#x} message={:#x}[{}] "
                "array={:#x}",
                ctx.r3.u32, ctx.r4.u32, message,
                message != 0 ? rb_blitz::store::GuestWords(base, message, 4) : std::string(),
                array);
    if (array != 0) {
      // The array is `{elements, ?, count, refs}` and its nodes are 8 bytes `{value, type}`
      // (`sub_823FAE00` walks them with an 8-byte stride); the helper reads nodes 2..5.
      const uint32_t elements = rb_blitz::store::GuestU32(base, array);
      REXLOG_INFO("store-probe: file-complete array words={} elements={:#x}",
                  rb_blitz::store::GuestWords(base, array, 4), elements);
      for (int i = 0; i < 6; ++i) {
        const uint32_t node = elements + static_cast<uint32_t>(i) * 8;
        REXLOG_INFO("store-probe: file-complete node {} value={:#x} type={}", i,
                    rb_blitz::store::GuestU32(base, node),
                    rb_blitz::store::GuestU32(base, node + 4));
      }
    }
  }
  __imp__sub_827538B8(ctx, base);
  if (trace) {
    REXLOG_INFO("store-probe: file-complete exit (out words {})",
                rb_blitz::store::GuestWords(base, ctx.r3.u32, 3));
  }
}

// The offers parser (`sub_8274F610(out, panel, message)`), the branch the panel uses for a row's
// page. It reads `offers_populated`, `sorted` and `offers` out of the record the message carries;
// logging the record's own top-level keys is what says whether the writer and the reader agree on
// the map convention (`sub_823FAE00` matches a sub-array whose first node is the key).
//
// Hooking `sub_823FAE00` itself was tried and is wrong: resolving the key from inside a lookup means
// calling a guest function (`sub_823F9538`) while the DTA engine is mid-evaluation, which is
// re-entrant and left boots faulting on a string read as a pointer (`read of guest 0x75706461`).
REX_EXTERN(__imp__sub_8274F610);
REX_EXTERN(__imp__sub_823F9538);
REX_HOOK_RAW(sub_8274F610) {
  const bool trace = rb_blitz::store::TraceEnabled();
  const uint32_t message = ctx.r5.u32;
  const uint32_t array = message != 0 ? rb_blitz::store::GuestU32(base, message + 4) : 0;
  // The array is the shape a page's delivery would need if the notification ever had to be replayed
  // (see `DeliverStoreRecord`); the retail body sends it, so this hook only reports.
  if (trace) {
    const uint32_t elements = array != 0 ? rb_blitz::store::GuestU32(base, array) : 0;
    // The record is the second node of the message's array (the same slot the menu helper reads).
    const uint32_t record = elements != 0 ? rb_blitz::store::GuestU32(base, elements + 16) : 0;
    REXLOG_INFO("store-probe: offers parse panel={:#x} message={:#x}[{}] record={:#x}", ctx.r4.u32,
                message, message != 0 ? rb_blitz::store::GuestWords(base, message, 2) : std::string(),
                record);
    if (record != 0) {
      const uint32_t items = rb_blitz::store::GuestU32(base, record);
      const uint32_t count = (rb_blitz::store::GuestU32(base, record + 8) >> 16) & 0xFFFF;
      REXLOG_INFO("store-probe: offers record count={} words={}", count,
                  rb_blitz::store::GuestWords(base, record, 4));
      for (uint32_t i = 0; i < count && i < 5; ++i) {
        const uint32_t node = items + i * 8;
        const uint32_t value = rb_blitz::store::GuestU32(base, node);
        const uint32_t type = rb_blitz::store::GuestU32(base, node + 4);
        std::string key;
        if (type == 16 && value != 0) {
          const uint32_t inner = rb_blitz::store::GuestU32(base, value);
          if (inner != 0) {
            PPCContext call = ctx;
            call.r3.u64 = inner;
            __imp__sub_823F9538(call, base);
            key = rb_blitz::store::GuestCString(base, call.r3.u32, 40);
          }
        }
        REXLOG_INFO("store-probe: offers record node {} type={} key='{}'", i, type, key);
      }
    }
  }
  __imp__sub_8274F610(ctx, base);
  if (trace) {
    // A fault inside the parser leaves no exit line, which is what separates "the record is wrong"
    // from "the DTA's `process_offers` is reading a key the writer does not emit".
    const uint32_t panel = ctx.r4.u32 != 0 ? ctx.r4.u32 - 204 : 0;
    REXLOG_INFO("store-probe: offers parse returned panel={:#x} out={} record={:#x}", ctx.r4.u32,
                ctx.r3.u32 != 0 ? rb_blitz::store::GuestWords(base, ctx.r3.u32, 4) : std::string(),
                array != 0 ? rb_blitz::store::GuestU32(base, array) : 0);
    // The panel's own state after the parse: `num_offers` and the offer vector live somewhere in
    // here, and dumping the words is what lets a later reader find them without another boot.
    if (panel != 0) {
      REXLOG_INFO("store-probe: panel words={}", rb_blitz::store::GuestWords(base, panel, 32));
    }
  }
}

REX_EXTERN(__imp__sub_8275D4E0);
REX_HOOK_RAW(sub_8275D4E0) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) {
    REXLOG_INFO("store-probe: menu parse enter obj={:#x} array={:#x} words={}", ctx.r3.u32,
                ctx.r4.u32, ctx.r4.u32 != 0 ? rb_blitz::store::GuestWords(base, ctx.r4.u32, 4)
                                            : std::string());
  }
  __imp__sub_8275D4E0(ctx, base);
  if (trace) {
    REXLOG_INFO("store-probe: menu parse exit menus={} submenus={:#x} name={:#x}",
                rb_blitz::store::GuestWords(base, ctx.r3.u32 + 60, 3),
                rb_blitz::store::GuestU32(base, ctx.r3.u32 + 64),
                rb_blitz::store::GuestU32(base, ctx.r3.u32 + 68));
  }
}

// The two steps inside the parse: the `submenus` value is stored by `sub_8240F3D8` (a ref
// assignment on `menu + 64`) and then walked by `sub_8275CBD8`, which finds the current menu by
// name. The last entry without its exit is the step a fault is in.
REX_EXTERN(__imp__sub_8240F3D8);
REX_HOOK_RAW(sub_8240F3D8) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) REXLOG_INFO("store-probe: submenus store enter dest={:#x} value={:#x}", ctx.r3.u32,
                         ctx.r4.u32);
  __imp__sub_8240F3D8(ctx, base);
  if (trace) REXLOG_INFO("store-probe: submenus store exit");
}

REX_EXTERN(__imp__sub_8275CBD8);
REX_HOOK_RAW(sub_8275CBD8) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) {
    REXLOG_INFO("store-probe: submenu walk enter menu={:#x} value={:#x} statics={}", ctx.r3.u32,
                ctx.r5.u32, rb_blitz::store::GuestWords(base, 0x82106B00u, 2));
  }
  __imp__sub_8275CBD8(ctx, base);
  if (trace) REXLOG_INFO("store-probe: submenu walk exit");
}

// The panel's own update, the builder's last step (`sub_82753288(panel)`). It is the one call after
// the menu is stored, so an entry without its exit names it as the fault's home.
REX_EXTERN(__imp__sub_82753288);
REX_HOOK_RAW(sub_82753288) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) REXLOG_INFO("store-probe: panel update enter panel={:#x} menus={}", ctx.r3.u32,
                         rb_blitz::store::GuestWords(base, ctx.r3.u32 + 52, 3));
  __imp__sub_82753288(ctx, base);
  if (trace) REXLOG_INFO("store-probe: panel update exit");
}

// Where the guest reads address 0, once the `store_trace` fault logger's backtrace is resolved
// against `rb_blitz.map` with the module base it logs.
// ---------------------------------------------------------------------------------------------
// P3 step 0: the readiness gate.
//
// `request_index_if_needed` is handled by 0x82753B20, whose branch calls 0x82753010, which returns
// immediately unless `*(0x82853D20 + 44) == 2` (measured 2026-10-20 in the recompiled sources; the
// first cut read 0x828A3D20, a cell this function never touches). That field is the panel's own
// state word, not the netcache definition: it stays below 2 on an offline boot even when the
// manager has been handed a local definition, which is why neither the file cache nor the request
// builder is ever reached.
//
// The hook writes 2 before the original runs. **Who it writes for changed 2026-10-22**: until then
// only `store_trace` forced it, so every store boot that drew a catalogue was a boot with the
// diagnostic switch on, and the first acceptance boot without one
// (`out/store-acceptance/p8-store/Store.log`, 16:37) drew the store's heading and LOADING and made
// no request at all. The word is therefore tied to the store's own master switch: RhythmVerse on
// takes the retail request path, RhythmVerse off is a pure pass-through (which is the silence the
// control leg measures), and `store_trace` still forces it for a diagnostic boot on a profile whose
// switch is off.
// ---------------------------------------------------------------------------------------------
// P3's row labels: the offer provider's `Text` (`sub_8275A230`) and the name lookup it uses.
//
// `StoreOfferProvider` answers a row's label only when `get_data("type")` equals the interned
// symbol `song`/`pack` (`sub_822D1CE0`); the body then hands `sub_82453090(offer)`'s answer to the
// label. The first boot with `(type song)` faulted on `read of guest 0x00000000` right after the
// page's parse returned, so these two hooks name the step instead of guessing: the provider's own
// vtable is the gate (any other list provider is skipped), and every line is behind `store_trace`.
REX_EXTERN(__imp__sub_8275A230);
REX_HOOK_RAW(sub_8275A230) {
  constexpr uint32_t kStoreOfferProviderVtable = 0x82105EB4u;
  const bool trace = rb_blitz::store::TraceEnabled();
  const uint32_t self = ctx.r3.u32;
  const bool ours = trace && self != 0 && rb_blitz::store::GuestU32(base, self) ==
                                         kStoreOfferProviderVtable;
  if (ours) {
    const uint32_t label = ctx.r6.u32;
    REXLOG_INFO("store-probe: provider text enter this={:#x} index={} label='{}'", self,
                ctx.r5.u32,
                label != 0
                    ? rb_blitz::store::GuestCString(base, rb_blitz::store::GuestU32(base, label + 116), 40)
                    : std::string());
  }
  __imp__sub_8275A230(ctx, base);
  if (ours) {
    const uint32_t out = ctx.r7.u32;
    REXLOG_INFO("store-probe: provider text exit out='{}'",
                out != 0
                    ? rb_blitz::store::GuestCString(base, rb_blitz::store::GuestU32(base, out + 116), 80)
                    : std::string());
  }
}

// `sub_82453090(offer)`: `get_data("name")` as a C string - the value the `song` label is set
// from. Logged because a null here is exactly the fault the first `(type song)` boot showed.
REX_EXTERN(__imp__sub_82453090);
REX_HOOK_RAW(sub_82453090) {
  const bool trace = rb_blitz::store::TraceEnabled();
  if (trace) {
    REXLOG_INFO("store-probe: offer name enter offer={:#x}", ctx.r3.u32);
  }
  __imp__sub_82453090(ctx, base);
  if (trace) {
    REXLOG_INFO("store-probe: offer name exit '{}'",
                ctx.r3.u32 != 0 ? rb_blitz::store::GuestCString(base, ctx.r3.u32, 96)
                                : std::string("<null>"));
  }
}

REX_EXTERN(__imp__sub_82753010);
REX_HOOK_RAW(sub_82753010) {
  // `lis r11,-32123` is 0x8285, not 0x828A: 65536 - 32123 = 33413 = 0x8285, and `addi r11,r11,
  // 15648` (0x3D20) makes the base 0x82853D20. The first P3 run forced 0x828A3D4C, which is not
  // this function's word - corrected 2026-10-20 after the generated body
  // (generated/default/rb_blitz_recomp.64.cpp, sub_82753010) was decoded again.
  constexpr uint32_t kStateField = 0x82853D20u + 44u;
  const bool trace = rb_blitz::store::TraceEnabled();
  uint32_t before = 0;
  uint32_t vector_begin = 0;
  uint32_t vector_end = 0;
  if (trace) {
    before = rb_blitz::store::GuestU32(base, kStateField);
    vector_begin = rb_blitz::store::GuestU32(base, ctx.r3.u32 + 52);
    vector_end = rb_blitz::store::GuestU32(base, ctx.r3.u32 + 56);
  }
  // The store's own switch decides, not the probe (see the comment above this hook): an offline
  // panel never reaches state 2 on its own, so the write *is* the feature's entry.
  const bool wanted = rb_blitz::store::Settings()->enabled || trace;
  const bool forced =
      wanted && rb_blitz::store::WriteGuestU32Protected(base, kStateField, 2);
  if (trace) {
    rb_blitz::store::LogGate(vector_begin, vector_end, before, forced);
  }
  __imp__sub_82753010(ctx, base);
}
