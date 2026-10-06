// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/hooks/menu_filter.h for what this does and what it owes the title's
// content checksum. The edit itself and its evidence are in
// src/ui/menu_options.{h,cpp} and docs/engine/main-menu-flow.md.

#include "hooks/menu_filter.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "ui/menu_options.h"

#if defined(_WIN32)

#include <windows.h>

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table.
// Define named symbols the same way, so the cvar declarations live next to their table.
REXCVAR_DECLARE(bool, enhancements_hide_menu_options);
REXCVAR_DECLARE(std::string, enhancements_hidden_menu_options);
REXCVAR_DECLARE(bool, enhancements_rename_mod_settings);
REXCVAR_DECLARE(bool, enhancements_skip_offline_dialog);

namespace {

// The row list and the toggles, read once at boot: they are restart-scoped, and
// reading them on a guest thread would be a lock nobody needs. A guest read
// that arrives before the boot hook has run finds the faithful defaults.
struct Settings {
  bool hide_rows = false;
  std::vector<std::string> rows;
  bool rename_settings_row = false;
  bool skip_offline_prompts = false;

  bool Any() const { return hide_rows || rename_settings_row || skip_offline_prompts; }
};

Settings g_settings;
bool g_configured = false;

// The title's content database lives in the loaded image as {const char* name;
// u8 sha1[20]} rows, and a patched file's digest has to be corrected there. The
// window is the image's own extent (the boot log prints image=82000000-82A40000
// and code=82190000-827F6ED4), which brackets the database the title ships.
constexpr uint32_t kImageBase = 0x82000000;
constexpr uint32_t kImageSize = 0x00A40000;

bool ReadableHost(const void* address) {
  MEMORY_BASIC_INFORMATION info{};
  if (VirtualQuery(address, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) {
    return false;
  }
  switch (info.Protect & 0xFF) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_WRITECOPY:
      return true;
    default:
      return false;
  }
}

std::string HexDigest(const uint8_t digest[20]) {
  static const char* kDigits = "0123456789abcdef";
  std::string text(40, '0');
  for (size_t i = 0; i < 20; ++i) {
    text[i * 2] = kDigits[digest[i] >> 4];
    text[i * 2 + 1] = kDigits[digest[i] & 0xF];
  }
  return text;
}

std::string Joined(const std::vector<std::string>& names) {
  std::string text;
  for (const std::string& name : names) {
    if (!text.empty()) {
      text += ", ";
    }
    text += name;
  }
  return text;
}

// Point the database row that holds `before` at `after`, and say how many rows
// were rewritten. Zero means the file is not in the database at all, which is
// the payload's case: the mod's files are read through the update/content
// device the database does not describe.
uint32_t RetargetChecksumRow(uint8_t* base, const uint8_t before[20], const uint8_t after[20]) {
  if (std::memcmp(before, after, 20) == 0) {
    return 0;
  }
  uint32_t rewritten = 0;
  uint8_t* const first = base + kImageBase;
  uint8_t* const limit = first + kImageSize;
  for (uint8_t* at = first; at + 20 <= limit;) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(at, &info, sizeof(info)) == 0) {
      break;
    }
    uint8_t* region_end = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
    if (region_end > limit) {
      region_end = limit;
    }
    if (ReadableHost(at)) {
      for (uint8_t* p = at; p + 20 <= region_end; ++p) {
        if (std::memcmp(p, before, 20) != 0) {
          continue;
        }
        std::memcpy(p, after, 20);
        ++rewritten;
        REXLOG_INFO("menu_filter:   content checksum row at guest {:#010X} now holds the "
                    "patched digest",
                    static_cast<uint32_t>(p - base));
      }
    }
    if (region_end <= at) {
      break;
    }
    at = region_end;
  }
  return rewritten;
}

