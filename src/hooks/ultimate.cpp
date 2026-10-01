// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Rock Band Blitz Ultimate (https://github.com/ultimate-mods-rb/blitz-ultimate)
//
// Guest addresses (VA = file offset in the decrypted image + 0x82000000):
//   0x8205DD74   8-byte constant holding "UPDATE:\0", the content device string.
//                It sits in a table of function pointers and asset names
//                ("ignored_content", "enumerate_save_game_..."), and no pointer or
//                lis/addi pair anywhere in the image refers to the slot, so the
//                consumer is reached by arithmetic we have not traced. The mod
//                rewrites the slot to "D:\0\0\0\0\0\0\0"; reproduced as a data
//                patch (bit 0).
//   0x821D0A18   sub_821D0A18(container, name, id): scans the 2-entry table at
//                0x82015FC8 - {"hierkommtalex", 1005106}, {"rockandrollstar",
//                1005109} - and returns 1 when the (name, id) pair is in it. Its
//                only caller (rb_blitz_recomp.12.cpp, in a song/content path) uses
//                that result to skip per-entry work. The mod patches the function
//                tail at 0x821D0A7C from "li r3,1; blr" to "li r3,0; blr", so the
//                table never matches; reproduced by overriding the function (bit 1).
//   0x8236C108   sub_8236C108(object, error): Blitz's PlatformMgr::SetDiskError. It
//                latches `error` at object+60 (early-outs when the latch already holds
//                the same value or 3), logs once behind a static bit at 0x8285489C,
//                notifies through a virtual call, and then never returns - it spins in
//                "li r3,1; bl Sleep; b" - so all seven references to it are tail
//                branches, and the neighbouring 0x8236C1CC clears the same guard bit.
//                The two callers that pass error=3 follow an immediate
//                "No checksum found for file %s\n" (0x82107EA4) and
//                "Checksum failure for file %s\n" (0x82107ECC), i.e. 3 is a failed
//                checksum: a mod-provided ark is absent from the retail checksum
//                database, so without this patch the validator latches the error,
//                raises the disc-error UI and the thread sleeps forever. The mod
//                patches the prologue at 0x8236C108 from "mflr r12" to "blr", i.e. the
//                whole function becomes a return with r3 intact (RB3DX does the same
//                to RB3's PlatformMgr::SetDiskError at 0x82516320); reproduced by
//                overriding the function (bit 2).
//
// Installing the mod is dropping its folder next to the game's own data, which leaves
// the payload's files and the game's files in two directories the game reads as one:
// boot opens d:\gen\patch_xbox.hdr and d:\gen\main_xbox.hdr, and the payload carries
// only the patch pair. The virtual file system resolves a file by resolving its
// directory first (VirtualFileSystem::OpenFile), so nothing per file - a device alias
// or a symbolic link - can merge the two; src/fs/payload_overlay.h is the union device
// that does, and docs/ultimate-compat.md has the evidence for every step.
//
// The payload's own default.xex is not used: this binary is recompiled from the retail
// image and reproduces the mod's three edits in it, and the overlay hides that file so
// that a stale payload copy cannot take over the boot.
//
// The Ultimate install; see docs/history/bringup-log.md B-012.
//
// Hook hygiene (docs/backlog.md §1): the faithful behaviour, and every deviation
// from it including each early return.
//
// Faithful behaviour. Vanilla Blitz is the default and is untouched: with no
// payload Configure() returns before patching anything, and every edit is behind a
// bit of `--ultimate_patches` - with the mask at 0 the two overrides call their
// originals and the content device slot keeps its retail bytes. Each override's
// early return when its bit is clear is therefore the faithful path, not a disable.
//
// Deviations (each reproduces one edit the mod makes to its own default.xex, so
// that a payload dropped beside the retail data behaves as if that image had been
// patched; the payload's own default.xex stays hidden so it cannot take over):
//   * 0x8205DD74, data patch: the content device string "UPDATE:\0" -> "D:\0...",
//     so content is read from the game directory. Applied only while the slot still
//     holds the retail bytes, which keeps an image with the edit already baked in
//     safe.
//   * 0x821D0A18, function override: the (name, id) table lookup always answers
//     "not in the table" (r3 = 0) - the mod's `li r3,1` -> `li r3,0` at 0x821D0A7C.
//     With the bit clear it calls __imp__sub_821D0A18, i.e. faithfully.
//   * 0x8236C108, function override: PlatformMgr::SetDiskError returns with r3
//     intact instead of latching a disk error, notifying and spinning forever - the
//     mod's `mflr r12` -> `blr`. Without it an ark the retail checksum database does
//     not know black-screens the boot. With the bit clear it calls
//     __imp__sub_8236C108, i.e. faithfully.
//   * The overlay device and the d:/game: aliases: the one deviation with no byte in
//     the mod's image, because the mod expects its files to be copied over the
//     game's. It is skipped when the payload is already merged into the game root.
//
// Every other early return on the patch/overlay path is a refusal to deviate
// further, not a disable: a slot that does not hold the retail string, a missing
// heap, VFS or directory, or a device that will not register all leave the guest's
// behaviour as it was, and each says why in the log.
//
// The decidable half of all that - the mode and payload truth table, the patch mask,
// the payload's spelling and the content-device slot classification - lives in
// src/hooks/ultimate_plan.h, which tests/ultimate_plan_tests.cpp covers without a boot.

