// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P0 gameplay-mapping probe. Diagnostics only: it makes the native gameplay
// action path visible and gives the P0 bypass/coexistence spike a way to
// dispatch a direct action without changing the guest's mapping data.
//
// The path this instrument follows (all addresses are guest functions in
// generated/default, named by the recompiler as sub_<address>):
//
//   sub_82435EB0  NewController: picks JoypadController (112-byte object) from
//                 (beatmatcher controllers beatmatch_controller_mapping)
//   sub_82443A70  JoypadController ctor: reads `mappings`, current index
//   sub_82443710  JoypadController::Handle: ButtonDownMsg / ButtonUpMsg
//   sub_82443478  ButtonDown: button -> kBMAction via sub_824432A0
//   sub_824432A0  action lookup in the selected mapping array
//   sub_824364B0  action press dispatch (lanes, track up/down, powerup, ...)
//   sub_824367E0  action release dispatch (lanes, powerup, swing)
//   sub_82443A30  JoypadController override called per frame; the injection point
//
// Enabling `probe_gameplay` logs those transitions. `probe_gameplay_bypass`
// additionally makes the native button->action lookup return "no action", which
// is the candidate controller-preset bypass. `probe_gameplay_inject` dispatches
// a fixed Lane0/TrackUp/Lane1/TrackDown/UsePowerUp sequence through the same
// validated dispatch function, so one song run can show both suppression and
// injection.
//
// Producer identity (static, 2026-10-18). Every native press reaches the same
// boundary and leaves its producer's return address in lr:
//
//   0x8244351C  sub_82443478  JoypadController::ButtonDown (digital buttons)
//   0x82442BA8  sub_82442B30  keyboard controller ButtonDown (key message)
//   0x82436AC4  sub_82436A28  symbol message producer, reached from the joypad
//                             Handle (+20 = sub_82443710) and the keyboard
//                             handler (sub_82442C50)
//   arg == 1    sub_82436BF8  held-action repeat, driven per frame by
//                             sub_82443A30 -> sub_82436C68 (tail branches, so
//                             lr is not its own)
//
// Releases use the same shape: 0x824435D4 (ButtonUp), 0x82442C2C (keyboard
// ButtonUp), 0x82436B20 (symbol message).
//
// The held-action repeat is the controller's own state: sub_824364B0 stores
// the action at [+68] and the time at [+72] when a track press arrives with
// arg == 0, sub_82436BF8 re-dispatches it with arg == 1 after [+60] (delay) and
// then every [+64], and sub_824367E0 clears [+68] when the same track action is
// released. A direct track action must therefore be released, not abandoned:
// the 2026-10-18 track arm dispatched 4 and 5 with arg 1 and no release, so the
// last one kept repeating until the process ended.
//
// This file is an instrument, not a feature. It is not wired to a user setting
// and it does not write the guest's mapping table.

#include <cstdint>
#include <cstring>
#include <SDL3/SDL.h>
#include "diag/gameplay_probe.h"
#include "input/native_menu.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/xmemory.h>

REX_EXTERN(__imp__sub_824432A0);
REX_EXTERN(__imp__sub_82443478);
REX_EXTERN(__imp__sub_82443528);
REX_EXTERN(__imp__sub_824364B0);
REX_EXTERN(__imp__sub_824367E0);
REX_EXTERN(__imp__sub_82442B30);
REX_EXTERN(__imp__sub_82436A28);
REX_EXTERN(__imp__sub_8238A0F0);
REX_EXTERN(__imp__sub_823F9630);
REX_EXTERN(sub_82436A28);
REX_EXTERN(sub_82442B30);
REX_EXTERN(sub_82442BB8);
REX_EXTERN(sub_8245C878);

