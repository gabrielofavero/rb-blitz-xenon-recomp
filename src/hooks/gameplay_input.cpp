// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 of the gameplay-mapping plan: the guest adapter that dispatches a gameplay table's actions
// into the title's own validated boundary
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md, D4/D5).
//
// The seam table (P0, plan §10.2):
//
//   sub_82435EB0  NewController            -> this is the song's controller; adopt it
//   sub_82443C20  controller destructor    -> forget it (a song end leaves nothing armed)
//   sub_82443A30  JoypadController update  -> the per-frame guest seam (~30 ms while playing)
//   sub_824364B0  action press boundary    -> dispatch presses; suppress native producers
//   sub_824367E0  action release boundary  -> dispatch releases; suppress native releases
//
// Why the boundary rather than the button->action lookup: P0 measured four producers into it
// (ButtonDown/ButtonUp, a keyboard ButtonDown, explicit `send_down_action`/`send_up_action`
// messages and the controller's own held-action repeat), and only one of them goes through the
// lookup (plan §10.3 "Bypass"). Suppressing the boundary covers all four, including the repeat,
// with one decision - and the direct calls use `__imp__sub_824364B0`, the unhooked function, so
// the suppressor can never eat the actions it is dispatching.
//
// Why the pad state is touched at all: Start is not a `kBMAction` (P0 measured it resolving to
// `None` while still pausing - plan §10.4 O10), so the direct Pause travels as a Start bit in the
// pad state the filter delivers (input::GameplayPadOverlay). Everything else the Controller table
// owns is *removed* from that state while the table is active, so the bypassed preset cannot read
// it either.
//
// The probe's P0 experiments live in src/diag/gameplay_probe.cpp and are called from these
// hooks, because one guest address can only have one definition (see gameplay_probe.h).

#include "hooks/gameplay_input.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/xmemory.h>

#include "diag/gameplay_probe.h"
#include "input/native_menu.h"
#include "input/source_gates.h"

REX_EXTERN(__imp__sub_82435EB0);
REX_EXTERN(__imp__sub_82443C20);
REX_EXTERN(__imp__sub_82443A30);
REX_EXTERN(__imp__sub_824364B0);
REX_EXTERN(__imp__sub_824367E0);