#include "hooks/ultimate.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/vfs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include "fs/payload_overlay.h"

// Both overridden functions are recompiled as weak aliases of their __imp__ form,
// so a strong definition here replaces every call site while __imp__<name> stays
// the original. See the DEFINE_REX_FUNC template in rexglue-sdk.
REX_EXTERN(__imp__sub_821D0A18);
REX_EXTERN(__imp__sub_8236C108);

namespace rb_blitz::ultimate {
namespace {

// Read once during Configure(), before the guest starts; guest threads only read.
uint32_t g_patches = 0;

// Guest-visible paths registered by Runtime::SetupVfs(), and the device this layer
// registers to union the payload with the game root (src/fs/payload_overlay.h).
constexpr std::string_view kGameMount = "\\Device\\Harddisk0\\Partition1";
constexpr std::string_view kOverlayMount = "\\Device\\BlitzOverlay";

REXCVAR_DEFINE_INT32(ultimate_mode, 1, "Compatibility",
                     "Rock Band Blitz Ultimate: 0=off, 1=auto, 2=force")
    .range(0, 2)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(ultimate_payload_root, "", "Compatibility",
                      "Rock Band Blitz Ultimate payload directory (default "
                      "<game_data_root>/ultimate, relative paths resolve against "
                      "game_data_root)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_UINT32(ultimate_patches, kPatchAll, "Compatibility",
                      "Rock Band Blitz Ultimate default.xex edits to reapply: "
                      "1=content device, 2=song blacklist, 4=disk error latch")
    .range(0, kPatchAll)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

std::string HexBytes(const uint8_t* bytes, size_t count) {
  constexpr char kDigits[] = "0123456789ABCDEF";
  std::string text;
  text.reserve(count * 3);
  for (size_t i = 0; i < count; ++i) {
    if (i != 0) text.push_back(' ');
    text.push_back(kDigits[bytes[i] >> 4]);
    text.push_back(kDigits[bytes[i] & 0xF]);
  }
  return text;
}

// 0x8205DD74. Refuses to touch the slot unless it still holds the retail string,
// which keeps a modified default.xex (where the edit is already baked in) and any
// future image layout safe. The slot is in a read-only XEX section, so the page has
// to be unprotected around the write (XexModule::Load protects code and read-only
// data with kMemoryProtectRead; a plain store there is reported as an unhandled
// guest access violation and kills the process).
void ApplyContentDevicePatch(rex::Runtime* runtime) {
  auto* memory = runtime->memory();
  auto* heap = memory != nullptr ? memory->LookupHeap(kContentDeviceSlot) : nullptr;
  if (heap == nullptr) {
    REXLOG_WARN("ultimate: no heap covers 0x{:08X}, content device string not patched",
                kContentDeviceSlot);
    return;
  }
  auto* slot = memory->TranslateVirtual<uint8_t*>(kContentDeviceSlot);
  switch (ClassifyContentDeviceSlot(slot)) {
    case ContentDeviceState::kAlreadyRedirected:
      REXLOG_INFO("ultimate: 0x{:08X} already redirects the content device, nothing to do",
                  kContentDeviceSlot);
      return;
    case ContentDeviceState::kUnrecognized:
      REXLOG_WARN("ultimate: 0x{:08X} holds [{}] instead of the retail content device string,"
                  " leaving it alone",
                  kContentDeviceSlot, HexBytes(slot, sizeof(kContentDeviceRetail)));
      return;
    case ContentDeviceState::kRetail:
      break;
  }
  uint32_t old_protect = 0;
  if (!heap->Protect(kContentDeviceSlot, sizeof(kContentDevicePayload),
                     rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite,
                     &old_protect)) {
    REXLOG_WARN("ultimate: cannot unprotect 0x{:08X}, content device string not patched",
                kContentDeviceSlot);
    return;
  }
  std::memcpy(slot, kContentDevicePayload, sizeof(kContentDevicePayload));
  heap->Protect(kContentDeviceSlot, sizeof(kContentDevicePayload),
                old_protect ? old_protect : rex::memory::kMemoryProtectRead);
  REXLOG_INFO("ultimate: content device string at 0x{:08X} patched: UPDATE: -> D:",
              kContentDeviceSlot);
}

// Makes the payload's files reachable from inside the game's own content directory
// without copying them: d: and game: - both aliases of the game root - are pointed at
// a union of the payload and the game root, read-only over the payload and
// write-through to the game root, so the game cannot damage the payload and dropping
// the folder stays a complete uninstall. src/fs/payload_overlay.h explains why the
// merge needs a device of its own.
void MountOverlay(rex::Runtime* runtime, const std::filesystem::path& payload_root,
                  const std::filesystem::path& game_data_root) {
  auto* file_system = runtime->file_system();
  if (file_system == nullptr) {
    REXLOG_WARN("ultimate: no virtual file system to merge {} into", payload_root.string());
    return;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(payload_root, ec)) {
    REXLOG_ERROR("ultimate: payload {} is not a directory", payload_root.string());
    return;
  }
  if (!std::filesystem::is_directory(game_data_root, ec)) {
    REXLOG_ERROR("ultimate: game root {} is not a directory", game_data_root.string());
    return;
  }

  // The union must not be more writable than the directory it wraps, and the game root
  // device is registered read-only unless the developer asked for game-relative writes
  // (Runtime::SetupVfs). Asking its root entry is the only way: the virtual file system
  // hands out entries, not the devices behind them.
  rex::filesystem::Entry* game_root_entry = file_system->ResolvePath(kGameMount);
  const bool base_read_only = game_root_entry == nullptr || game_root_entry->is_read_only();

  auto overlay = std::make_unique<fs::PayloadOverlayDevice>(
      kOverlayMount, std::filesystem::absolute(payload_root, ec),
      std::filesystem::absolute(game_data_root, ec), base_read_only,
      std::vector<std::string>{std::string(kEntrypoint)});
  if (ec || !overlay->Initialize()) {
    REXLOG_ERROR("ultimate: cannot merge {} with {}", payload_root.string(),
                 game_data_root.string());
    return;
  }
  if (!file_system->RegisterDevice(std::move(overlay))) {
    REXLOG_ERROR("ultimate: cannot register the overlay device");
    return;
  }

  // Both aliases move together: they point at the same directory, and a game: that
  // could not see the payload would be a different directory than d:. A symbolic link
  // is inserted without overwriting, so the old target has to go first.
  for (const std::string_view alias : {"d:", "game:"}) {
    file_system->UnregisterSymbolicLink(alias);
    if (!file_system->RegisterSymbolicLink(alias, kOverlayMount)) {
      REXLOG_ERROR("ultimate: cannot point {} at the overlay", alias);
    }
  }
}

}  // namespace

bool PatchEnabled(uint32_t bit) { return HasPatch(g_patches, bit); }

void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  if (runtime == nullptr) return;