REXCVAR_DEFINE_BOOL(probe_gameplay, false, "Diagnostics",
                    "P0: log the native ButtonDown/ButtonUp -> kBMAction -> gameplay dispatch path. "
                    "Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(probe_gameplay_bypass, false, "Diagnostics",
                    "P0: make the native joypad mapping return no action (candidate native-preset "
                    "bypass). Pair with probe_gameplay to see the suppression. Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(probe_gameplay_inject, false, "Diagnostics",
                    "P0: dispatch a fixed Lane0/Lane1/UsePowerUp sequence into the native action "
                    "boundary once a song controller exists. Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(probe_gameplay_inject_track, false, "Diagnostics",
                    "P0: include TrackUp/TrackDown in the injected sequence. Each track action is "
                    "held long enough for the controller's own repeat, then released. Diagnostics "
                    "only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(probe_gameplay_block_dispatch, false, "Diagnostics",
                    "P0: suppress every native entry into the action boundary (press and release) "
                    "while the song controller exists, covering all four producers. Direct probe "
                    "actions still dispatch. Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(probe_gameplay_inject_spam, false, "Diagnostics",
                    "P0: replace the fixed sequence with an alternating Lane0/Lane1 press-released "
                    "loop starting 8 s into the song, to test note judgement against the HUD "
                    "score. Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(probe_gameplay_challenge, false, "Diagnostics",
                    "P0: exercise dormant message and keyboard producers using guest-stack "
                    "fixtures. Pair with block_dispatch for before/after evidence. Diagnostics only.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(probe_gameplay_sources, false, "Diagnostics",
                    "P0: raw G/H or mouse left/right dispatch notes; N/M dispatch tracks. "
                    "Uses the live song guest thread. Diagnostics only, not saved mappings.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

enum BmAction : int32_t {
  kBmNone = 0,
  kLane0 = 1,
  kLane1 = 2,
  kLane2 = 3,
  kTrackUp = 4,
  kTrackDown = 5,
  kUsePowerUp = 6,
  kSwing = 7,
  kAwardJackpot = 8,
};

// Guest memory is big-endian. base is the guest image base handed to every hook.
uint32_t LoadBe32(uint8_t* base, uint32_t guest_address) {
  const uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, guest_address);
  if (p == nullptr) {
    return 0;
  }
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

const char* ActionName(int32_t action) {
  switch (action) {
    case kBmNone:
      return "none";
    case kLane0:
      return "lane0";
    case kLane1:
      return "lane1";
    case kLane2:
      return "lane2";
    case kTrackUp:
      return "track_up";
    case kTrackDown:
      return "track_down";
    case kUsePowerUp:
      return "use_powerup";
    case kSwing:
      return "swing";
    case kAwardJackpot:
      return "award_jackpot";
    default:
      return "?";
  }
}

// sub_823F9630 is the DataNode -> int accessor the ButtonDown handler itself
// uses. Calling it on the same node keeps this probe's reading identical to the
// guest's.
int32_t NodeInt(PPCContext& ctx, uint8_t* base, uint32_t node) {
  PPCContext call = ctx;
  call.r1.u32 -= 256;
  call.r3.u64 = node;
  __imp__sub_823F9630(call, base);
  return static_cast<int32_t>(call.r3.s32);
}

// sub_824432A0(this, button, use_config1) -> kBMAction.
int32_t LookupAction(PPCContext& ctx, uint8_t* base, uint32_t controller, uint32_t button,
                     uint32_t use_config1) {
  PPCContext call = ctx;
  call.r1.u32 -= 256;
  call.r3.u64 = controller;
  call.r4.u64 = button;
  call.r5.u64 = use_config1;
  __imp__sub_824432A0(call, base);
  return static_cast<int32_t>(call.r3.s32);
}

// The ButtonDown handler's own reading of its Message argument: [msg+4] is the
// DataArray, [array] is the node storage, and the first node's data is at +24.
uint32_t MessageButton(PPCContext& ctx, uint8_t* base, uint32_t message) {
  const uint32_t array = LoadBe32(base, message + 4);
  if (array == 0) {
    return 0;
  }
  const uint32_t nodes = LoadBe32(base, array);
  if (nodes == 0) {
    return 0;
  }
  return static_cast<uint32_t>(NodeInt(ctx, base, nodes + 24));
}

// The JoypadController the game created for the current song. Kept from the
// factory hook so the per-frame seam can dispatch into the same object the
// native mapping uses. The production hook (src/hooks/gameplay_input.cpp) owns
// the pointer; this copy exists for the probe's fixtures and block rule.
thread_local uint32_t g_controller = 0;
thread_local uint32_t g_last_update_game = 0;
thread_local uint32_t g_fixture_controller = 0;
thread_local int32_t g_spam_action = 0;
thread_local uint32_t g_sources = 0;

bool BlockNative(uint32_t controller) {
  return REXCVAR_GET(probe_gameplay_block_dispatch) &&
      (controller == g_controller || (g_fixture_controller && controller == g_fixture_controller));
}

// Which producer called the action boundary, from the return address the
// generated call site leaves in lr (see the header table).
const char* ProducerOf(uint32_t lr, uint32_t arg) {
  switch (lr) {
    case 0x8244351C:
      return "button_down";
    case 0x824435D4:
      return "button_up";
    case 0x82442BA8:
      return "keyboard_down";
    case 0x82442C2C:
      return "keyboard_up";
    case 0x82436AC4:
    case 0x82436B20:
      return "message";
    default:
      return lr == 0x82443A48 && arg == 1 ? "repeat" : "unknown";
  }
}

// The controller's held-action slot: nonzero means the repeat timer at
// sub_82436BF8 will keep re-dispatching that action. [+76] is a byte (stb), not
// a word, so read the single big-endian byte.
uint32_t LoadBe8(uint8_t* base, uint32_t guest_address) {
  const uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, guest_address);
  return p == nullptr ? 0 : uint32_t(*p);
}

