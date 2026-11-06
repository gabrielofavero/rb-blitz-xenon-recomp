// SPDX-License-Identifier: GPL-2.0-only
#include "messages/messages_guest.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>

#include "messages/messages_guest_model.h"
#include "messages/messages_icons.h"
#include "messages/messages_service.h"

REX_EXTERN(__imp__sub_822D5928);  // retail InboxProvider::build_list
REX_EXTERN(__imp__sub_822D4E70);  // retail InboxProvider material initialization
REX_EXTERN(__imp__sub_822D52D8);  // retail InboxProvider row material callback
REX_EXTERN(__imp__sub_823E0C00);  // guest allocator
REX_EXTERN(sub_82402DE8);         // ObjectDir::Find
REX_EXTERN(sub_824EC398);         // checked RndMat cast
REX_EXTERN(sub_82459130);         // String(char*) constructor; retail uses this for inbox text
REX_EXTERN(sub_82747578);         // InboxMessage constructor
REX_EXTERN(sub_824AF4E0);         // frees one provider entry, as retail build_list does
REX_EXTERN(sub_823A0158);         // clears the provider's vector
REX_EXTERN(sub_821F73B8);         // appends one entry pointer to the provider's vector

namespace rb_blitz::messages {
namespace {
bool g_installed = false;
std::mutex g_pool_mutex;
GuestObjectPool g_objects;
std::uint32_t g_pointer_cell = 0;
struct ProviderIcons {
  std::map<std::string_view, std::uint32_t> materials;
  std::vector<std::string> rows;
};
std::map<std::uint32_t, ProviderIcons> g_provider_icons;

std::uint32_t Read32(std::uint8_t* base, std::uint32_t address) {
  const auto* p = rex::memory::GuestPtr<const std::uint8_t*>(base, address);
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | p[3];
}
void Write32(std::uint8_t* base, std::uint32_t address, std::uint32_t value) {
  auto* p = rex::memory::GuestPtr<std::uint8_t*>(base, address);
  for (int byte = 0; byte < 4; ++byte)
    p[byte] = static_cast<std::uint8_t>(value >> (24 - byte * 8));
}
std::uint32_t Allocate(PPCContext& caller, std::uint8_t* base, std::size_t size) {
  PPCContext call = caller;
  call.r3.u64 = size;
  __imp__sub_823E0C00(call, base);
  return call.r3.u32;
}

void CaptureMaterials(PPCContext& caller, std::uint8_t* base, std::uint32_t provider) {
  // Retail has already resolved icon_misc.mat. Its owning ObjectDir contains
  // the other authored inbox materials and remains valid while the list lives.
  const auto misc = Read32(base, provider + 68);
  if (!misc) return;
  // RndMat's returned pointer is already its Hmx::Object header. The owner
  // directory is at +36, as confirmed against retail's icon_misc.mat object.
  const auto directory = Read32(base, misc + 36);
  if (!directory) return;
  ProviderIcons found;
  PPCContext call = caller;
  call.r1.u32 -= 256;
  const auto chars = call.r1.u32 + 128;
  for (const auto& icon : kBuiltInIcons) {
    if (icon.material.empty()) continue;
    std::memcpy(rex::memory::GuestPtr<void*>(base, chars), icon.material.data(),
                icon.material.size());
    *rex::memory::GuestPtr<char*>(base, chars + icon.material.size()) = 0;
    call.r3.u64 = directory;
    call.r4.u64 = chars;
    call.r5.u64 = 1;
    call.r6.u64 = 1;
    sub_82402DE8(call, base);
    if (!call.r3.u32) continue;
    call.r4.u64 = 0;
    call.r5.u64 = 0x8280056C;  // Hmx::Object RTTI
    call.r6.u64 = 0x8280613C;  // RndMat RTTI
    call.r7.u64 = 0;
    sub_824EC398(call, base);
    if (call.r3.u32) found.materials.emplace(icon.material, call.r3.u32);
  }
  std::lock_guard lock(g_pool_mutex);
  auto& state = g_provider_icons[provider];
  state.materials = std::move(found.materials);
  REXLOG_INFO("messages: resolved {}/11 inbox row materials", state.materials.size());
}

std::uint32_t MakeString(PPCContext& caller, std::uint8_t* base,
                         const std::string& text) {
  // The displayed subject is a String object at the InboxMessage constructor boundary. Its source bytes and
  // String object each survive for the boot; the constructor copies text into its own storage.
  const auto storage = Allocate(caller, base, 8 + text.size() + 1);
  if (!storage) return 0;
  auto* chars = rex::memory::GuestPtr<char*>(base, storage + 8);
  std::memcpy(chars, text.c_str(), text.size() + 1);
  PPCContext call = caller;
  call.r3.u64 = storage;
  call.r4.u64 = storage + 8;
  sub_82459130(call, base);
  return storage;
}

std::uint32_t MakeChars(PPCContext& caller, std::uint8_t* base,
                        const std::string& text) {
  // The displayed body is a C string, not a String object. Retail passes sub_82462FA0's
  // character pointer directly to r4, then sub_82747578 constructs its own String at +44.
  const auto storage = Allocate(caller, base, text.size() + 1);
  if (!storage) return 0;
  std::memcpy(rex::memory::GuestPtr<void*>(base, storage), text.c_str(), text.size() + 1);
  return storage;
}

std::uint32_t MakeMessage(PPCContext& caller, std::uint8_t* base,
                          const Message& message) {
  if (const auto* existing = g_objects.Find(message)) return existing->message;
  const auto subject = MakeString(caller, base, SubjectLine(message));
  const auto body = MakeChars(caller, base, message.body);
  const auto object = Allocate(caller, base, 188);
  if (!subject || !body || !object) return 0;
  PPCContext call = caller;
  call.r3.u64 = object;
  call.r4.u64 = body;
  call.r5.u64 = 1;  // the plain-text type used by retail's "No messages" entry
  call.r6.u64 = subject;
  call.r7.u64 = 0;
  call.r8.s64 = -1;
  call.r9.s64 = -1;
  sub_82747578(call, base);
  if (call.r3.u32 != object || Read32(base, object + 60) != 1 ||
      *rex::memory::GuestPtr<const std::uint8_t*>(base, object + 138) != 0) {
    REXLOG_WARN("messages: InboxMessage constructor shape changed; retaining retail list");
    return 0;
  }
  g_objects.Remember(message, {object, subject, body});
  return object;
}

void ReplaceList(PPCContext& ctx, std::uint8_t* base, std::uint32_t provider,
                 const MessageFeed& feed) {
  // A valid empty feed still owns the screen's data. Synthesize the same plain-text row retail
  // builds for an empty manager; otherwise a stale retail manager could show unrelated messages.
  const Message empty_message{"messages-empty", "No messages",
                              "Sorry, you don't have any messages at this time.", ""};
  const bool empty = feed.messages.empty();
  const auto begin = Read32(base, provider + 52);
  const auto end = Read32(base, provider + 56);
  if (!begin || end < begin || (end - begin) % 4 != 0 || end - begin > 4096) {
    REXLOG_WARN("messages: provider vector shape unexpected; retaining retail list");
    return;
  }
  const auto first_entry = Read32(base, begin);
  if (!first_entry) {
    REXLOG_WARN("messages: provider has no entry template; retaining retail list");
    return;
  }
  const auto symbol = Read32(base, first_entry);
  if (!symbol) return;
  std::lock_guard lock(g_pool_mutex);
  if (!g_pointer_cell) g_pointer_cell = Allocate(ctx, base, 4);
  if (!g_pointer_cell) return;

  std::vector<std::uint32_t> entries;
  entries.reserve(empty ? 1 : feed.messages.size());
  for (std::size_t index = 0; index < (empty ? 1 : feed.messages.size()); ++index) {
    const auto& message = empty ? empty_message : feed.messages[index];
    const auto object = MakeMessage(ctx, base, message);
    const auto entry = object ? Allocate(ctx, base, 16) : 0;
    if (!entry) {
      for (auto allocated : entries) {
        PPCContext release = ctx;
        release.r3.u64 = allocated;
        sub_824AF4E0(release, base);
      }
      REXLOG_WARN("messages: guest allocation failed; retaining retail list");
      return;
    }
    const auto bytes = EncodeEntry(symbol, object);
    std::memcpy(rex::memory::GuestPtr<void*>(base, entry), bytes.data(), bytes.size());
    entries.push_back(entry);
  }

  // Match the original's ownership: free each 16-byte entry, clear the vector and append our
  // entries. Message objects stay in g_objects because a selected row may retain their pointer.
  for (auto cursor = begin; cursor < end; cursor += 4) {
    PPCContext release = ctx;
    release.r3.u64 = Read32(base, cursor);
    sub_824AF4E0(release, base);
  }
  PPCContext clear = ctx;
  clear.r3.u64 = provider + 52;
  clear.r4.u64 = begin;
  clear.r5.u64 = end;
  sub_823A0158(clear, base);
  for (auto entry : entries) {
    Write32(base, g_pointer_cell, entry);
    PPCContext append = ctx;
    append.r3.u64 = provider + 52;
    append.r4.u64 = g_pointer_cell;
    sub_821F73B8(append, base);
  }
  auto& icon_state = g_provider_icons[provider];
  icon_state.rows.clear();
  if (empty) icon_state.rows.emplace_back("icon_misc");
  else for (const auto& message : feed.messages) icon_state.rows.push_back(message.icon);
  REXLOG_INFO("messages: guest list built with {} message(s)", entries.size());
}

std::uint32_t RowIcon(std::uint32_t provider, std::uint32_t row) {
  std::lock_guard lock(g_pool_mutex);
  const auto it = g_provider_icons.find(provider);
  if (it == g_provider_icons.end() || row >= it->second.rows.size()) return 0;
  const auto material = RowMaterial(it->second.rows[row]);
  const auto found = it->second.materials.find(material);
  return found == it->second.materials.end() ? 0 : found->second;
}
}  // namespace

void InstallMessagesGuest() {
  g_installed = true;
  REXLOG_INFO("messages: InboxProvider list and icon hooks installed");
}
}  // namespace rb_blitz::messages

