// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the pad remap (src/input/remap.h). See src/profile/remap.h for what a
// binding means; this file is only the device side of it.

#include "input/remap.h"
#include "hooks/gameplay_input.h"
#include "input/effective_input.h"
#include "input/host_pad.h"
#include "input/input_capture.h"
#include "input/modal_input.h"
#include "input/menu_music_bindings.h"
#include "hooks/menu_music_now_playing.h"
#include "input/native_menu.h"
#include "input/pad_packet.h"
#include "input/source_gates.h"
#include "input/title_input.h"
#include "settings/input_bindings.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/kernel/xam/module.h>
#include <rex/logging.h>

#include "profile/profile.h"
#include "profile/gameplay_remap.h"
#include "profile/profile_apply.h"
#include "profile/remap.h"

// P1 of docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md: the device gates D2
// defines. Controller Settings writes them through its profile (the allowlist in
// src/settings/cvars.h), so a boot nobody launched through Controller Settings has to read them
// from here; the compiled defaults match the schema rows' own defaults. The two gameplay routing
// switches that used to live here are retired (P2 of the interface/gameplay mapping restructure
// plan, D3) - custom is inferred from each table, never from a profile switch.
REXCVAR_DEFINE_BOOL(keyboard_support, true, "Input",
                    "Keyboard support: while on, the title's keyboard rows (General Mapping's key "
                    "sources and the Mouse & Keyboard gameplay table) drive the game. Off blocks "
                    "that device; Controller Settings' own keys are unaffected. Applied live at the "
                    "menu/pause boundary the in-game Controls page saves at.");
REXCVAR_DECLARE(bool, enhancements_background_music_nav);

// P2's live instrument (plan §6): one line per press/release the physical layer sees for a
// gameplay table, naming the binding that caused it. Off by default; the acceptance arm runs with
// it on so the sampler and the gates can be observed without a debugger.
REXCVAR_DEFINE_BOOL(input_source_trace, false, "Diagnostics",
                    "Trace the gameplay source sampler: table, action, edge and the binding that "
                    "fired it.");

namespace rb_blitz::input {
namespace {

using rb_blitz::profile::remap::Source;
using rb_blitz::profile::remap::SourceKind;

// The same word the enhancements' toggle log uses, so two boot lines cannot spell a source
// differently.
const char* CvarSourceName(const rex::cvar::Source source) {
  switch (source) {
    case rex::cvar::Source::kDefault:
      return "default";
    case rex::cvar::Source::kConfig:
      return "config";
    case rex::cvar::Source::kEnvironment:
      return "environment";
    case rex::cvar::Source::kCommandLine:
      return "command line";
    case rex::cvar::Source::kRuntime:
      return "runtime";
  }
  return "unknown";
}

// The mouse buttons the grammar names now live in the SDK-free source-gate module
// (`MouseMaskFor` in src/input/source_gates.h), with the same SDL mask bits
// `SDL_GetMouseState()` returns, so the sampler and the tests spell one table.

// The physical snapshot every mapping evaluator reads for one poll (D1/D5): raw devices, read
// before the SDK merges anything and before General Mapping rewrites anything.
PhysicalInput SamplePhysicalInput() {
  PhysicalInput input;
  // The window thread's own focus answer (input/mouse_ui.cpp), not SDL's: SDL documents
  // `SDL_GetKeyboardFocus`/`SDL_GetMouseState` as main-thread calls and this sampler runs on the
  // guest's input thread. The native menu's own focus is a different fact (the pointer's
  // availability), so a menu overlay must not stop the keyboard from playing.
  input.window_focused = HostInputFocused();
  rex::input::X_INPUT_STATE physical_pad{};
  input.pad_connected = SamplePhysicalPad(&physical_pad);
  const auto& pad = physical_pad.gamepad;
  input.pad = rb_blitz::profile::remap::PadState{pad.buttons, pad.left_trigger, pad.right_trigger,
                                                 pad.thumb_lx, pad.thumb_ly, pad.thumb_rx,
                                                 pad.thumb_ry};
  if (input.window_focused) {
    // The raw button mask (a gameplay `mouse:` source is sampled here, never from the native
    // menu's queue - D3), and the keyboard from the window's own key-event latch
    // (`HostKeyDown`). SDL's global keyboard array is deliberately not the source: P2's live arm
    // measured it blind in this process (SDL reports no keyboard focus and no key state) while
    // the events keep working.
    input.mouse_buttons = HostMouseButtons();
    // Wheel detents are events: consume them here so exactly one poll sees each one, and a
    // gameplay `mouse:wheel_up`/`mouse:wheel_down` reads as a press followed by a release.
    input.wheel_detents = HostTakeMouseWheel();
    input.key_down = [](std::string_view name) { return HostKeyDown(name); };
  }
  return input;
}

// The boundary counter `InvalidateGameplayInput` bumps. The poll that sees a new value drops the
// held state for both gameplay tables instead of releasing into the guest late.
std::atomic<uint32_t> g_gameplay_boundary{0};

// Everything a source resolves against, kept alive by the filter that holds it.
struct RemapContext {
  std::mutex packet_mutex;
  std::array<PadPacket, 4> packets;

