// SPDX-License-Identifier: GPL-2.0-only
// Blitz's SaveData is sent to remoteconfig/updatesavedata/ (8235BC08), not to
// Xbox content. Offline sessions therefore lose scores, stars and controls.
// Preserve the native revision-11 serializer locally; never dump guest pointers.
// ConsoleSaveData already has a content save path: queue its native autosave
// when dirty and idle, after initial loading, rather than replacing that format.
// With local_save=false every hook calls only its original implementation.

#include "hooks/local_save.h"
#include "fs/local_save.h"

#include <chrono>
#include <cstdio>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(local_save, true, "Compatibility",
                    "Persist offline stars, scores and controller mapping locally, and "
                    "autosave dirty audio/video settings. Off restores the retail offline "
                    "behaviour, whose player save destination was Rock Central.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REX_EXTERN(__imp__sub_821CA620);  // SaveData/ConsoleSaveData initialization
REX_EXTERN(__imp__sub_821CA3D8);  // SaveData teardown (release + 0x44, then clear)
REX_EXTERN(__imp__sub_821CA998);  // SaveData::Handle
REX_EXTERN(__imp__sub_821CA930);  // SaveData::UpdateStarRating
REX_EXTERN(__imp__sub_821C94D8);  // SaveData::SetLastStars
REX_EXTERN(__imp__sub_821C94E8);  // SaveData::AddTotalStars
REX_EXTERN(__imp__sub_821D6278);  // SongProgress::SetHighScore
REX_EXTERN(__imp__sub_821CCFB8);  // SaveLoadManager::Poll
REX_EXTERN(sub_821CA2A0);        // SaveData::Save
REX_EXTERN(sub_821CA740);        // SaveData::Load
REX_EXTERN(sub_82467F80);        // MemStream constructor
REX_EXTERN(sub_822CAA70);        // MemStream destructor
REX_EXTERN(sub_824628A8);        // BufStream constructor
REX_EXTERN(sub_82462BC0);        // BufStream destructor

namespace rb_blitz::local_save {
namespace {

constexpr uint32_t kSaveData = 0x82849890;
constexpr uint32_t kConsoleData = 0x82849894;
bool enabled = false;
bool ready = false;
bool dirty = false;
bool writable = true;
std::filesystem::path save_path;
Bytes last_saved;
std::chrono::steady_clock::time_point retry_at{};

uint32_t Load32(uint8_t* base, uint32_t address) {
  return Read32(rex::memory::GuestPtr<const uint8_t*>(base, address));
}

// Side calls have their own register context and ABI spill area. The stream
// object lives in guest memory, since its virtual functions are guest code too.
struct GuestStream {
  PPCContext call;
  uint8_t* base;
  rex::memory::Memory* memory;
  uint32_t address;
  uint32_t payload = 0;
  bool reading;
  bool constructed = false;