  const Mode mode = ModeFromValue(REXCVAR_GET(ultimate_mode));
  const std::filesystem::path payload_root =
      ResolvePayloadRoot(REXCVAR_GET(ultimate_payload_root), game_data_root);
  const bool has_payload = HasPayload(payload_root);
  const bool merged = HasPayload(game_data_root);

  switch (DecideOutcome(mode, has_payload, merged)) {
    case Outcome::kOff:
      REXLOG_INFO("ultimate: off, booting the retail game data");
      return;
    case Outcome::kRetailNoPayload:
      REXLOG_INFO("ultimate: no payload at {} and no merged {}, booting the retail game data",
                  payload_root.string(), std::string(kPayloadHeader));
      return;
    case Outcome::kForcedWithoutPayload:
      REXLOG_WARN("ultimate: forced on but neither {} nor {} exists, the payload's content will"
                  " not be readable",
                  payload_root.string(), std::string(kPayloadHeader));
      break;
    case Outcome::kPayload:
      break;
  }

  if (has_payload && !HasPayloadArchive(payload_root)) {
    REXLOG_WARN("ultimate: {} without {}, the game treats that pair as a damaged disc",
                std::string(kPayloadHeader), std::string(kPayloadArchive));
  }

  g_patches = ClampPatchMask(REXCVAR_GET(ultimate_patches));
  REXLOG_INFO("ultimate: payload {}, patches 0x{:X}{}", payload_root.string(), g_patches,
              merged ? " (merged into the game root)" : "");

  if (PatchEnabled(kPatchContentDevice)) {
    ApplyContentDevicePatch(runtime);
  }
  if (ShouldMountOverlay(has_payload, merged)) {
    MountOverlay(runtime, payload_root, game_data_root);
  }
}

}  // namespace rb_blitz::ultimate

// 0x821D0A18: the (name, id) table lookup, forced to "not in the table".
extern "C" REX_FUNC(sub_821D0A18) {
  if (rb_blitz::ultimate::PatchEnabled(rb_blitz::ultimate::kPatchSongBlacklist)) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_821D0A18(ctx, base);
}

// 0x8236C108 (PlatformMgr::SetDiskError): forced to a return with r3 intact, so a
// payload ark that the retail checksum database does not know cannot latch a disk
// error and black-screen the boot.
extern "C" REX_FUNC(sub_8236C108) {
  if (rb_blitz::ultimate::PatchEnabled(rb_blitz::ultimate::kPatchDiskError)) {
    return;
  }
  __imp__sub_8236C108(ctx, base);
}