REX_HOOK_RAW(sub_822D5928) {
  const auto provider = ctx.r3.u32;
  __imp__sub_822D5928(ctx, base);
  if (!rb_blitz::messages::g_installed || !provider) return;
  const auto feed = rb_blitz::messages::Snapshot();
  if (feed) rb_blitz::messages::ReplaceList(ctx, base, provider, *feed);
}

REX_HOOK_RAW(sub_822D4E70) {
  const auto provider = ctx.r3.u32;
  __imp__sub_822D4E70(ctx, base);
  if (rb_blitz::messages::g_installed && provider)
    rb_blitz::messages::CaptureMaterials(ctx, base, provider);
}

REX_HOOK_RAW(sub_822D52D8) {
  const auto provider = ctx.r3.u32;
  const auto row = ctx.r5.u32;
  __imp__sub_822D52D8(ctx, base);
  if (!rb_blitz::messages::g_installed || !provider || !ctx.r3.u32) return;
  // Retail returns one of these five cached materials only for the "icon"
  // list mesh. Other mesh callbacks must retain their original result.
  bool is_icon = false;
  for (std::uint32_t offset : {64u, 68u, 72u, 76u, 80u})
    is_icon |= ctx.r3.u32 == rb_blitz::messages::Read32(base, provider + offset);
  if (!is_icon) return;
  const auto material = rb_blitz::messages::RowIcon(provider, row);
  if (material) ctx.r3.u64 = material;
}