  GuestStream(PPCContext& parent, uint8_t* guest_base, bool read)
      : call(parent), base(guest_base), memory(rex::system::kernel_state()->memory()),
        address(memory->SystemHeapAlloc(48)), reading(read) {
    call.r1.u32 -= 0x100;
  }
  void Construct(std::span<const uint8_t> bytes = {}) {
    call.r3.u64 = address;
    call.r4.u64 = 0;  // big-endian stream
    if (reading) {
      payload = memory->SystemHeapAlloc(uint32_t(bytes.size()));
      if (!payload) return;
      std::memcpy(rex::memory::GuestPtr(base, payload), bytes.data(), bytes.size());
      call.r4.u64 = payload;
      call.r5.u64 = bytes.size();
      call.r6.u64 = 0;
      sub_824628A8(call, base);
    } else {
      sub_82467F80(call, base);
    }
    constructed = true;
  }
  ~GuestStream() {
    if (address) {
      if (constructed) {
        call.r3.u64 = address;
        if (reading) sub_82462BC0(call, base);
        else sub_822CAA70(call, base);
      }
      memory->SystemHeapFree(address);
    }
    if (payload) memory->SystemHeapFree(payload);
  }
};

// The guest puts SaveData back the way the retail flow expects before it asks
// Rock Central for the player's save data (sub_822CB0D0 tears the object down,
// then waits for a `save_data` response). Offline that response never arrives,
// so the checkpoint has to be applied again here - without this the object is
// empty for the whole session and the next Poll would overwrite the file with
// that emptiness.
bool Restore(PPCContext& ctx, uint8_t* base, const char* when) {
  if (last_saved.empty()) return false;
  GuestStream stream(ctx, base, true);
  if (!stream.address) {
    REXLOG_ERROR("local_save: cannot allocate a stream to restore {} ({})",
                 save_path.string(), when);
    return false;
  }
  stream.Construct(last_saved);
  if (!stream.payload) {
    REXLOG_ERROR("local_save: cannot allocate a buffer to restore {} ({})",
                 save_path.string(), when);
    return false;
  }
  const auto object = Load32(base, kSaveData);
  stream.call.r3.u64 = object;
  stream.call.r4.u64 = stream.address;
  sub_821CA740(stream.call, base);
  REXLOG_INFO("local_save: restored {} bytes {} (mapping={}, guest loader read {} of {})",
              last_saved.size(), when, object ? Load32(base, object + 56) : 0u,
              Load32(base, stream.address + 24), Load32(base, stream.address + 28));
  return true;
}

void Checkpoint(PPCContext& ctx, uint8_t* base) {
  if (!enabled || !ready || !dirty || !writable ||
      std::chrono::steady_clock::now() < retry_at) return;
  const auto data = Load32(base, kSaveData);
  if (!data) return;
  GuestStream stream(ctx, base, false);
  if (!stream.address) return;
  stream.Construct();
  stream.call.r3.u64 = data;
  stream.call.r4.u64 = stream.address;
  sub_821CA2A0(stream.call, base);
  const uint32_t begin = Load32(base, stream.address + 24);
  const uint32_t end = Load32(base, stream.address + 28);
  if (!begin || end < begin || end - begin > kMaxPayload) {
    REXLOG_ERROR("local_save: guest serializer returned an invalid buffer");
    return;
  }
  const auto* p = rex::memory::GuestPtr<const uint8_t*>(base, begin);
  Bytes bytes(p, p + (end - begin));
  if (bytes != last_saved) {
    const auto progress = Summarize(bytes);
    if (progress.empty() && !Summarize(last_saved).empty()) {
      REXLOG_WARN("local_save: the guest serialized an empty SaveData while {} still holds "
                  "progress; keeping that file and not overwriting it",
                  save_path.string());
      dirty = false;
      return;
    }
    std::string error;
    if (!Save(save_path, bytes, error)) {
      REXLOG_ERROR("local_save: could not save {}: {}; retaining previous save",
                   save_path.string(), error);
      retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      return;
    }
    last_saved = std::move(bytes);
    REXLOG_INFO("local_save: saved {} bytes to {}", last_saved.size(), save_path.string());
  }
  dirty = false;
}

}  // namespace

void Configure(rex::Runtime* runtime) {
  enabled = REXCVAR_GET(local_save);
  if (!enabled) { REXLOG_INFO("local_save: disabled; retail offline persistence"); return; }
  char xuid[17];
  std::snprintf(xuid, sizeof(xuid), "%016llX",
                static_cast<unsigned long long>(runtime->kernel_state()->user_profile()->xuid()));
  save_path = runtime->user_data_root() / xuid / "5841122D" / "local_save.bin";
  REXLOG_INFO("local_save: offline progress at {}; native audio/video autosave enabled",
              save_path.string());
}

}  // namespace rb_blitz::local_save

using namespace rb_blitz::local_save;

extern "C" REX_FUNC(sub_821CA620) {
  __imp__sub_821CA620(ctx, base);
  if (!enabled) return;
  Bytes bytes;
  const auto result = Load(save_path, bytes);
  if (result == LoadResult::Invalid) {
    writable = false;
    REXLOG_ERROR("local_save: cannot read or validate {}; preserving it, local progress writes "
                 "disabled for this session", save_path.string());
  } else if (result == LoadResult::Loaded) {
    last_saved = std::move(bytes);
    Restore(ctx, base, "at initialization");
  }
  ready = true;
}

// The save-data (re)load path: whatever the guest just cleared is what Rock
// Central would fill in online, so put the local checkpoint back instead.
extern "C" REX_FUNC(sub_821CA3D8) {
  __imp__sub_821CA3D8(ctx, base);
  if (!enabled || !ready || !writable) return;
  Restore(ctx, base, "after the guest's SaveData teardown");
}

extern "C" REX_FUNC(sub_821CA998) {
  const auto object = ctx.r4.u32;
  const auto mapping = enabled ? Load32(base, object + 56) : 0;
  __imp__sub_821CA998(ctx, base);
  if (enabled && mapping != Load32(base, object + 56)) dirty = true;
}

// Mark the native mutators; checkpoint on the next guest Poll after the whole
// operation has completed. No allocations or filesystem work inside tree edits.
#define SAVE_MUTATOR(name) \
  extern "C" REX_FUNC(name) { \
    __imp__##name(ctx, base); \
    if (enabled && ready) dirty = true; \
  }
SAVE_MUTATOR(sub_821CA930)
SAVE_MUTATOR(sub_821C94D8)
SAVE_MUTATOR(sub_821C94E8)
SAVE_MUTATOR(sub_821D6278)
#undef SAVE_MUTATOR

extern "C" REX_FUNC(sub_821CCFB8) {
  const uint32_t manager = ctx.r3.u32;
  if (enabled && ready && Load32(base, manager + 52) == 0 &&
      *rex::memory::GuestPtr(base, manager + 44) == 0) {
    const auto console = Load32(base, kConsoleData);
    if (console && *rex::memory::GuestPtr(base, console + 44)) {
      *rex::memory::GuestPtr(base, manager + 108) = 1;
    }
  }
  __imp__sub_821CCFB8(ctx, base);
  Checkpoint(ctx, base);
}
