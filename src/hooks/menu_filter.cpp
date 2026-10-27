// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/hooks/menu_filter.h for what this does and what it owes the title's
// content checksum. The edit itself and its evidence are in
// src/ui/menu_options.{h,cpp} and docs/engine/main-menu.md (part 1, the option list).

#include "hooks/menu_filter.h"

#include <algorithm>
#include <array>
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

#include "hooks/dlc.h"
#include "hooks/content_progress.h"
#include "settings/menu_compat.h"
#include "settings/boot.h"
#include "ui/menu_options.h"

#if defined(_WIN32)

#include <windows.h>

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table.
// Define named symbols the same way, so the cvar declarations live next to their table.
REXCVAR_DECLARE(bool, enhancements_hide_menu_options);
REXCVAR_DECLARE(std::string, enhancements_hidden_menu_options);
REXCVAR_DECLARE(bool, enhancements_rename_mod_settings);
REXCVAR_DECLARE(bool, enhancements_skip_offline_dialog);
REXCVAR_DECLARE(std::string, startup_mode);
REXCVAR_DECLARE(bool, enhancements_dlc_cache);
REXCVAR_DECLARE(bool, enhancements_refresh_music_library);

namespace {

// The row list and the toggles, read once at boot: they are restart-scoped, and
// reading them on a guest thread would be a lock nobody needs. A guest read
// that arrives before the boot hook has run finds the faithful defaults.
struct Settings {
  bool hide_rows = false;
  std::vector<std::string> rows;
  bool rename_settings_row = false;
  bool skip_offline_prompts = false;
  bool tap_refresh_row = false;
  // The refresh entry's own switch (`general.refresh_music_library`): with it off, the row that
  // performs the refresh is hidden even while R7 owns it.
  bool refresh_row_on = true;
  bool relabel_refresh_row = false;
  bool relabel_cache_load = false;

