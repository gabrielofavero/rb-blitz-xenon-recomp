// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 of the gameplay-mapping plan: the guest adapter that dispatches a gameplay table's actions
// into the title's own validated boundary
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md, D4/D5).
//
// The seam table (P0, plan Â§10.2):
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
// lookup (plan Â§10.3 "Bypass"). Suppressing the boundary covers all four, including the repeat,
// with one decision - and the direct calls use `__imp__sub_824364B0`, the unhooked function, so
// the suppressor can never eat the actions it is dispatching.
//
// Why the pad state is touched at all: Start is not a `kBMAction` (P0 measured it resolving to
// `None` while still pausing - plan Â§10.4 O10), so the direct Pause travels as a Start bit in the
// pad state the filter delivers (input::GameplayPadOverlay). Everything else the Controller table
// owns is *removed* from that state while the table is active, so the bypassed preset cannot read
// it either.
//
// The probe's P0 experiments live in src/diag/gameplay_probe.cpp and are called from these
// hooks, because one guest address can only have one definition (see gameplay_probe.h).

#include "settings/runtime_values.h"
#include "hooks/gameplay_input.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>

#include "settings/runtime_settings.h"
#include <rex/ppc/hook.h>
#include <rex/logging/log_category.h>
#include <rex/ppc/context.h>
#include <rex/memory/endian.h>
#include <rex/memory/mapped_ptr.h>

#include "diag/gameplay_probe.h"
#include "diag/shader_trace.h"
#include "input/effective_input.h"
#include "input/touch_controls.h"
#include "input/touch_timing.h"
#include "input/native_menu.h"
#include "input/source_gates.h"

REX_EXTERN(__imp__sub_82435EB0);
REX_EXTERN(__imp__sub_82443C20);
REX_EXTERN(__imp__sub_82443A30);
REX_EXTERN(__imp__sub_824364B0);
REX_EXTERN(__imp__sub_824367E0);
REX_EXTERN(__imp__sub_82441D20);
REX_EXTERN(__imp__sub_824423F8);