  // P2's per-poll gameplay state: one hold per action per table, and the facts a boundary is made
  // of. P3 publishes the union of the active tables' holds (plan D5's action boundary) through
  // `hooks/gameplay_input.h`, which queues the edges the guest seam dispatches.
  std::mutex gameplay_mutex;
  std::array<ActionHold, profile::gameplay_remap::kTableKindCount *
                             profile::gameplay_remap::kActionCount>
      gameplay_holds;
  // The last raw held state per action, for `input_source_trace` only. Kept apart from
  // `gameplay_holds` so the trace can prove the sampler even while the sequencing guard holds
  // both switches off.
  std::array<bool, profile::gameplay_remap::kTableKindCount *
                       profile::gameplay_remap::kActionCount>
      trace_held{};
  bool gameplay_initialized = false;
  bool gameplay_active = false;
  bool gameplay_pad_connected = false;
  SourceGates gameplay_gates;
  GameplayRouting gameplay_routing;
  uint32_t gameplay_boundary = 0;

  gameplay_input::PadOverlay ObserveGameplay(const PhysicalInput& input, const SourceGates& gates,
                                             const GameplayRouting& routing, bool gameplay,
                                             bool trace, const EffectiveInput& config,
                                             const uint16_t controller_buttons) {
    // A low-rate heartbeat while tracing: it says whether the sampler is running at all and what
    // it sees (gameplay context, focus, a physical pad), which is what a trace that stays silent
    // otherwise leaves ambiguous. The raw key probe reads the very same `KeyDown` path the tables
    // use, at a handful of their own default names: a heartbeat that shows `keys=00` while a key
    // is held is the difference between "the table is not being evaluated" and "SDL's state never
    // saw the key".
    static std::atomic<uint32_t> heartbeat{0};
    if (trace && (heartbeat.fetch_add(1, std::memory_order_relaxed) % 512u) == 0) {
      constexpr const char* kProbeKeys[] = {"J", "K", "D", "Space", "X", "N", "M", "Return"};
      uint32_t keys = 0;
      for (uint32_t i = 0; i < 8; ++i) {
        if (input.KeyDown(kProbeKeys[i])) {
          keys |= 1u << i;
        }
      }
      REXLOG_INFO(
          "input source: sampler gameplay={} focused={} sdl_focus={} pad_connected={} trace={} "
          "keys={:02X} mouse={:02X}",
          gameplay ? "on" : "off", input.window_focused ? "on" : "off",
          HostSdlKeyboardFocused() ? "on" : "off", input.pad_connected ? "on" : "off",
          input.key_down ? "keys" : "no keys", keys, input.mouse_buttons);
    }
    const uint32_t boundary = g_gameplay_boundary.load(std::memory_order_relaxed);
    // Everything that makes the previous poll's answer unusable: a focus loss or screen change
    // (the counter), leaving or entering gameplay, a flipped switch, or a pad arriving/leaving.
    // A hold is dropped without an edge - a boundary is not a release.
    const bool reset = !gameplay_initialized || boundary != gameplay_boundary ||
                       gameplay != gameplay_active ||
                       input.pad_connected != gameplay_pad_connected ||
                       !(gates == gameplay_gates) || !(routing == gameplay_routing);
    // P3: a boundary is also the dispatcher's, and it is taken *before* this poll evaluates
    // anything. Queued edges of the old context must not survive into the new one, and the guest
    // seam has to release what it already dispatched (a held track action repeats until it is
    // released). Done outside `gameplay_mutex`: `NoteBoundary` owns its own lock and nothing here
    // may hold two.
    if (reset) {
      gameplay_input::NoteBoundary();
    }
    struct TraceLine {
      profile::gameplay_remap::TableKind kind;
      profile::gameplay_remap::Action action;
      bool down;
      bool active;
      std::string source;
    };
    std::vector<TraceLine> lines;
    gameplay_input::Poll poll;
    {
      std::lock_guard lock(gameplay_mutex);
      if (reset) {
        for (ActionHold& hold : gameplay_holds) {
          hold.Invalidate();
        }
        trace_held.fill(false);
        gameplay_initialized = true;
        gameplay_active = gameplay;
        gameplay_pad_connected = input.pad_connected;
        gameplay_gates = gates;
        gameplay_routing = routing;
        gameplay_boundary = boundary;
      }
      for (const profile::gameplay_remap::TableKind kind : profile::gameplay_remap::TableKinds()) {
        const profile::gameplay_remap::Table& table =
            kind == profile::gameplay_remap::TableKind::kController ? config.controller
                                                                   : config.keyboard_mouse;
        const bool active = TableActive(routing, kind, gameplay);
        for (const profile::gameplay_remap::Action action : profile::gameplay_remap::Actions()) {
          // One evaluation per action per poll, whatever is done with it: the sampler's answer is
          // the same fact whether the table acts on it or only the trace reports it.
          const profile::remap::Source* held = HeldSource(input, gates, table, action);
          const bool down = held != nullptr;
          bool& last = trace_held[Index(kind, action)];
          if (trace && down != last) {
            lines.push_back({kind, action, down, active,
                             down ? profile::remap::FormatSource(*held) : "-"});
          }
          last = down;
          ActionHold& hold = gameplay_holds[Index(kind, action)];
          if (!active) {
            // A switched-off table (or a menu): nothing to fire, and nothing to release.
            if (!reset) hold.Invalidate();
            continue;
          }
          hold.Observe(down);
        }
      }
      // Plan D5's action boundary: the two tables are unioned per action, so a pad binding and a
      // key binding on the same note hold it down until both release, and a press that follows a
      // release of every contributing source is a new press.
      for (const profile::gameplay_remap::Action action : profile::gameplay_remap::Actions()) {
        const bool controller_held =
            gameplay_holds[Index(profile::gameplay_remap::TableKind::kController, action)].held;
        const bool keyboard_mouse_held =
            gameplay_holds[Index(profile::gameplay_remap::TableKind::kKeyboardMouse, action)].held;
        poll.held[static_cast<std::size_t>(action)] = controller_held || keyboard_mouse_held;
      }
      poll.gameplay = gameplay;
      poll.controller_active =
          TableActive(routing, profile::gameplay_remap::TableKind::kController, gameplay);
      poll.keyboard_mouse_active =
          TableActive(routing, profile::gameplay_remap::TableKind::kKeyboardMouse, gameplay);
      poll.controller_buttons = controller_buttons;
    }
    // Logged outside the lock: a trace line is a formatted string and a write, and no other poll
    // should wait on it.
    for (const TraceLine& line : lines) {
      REXLOG_INFO(
          "input source: gameplay={} table={} action={} {} active={} source={}",
          gameplay ? "on" : "off", profile::gameplay_remap::TableKindSection(line.kind),
          profile::gameplay_remap::ActionName(line.action), line.down ? "down" : "up",
          line.active ? "on" : "off (table inactive)", line.source);
    }
    // P3 publishes the poll and gets back the overlay for the pad state of this same poll. The
    // dispatcher queues the edges and the guest seam (src/hooks/gameplay_input.cpp) consumes
    // them; nothing here calls into the guest.
    return gameplay_input::PublishPoll(poll);
  }