void LogTrackSlot(const char* when, uint8_t* base, uint32_t controller) {
  const uint32_t action = LoadBe32(base, controller + 68);
  REXLOG_INFO("gameplay_probe: track slot {} action={} ({}) repeating={}", when, action,
              ActionName(static_cast<int32_t>(action)), LoadBe8(base, controller + 76));
}

void DispatchAction(PPCContext& ctx, uint8_t* base, uint32_t controller, int32_t action,
                    uint32_t arg) {
  REXLOG_INFO("gameplay_probe: direct dispatch action={} ({}) controller={:#x}", action,
              ActionName(action), controller);
  PPCContext call = ctx;
  call.r1.u32 -= 256;  // isolated side-call linkage, below the intercepted frame
  call.r3.u64 = controller;
  call.r4.u64 = static_cast<uint32_t>(action);
  call.r5.u64 = arg;
  __imp__sub_824364B0(call, base);
}

void ReleaseAction(PPCContext& ctx, uint8_t* base, uint32_t controller, int32_t action) {
  REXLOG_INFO("gameplay_probe: direct release action={} ({}) controller={:#x}", action,
              ActionName(action), controller);
  PPCContext call = ctx;
  call.r1.u32 -= 256;
  call.r3.u64 = controller;
  call.r4.u64 = static_cast<uint32_t>(action);
  __imp__sub_824367E0(call, base);
}

// The fixed sequence. Track actions are pressed with arg 0 (which arms the
// controller's repeat), held past the repeat delay so the repeat is visible,
// then released: sub_824367E0 clears [+68], so the repeat stops. The slot is
// logged before the press and after the release on every arm.
constexpr int kSpamFirstTick = 300;  // about 8 s at 28 ms/tick: the HUD still reads 0

void TimedInject(PPCContext& ctx, uint8_t* base, uint32_t controller, int tick) {
  if (REXCVAR_GET(probe_gameplay_inject_spam)) {
    if (tick < kSpamFirstTick) {
      if (tick == kSpamFirstTick - 1) {
        REXLOG_INFO("gameplay_probe: spam starts on the next tick");
      }
      return;
    }
    if (tick % 8 == 0) {
      g_spam_action = g_spam_action == kLane0 ? kLane1 : kLane0;
      DispatchAction(ctx, base, controller, g_spam_action, 0);
    } else if (tick % 8 == 4 && g_spam_action != 0) {
      ReleaseAction(ctx, base, controller, g_spam_action);
    }
    return;
  }
  const bool tracks = REXCVAR_GET(probe_gameplay_inject_track);
  switch (tick) {
    case 60:
      DispatchAction(ctx, base, controller, kLane0, 0);
      break;
    case 70:
      ReleaseAction(ctx, base, controller, kLane0);
      break;
    case 120:
      if (tracks) {
        LogTrackSlot("before track_up", base, controller);
        DispatchAction(ctx, base, controller, kTrackUp, 0);
        LogTrackSlot("after track_up press", base, controller);
      }
      break;
    case 600:
      if (tracks) {
        ReleaseAction(ctx, base, controller, kTrackUp);
        LogTrackSlot("after track_up release", base, controller);
      }
      break;
    case 650:
      DispatchAction(ctx, base, controller, kLane1, 0);
      break;
    case 660:
      ReleaseAction(ctx, base, controller, kLane1);
      break;
    case 710:
      if (tracks) {
        LogTrackSlot("before track_down", base, controller);
        DispatchAction(ctx, base, controller, kTrackDown, 0);
        LogTrackSlot("after track_down press", base, controller);
      }
      break;
    case 1190:
      if (tracks) {
        ReleaseAction(ctx, base, controller, kTrackDown);
        LogTrackSlot("after track_down release", base, controller);
      }
      break;
    case 1240:
      DispatchAction(ctx, base, controller, kUsePowerUp, 0);
      break;
    case 1250:
      ReleaseAction(ctx, base, controller, kUsePowerUp);
      break;
    default:
      break;
  }
}

