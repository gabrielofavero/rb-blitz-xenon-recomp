// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R7's in-game trigger. The main menu is compiled data (docs/engine/main-menu-flow.md),
// so a "Refresh DLC Cache" row cannot call host code directly. What it *can* do is ask
// the title a question the host serves: the row is edited (src/ui/menu_options.cpp) to
// call the title's own `file_exists` on a name no file has, `rbbz_dlc_refresh`, and this
// file watches the file calls the engine makes on the title's behalf. The name appears
// in the path handed to NtCreateFile / NtOpenFile / NtQueryFullAttributesFile, and that
// path is the signal. The query itself stays harmless: the file does not exist, so the
// title's `file_exists` answers false and the row's DTA does nothing with it.
//
// The *device* in the path matters, and it was measured rather than guessed: a name
// under the game-data mount (`d:/…`) is answered out of the ark the title already has
// mapped and never reaches a file call at all, while a device the title owns is asked
// through these calls - `songcache:/rbbz_dlc_refresh` is the one that arrives. The
// recompiled guest imports no CRT file entry point (no GetFileAttributesA, no
// FindFirstFileA; see generated/default/rb_blitz_funcs.h), so the Nt* calls above are
// the whole surface a guest file question can reach the host through.
//
// Faithful behaviour is the toggle off: no row is edited (src/ui/menu_options.cpp) and
// this file's hooks do no work at all, because RefreshArmed() is false - the flag is
// read before any path is touched. Under the toggle, the sentinel path re-runs the
// library scan on the thread that asked, which is the title's own frame: the game is the
// loading screen, the cache is rewritten, and the content manager is handed the fresh
// list so the next enumeration sees a package that was dropped in mid-session.

#include <algorithm>
#include <cstdint>
#include <string_view>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "hooks/dlc.h"

#if defined(_WIN32)

#include <windows.h>

namespace {

// The name the menu row's DTA asks for. Deliberately not a path any content can have.
constexpr char kRefreshSentinel[] = "rbbz_dlc_refresh";

using ImportFunc = void (*)(PPCContext&, uint8_t*);

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// A string as the guest holds it: where it is and how long it is. Kept as a view rather
// than a std::string because this is read on every file call the title makes while the
// toggle is on, and the answer for almost all of them is "not the sentinel".
struct GuestString {
  const char* text = nullptr;
  size_t length = 0;
  bool empty() const { return text == nullptr || length == 0; }
};

// The ANSI path an X_OBJECT_ATTRIBUTES at `addr` names. The layout is the SDK's own
// (rex/system/xio.h): root_directory at 0, name_ptr at 4, and the ANSI string it points
// at is {u16 length, u16 max, u32 buffer}.
GuestString ObjectPath(uint8_t* base, const uint32_t addr) {
  if (addr == 0) {
    return {};
  }
  const uint8_t* attrs = rex::memory::GuestPtr<const uint8_t*>(base, addr);
  if (attrs == nullptr) {
    return {};
  }
  const uint32_t name_ptr = Be32(attrs + 4);
  if (name_ptr == 0) {
    return {};
  }
  const uint8_t* text = rex::memory::GuestPtr<const uint8_t*>(base, name_ptr);
  if (text == nullptr) {
    return {};
  }
  const size_t length = size_t((uint16_t(text[0]) << 8) | uint16_t(text[1]));
  const uint32_t buffer = Be32(text + 4);
  if (buffer == 0 || length == 0 || length > 4096) {
    return {};
  }
  const char* chars = rex::memory::GuestPtr<const char*>(base, buffer);
  return chars == nullptr ? GuestString{} : GuestString{chars, length};
}

void CheckSentinel(GuestString path) {
  if (path.empty()) {
    return;
  }
  constexpr size_t kSentinelLength = sizeof(kRefreshSentinel) - 1;
  if (path.length < kSentinelLength ||
      std::search(path.text, path.text + path.length, kRefreshSentinel,
                  kRefreshSentinel + kSentinelLength) == path.text + path.length) {
    return;
  }
  REXLOG_INFO("dlc: refresh asked for from the main menu (\"{}\"), re-scanning the DLC "
              "libraries",
              std::string_view(path.text, path.length));
  rb_blitz::dlc::RefreshConfigured();
}

ImportFunc ResolveImport(const char* symbol) {
  HMODULE runtime = GetModuleHandleA("rexruntime.dll");
  return runtime != nullptr ? reinterpret_cast<ImportFunc>(GetProcAddress(runtime, symbol))
                            : nullptr;
}

// Forwards the call to the SDK's own implementation, resolving it once. A symbol that
// cannot be resolved is reported rather than silently dropping the call.
void Forward(ImportFunc* slot, const char* symbol, PPCContext& ctx, uint8_t* base) {
  if (*slot == nullptr) {
    *slot = ResolveImport(symbol);
    if (*slot == nullptr) {
      REXLOG_ERROR("dlc: cannot resolve {} from rexruntime.dll; the file call is dropped",
                   symbol);
      return;
    }
  }
  (*slot)(ctx, base);
}

}  // namespace

// The object attributes argument is r5 for the two open/create calls and r3 for the
// full-attributes query, matching the SDK's own entry signatures
// (rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_io.cpp).
extern "C" REX_FUNC(__imp__NtCreateFile) {
  if (rb_blitz::dlc::RefreshArmed()) {
    CheckSentinel(ObjectPath(base, ctx.r5.u32));
  }
  static ImportFunc original = nullptr;
  Forward(&original, "__imp__NtCreateFile", ctx, base);
}

extern "C" REX_FUNC(__imp__NtOpenFile) {
  if (rb_blitz::dlc::RefreshArmed()) {
    CheckSentinel(ObjectPath(base, ctx.r5.u32));
  }
  static ImportFunc original = nullptr;
  Forward(&original, "__imp__NtOpenFile", ctx, base);
}

extern "C" REX_FUNC(__imp__NtQueryFullAttributesFile) {
  if (rb_blitz::dlc::RefreshArmed()) {
    CheckSentinel(ObjectPath(base, ctx.r3.u32));
  }
  static ImportFunc original = nullptr;
  Forward(&original, "__imp__NtQueryFullAttributesFile", ctx, base);
}

#endif  // _WIN32