  static std::size_t Index(profile::gameplay_remap::TableKind kind,
                           profile::gameplay_remap::Action action) {
    return static_cast<std::size_t>(kind) * profile::gameplay_remap::kActionCount +
           static_cast<std::size_t>(action);
  }
};

// General Mapping's `key:`/`mouse:` sources: the device state comes from the same physical
// snapshot the gameplay tables read, and the context rule from the source-gate module. A pad
// source is answered by the rewrite itself from the pad state it was handed.
struct DownView {
  PhysicalInput physical;
  SourceGates gates;
  bool gameplay = false;
  // The key/mouse sources an active gameplay table is holding this poll. General Mapping must not
  // also act on one: measured 2026-10-20 (P2), the Escape that paused was still held when the
  // pause menu became the current context, and General Mapping's Back then resumed the game.
  const std::vector<Source>* consumed = nullptr;
};

bool SourceDownThunk(void* context, const Source& source) {
  const auto* view = static_cast<const DownView*>(context);
  if (source.kind == SourceKind::kPad) {
    return false;
  }
  if (view->consumed != nullptr &&
      std::find(view->consumed->begin(), view->consumed->end(), source) != view->consumed->end()) {
    return false;
  }
  if (!GeneralSourceAllowed(view->gates, view->gameplay, source.kind)) {
    return false;
  }
  return SourceHeld(view->physical, view->gates, source);
}

// P8: the recovery watch (remap.h). One state machine, observed from both the state filter and
// the settings overlay's per-frame draw, so the observation also happens in the one state where
// the filter never runs (no device enumerates, so `GetState` answers "not connected" before the
// filter). The mutex is all the two call sites share; nothing is held across the commit.
struct RecoveryWatch {
  std::mutex mutex;
  RecoveryHold hold;
  bool needed = false;
  std::chrono::steady_clock::time_point confirmed_until{};
};
RecoveryWatch g_recovery;

// How long the caption keeps saying that the keyboard was reopened. It outlasts the key's own
// release by a comfortable margin, so the confirmation is read after the hold ended.
constexpr std::chrono::seconds kRecoveryConfirmFor{8};

// The display spelling of `kRecoveryKey`; the sentence a player reads, so it is not the profile's
// lowercase name.
constexpr char kRecoveryKeyLabel[] = "F10";

}  // namespace

bool KeyboardSupportEnabled() { return REXCVAR_GET(keyboard_support); }

void InvalidateGameplayInput() {
  g_gameplay_boundary.fetch_add(1, std::memory_order_relaxed);
  // P3 (plan D5): the dispatcher drops what it queued and releases what the guest already holds
  // on its next frame, so a press or release from the old context can never arrive late - and a
  // held track action cannot keep repeating across a pause, a screen change or a focus loss.
  gameplay_input::NoteBoundary();
}

void TickInputRecovery() {
  const SourceGates gates{KeyboardSupportEnabled(), MouseSupportEnabled()};
  // The normal path costs two cvar reads and nothing else: the pad is sampled only while both
  // gates are closed, which is the one configuration that can need recovery.
  const bool needed =
      (!gates.keyboard && !gates.mouse) ? RecoveryNeeded(gates, SamplePhysicalPad(nullptr)) : false;
  // The recovery key is read from the raw latch, never through a gated source evaluation: the
  // gate this path exists to reopen is the one that would otherwise hide the key. Focus is part
  // of it because the latch is fed by window key events - an unfocused window hears nothing.
  const bool key_down = needed && HostInputFocused() && HostKeyDown(kRecoveryKey);
  bool fire = false;
  bool announce = false;
  {
    std::lock_guard lock(g_recovery.mutex);
    announce = needed && !g_recovery.needed;
    g_recovery.needed = needed;
    fire = g_recovery.hold.Observe(needed, key_down, std::chrono::steady_clock::now());
  }
  if (announce) {
    REXLOG_WARN(
        "input recovery: no usable input - Keyboard and Mouse support are both off and no pad is "
        "connected; hold {} for {} ms to reopen the keyboard",
        kRecoveryKeyLabel, static_cast<long long>(kRecoveryHold.count()));
  }
  if (!fire) {
    return;
  }
  std::string why;
  const RecoveryOutcome outcome = RecoverKeyboard(settings::InputBindings(), &why);
  if (outcome == RecoveryOutcome::kRearmed) {
    {
      std::lock_guard lock(g_recovery.mutex);
      g_recovery.confirmed_until = std::chrono::steady_clock::now() + kRecoveryConfirmFor;
    }
    REXLOG_INFO(
        "input recovery: {} held for {} ms with no usable input; keyboard_support is on again "
        "(one switch saved through the Controls store; nothing else was changed)",
        kRecoveryKeyLabel, static_cast<long long>(kRecoveryHold.count()));
  } else {
    // A locked switch (command line or environment) or an unwritable profile. Logged, never
    // reported as success, and the notice keeps asking for the hold once the key is released.
    REXLOG_WARN("input recovery: {} held, but the keyboard gate was not reopened: {}",
                kRecoveryKeyLabel, why);
  }
}

std::string RecoveryNotice() {
  std::lock_guard lock(g_recovery.mutex);
  if (g_recovery.needed) {
    std::string text = "No input is available: Keyboard and Mouse support are both off and no ";
    text += "controller is connected. Hold ";
    text += kRecoveryKeyLabel;
    text += " for ";
    text += std::to_string(std::chrono::duration_cast<std::chrono::seconds>(kRecoveryHold).count());
    text += " seconds to turn Keyboard Support back on.";
    return text;
  }
  if (std::chrono::steady_clock::now() < g_recovery.confirmed_until) {
    return "Keyboard Support is back on. Help & Options > Controls > Input Types has the other "
           "switches.";
  }
  return {};
}

GameplaySwitches ReadGameplaySwitches() {
  GameplaySwitches switches;
  switches.keyboard_support = REXCVAR_GET(keyboard_support);
  switches.mouse_support = MouseSupportEnabled();
  return switches;
}

void InstallPadRemap(rex::RuntimeConfig& config) {
  // Tool mode has no window, no pad and no guest.
  if (config.tool_mode) {
    return;
  }

  // P7: the three binding tables come from the one published snapshot
  // (src/input/effective_input.h). A poll copies the pointer once and uses that copy for its
  // whole length, so a commit that lands between two polls is observed whole or not at all.
  // The initial snapshot is published by the boot's Controls store before the input system is
  // created; a tool or a test sees compiled defaults.
  auto context = std::make_shared<RemapContext>();

  std::function<std::unique_ptr<rex::system::IInputSystem>(bool)> backend;
  if (config.input_factory) {
    backend = std::move(config.input_factory);
  } else {
    // The title's own default system (src/input/title_input.h): the SDK's drivers with the MnK
    // emulation behind the keyboard gate, and the physical drivers remembered for sampling.
    backend = REX_INPUT_BACKEND(rb_blitz::input::CreateTitleInputSystem);
  }

  config.input_factory = [backend = std::move(backend), context = std::move(context)](
                            bool tool_mode) {
    std::unique_ptr<rex::system::IInputSystem> system = backend(tool_mode);
    if (tool_mode || !system) {
      return system;
    }
    // The same cast mouse_ui makes, and for the same reason: this is the one input system the
    // SDK and this project ship, and the filter is the seam the SDK grew for exactly this.
    static_cast<rex::input::InputSystem*>(system.get())
        ->SetStateFilter([context](uint32_t user_index,
                                             rex::input::X_INPUT_STATE& state) {
          namespace remap = rb_blitz::profile::remap;
          // P8: the Controls recovery path observes one host-wide key once per poll (plan
          // §17.2). It is also observed by the settings overlay, because this filter does not
          // run at all when no device enumerates - see remap.h.
          const bool system_ui = rex::kernel::xam::xeXamIsUIActive();
          if (IsGameplayUser(user_index) && !system_ui) {
            TickInputRecovery();
          }
          // P7: one snapshot for the whole poll; see InstallPadRemap.
          const std::shared_ptr<const EffectiveInput> config = CurrentInput();
          const bool has_table = config->profile_present;
          // D2: both gates are read at poll time, so config/env/CLI precedence and a runtime
          // change are observed by every consumer from one place.
          const GameplaySwitches switches = ReadGameplaySwitches();
          const SourceGates gates{switches.keyboard_support, switches.mouse_support};
          const bool gameplay = GameScreenActive();
          // D3/D4 of the restructure plan: the Mouse & Keyboard table is always active in a song
          // and the Controller table acts only once it has a custom row. With no custom row the
          // native preset keeps driving the pad, which is what "gameplay mapping is always on"
          // means for a controller.
          const GameplayRouting routing{config->controller.Custom(), true};

          // One physical snapshot per poll, shared by every evaluator (D5): the rewrite's
          // key/mouse sources and both gameplay tables read the same raw devices, so they can
          // never disagree about what is held.
          const PhysicalInput physical = SamplePhysicalInput();
          static thread_local std::array<ModalInputGate, 4> modal_gates;
          const auto& raw = state.gamepad;
          const bool held = HostAnyKeyDown() || physical.mouse_buttons ||
                            raw.buttons || raw.left_trigger > 30 || raw.right_trigger > 30 ||
                            std::abs(int(raw.thumb_lx)) > 8000 ||
                            std::abs(int(raw.thumb_ly)) > 8000 ||
                            std::abs(int(raw.thumb_rx)) > 8000 ||
                            std::abs(int(raw.thumb_ry)) > 8000;
          const bool modal_blocked = user_index < modal_gates.size()
              ? modal_gates[user_index].Block(system_ui, held) : system_ui;
          if (modal_blocked && IsGameplayUser(user_index)) InvalidateGameplayInput();
          // The sources a direct table is holding right now. General Mapping is rewritten below,
          // before the gameplay tables are evaluated, so the collector runs here: a press that
          // belongs to a gameplay action must not also become a General Mapping target when the
          // context flips mid-hold (the Escape that pauses vs Interface Mapping's Back).
          //
          // The latch is per input thread and per poll: `CollectGameplayConsumed` keeps a source
          // taken until it is released, so a menu that opens between the key's down and its up
          // (the pause panel, measured 22 ms apart) cannot receive the same key as a menu row.
          static thread_local std::vector<Source> gameplay_consumed;
          std::vector<Source> consumed;
          CollectGameplayConsumed(physical, gates, routing, gameplay, config->controller,
                                  config->keyboard_mouse, &gameplay_consumed, &consumed);
          // P7: the listening dialog. The raw frame is consumed every poll so the key serial stays
          // current; while a capture is armed the guest is handed a neutral pad below, which is
          // what keeps the candidate press from navigating the menu it is being bound in. The
          // capture sees the devices ungated, so a binding can be prepared for a disabled device.
          static thread_local uint64_t key_serial = 0;
          std::vector<std::string> new_keys;
          HostKeyEventsSince(&key_serial, &new_keys);
          const bool capture_armed = capture::Armed() && !modal_blocked;
          if (capture_armed) {
            capture::Observation observation;
            observation.focused = physical.window_focused;
            observation.any_key_down = HostAnyKeyDown();
            observation.new_keys = std::move(new_keys);
            observation.mouse_buttons = physical.mouse_buttons;
            observation.wheel_detents = physical.wheel_detents;
            observation.pad = physical.pad;
            const capture::Tick tick = capture::Observe(observation);
            if (tick.captured.has_value()) {
              settings::InputBindingStore* store = settings::InputBindings();
              std::string why;
              if (store != nullptr &&
                  store->CompleteCapture(tick.captured->token, tick.captured->source, &why)) {
                // Leave the dialog the way a player's B would: a real pad bit for two polls.
                PulseHostPad(0x2000);
              } else {
                capture::PostStatusMessage("Could not save: " +
                                           (why.empty() ? std::string("the profile was refused")
                                                        : why));
              }
            } else if (tick.leave) {
              // The 3-second countdown expired (D11). The row is untouched because nothing was
              // ever staged, and the dialog leaves the same way a capture does.
              PulseHostPad(0x2000);
            }
          }
          // Music actions own physical sources before General Mapping can turn
          // those same inputs into a guest control. Sampling runs on every poll.
          std::vector<Source> music_consumed;
          if (user_index == 0) {
            static thread_local menu_music_nav::BindingLatch music_latch;
            const auto snapshot = shell_music::NowPlaying().ReadSnapshot();
            const bool active = REXCVAR_GET(enhancements_background_music_nav) &&
                                snapshot.available && MainMenuMusicInputAvailable() &&
                                physical.window_focused && !capture_armed && !modal_blocked;
            if (auto press = music_latch.Update(config->music, physical.pad,
                    [](std::string_view key) { return HostKeyDown(key); }, gates.keyboard,
                    active, snapshot.playback.capabilities.play_pause, &music_consumed)) {
              const auto result = menu_music_nav::Post(press->command, press->producer, snapshot.epoch);
              REXLOG_INFO("music_nav: input command {} status {}", int(press->command), int(result.status));
            }
            consumed.insert(consumed.end(), music_consumed.begin(), music_consumed.end());
          }
          // General Mapping's input pad. It is the merged state - the physical pads plus the
          // SDK's legacy MnK emulation when `mnk_mode` is on, which is how every acceptance
          // harness drives the menus - except while a gameplay table routes input: then the
          // guest sees the *physical* pad only, because the MnK emulation is a second keyboard
          // path into the native preset and General Mapping's key/mouse sources stop at the menu
          // while a direct table is on (R4). A `pad:` gameplay source reads the physical pad in
          // either case (src/input/source_gates.h). With Keyboard Support off the MnK emulation
          // stands down in every context (P7's live gate), so the physical pad is what the guest
          // may see - a synthesized bit is a keyboard path whether a menu or a song is current.
          const remap::PadState merged{state.gamepad.buttons,     state.gamepad.left_trigger,
                                      state.gamepad.right_trigger, state.gamepad.thumb_lx,
                                      state.gamepad.thumb_ly,      state.gamepad.thumb_rx,
                                      state.gamepad.thumb_ry};
          const bool direct_routing = gameplay;
          const bool music_key_owned = std::any_of(music_consumed.begin(), music_consumed.end(),
              [](const Source& source) { return source.kind == SourceKind::kKey; });
          remap::PadState pad =
              (direct_routing || !gates.keyboard || music_key_owned) ? physical.pad : merged;
          for (const auto& source : music_consumed) if (source.kind == SourceKind::kPad) {
            if (auto target = remap::ParseTarget(source.name)) {
              pad.buttons &= ~remap::TargetBit(*target);
              if (*target == remap::Target::kLeftTrigger) pad.left_trigger = 0;
              if (*target == remap::Target::kRightTrigger) pad.right_trigger = 0;
            }
          }
          remap::PadState out;
          if (has_table) {
            DownView view;
            view.physical = physical;
            view.gates = gates;
            view.gameplay = gameplay;
            view.consumed = &consumed;
            remap::Apply(config->general, pad, &SourceDownThunk, &view, &out);
          } else {
            // No profile to read: the pad is left exactly as the SDK reports it, and the one
            // thing that still happens is the prohibition. Guide therefore cannot reach the guest
            // on this path either, rather than only on the paths that go through a profile.
            out = remap::WithoutProhibitedButtons(pad);
          }
          state.gamepad.buttons = out.buttons;
          // An unbound trigger keeps its analog travel: Apply copies the state it was handed
          // before it rewrites anything, so only a bound one is driven to an end.
          state.gamepad.left_trigger = out.left_trigger;
          state.gamepad.right_trigger = out.right_trigger;
          // The axes are the *other* half of the MnK gate, and forgetting them was a measured
          // leak (P3, 2026-10-18): the legacy emulation synthesizes stick travel from WASD as
          // well as buttons, and the title derives its own stick-direction buttons (kPad 16-23)
          // from the axes, so a held `D` produced a native lane0 press in a song while every
          // button bit was already being cleared. `Apply` copies the axes it was handed, so
          // writing them back is the identity when the merged state was used and the physical
          // axes when a gameplay table routes.
          state.gamepad.thumb_lx = out.left_x;
          state.gamepad.thumb_ly = out.left_y;
          state.gamepad.thumb_rx = out.right_x;
          state.gamepad.thumb_ry = out.right_y;
          // Fixed menu actions bypass user pad remaps. The synthetic driver keeps
          // a controller slot connected, but contributes no buttons before this filter.
          // Wheel detents travel as D-pad presses (1/2/4/8), not stick travel: the
          // guest treats each press as one discrete step, where a two-frame stick
          // pulse depends on its own analog threshold/repeat handling that can drop
          // or multiply steps unpredictably.
          if (user_index == 0) {
            const auto mouse = SampleMenuMousePadState();
            if (!capture_armed) {
              state.gamepad.buttons = uint16_t(state.gamepad.buttons) | (mouse & 0x300F);
            }
            // P7: the host's own Back pulse (the listening dialog leaving itself). It follows the
            // same delivery as a player press and is never gated by Mouse Support.
            state.gamepad.buttons = uint16_t(state.gamepad.buttons) | TakeHostPadPulse();
          }
          // A capture owns raw input until it completes or is cancelled: the guest sees a neutral
          // pad and no gameplay action, so the press being bound cannot navigate, pause, or strike
          // a note (plan §14.3).
          if (capture_armed || modal_blocked) {
            state.gamepad.buttons = 0;
            state.gamepad.left_trigger = 0;
            state.gamepad.right_trigger = 0;
            state.gamepad.thumb_lx = 0;
            state.gamepad.thumb_ly = 0;
            state.gamepad.thumb_rx = 0;
            state.gamepad.thumb_ry = 0;
          }
          // P3: the gameplay tables' source state, edges and pad overlay, for player one only.
          // Host keyboard and mouse actions belong to that slot; users 1..3 are not polled with
          // anything attached and must not receive a copy of player one's input. The overlay is
          // applied here - after General Mapping and after the native pointer's pulses - so it is
          // the last word on the two controls that cross the native gameplay boundary: the
          // Controller table's own buttons are removed while it routes, and Start carries the
          // direct Pause action (plan D3/D4). While a capture is armed the overlay is skipped: the
          // candidate press is the dialog's input, not the song's.
          if (IsGameplayUser(user_index) && !capture_armed && !modal_blocked) {
            const uint16_t controller_buttons = ControllerOwnedButtons(config->controller);
            const gameplay_input::PadOverlay overlay = context->ObserveGameplay(
                physical, gates, routing, gameplay, REXCVAR_GET(input_source_trace), *config,
                controller_buttons);
            state.gamepad.buttons =
                uint16_t((uint16_t(state.gamepad.buttons) & ~overlay.clear_buttons) |
                         overlay.set_buttons);
          }
          // Publish the identity of the state actually delivered, after both
          // remapping and native mouse input. Taking max(device packets) can
          // leave this unchanged even though a mouse button changed.
          if (user_index < context->packets.size()) {
            const auto& pad = state.gamepad;
            std::lock_guard lock(context->packet_mutex);
            state.packet_number = context->packets[user_index].Observe(
                {pad.buttons, pad.left_trigger, pad.right_trigger,
                 pad.thumb_lx, pad.thumb_ly, pad.thumb_rx, pad.thumb_ry});
          }
        });
    // P1's evidence line (plan §6 P1): the effective device gates and their sources. The binding
    // tables and the controller preset are logged by the Controls store when it is created (P7),
    // because the store is the one owner of the tables and this point runs before it.
    const GameplaySwitches switches = ReadGameplaySwitches();
    REXLOG_INFO("gameplay mapping: keyboard_support={} ({}) mouse_ui_nav={} ({})",
                switches.keyboard_support ? "on" : "off",
                CvarSourceName(rex::cvar::GetFlagSource("keyboard_support")),
                switches.mouse_support ? "on" : "off",
                CvarSourceName(rex::cvar::GetFlagSource("mouse_ui_nav")));
    REXLOG_INFO(
        "gameplay mapping: routing is always on (restructure plan D3) - the Mouse & Keyboard "
        "table drives gameplay and the Controller table acts once it has a custom row; with no "
        "custom row the title's native preset drives the pad (D4). The old "
        "gameplay_mapping_controller/gameplay_mapping_keyboard_mouse switches are retired and "
        "decide nothing.");
    // P2's provenance line: where each source kind is read from. `pad` is the physical drivers
    // (never the merged state), key/mouse are SDL's own raw state, and the mouse is gameplay-only
    // because the native pointer owns it in menus.
    REXLOG_INFO(
        "input sources: pad=physical-drivers-only keyboard=SDL-state mouse=SDL-state "
        "(gameplay only) focus=required; MnK legacy={}; source trace={} ({})",
        rex::cvar::GetFlagByName("mnk_mode") == "true"
            ? (switches.keyboard_support ? "on (merged into the guest pad, never a `pad:` gameplay "
                                           "source)"
                                         : "installed but standing down: keyboard_support is off, "
                                           "so none of its state is delivered")
            : "off",
        REXCVAR_GET(input_source_trace) ? "on" : "off",
        CvarSourceName(rex::cvar::GetFlagSource("input_source_trace")));
    return system;
  };
}

}  // namespace rb_blitz::input