// A guest read that carries a .dtb which is not one of the files these edits are
// for, or one whose edit cannot balance. Both look like "the toggle did nothing"
// unless the reason is written down - but the title reads a few hundred .dtb
// files, so each distinct reason is written once rather than once per file. The
// reasons come from a fixed set in menu_options.cpp, so this stays bounded.
void LogRejectionOnce(const char* edit, const std::string& reason) {
  static std::mutex mutex;
  static std::set<std::string> logged;
  std::lock_guard<std::mutex> lock(mutex);
  if (logged.insert(std::string(edit) + ": " + reason).second) {
    REXLOG_INFO("menu_filter: {} left a .dtb read alone: {}", edit, reason);
  }
}

// The title's content database holds a digest per file and refuses one that does
// not match it, so a patched file's row has to be given the patched digest. The
// payload's own files are not in the database at all: the mod's reads come through
// the overlay device the database does not describe, and then there is no row to
// correct.
void RetargetIfNeeded(uint8_t* base, size_t file_size, const uint8_t before[20],
                      const uint8_t after[20]) {
  const uint32_t rows = RetargetChecksumRow(base, before, after);
  REXLOG_INFO("menu_filter: {} bytes, digest {} -> {}", file_size, HexDigest(before),
              HexDigest(after));
  if (rows == 0) {
    REXLOG_INFO("menu_filter:   the title's content database does not carry this file, so no row "
                "had to be corrected");
  }
}

// Patch whatever this buffer carries that these edits are for. True when the bytes
// were rewritten - a candidate that is one file but not the other must not stop the
// scan, because a single read block can carry both.
bool PatchFile(uint8_t* base, uint8_t* file, size_t available) {
  bool patched = false;

  if (g_settings.hide_rows) {
    const rb_blitz::menu_options::Outcome outcome =
        rb_blitz::menu_options::HideRows(file, available, g_settings.rows);
    if (outcome.applied) {
      REXLOG_INFO("menu_filter: hid {} row(s) from the main menu's option list ({} bytes moved "
                  "into the list's #ifdef macro name): {}",
                  outcome.removed.size(), outcome.freed, Joined(outcome.removed));
      RetargetIfNeeded(base, outcome.file_size, outcome.digest_before, outcome.digest_after);
      patched = true;
    } else {
      LogRejectionOnce("hide rows", outcome.reason);
    }
  }

  if (g_settings.rename_settings_row) {
    // The Ultimate mod's own settings row: a run without the payload never reads
    // this file, so there is nothing to leave alone.
    const rb_blitz::menu_options::Renamed renamed = rb_blitz::menu_options::RenameLabel(
        file, available, rb_blitz::menu_options::kModSettingsKey,
        rb_blitz::menu_options::kModSettingsLabel, rb_blitz::menu_options::kUltimateSettingsLabel);
    if (renamed.applied) {
      REXLOG_INFO("menu_filter: relabelled \"{}\" to \"{}\" ({} bytes taken from an unused macro "
                  "name)",
                  renamed.from, renamed.to, renamed.borrowed);
      RetargetIfNeeded(base, renamed.file_size, renamed.digest_before, renamed.digest_after);
      patched = true;
    } else {
      LogRejectionOnce("rename label", renamed.reason);
    }
  }

  if (g_settings.skip_offline_prompts) {
    // The panel a start goes through: without Rock Central its connect process
    // fails, and the title asks the player twice before it shows the menu.
    const rb_blitz::menu_options::Skipped skipped =
        rb_blitz::menu_options::SkipOfflinePrompts(file, available);
    if (skipped.applied) {
      REXLOG_INFO("menu_filter: the offline prompts are skipped ({} bytes of unused state "
                  "constants paid for {} bytes of transitions{}): a start goes straight to the "
                  "menu in offline mode",
                  skipped.removed, skipped.added,
                  skipped.padding == 0 ? "" : ", with the leftover parked in a skipped block");
      RetargetIfNeeded(base, skipped.file_size, skipped.digest_before, skipped.digest_after);
      patched = true;
    } else {
      LogRejectionOnce("skip offline prompts", skipped.reason);
    }
  }

  return patched;
}