namespace rb_blitz::gameplay_input {
namespace detail {

namespace gameplay = rb_blitz::profile::gameplay_remap;
using Action = gameplay::Action;

// The guest's own `kBMAction` values (assets/game/360/config/gen/macros.dtb, plan Â§10.1). The
// three the plan lists as unreachable from any shipped preset (`Lane2`, `Swing`,
// `AwardJackpot`) are deliberately absent: P0 gave them an inert decision (Â§10.4 O7).
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
// The guest image base the controller pointer is read through (P2's preset observation). Kept
// beside the pointer so a reader outside a hook can still resolve it.
std::atomic<uint8_t*> g_base{nullptr};
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

bool TraceOn() { return rb_blitz::settings::InspectRuntimeSetting("input_source_trace").active_value == "true"; }

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

// `sub_824364B0(controller, action, arg)` - the title's validated press path (P0 Â§10.2). The
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

// Guest-thread state only. A scoped token is set exclusively around a direct
// touch note press, never around a physical binding, repeat, or frame update.
struct TouchMistake {
  uint32_t matcher, lane, gem_index, gem;
  double time;
};
struct TouchPress {
  Action action;
  bool swiped = false;
  bool dispatched = false;
  std::vector<TouchMistake> mistakes;
};
thread_local uint64_t g_touch_press = 0;
thread_local uint64_t g_touch_timestamp_ns = 0;
thread_local uint64_t g_touch_dispatch_ns = 0;
thread_local std::map<uint64_t, TouchPress> g_touch_presses;
thread_local uint32_t g_touch_controller = 0;

bool TouchHeld(Action action) {
  for (const auto& [token, press] : g_touch_presses)
    if (press.dispatched && press.action == action) return true;
  return false;
}

// `sub_824367E0(controller, action)` - the release path. For the two track actions it is also
// what clears the controller's held-action slot, so a held binding stops repeating when it is
// released (P0 Â§10.5 item 3).
void CallRelease(PPCContext& ctx, uint8_t* base, uint32_t controller, int32_t action) {
  PPCContext call = ctx;
  call.r1.u32 -= 256;
  call.r3.u64 = controller;
  call.r4.u64 = static_cast<uint32_t>(action);
  __imp__sub_824367E0(call, base);
}

void DispatchTouch(PPCContext& ctx, uint8_t* base, uint32_t controller) {
  using namespace rb_blitz::input::touch;
  if (g_touch_controller != controller) {
    g_touch_presses.clear();
    g_touch_controller = controller;
  }
  const auto release = [&](Action action) {
    bool physical_held;
    {
      auto& state = State();
      std::lock_guard lock(state.mutex);
      physical_held = state.dispatched[static_cast<size_t>(action)];
    }
    if (!TouchHeld(action) && !physical_held)
      CallRelease(ctx, base, controller, BmAction(action));
  };
  for (const auto& event : DrainGameplay()) {
    if (event.kind == GameplayKind::kCancel) {
      const auto previous = std::move(g_touch_presses);
      g_touch_presses.clear();
      for (const auto& [token, press] : previous)
        if (press.dispatched) release(press.action);
    } else if (event.kind == GameplayKind::kPress) {
      const auto now = SDL_GetTicksNS();
      if (InputAgeNs(event.timestamp_ns, now) > kMaxInputAgeNs) continue;
      g_touch_presses.emplace(event.token, TouchPress{event.action, false, !event.swiped, {}});
      if (event.swiped) continue;
      g_touch_press = event.token;
      g_touch_timestamp_ns = event.timestamp_ns;
      g_touch_dispatch_ns = now;
      if (event.action == Action::kPowerup && TraceOn())
        REXLOG_INFO("gameplay input: touch power-up dispatched to controller {:#x}", controller);
      CallPress(ctx, base, controller, BmAction(event.action));
      g_touch_timestamp_ns = 0;
      g_touch_dispatch_ns = 0;
      g_touch_press = 0;
    } else if (event.kind == GameplayKind::kSwipe) {
      const auto it = g_touch_presses.find(event.token);
      if (it == g_touch_presses.end()) continue;
      it->second.swiped = true;
      it->second.mistakes.clear();
      CallPress(ctx, base, controller, BmAction(event.action));
      release(event.action);
    } else if (event.kind == GameplayKind::kRelease) {
      const auto it = g_touch_presses.find(event.token);
      if (it == g_touch_presses.end()) continue;
      if (!event.swiped && !it->second.swiped) {
        for (const auto& miss : it->second.mistakes) {
          // +16 is Gem's flag word: Hit=1, wrong swing=2, passed miss=4.
          // A long-held touch may outlive the note's normal miss. That path
          // already charged the penalty; do not charge it a second time.
          const auto* flags = rex::memory::GuestPtr<uint16_t*>(base, miss.gem + 16);
          if (flags == nullptr || (rex::memory::load_and_swap<uint16_t>(flags) & 4)) continue;
          PPCContext call = ctx;
          call.r1.u32 -= 256;
          call.r3.u64 = miss.matcher;
          call.r5.u64 = miss.lane;
          call.r6.u64 = miss.gem_index;
          call.r7.u64 = miss.gem;
          call.f1.f64 = miss.time;
          __imp__sub_82441D20(call, base);
        }
      }
      const auto action = it->second.action;
      const bool dispatched = it->second.dispatched;
      g_touch_presses.erase(it);
      if (dispatched) release(action);
    }
  }
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
    if (release[index] && !TouchHeld(action)) {
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

int LiveControllerPreset() {
  const uint32_t controller = detail::g_controller.load(std::memory_order_relaxed);
  uint8_t* base = detail::g_base.load(std::memory_order_relaxed);
  if (controller == 0 || base == nullptr) {
    return -1;
  }
  return static_cast<int>(detail::LoadBe32(base, controller + 80));
}

}  // namespace rb_blitz::gameplay_input

// SwingAtLane receives the calibrated song time from BeatMatcher (+96).
// Correct only the direct touch call, leaving chart polling, native controls,
// calibration offsets and the game's hit windows intact.
REX_HOOK_RAW(sub_824423F8) {
  using namespace rb_blitz::gameplay_input::detail;
  const double original = ctx.f1.f64;
  const bool trace = g_touch_timestamp_ns && TraceOn();
  if (g_touch_timestamp_ns) {
    ctx.f1.f64 = rb_blitz::input::touch::EventTimeMs(
        original, g_touch_timestamp_ns, g_touch_dispatch_ns);
  }
  const double corrected = ctx.f1.f64;
  __imp__sub_824423F8(ctx, base);
  if (trace)
    REXLOG_INFO("touch timing: token={} queue_ms={:.3f} song_ms={:.3f} event_ms={:.3f} hit={}",
        g_touch_press, double(rb_blitz::input::touch::InputAgeNs(
            g_touch_timestamp_ns, g_touch_dispatch_ns)) * 1e-6, original, corrected, ctx.r3.u32 != 0);
}

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
  rb_blitz::input::touch::PublishPowerupReady(false);
  using rb_blitz::gameplay_input::detail::Dispatcher;
  using rb_blitz::gameplay_input::detail::State;
  rb_blitz::gameplay_input::detail::g_controller.store(controller,
                                                       std::memory_order_relaxed);
  rb_blitz::gameplay_input::detail::g_base.store(base, std::memory_order_relaxed);
  rb_blitz::gameplay_input::detail::g_tick.store(0, std::memory_order_relaxed);
  {
    Dispatcher& state = State();
    std::lock_guard lock(state.mutex);
    state.dispatched.fill(false);
    state.edges.Clear();
    state.release_all = false;
  }
  // P2: the retail preset this controller was built with. `+80` is the preset-index slot the P0
  // probe measured (src/diag/gameplay_probe.cpp:388); reading it here is what lets a Controller
  // table without a custom row follow the player's selection instead of a frozen preset 0. The
  // read happens before the first gameplay poll of the song, and the store's publish stamps it, so
  // the very first dispatch already uses the right preset.
  const int preset =
      static_cast<int>(rb_blitz::gameplay_input::detail::LoadBe32(base, controller + 80));
  rb_blitz::input::ObserveControllerPreset(preset);
  const uint32_t vtable = rb_blitz::gameplay_input::detail::LoadBe32(base, controller);
  REXLOG_INFO(
      "gameplay input: song controller {:#x} (vtable {:#x}), controller preset {} observed; "
      "the Mouse & Keyboard table drives gameplay and the Controller table acts only if it has "
      "a custom row",
      controller, vtable, rb_blitz::profile::gameplay_remap::PresetLabel(preset));
  rb_blitz::diag::gameplay_probe::ControllerCreated(controller, config, vtable);
}

REX_HOOK_RAW(sub_82443C20) {
  if (rb_blitz::gameplay_input::detail::g_touch_controller == ctx.r3.u32) {
    rb_blitz::gameplay_input::detail::g_touch_presses.clear();
    rb_blitz::gameplay_input::detail::g_touch_controller = 0;
    rb_blitz::input::touch::CancelGameplay();
  }
  const uint32_t controller = static_cast<uint32_t>(ctx.r3.u32);
  if (controller != 0 &&
      rb_blitz::gameplay_input::detail::g_controller.load(std::memory_order_relaxed) ==
          controller) {
    rb_blitz::diag::gameplay_probe::ControllerDestroyed(controller);
    rb_blitz::input::touch::PublishPowerupReady(false);
    rb_blitz::gameplay_input::detail::g_controller.store(0, std::memory_order_relaxed);
    rb_blitz::gameplay_input::detail::g_base.store(nullptr, std::memory_order_relaxed);
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
    const bool receives_input = rb_blitz::input::GameScreenActive() &&
        rb_blitz::gameplay_input::detail::ControllerReceivesInput(base, controller);
    rb_blitz::diag::shader_trace::ObserveSongController(receives_input);
    if (receives_input) {
      const int tick =
          rb_blitz::gameplay_input::detail::g_tick.fetch_add(1, std::memory_order_relaxed) + 1;
      rb_blitz::diag::gameplay_probe::Frame(ctx, base, controller, tick);
    }
    // Releases flush in any context: a boundary (pause, song end, focus loss) must stop a
    // repeating track action even though no new press can be queued outside a live song.
    if (!receives_input) rb_blitz::input::touch::CancelGameplay();
    rb_blitz::gameplay_input::detail::DispatchPending(ctx, base, controller);
    rb_blitz::gameplay_input::detail::DispatchTouch(ctx, base, controller);
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

// Native wrong-swing handler, called only by sub_824423F8 after a failed
// swing-at-lane judgement. Natural chart misses use sub_824415B0 instead.
// Keep the game's successful-hit path immediate; defer only this failed result.
REX_HOOK_RAW(sub_82441D20) {
#if defined(RB_BLITZ_IOS)
  using namespace rb_blitz::gameplay_input::detail;
  if (g_touch_press != 0) {
    const auto it = g_touch_presses.find(g_touch_press);
    if (it != g_touch_presses.end()) {
      it->second.mistakes.push_back({ctx.r3.u32, ctx.r5.u32, ctx.r6.u32,
                                     ctx.r7.u32, ctx.f1.f64});
      return;
    }
  }
#endif
  __imp__sub_82441D20(ctx, base);
}