  bool Any() const {
    return hide_rows || rename_settings_row || skip_offline_prompts || tap_refresh_row ||
           relabel_refresh_row || relabel_cache_load;
  }
};

Settings g_settings;
bool g_configured = false;

// A candidate file the last read cut short: where its bytes are in guest memory, how
// many contiguous bytes followed them, and the archive offset they came from. The next
// read either continues it (and the edit runs on the whole file) or clears it. Per thread:
// a file is read by one thread, and a shared slot would be a race the read hook has no
// lock for.
//
// `head` is a host copy of the bytes read so far. It exists because the two halves of a
// file that spans reads are usually **not** adjacent in guest memory (measured
// 2026-10-10: the locale's second block landed in a buffer 192 KiB from the end of the
// first), so the file is assembled here rather than by the guest.
struct Pending {
  uint8_t* buffer = nullptr;   // the guest buffer the previous read filled
  size_t file_offset = 0;      // where in that buffer the candidate file starts
  size_t length = 0;           // how many contiguous bytes the run holds so far
  uint64_t offset = 0;         // the archive byte offset `buffer` was read from
  std::vector<uint8_t> head;   // the candidate's bytes, `file_offset` to `length`
};
thread_local Pending g_pending;

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
struct ChecksumEdit {
  size_t size;
  std::array<uint8_t, 20> before, after;
};

void CommitChecksums(uint8_t* base, const std::vector<ChecksumEdit>& edits) {
  for (const auto& edit : edits)
    RetargetIfNeeded(base, edit.size, edit.before.data(), edit.after.data());
}

bool PatchFile(uint8_t* base, uint8_t* file, size_t available,
               std::vector<ChecksumEdit>& edits) {
  const auto defer = [&](size_t size, const uint8_t* before, const uint8_t* after) {
    ChecksumEdit edit{size};
    std::copy_n(before, 20, edit.before.begin());
    std::copy_n(after, 20, edit.after.begin());
    edits.push_back(edit);
  };
  bool patched = false;

  if (g_settings.hide_rows) {
    const rb_blitz::menu_options::Outcome outcome =
        rb_blitz::menu_options::HideRows(file, available, g_settings.rows);
    if (outcome.applied) {
      REXLOG_INFO("menu_filter: hid {} row(s) from the main menu's option list ({} bytes moved "
                  "into the list's #ifdef macro name): {}",
                  outcome.removed.size(), outcome.freed, Joined(outcome.removed));
      defer(outcome.file_size, outcome.digest_before, outcome.digest_after);
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
      defer(renamed.file_size, renamed.digest_before, renamed.digest_after);
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
      defer(skipped.file_size, skipped.digest_before, skipped.digest_after);
      patched = true;
    } else {
      LogRejectionOnce("skip offline prompts", skipped.reason);
    }
  }

  if (g_settings.tap_refresh_row) {
    // R7: give the downloadable-content row the refresh job. The row's action becomes a
    // `file_exists` on a name no file has; src/hooks/dlc_refresh.cpp hears the question. This is
    // the byte-budgeted twin of the payload's own body (src/settings/menu_payload.cpp): it owns
    // the retail record, while the record the overlay prefers carries the body already.
    const rb_blitz::menu_options::Tapped tapped =
        rb_blitz::menu_options::TapRefreshCache(file, available);
    if (tapped.applied) {
      REXLOG_INFO("menu_filter: the downloadable-content row now refreshes the DLC cache{} ({} "
                  "bytes of its action paid for the call)",
                  tapped.navigates ? " and re-runs the discovery pass" : "", tapped.padding);
      defer(tapped.file_size, tapped.digest_before, tapped.digest_after);
      patched = true;
    } else {
      LogRejectionOnce("tap refresh row", tapped.reason);
    }
  }

  if (g_settings.relabel_refresh_row) {
    // The row the refresh job was given draws its label from the locale key it is filed
    // under (`splash_dlc` -> "Download Content"); with R7 on the key names a refresh, so
    // the label says so. The locale file is 76 KB and arrives in two archive blocks; the
    // read that completes it is the one this edit runs on.
    const rb_blitz::menu_options::Renamed renamed = rb_blitz::menu_options::RenameLabel(
        file, available, rb_blitz::menu_options::kDlcRowKey,
        rb_blitz::menu_options::kDlcRowLabel, rb_blitz::menu_options::kDlcRowRefreshLabel);
    if (renamed.applied) {
      REXLOG_INFO("menu_filter: relabelled the downloadable-content row \"{}\" to \"{}\" ({} "
                  "bytes taken from an unused macro name)",
                  renamed.from, renamed.to, renamed.borrowed);
      defer(renamed.file_size, renamed.digest_before, renamed.digest_after);
      patched = true;
    } else {
      LogRejectionOnce("relabel the refresh row", renamed.reason);
    }
  }

  if (g_settings.relabel_cache_load && rb_blitz::dlc::CacheServedThisBoot()) {
    // This boot's enumeration came out of the host cache, so the connect panel is not
    // discovering anything: its status line says what it is really doing.
    const rb_blitz::menu_options::Renamed renamed = rb_blitz::menu_options::RenameLabel(
        file, available, rb_blitz::menu_options::kEnumeratingContentKey,
        rb_blitz::menu_options::kDiscoveringContentLabel, rb_blitz::menu_options::kLoadingCacheLabel);
    if (renamed.applied) {
      REXLOG_INFO("menu_filter: relabelled \"{}\" to \"{}\" ({} bytes taken from an unused macro "
                  "name); the enumeration is coming from the cache (R7)",
                  renamed.from, renamed.to, renamed.borrowed);
      defer(renamed.file_size, renamed.digest_before, renamed.digest_after);
      patched = true;
    } else {
      LogRejectionOnce("relabel the cache load", renamed.reason);
    }
  }

  return patched;
}

// The second half of a file that spans two reads has arrived: assemble the whole file in a
// host buffer, let the edits run on it, and put the result back where the guest will still
// read it - the block this read carries, and nowhere else.
//
// Why the bytes are joined here and not by the guest: the two blocks are usually NOT
// adjacent in guest memory. Measured 2026-10-10, the English locale's second block landed
// in a buffer 192 KiB away from the end of the first, so the rule this replaces - which
// required the two guest buffers to be consecutive - dropped the locale on every boot, and
// with it both of R7's relabels (docs/guides/dlc.md §4.2). What identifies the continuation is the
// *archive* offset, which the caller has already checked.
//
// Why the first block is not written back, even though it is known to still hold the file's
// first bytes: **the guest has already copied them** out by the time the second read lands.
// Patching it then produced a file whose first half was the old bytes and whose second half
// was the new ones - a file with a four-byte hole where the label grew - and the title
// dereferenced garbage and died (measured, same day: `Unhandled guest access violation:
// read of guest 0x0000000A` immediately after the patch). It is also unsafe in principle:
// the guest reuses read buffers, so that address may belong to something else by then. An
// edit that reaches into the first half is therefore refused here, and said so, rather than
// delivered as a file that cannot parse.
void CompleteSplitFile(uint8_t* base, uint8_t* second, size_t second_length) {
  const size_t head_size = g_pending.head.size();
  std::vector<uint8_t> whole;
  whole.reserve(head_size + second_length);
  whole.insert(whole.end(), g_pending.head.begin(), g_pending.head.end());
  whole.insert(whole.end(), second, second + second_length);

  std::vector<ChecksumEdit> edits;
  if (!PatchFile(base, whole.data(), whole.size(), edits)) {
    LogRejectionOnce("a file spanning two reads", "no edit is for it");
    return;
  }

  if (!rb_blitz::menu_options::DeliverSplitEdit(g_pending.head, whole,
                                               std::span<uint8_t>(second, second_length))) {
    REXLOG_WARN("menu_filter: an edit to a file spanning two reads (ark offset {}) reaches into "
                "the first {} byte(s), which this read does not carry and the guest has already "
                "copied; it was not delivered",
                g_pending.offset + g_pending.file_offset, head_size);
    return;
  }
  CommitChecksums(base, edits);
  REXLOG_INFO("menu_filter: patched a file spanning two reads at ark offset {} ({} bytes before "
              "the seam, {} after; the edit falls entirely after it)",
              g_pending.offset + g_pending.file_offset, head_size, second_length);
}

// The three arguments the scan needs, read out of the guest context **before** the original call.
//
// `ctx` is guest register state and r8/r9/r10 are caller-saved in the PowerPC ABI, so nothing
// guarantees they still hold the caller's arguments once the original host function has run.
// Reading them afterwards works only by luck, and the scan's cost is quadratic in `length` - a
// stale `0xFFFFFFFF` there is a four-billion-iteration loop on a guest thread, which reads from
// outside as a hung boot. (Measured 2026-10-12, while chasing exactly such a hang in Ultimate mode:
// the scan really is where the CPU goes when a boot stalls. That boot's reads were a legitimate
// 64 KiB each, so the capture below is hardening for a real hazard, not the fix for that hang -
// see docs/settings/status.md (part 2, the black startup).)
struct ReadArgs {
  uint32_t length = 0;
  uint32_t buffer = 0;
  uint32_t offset_ptr = 0;
  uint32_t status_ptr = 0;
};

ReadArgs Capture(const PPCContext& ctx) {
  return ReadArgs{ctx.r9.u32, ctx.r8.u32, ctx.r10.u32, ctx.r7.u32};
}

void HandleRead(const char* which, PPCContext& ctx, uint8_t* base, const ReadArgs& args) {
  if (!g_configured || !g_settings.Any()) {
    return;
  }
  const uint32_t length = args.length;
  const uint32_t buffer = args.buffer;
  if (buffer == 0 || length < 32) {
    return;
  }
  uint8_t* host = rex::memory::GuestPtr<uint8_t*>(base, buffer);
  if (host == nullptr) {
    return;
  }

  // The byte offset is the plain read's eighth argument (r10 points at a u64).
  // NtReadFileScatter's r10 is a segment array instead, so a scatter read neither
  // continues a split file nor starts one.
  const bool plain = std::strcmp(which, "plain") == 0;
  bool have_offset = false;
  uint64_t offset = 0;
  if (plain && args.offset_ptr != 0) {
    if (const uint8_t* at = rex::memory::GuestPtr<const uint8_t*>(base, args.offset_ptr)) {
      offset = 0;
      for (int i = 0; i < 8; ++i) {
        offset = (offset << 8) | at[i];
      }
      have_offset = true;
    }
  }

  // A file larger than one read - the game's English locale is 76,726 bytes, just over the
  // 64 KiB the title reads at a time - arrives as two reads. The first read's bytes are
  // kept (host-side, in g_pending), and the read that resumes at the next archive offset
  // completes the file. See CompleteSplitFile for why the archive offset, and not the
  // guest address, is what is compared.
  if (g_pending.buffer != nullptr) {
    const bool continues = have_offset && !g_pending.head.empty() &&
                           offset == g_pending.offset + g_pending.length;
    if (continues) {
      CompleteSplitFile(base, host, length);
    }
    g_pending = Pending{};
  }

  // The ark aligns neither its entries nor their seeds, so every byte of the
  // block is a candidate for the file's first byte. The header test is what
  // makes the scan cheap enough to run on every read, and the edit's own
  // validation is what keeps a false positive harmless.
  constexpr size_t kNoCandidate = static_cast<size_t>(-1);
  size_t unfinished = kNoCandidate;
  for (size_t at = 0; at + 11 <= length; ++at) {
    if (!rb_blitz::menu_options::LooksLikeDtb(host + at, length - at)) {
      continue;
    }
    std::vector<ChecksumEdit> edits;
    if (PatchFile(base, host + at, length - at, edits)) {
      CommitChecksums(base, edits);
      REXLOG_INFO("menu_filter: patched a file at byte {} of a {} byte {} read", at, length, which);
    } else {
      // Either not a file these edits are for, or one this read cut short - and the
      // next read, if it continues this buffer, is what tells the two apart.
      unfinished = at;
    }
  }

  // Remember only what no edit handled, with a copy of its bytes: a patched file needs no
  // second look, and the one that spans reads is the candidate nearest the end of the block.
  if (plain && have_offset && unfinished != kNoCandidate) {
    g_pending.buffer = host;
    g_pending.file_offset = unfinished;
    g_pending.length = length;
    g_pending.offset = offset;
    g_pending.head.assign(host + unfinished, host + length);
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

void RetargetRecord(uint8_t* base, const uint8_t before[20], const uint8_t after[20]) {
  RetargetChecksumRow(base, before, after);
}

void Configure(bool settings_restart_boot) {
  g_settings.hide_rows = REXCVAR_GET(enhancements_hide_menu_options);
  g_settings.rename_settings_row = REXCVAR_GET(enhancements_rename_mod_settings);
  g_settings.skip_offline_prompts = settings_restart_boot || REXCVAR_GET(startup_mode) == "fast" ||
                                    REXCVAR_GET(enhancements_skip_offline_dialog);
  g_settings.tap_refresh_row = REXCVAR_GET(enhancements_dlc_cache);
  g_settings.refresh_row_on = REXCVAR_GET(enhancements_refresh_music_library);

  // P10: the hide decision is the §3.5 rule, not a branch. The in-game page now edits each
  // main-menu entry's visibility directly, so an explicit selection is authoritative even though
  // the old `hide_main_menu` master is no longer drawn: the master stays in the file for backward
  // compatibility, but a selection the player made - including an explicitly empty one - is what
  // applies. An untouched config keeps the old "nothing hidden" default.
  const std::string configured_rows = REXCVAR_GET(enhancements_hidden_menu_options);
  rb_blitz::settings::menu_compat::HideRequest request;
  request.selection_explicit = configured_rows != menu_options::kDefaultRows;
  if (const auto* boot = rb_blitz::settings::boot::SettingsBoot::Current(); boot && boot->service()) {
    request.selection_explicit = request.selection_explicit ||
        boot->service()->EffectiveSource(rb_blitz::settings::SettingId::kHiddenMainMenu) !=
            rb_blitz::settings::Source::kCompiledDefault;
  }
  request.master = g_settings.hide_rows || request.selection_explicit;
  request.selected = menu_options::ParseRowNames(configured_rows);
  if (!g_settings.tap_refresh_row) {
    // With the cache off splash_dlc is the original Download Content entry. Its single
    // visibility preference still applies; Refresh Songs must not take over its identity.
    if (!request.master) request.selected.clear();
    std::erase(request.selected, "splash_dlc");
    if (const auto* boot = rb_blitz::settings::boot::SettingsBoot::Current(); boot && boot->service() &&
        boot->service()->EffectiveValue(rb_blitz::settings::SettingId::kHideDownloadContent).boolean)
      request.selected.push_back("splash_dlc");
    request.master = true;
    request.selection_explicit = true;
  }
  request.defaults = menu_options::ParseRowNames(menu_options::kDefaultRows);
  request.refresh_row_owns_refresh = g_settings.tap_refresh_row;
  request.refresh_row_enabled = g_settings.refresh_row_on;
  const rb_blitz::settings::menu_compat::HideDecision decision =
      rb_blitz::settings::menu_compat::DecideHiddenRows(request);
  g_settings.rows = decision.hidden;
  // The list is edited whenever the decision named a row: the hide master governs the *selection*,
  // not the refresh entry's own switch, which takes its row away with the master on or off (the
  // decision above says which rows those are, and its note is logged below).
  g_settings.hide_rows = !decision.hidden.empty();

  if (g_settings.tap_refresh_row) {
    // R7 gives the downloadable-content row a new job (TapRefreshCache), so it stays in the list
    // (the decision above says so), and the same toggle repaints the row and the discovery screen:
    // the row's shipped label ("Download Content") is an online-store name for something that now
    // refreshes a library, and the discovery screen's is a lie whenever the host cache answered.
    g_settings.relabel_cache_load = true;
    if (g_settings.refresh_row_on) {
      g_settings.relabel_refresh_row = true;
      REXLOG_INFO("menu_filter: the downloadable-content row refreshes the DLC cache and is drawn "
                  "as \"{}\" (R7)",
                  menu_options::kDlcRowRefreshLabel);
    } else {
      // The entry is off (Help & Options -> Downloadable Content), so the row was hidden by the
      // decision above and there is nothing to repaint. The discovery screen's caption still
      // belongs to the cache, which is untouched by that switch.
      REXLOG_INFO("menu_filter: R7 is on but the Refresh Songs entry is off; the "
                  "downloadable-content row is hidden from the menu");
    }
  }
  g_configured = true;

  REXLOG_INFO("menu_filter: R5 {}: {}", request.master ? "on" : "off", decision.note);
  if (decision.alternate_refresh_needed) {
    // Said once and plainly: the refresh lost its row, so the only in-menu way to run it is gone
    // until the merged menu grows its own action (P10's follow-up, plan §3.5).
    REXLOG_WARN("menu_filter: the downloadable-content row was hidden by an explicit selection; "
                "the DLC library refresh has no main-menu row in this run");
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

const std::vector<std::string>& HiddenRows() { return g_settings.rows; }

}  // namespace rb_blitz::menu_filter

extern "C" REX_FUNC(__imp__NtReadFile) {
  const auto io = rb_blitz::content_progress::BeginIo(ctx.r3.u32, false);
  ReadArgs args = Capture(ctx);
  if (auto original = ResolveOriginal(/*scatter=*/false)) {
    original(ctx, base);
  }
  // NtReadFile performs the read synchronously, then may return STATUS_PENDING
  // for APC delivery. The IOSB carries the completed status and actual byte count.
  const uint32_t result = args.status_ptr && !(ctx.r3.u32 & 0x80000000u)
      ? rex::memory::load_and_swap<uint32_t>(base + args.status_ptr) : ctx.r3.u32;
  rb_blitz::content_progress::EndIo(io, result);
  if (result == 0) {
    if (args.status_ptr) args.length = std::min(args.length,
        rex::memory::load_and_swap<uint32_t>(base + args.status_ptr + 4));
    HandleRead("plain", ctx, base, args);
  }
}

extern "C" REX_FUNC(__imp__NtReadFileScatter) {
  const auto io = rb_blitz::content_progress::BeginIo(ctx.r3.u32, false);
  ReadArgs args = Capture(ctx);
  if (auto original = ResolveOriginal(/*scatter=*/true)) {
    original(ctx, base);
  }
  const uint32_t result = args.status_ptr && !(ctx.r3.u32 & 0x80000000u)
      ? rex::memory::load_and_swap<uint32_t>(base + args.status_ptr) : ctx.r3.u32;
  rb_blitz::content_progress::EndIo(io, result);
  // r8 is a segment table for Scatter, not a contiguous file buffer. Scanning
  // it as args.length bytes can inspect unrelated guest memory. No locale edit.

}

#endif  // _WIN32