void Store32(uint8_t* base, uint32_t at, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(base + at, value);
}

// Use the real guest handlers, not a fabricated LR at the boundary. All fixture
// storage is above an isolated call's linkage area, and lives only for this call.
// The keyboard fixture borrows the live controller's MessageSource/sink header;
// its mapping vector and empty key-down set follow sub_82442EC8 exactly. The
// matching ButtonUp frees the node that ButtonDown inserts into that set.
void ChallengeProducers(PPCContext& ctx, uint8_t* base) {
  PPCContext call = ctx;
  call.r1.u32 -= 1024;
  const uint32_t scratch = call.r1.u32 + 128;
  std::memset(base + scratch, 0, 768);
  const uint32_t array = scratch + 16, nodes = scratch + 64;
  Store32(base, array, nodes);
  rex::memory::store_and_swap<uint16_t>(base + array + 8, 3);
  Store32(base, nodes + 16, kLane0);
  for (const char* name : {"send_down_action", "send_up_action"}) {
    // The symbols are verified against the literals used by sub_82436A28.
    std::strcpy(reinterpret_cast<char*>(base + scratch + 128), name);
    call.r3.u32 = scratch + 104;
    call.r4.u32 = scratch + 128;
    sub_8245C878(call, base);
    Store32(base, nodes + 8, LoadBe32(base, scratch + 104));
    Store32(base, nodes + 12, 5);  // Symbol
    call.r3.u32 = scratch;  // DataNode return storage (NOT the controller)
    call.r4.u32 = g_controller;
    call.r5.u32 = array;
    call.r6.u32 = 0;
    REXLOG_INFO("gameplay_probe: challenge message {}", name);
    sub_82436A28(call, base);
  }

  const uint32_t keyboard = scratch + 256;
  std::memcpy(base + keyboard, base + g_controller, 80);
  Store32(base, keyboard + 80, 0);  // preset index
  Store32(base, keyboard + 84, scratch + 400);
  Store32(base, keyboard + 88, 1);
  Store32(base, keyboard + 92, 1);
  Store32(base, keyboard + 104, keyboard + 96);  // empty set left/right
  Store32(base, keyboard + 108, keyboard + 96);
  Store32(base, scratch + 400, scratch + 416);
  Store32(base, scratch + 416, scratch + 448);
  rex::memory::store_and_swap<uint16_t>(base + scratch + 424, 2);
  Store32(base, scratch + 448, 'g');
  Store32(base, scratch + 456, kLane1);
  Store32(base, scratch + 4, array);  // Message::mData
  Store32(base, nodes + 16, 'g');
  g_fixture_controller = keyboard;
  REXLOG_INFO("gameplay_probe: challenge keyboard fixture={:#x}", keyboard);
  call.r3.u32 = keyboard;
  call.r4.u32 = scratch;
  sub_82442B30(call, base);
  call.r3.u32 = keyboard;
  call.r4.u32 = scratch;
  sub_82442BB8(call, base);
  REXLOG_INFO("gameplay_probe: challenge keyboard remaining_keys={}",
              LoadBe32(base, keyboard + 112));
  g_fixture_controller = 0;
}