namespace rb_blitz::gameplay_input {
namespace detail {

namespace gameplay = rb_blitz::profile::gameplay_remap;
using Action = gameplay::Action;

// The guest's own `kBMAction` values (assets/game/360/config/gen/macros.dtb, plan §10.1). The
// three the plan lists as unreachable from any shipped preset (`Lane2`, `Swing`,
// `AwardJackpot`) are deliberately absent: P0 gave them an inert decision (§10.4 O7).
constexpr int32_t kBmActionLane0 = 1;
constexpr int32_t kBmActionLane1 = 2;
constexpr int32_t kBmActionTrackUp = 4;
constexpr int32_t kBmActionTrackDown = 5;
constexpr int32_t kBmActionUsePowerUp = 6;

// Start is the pause path and travels as pad state, not through the boundary; every other action
// names its `kBMAction`.
int32_t BmAction(Action action) {
  switch (action) {
    case Action::kFirstNote:
      return kBmActionLane0;
    case Action::kSecondNote:
      return kBmActionLane1;
    case Action::kMoveLaneRight:
      return kBmActionTrackUp;
    case Action::kMoveLaneLeft:
      return kBmActionTrackDown;
    case Action::kPowerup:
      return kBmActionUsePowerUp;
    case Action::kPause:
      return 0;
  }
  return 0;
}

const char* BmActionName(int32_t action) {
  switch (action) {
    case kBmActionLane0:
      return "lane0";
    case kBmActionLane1:
      return "lane1";
    case kBmActionTrackUp:
      return "track_up";
    case kBmActionTrackDown:
      return "track_down";
    case kBmActionUsePowerUp:
      return "use_powerup";
    default:
      return "none";
  }
}

// The controller the game built for the song. One title, one player: the factory runs once per
// song and the destructor clears it, so a song end can never leave a stale pointer armed.
std::atomic<uint32_t> g_controller{0};
std::atomic<int> g_tick{0};

// The dispatcher's own state. The filter thread publishes, the guest seam consumes; a lock is the
// whole protocol and neither side ever calls into the other's domain.
struct Dispatcher {
  std::mutex mutex;
  rb_blitz::input::ActionEdges edges;
  // What the guest currently believes: true between a dispatched press and its release.
  std::array<bool, gameplay::kActionCount> dispatched{};
  // A boundary happened: release everything the guest believes is down on its next visit.
  bool release_all = false;
  // The routing the last poll reported (plan D2, read at poll time).
  bool controller_active = false;
  bool keyboard_mouse_active = false;
};

Dispatcher& State() {
  static Dispatcher dispatcher;
  return dispatcher;
}

bool TraceOn() { return rex::cvar::GetFlagByName("input_source_trace") == "true"; }

uint32_t LoadBe32(uint8_t* base, uint32_t guest_address) {
  const uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, guest_address);
  if (p == nullptr) {
    return 0;
  }
  return rex::memory::load_and_swap<uint32_t>(p);
}

uint32_t LoadBe8(uint8_t* base, uint32_t guest_address) {
  const uint8_t* p = rex::memory::GuestPtr<uint8_t*>(base, guest_address);
  return p == nullptr ? 0 : uint32_t(*p);
}

// The controller object P0 measured at `[+52]`: nonzero while it accepts input. A stale or
// switched-off controller must not receive actions.
bool ControllerReceivesInput(uint8_t* base, uint32_t controller) {
  return LoadBe8(base, controller + 52) != 0;
}

// `sub_824364B0(controller, action, arg)` - the title's validated press path (P0 §10.2). The
// side call copies the hook's context so this thread's registers survive, and takes its own
// linkage area below the intercepted frame.
void CallPress(PPCContext& ctx, uint8_t* base, uint32_t controller, int32_t action) {
  PPCContext call = ctx;
  call.r1.u32 -= 256;
  call.r3.u64 = controller;
  call.r4.u64 = static_cast<uint32_t>(action);
  call.r5.u64 = 0;
  __imp__sub_824364B0(call, base);
}

// `sub_824367E0(controller, action)` - the release path. For the two track actions it is also
// what clears the controller's held-action slot, so a held binding stops repeating when it is
// released (P0 §10.5 item 3).
void CallRelease(PPCContext& ctx, uint8_t* base, uint32_t controller, int32_t action) {
  PPCContext call = ctx;
  call.r1.u32 -= 256;
  call.r3.u64 = controller;
  call.r4.u64 = static_cast<uint32_t>(action);
  __imp__sub_824367E0(call, base);
}

void TraceDispatch(bool pressed, Action action, uint32_t controller) {
  if (!TraceOn()) {
    return;
  }
  REXLOG_INFO("gameplay input: {} action={} ({}) controller={:#x}",
              pressed ? "press" : "release", gameplay::ActionName(action), BmAction(action),
              controller);
}

// Drains what the poll queued and reconciles it against the latest held state, then calls the
// guest. Presses before releases, so a tap that began and ended between two visits arrives in
// the order the native preset would have produced it.
void DispatchPending(PPCContext& ctx, uint8_t* base, uint32_t controller) {
  std::array<bool, gameplay::kActionCount> press{};
  std::array<bool, gameplay::kActionCount> release{};
  {
    Dispatcher& state = State();
    std::lock_guard lock(state.mutex);
    if (state.release_all) {
      state.release_all = false;
      for (const Action action : gameplay::Actions()) {
        const auto index = static_cast<std::size_t>(action);
        if (state.dispatched[index]) {
          release[index] = true;
        }
      }
    }
    for (const Action action : gameplay::Actions()) {
      const auto index = static_cast<std::size_t>(action);
      bool events[2] = {false, false};
      const std::size_t count = state.edges.Drain(action, events, 2);
      bool next = state.dispatched[index];
      for (std::size_t i = 0; i < count; ++i) {
        if (events[i]) {
          press[index] = true;
          next = true;
        } else {
          release[index] = true;
          next = false;
        }
      }
      // The level the last poll published is the truth; a collapsed burst must not leave an
      // action stuck either way.
      const bool held = state.edges.Held(action);
      if (held != next) {
        if (held) {
          press[index] = true;
        } else {
          release[index] = true;
        }
        next = held;
      }
      state.dispatched[index] = next;
    }
  }
  for (const Action action : gameplay::Actions()) {
    // Start is the pad overlay's business (see the file header), not this boundary's.
    if (action == Action::kPause) {
      continue;
    }
    const auto index = static_cast<std::size_t>(action);
    if (press[index]) {
      TraceDispatch(true, action, controller);
      CallPress(ctx, base, controller, BmAction(action));
    }
    if (release[index]) {
      TraceDispatch(false, action, controller);
      CallRelease(ctx, base, controller, BmAction(action));
    }
  }
}

}  // namespace detail

PadOverlay PublishPoll(const Poll& poll) {
  detail::Dispatcher& state = detail::State();
  std::lock_guard lock(state.mutex);
  for (const detail::Action action : detail::gameplay::Actions()) {
    state.edges.Observe(action, poll.held[static_cast<std::size_t>(action)]);
  }
  state.controller_active = poll.controller_active;
  state.keyboard_mouse_active = poll.keyboard_mouse_active;
  const input::GameplayPadOverlay overlay = input::GameplayOverlay(
      poll.controller_active, poll.keyboard_mouse_active,
      poll.held[static_cast<std::size_t>(detail::Action::kPause)], poll.controller_buttons);
  return PadOverlay{overlay.clear_buttons, overlay.set_buttons};
}

void NoteBoundary() {
  detail::Dispatcher& state = detail::State();
  std::lock_guard lock(state.mutex);
  state.edges.Clear();
  // Not a release into the guest: the seam releases what it already dispatched on its next
  // visit, and the queued edges of the old context are gone (plan D5).
  state.release_all = true;
  state.controller_active = false;
  state.keyboard_mouse_active = false;
}