void HandleRead(const char* which, PPCContext& ctx, uint8_t* base) {
  if (!g_configured || !g_settings.Any()) {
    return;
  }
  const uint32_t length = ctx.r9.u32;
  const uint32_t buffer = ctx.r8.u32;
  if (buffer == 0 || length < 32) {
    return;
  }
  uint8_t* host = rex::memory::GuestPtr<uint8_t*>(base, buffer);
  if (host == nullptr) {
    return;
  }
  // The ark aligns neither its entries nor their seeds, so every byte of the
  // block is a candidate for the file's first byte. The header test is what
  // makes the scan cheap enough to run on every read, and the edit's own
  // validation is what keeps a false positive harmless.
  for (size_t at = 0; at + 11 <= length; ++at) {
    if (!rb_blitz::menu_options::LooksLikeDtb(host + at, length - at)) {
      continue;
    }
    if (PatchFile(base, host + at, length - at)) {
      REXLOG_INFO("menu_filter: patched a file at byte {} of a {} byte {} read", at, length, which);
    }
  }
}

// The runtime exports the host wrapper for the guest import under the name the
// recompiled call sites resolve to, and it takes the same PPCContext, so
// forwarding needs no argument marshalling.
using ReadFunc = void (*)(PPCContext&, uint8_t*);

ReadFunc ResolveOriginal(bool scatter) {
  static ReadFunc plain = nullptr;
  static ReadFunc scatter_fn = nullptr;
  ReadFunc* slot = scatter ? &scatter_fn : &plain;
  if (*slot == nullptr) {
    HMODULE runtime = GetModuleHandleA("rexruntime.dll");
    const char* symbol = scatter ? "__imp__NtReadFileScatter" : "__imp__NtReadFile";
    *slot = runtime ? reinterpret_cast<ReadFunc>(GetProcAddress(runtime, symbol)) : nullptr;
    if (*slot == nullptr) {
      REXLOG_ERROR("menu_filter: cannot resolve {} from rexruntime.dll; reads will be dropped",
                   symbol);
    }
  }
  return *slot;
}

}  // namespace

namespace rb_blitz::menu_filter {

void Configure() {
  g_settings.hide_rows = REXCVAR_GET(enhancements_hide_menu_options);
  g_settings.rows = menu_options::ParseRowNames(REXCVAR_GET(enhancements_hidden_menu_options));
  g_settings.rename_settings_row = REXCVAR_GET(enhancements_rename_mod_settings);
  g_settings.skip_offline_prompts = REXCVAR_GET(enhancements_skip_offline_dialog);
  if (g_settings.rows.empty()) {
    g_settings.rows = menu_options::ParseRowNames(menu_options::kDefaultRows);
    REXLOG_WARN("menu_filter: enhancements_hidden_menu_options is empty; hiding the compiled "
                "default ({}) instead",
                menu_options::kDefaultRows);
  }
  g_configured = true;
  if (g_settings.hide_rows) {
    REXLOG_INFO("menu_filter: hiding {} row(s) from the main menu (R5): {}", g_settings.rows.size(),
                Joined(g_settings.rows));
  } else {
    REXLOG_INFO("menu_filter: R5 off; the main menu lists every row the title ships");
  }
  if (g_settings.rename_settings_row) {
    REXLOG_INFO("menu_filter: relabelling the mod's \"{}\" row to \"{}\" (R10)",
                menu_options::kModSettingsLabel, menu_options::kUltimateSettingsLabel);
  } else {
    REXLOG_INFO("menu_filter: R10 off; the mod's own label is drawn as it ships");
  }
  if (g_settings.skip_offline_prompts) {
    REXLOG_INFO("menu_filter: answering the offline-mode prompts for the player (R3); a start "
                "goes straight to the main menu");
  } else {
    REXLOG_INFO("menu_filter: R3 off; the failed-login and offline-mode prompts are shown");
  }
}

}  // namespace rb_blitz::menu_filter

extern "C" REX_FUNC(__imp__NtReadFile) {
  if (auto original = ResolveOriginal(/*scatter=*/false)) {
    original(ctx, base);
  }
  HandleRead("plain", ctx, base);
}

extern "C" REX_FUNC(__imp__NtReadFileScatter) {
  if (auto original = ResolveOriginal(/*scatter=*/true)) {
    original(ctx, base);
  }
  HandleRead("scatter", ctx, base);
}

#endif  // _WIN32