void SampleSources(PPCContext& ctx, uint8_t* base) {
  const bool focused = SDL_GetKeyboardFocus() != nullptr;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  const uint32_t mouse = focused ? SDL_GetMouseState(nullptr, nullptr) : 0;
  const uint32_t now = focused && keys ?
      (uint32_t(keys[SDL_SCANCODE_G]) << 0) | (uint32_t(keys[SDL_SCANCODE_H]) << 1) |
      (uint32_t((mouse & SDL_BUTTON_LMASK) != 0) << 2) |
      (uint32_t((mouse & SDL_BUTTON_RMASK) != 0) << 3) |
      (uint32_t(keys[SDL_SCANCODE_N]) << 4) | (uint32_t(keys[SDL_SCANCODE_M]) << 5) : 0;
  constexpr int32_t actions[] = {kLane0, kLane1, kLane0, kLane1, kTrackDown, kTrackUp};
  for (uint32_t i = 0; i < 6; ++i) {
    const uint32_t bit = 1u << i;
    if ((now ^ g_sources) & bit) {
      REXLOG_INFO("gameplay_probe: source={} index={} down={}",
                  i == 2 || i == 3 ? "mouse" : "key", i, (now & bit) != 0);
      if (now & bit) DispatchAction(ctx, base, g_controller, actions[i], 0);
      else ReleaseAction(ctx, base, g_controller, actions[i]);
    }
  }
  g_sources = now;
}

}  // namespace

// The factory: the production hook (src/hooks/gameplay_input.cpp) owns sub_82435EB0, because one
// guest address can only have one definition. It calls this with the object it adopted.
void rb_blitz::diag::gameplay_probe::ControllerCreated(uint32_t controller, uint32_t config,
                                                      uint32_t vtable) {
  g_controller = controller;
  g_spam_action = 0;
  g_sources = 0;
  if (REXCVAR_GET(probe_gameplay)) {
    REXLOG_INFO("gameplay_probe: NewController -> {:#x} (cfg={:#x}, vtable={:#x})", controller,
                config, vtable);
  }
}

// The destructor callback, called by the production hook before the original runs.
void rb_blitz::diag::gameplay_probe::ControllerDestroyed(uint32_t controller) {
  if (REXCVAR_GET(probe_gameplay) && controller == g_controller) {
    REXLOG_INFO("gameplay_probe: controller destroyed");
  }
  if (controller == g_controller) {
    g_controller = 0;
  }
}

// Game::update_controls is setup, not the per-frame injection seam.
REX_HOOK_RAW(sub_8238A0F0) {
  if (REXCVAR_GET(probe_gameplay)) {
    const uint32_t game = static_cast<uint32_t>(ctx.r3.u32);
    if (game != g_last_update_game) {
      g_last_update_game = game;
      const uint32_t beatmatcher = LoadBe32(base, game + 220);
      const uint32_t sub = beatmatcher == 0 ? 0 : LoadBe32(base, beatmatcher + 20);
      REXLOG_INFO("gameplay_probe: Game::update_controls game={:#x} +220={:#x} +20={:#x}", game,
                  beatmatcher, sub);
    }
  }
  __imp__sub_8238A0F0(ctx, base);
}


// The native mapping lookup: optionally the P0 bypass.
REX_HOOK_RAW(sub_824432A0) {
  if (REXCVAR_GET(probe_gameplay) && REXCVAR_GET(probe_gameplay_bypass) && ctx.r5.u32 == 0) {
    REXLOG_INFO("gameplay_probe: bypass lookup controller={:#x} button={}", ctx.r3.u32,
                ctx.r4.s32);
    ctx.r3.u64 = 0;
    return;
  }
  const bool on = REXCVAR_GET(probe_gameplay);
  const uint32_t controller = static_cast<uint32_t>(ctx.r3.u32);
  const uint32_t button = static_cast<uint32_t>(ctx.r4.u32);
  __imp__sub_824432A0(ctx, base);
  if (on) {
    REXLOG_INFO("gameplay_probe: lookup controller={:#x} button={} -> {} ({})", controller, button,
                ctx.r3.s32, ActionName(static_cast<int32_t>(ctx.r3.s32)));
  }
}

// The ButtonDown handler: log the message's button and the action it resolves to.
REX_HOOK_RAW(sub_82443478) {
  const bool on = REXCVAR_GET(probe_gameplay);
  uint32_t controller = 0;
  uint32_t button = 0;
  int32_t action = 0;
  if (on) {
    controller = static_cast<uint32_t>(ctx.r3.u32);
    button = MessageButton(ctx, base, static_cast<uint32_t>(ctx.r4.u32));
    action = LookupAction(ctx, base, controller, button, 0);
    REXLOG_INFO("gameplay_probe: down controller={:#x} button={} -> action={} ({})", controller,
                button, action, ActionName(action));
  }
  __imp__sub_82443478(ctx, base);
}