bool NativeActionsSuppressed() {
  detail::Dispatcher& state = detail::State();
  std::lock_guard lock(state.mutex);
  return state.controller_active;
}

}  // namespace rb_blitz::gameplay_input

// ---------------------------------------------------------------------------------------------
// The guest hooks. One address, one definition: gameplay_probe.h explains why the probe's
// experiments are called from here rather than owning these symbols themselves.
// ---------------------------------------------------------------------------------------------

REX_HOOK_RAW(sub_82435EB0) {
  const uint32_t config = static_cast<uint32_t>(ctx.r4.u32);
  __imp__sub_82435EB0(ctx, base);
  const uint32_t controller = static_cast<uint32_t>(ctx.r3.u32);
  if (controller == 0) {
    return;
  }
  using rb_blitz::gameplay_input::detail::Dispatcher;
  using rb_blitz::gameplay_input::detail::State;
  rb_blitz::gameplay_input::detail::g_controller.store(controller,
                                                       std::memory_order_relaxed);
  rb_blitz::gameplay_input::detail::g_tick.store(0, std::memory_order_relaxed);
  {
    Dispatcher& state = State();
    std::lock_guard lock(state.mutex);
    state.dispatched.fill(false);
    state.edges.Clear();
    state.release_all = false;
  }
  const uint32_t vtable = rb_blitz::gameplay_input::detail::LoadBe32(base, controller);
  REXLOG_INFO(
      "gameplay input: song controller {:#x} (vtable {:#x}); a gameplay table whose switch is "
      "on drives the six actions from the next poll",
      controller, vtable);
  rb_blitz::diag::gameplay_probe::ControllerCreated(controller, config, vtable);
}

REX_HOOK_RAW(sub_82443C20) {
  const uint32_t controller = static_cast<uint32_t>(ctx.r3.u32);
  if (controller != 0 &&
      rb_blitz::gameplay_input::detail::g_controller.load(std::memory_order_relaxed) ==
          controller) {
    rb_blitz::diag::gameplay_probe::ControllerDestroyed(controller);
    rb_blitz::gameplay_input::detail::g_controller.store(0, std::memory_order_relaxed);
    // The controller's own held-action state dies with it, so there is nothing to release.
    rb_blitz::gameplay_input::detail::Dispatcher& state =
        rb_blitz::gameplay_input::detail::State();
    std::lock_guard lock(state.mutex);
    state.dispatched.fill(false);
    state.edges.Clear();
    state.release_all = false;
  }
  __imp__sub_82443C20(ctx, base);
}

REX_HOOK_RAW(sub_82443A30) {
  const uint32_t controller = static_cast<uint32_t>(ctx.r3.u32);
  if (controller != 0 &&
      rb_blitz::gameplay_input::detail::g_controller.load(std::memory_order_relaxed) ==
          controller) {
    // The probe's tick only advances in a live song, which is what its arms were calibrated
    // against.
    if (rb_blitz::input::GameScreenActive() &&
        rb_blitz::gameplay_input::detail::ControllerReceivesInput(base, controller)) {
      const int tick =
          rb_blitz::gameplay_input::detail::g_tick.fetch_add(1, std::memory_order_relaxed) + 1;
      rb_blitz::diag::gameplay_probe::Frame(ctx, base, controller, tick);
    }
    // Releases flush in any context: a boundary (pause, song end, focus loss) must stop a
    // repeating track action even though no new press can be queued outside a live song.
    rb_blitz::gameplay_input::detail::DispatchPending(ctx, base, controller);
  }
  __imp__sub_82443A30(ctx, base);
}

REX_HOOK_RAW(sub_824364B0) {
  if (rb_blitz::diag::gameplay_probe::PressBoundary(ctx, base)) {
    return;
  }
  if (rb_blitz::gameplay_input::NativeActionsSuppressed()) {
    if (rb_blitz::gameplay_input::detail::TraceOn()) {
      REXLOG_INFO("gameplay input: suppressed native press action={} ({}) controller={:#x}",
                  ctx.r4.s32, rb_blitz::gameplay_input::detail::BmActionName(ctx.r4.s32),
                  ctx.r3.u32);
    }
    return;
  }
  __imp__sub_824364B0(ctx, base);
}

REX_HOOK_RAW(sub_824367E0) {
  if (rb_blitz::diag::gameplay_probe::ReleaseBoundary(ctx, base)) {
    return;
  }
  if (rb_blitz::gameplay_input::NativeActionsSuppressed()) {
    if (rb_blitz::gameplay_input::detail::TraceOn()) {
      REXLOG_INFO("gameplay input: suppressed native release action={} ({}) controller={:#x}",
                  ctx.r4.s32, rb_blitz::gameplay_input::detail::BmActionName(ctx.r4.s32),
                  ctx.r3.u32);
    }
    return;
  }
  __imp__sub_824367E0(ctx, base);
}