// The ButtonUp handler: same reading, release is action-specific.
REX_HOOK_RAW(sub_82443528) {
  if (REXCVAR_GET(probe_gameplay)) {
    const uint32_t controller = static_cast<uint32_t>(ctx.r3.u32);
    const uint32_t button = MessageButton(ctx, base, static_cast<uint32_t>(ctx.r4.u32));
    const int32_t action = LookupAction(ctx, base, controller, button, 0);
    REXLOG_INFO("gameplay_probe: up controller={:#x} button={} -> action={} ({})", controller,
                button, action, ActionName(action));
  }
  __imp__sub_82443528(ctx, base);
}

// The validated dispatch boundary, called from the production hook
// (src/hooks/gameplay_input.cpp): every press of a mapped action arrives here, from all four
// producers. Logs it and answers whether the probe's own block switch asks for it to be dropped.
bool rb_blitz::diag::gameplay_probe::PressBoundary(PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(probe_gameplay)) {
    return false;
  }
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  REXLOG_INFO(
      "gameplay_probe: dispatch controller={:#x} action={} ({}) arg={} from={} lr={:#x} origin={}",
      ctx.r3.u32, ctx.r4.s32, ActionName(static_cast<int32_t>(ctx.r4.s32)), ctx.r5.u32,
      ProducerOf(lr, static_cast<uint32_t>(ctx.r5.u32)), lr, "native");
  if (BlockNative(ctx.r3.u32)) {
    REXLOG_INFO("gameplay_probe: blocked native dispatch action={} ({}) from={} lr={:#x}",
                ctx.r4.s32, ActionName(static_cast<int32_t>(ctx.r4.s32)),
                ProducerOf(lr, static_cast<uint32_t>(ctx.r5.u32)), lr);
    return true;
  }
  return false;
}

// The release half of the same boundary, blocked by the same switch.
bool rb_blitz::diag::gameplay_probe::ReleaseBoundary(PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(probe_gameplay)) {
    return false;
  }
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  REXLOG_INFO("gameplay_probe: release controller={:#x} action={} ({}) from={} lr={:#x}",
              ctx.r3.u32, ctx.r4.s32, ActionName(static_cast<int32_t>(ctx.r4.s32)),
              ProducerOf(lr, 0), lr);
  if (BlockNative(ctx.r3.u32)) {
    REXLOG_INFO("gameplay_probe: blocked native release action={} ({}) from={} lr={:#x}",
                ctx.r4.s32, ActionName(static_cast<int32_t>(ctx.r4.s32)), ProducerOf(lr, 0), lr);
    return true;
  }
  return false;
}

// The keyboard controller's ButtonDown. Its producer is inert under this port
// (the title imports no keystroke API, so the engine never delivers key
// messages); the hook exists so a run can prove it never fires.
REX_HOOK_RAW(sub_82442B30) {
  if (REXCVAR_GET(probe_gameplay)) {
    REXLOG_INFO("gameplay_probe: keyboard controller down controller={:#x} msg={:#x}", ctx.r3.u32,
                ctx.r4.u32);
  }
  __imp__sub_82442B30(ctx, base);
}

// Explicit send_down_action/send_up_action messages. Sticks/triggers use ButtonDown.
REX_HOOK_RAW(sub_82436A28) {
  if (REXCVAR_GET(probe_gameplay)) {
    REXLOG_INFO("gameplay_probe: symbol message controller={:#x} array={:#x}", ctx.r4.u32,
                ctx.r5.u32);
  }
  __imp__sub_82436A28(ctx, base);
}

// The per-frame seam: the production hook (src/hooks/gameplay_input.cpp) owns sub_82443A30 and
// calls this only for a live song's controller with the probe's own tick. `controller` is the
// object the production hook adopted.
void rb_blitz::diag::gameplay_probe::Frame(PPCContext& ctx, uint8_t* base, uint32_t controller,
                                           int tick) {
  if (!REXCVAR_GET(probe_gameplay)) {
    return;
  }
  if (tick == 30 && REXCVAR_GET(probe_gameplay_challenge)) ChallengeProducers(ctx, base);
  if (REXCVAR_GET(probe_gameplay_inject)) TimedInject(ctx, base, controller, tick);
  if (REXCVAR_GET(probe_gameplay_sources)) SampleSources(ctx, base);
}
