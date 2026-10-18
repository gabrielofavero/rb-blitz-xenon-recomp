// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the pad remap (src/input/remap.h). See src/profile/remap.h for what a
// binding means; this file is only the device side of it.

#include "input/remap.h"
#include "hooks/gameplay_input.h"
#include "input/native_menu.h"
#include "input/pad_packet.h"
#include "input/source_gates.h"
#include "input/title_input.h"

#include <array>
#include <atomic>
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
#include <rex/logging.h>

#include "profile/profile.h"
#include "profile/gameplay_remap.h"
#include "profile/profile_apply.h"
#include "profile/remap.h"

// P1 of docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md: the switches D2/D6
// define. Controller Settings writes them through its profile (the allowlist in
// src/settings/cvars.h), so a boot nobody launched through Controller Settings has to read them
// from here; the compiled defaults match the schema rows' own defaults.
REXCVAR_DEFINE_BOOL(keyboard_support, true, "Input",
                    "Keyboard support: while on, the title's keyboard rows (General Mapping's key "
                    "sources and the Keyboard + Mouse gameplay table) drive the game. Off blocks "
                    "that device; Controller Settings' own keys are unaffected. Needs a restart.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(gameplay_mapping_controller, false, "Input",
                    "Controller Gameplay Mapping: on, the Controller binding table drives the "
                    "six gameplay actions directly and the saved native preset is bypassed. "
                    "Off, the saved preset plays exactly as before. Needs a restart.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(gameplay_mapping_keyboard_mouse, true, "Input",
                    "Keyboard + Mouse Gameplay Mapping: on, that binding table drives the six "
                    "gameplay actions directly while the native controller preset stays "
                    "authoritative for a pad. Off, keys and mouse use General Mapping. Needs a "
                    "restart.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

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
    input.key_down = [](std::string_view name) { return HostKeyDown(name); };
  }
  return input;
}

// The boundary counter `InvalidateGameplayInput` bumps. The poll that sees a new value drops the
// held state for both gameplay tables instead of releasing into the guest late.
std::atomic<uint32_t> g_gameplay_boundary{0};

// Plan §6's sequencing guard. P2 held both gameplay switches' routing off while nothing
// consumed them; P3's dispatch exists (src/hooks/gameplay_input.cpp), so the switches now select
// the routing their names describe. The constant stays as the one place a build could hold the
// direct tables off again without deleting the code path - and the boot line below names which
// side of it the build is on.
constexpr bool kGameplayDispatchReady = true;

GameplaySwitches EffectiveGameplayRouting(GameplaySwitches switches) {
  if (!kGameplayDispatchReady) {
    switches.controller_mapping = false;
    switches.keyboard_mouse_mapping = false;
  }
  return switches;
}

// Everything a source resolves against, kept alive by the filter that holds it.
struct RemapContext {
  rb_blitz::profile::remap::Table table;
  // The two gameplay-mapping tables (src/profile/gameplay_remap.h). Loaded here, with the same
  // profile read as `[remap]`, so every consumer sees one configuration; neither is dispatched by
  // the rewrite - P2 evaluates them for the source gates and the edges, P3 dispatches. Their
  // kinds are fixed at construction, because a table with no profile rows still answers from its
  // own compiled defaults.
  rb_blitz::profile::gameplay_remap::Table controller_actions{
      rb_blitz::profile::gameplay_remap::TableKind::kController};
  rb_blitz::profile::gameplay_remap::Table keyboard_mouse_actions{
      rb_blitz::profile::gameplay_remap::TableKind::kKeyboardMouse};
  // The pad controls the Controller table's sources name (P3, plan D4): while that table is
  // active they are taken out of the state the guest reads, because the preset they would feed is
  // bypassed. Fixed at load - a table is not edited while a game runs (D7).
  uint16_t controller_buttons = 0;
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
                                             const GameplaySwitches& effective, bool gameplay,
                                             bool trace) {
    const GameplayRouting routing{effective.controller_mapping,
                                  effective.keyboard_mouse_mapping};
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
            kind == profile::gameplay_remap::TableKind::kController ? controller_actions
                                                                   : keyboard_mouse_actions;
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
  bool keyboard_mouse_mapping = true;
  bool controller_mapping = false;
};

bool SourceDownThunk(void* context, const Source& source) {
  const auto* view = static_cast<const DownView*>(context);
  if (source.kind == SourceKind::kPad) {
    return false;
  }
  if (!GeneralSourceAllowed(view->gates, view->gameplay, view->keyboard_mouse_mapping,
                            view->controller_mapping, source.kind)) {
    return false;
  }
  return SourceHeld(view->physical, view->gates, source);
}

}  // namespace

bool KeyboardSupportEnabled() { return REXCVAR_GET(keyboard_support); }

void InvalidateGameplayInput() {
  g_gameplay_boundary.fetch_add(1, std::memory_order_relaxed);
  // P3 (plan D5): the dispatcher drops what it queued and releases what the guest already holds
  // on its next frame, so a press or release from the old context can never arrive late - and a
  // held track action cannot keep repeating across a pause, a screen change or a focus loss.
  gameplay_input::NoteBoundary();
}

GameplaySwitches ReadGameplaySwitches() {
  GameplaySwitches switches;
  switches.keyboard_support = REXCVAR_GET(keyboard_support);
  switches.mouse_support = MouseSupportEnabled();
  switches.controller_mapping = REXCVAR_GET(gameplay_mapping_controller);
  switches.keyboard_mouse_mapping = REXCVAR_GET(gameplay_mapping_keyboard_mouse);
  return switches;
}

void InstallPadRemap(rex::RuntimeConfig& config) {
  // Tool mode has no window, no pad and no guest.
  if (config.tool_mode) {
    return;
  }

  // The same file Controller Settings' own Launch Game hands over with --launcher_profile, read the
  // same way the profile's `[settings]` rows are (controller-settings/profile_apply.h). A profile this build
  // cannot read - or a machine that has never run Controller Settings - is not a reason to refuse to
  // start and not a reason to install no filter at all: `has_table` is false and the filter's
  // whole job is then the Guide prohibition below. Controller Settings is where a user is told about a
  // file that did not parse.
  std::string unreadable;
  const std::optional<rb_blitz::profile::Profile> profile =
      rb_blitz::profile::ReadProfile(&unreadable);
  if (!profile && !unreadable.empty()) {
    REXLOG_WARN("remap: ignoring the profile, {}", unreadable);
  }

  // Held by the filter, which the input system copies: this frame's table is not what it reads
  // from.
  auto context = std::make_shared<RemapContext>();
  // The two gameplay tables are read whether or not a profile exists: an absent profile means the
  // compiled defaults, which is a complete scheme (D6: a fresh install can play without opening
  // Controller Settings).
  namespace gameplay = rb_blitz::profile::gameplay_remap;
  const bool has_table = profile.has_value();
  std::size_t bound = 0;
  if (profile) {
    context->table = rb_blitz::profile::remap::Table::FromRows(profile->remap);
    context->controller_actions =
        gameplay::Table::FromRows(gameplay::TableKind::kController,
                                  profile->gameplay_remap_controller);
    context->keyboard_mouse_actions =
        gameplay::Table::FromRows(gameplay::TableKind::kKeyboardMouse,
                                  profile->gameplay_remap_keyboard_mouse);
    // Guide is a platform button and is never delivered to the guest (contracts §2.3). A row a
    // person wrote once is preserved by a save but never applied, so the only honest thing to do
    // with it is to say so - "the button I bound does nothing" is otherwise indistinguishable
    // from a bug in the remap.
    if (!rb_blitz::profile::remap::RefusedRowNames(profile->remap).empty()) {
      REXLOG_WARN(
          "remap: Guide is not remappable, so the profile's guide row is ignored (and Guide is "
          "kept out of the pad state the guest sees)");
    }
    // The table always has work to do, even with no `[remap]` rows: every control without a row
    // has Controller Settings' default binding - its own pad control plus a keyboard key - which is how
    // a keyboard works without the profile being edited. `BoundCount` is how many the *user*
    // rebound, which is what the log names.
    if (context->table.BoundCount() > 0 && !context->table.AnySourceSet()) {
      // A table of empty bindings is a pad that does nothing, which is a real thing to want and a
      // terrible thing to hit by accident. It is honoured, and said out loud.
      REXLOG_WARN(
          "remap: the profile binds {} control(s) to nothing, so no pad button will "
          "reach the game",
          context->table.BoundCount());
    }
    bound = context->table.BoundCount();
  }
  // P3: the pad controls the Controller table owns. While that table routes gameplay input the
  // preset is bypassed, so these bits are removed from the state the guest reads and only the
  // direct dispatch can produce their actions. Computed with no profile too: the compiled defaults
  // own the same controls the native preset 0 does, so enabling the Controller table without ever
  // opening Controller Settings works as promised.
  context->controller_buttons = ControllerOwnedButtons(context->controller_actions);

  std::function<std::unique_ptr<rex::system::IInputSystem>(bool)> backend;
  if (config.input_factory) {
    backend = std::move(config.input_factory);
  } else {
    // The title's own default system (src/input/title_input.h): the SDK's drivers with the MnK
    // emulation behind the keyboard gate, and the physical drivers remembered for sampling.
    backend = REX_INPUT_BACKEND(rb_blitz::input::CreateTitleInputSystem);
  }

  config.input_factory = [backend = std::move(backend), context = std::move(context), has_table,
                          bound](bool tool_mode) {
    std::unique_ptr<rex::system::IInputSystem> system = backend(tool_mode);
    if (tool_mode || !system) {
      return system;
    }
    // The same cast mouse_ui makes, and for the same reason: this is the one input system the
    // SDK and this project ship, and the filter is the seam the SDK grew for exactly this.
    static_cast<rex::input::InputSystem*>(system.get())
        ->SetStateFilter([context, has_table](uint32_t user_index,
                                             rex::input::X_INPUT_STATE& state) {
          namespace remap = rb_blitz::profile::remap;
          // D2: both gates are read at poll time, so config/env/CLI precedence and a runtime
          // change are observed by every consumer from one place.
          const GameplaySwitches requested = ReadGameplaySwitches();
          // P3: the switches now select the routing their names describe. The sequencing-guard
          // constant is the one place a build can hold them off again.
          const GameplaySwitches effective = EffectiveGameplayRouting(requested);
          const SourceGates gates{requested.keyboard_support, requested.mouse_support};
          const bool gameplay = GameScreenActive();

          // One physical snapshot per poll, shared by every evaluator (D5): the rewrite's
          // key/mouse sources and both gameplay tables read the same raw devices, so they can
          // never disagree about what is held.
          const PhysicalInput physical = SamplePhysicalInput();
          // General Mapping's input pad. It is the merged state - the physical pads plus the
          // SDK's legacy MnK emulation when `mnk_mode` is on, which is how every acceptance
          // harness drives the menus - except while a gameplay table routes input: then the
          // guest sees the *physical* pad only, because the MnK emulation is a second keyboard
          // path into the native preset and General Mapping's key/mouse sources stop at the menu
          // while a direct table is on (R4). A `pad:` gameplay source reads the physical pad in
          // either case (src/input/source_gates.h).
          const remap::PadState merged{state.gamepad.buttons,     state.gamepad.left_trigger,
                                      state.gamepad.right_trigger, state.gamepad.thumb_lx,
                                      state.gamepad.thumb_ly,      state.gamepad.thumb_rx,
                                      state.gamepad.thumb_ry};
          const bool direct_routing =
              gameplay && (effective.controller_mapping || effective.keyboard_mouse_mapping);
          const remap::PadState pad = direct_routing ? physical.pad : merged;
          remap::PadState out;
          if (has_table) {
            DownView view;
            view.physical = physical;
            view.gates = gates;
            view.gameplay = gameplay;
            view.keyboard_mouse_mapping = effective.keyboard_mouse_mapping;
            view.controller_mapping = effective.controller_mapping;
            remap::Apply(context->table, pad, &SourceDownThunk, &view, &out);
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
            state.gamepad.buttons = uint16_t(state.gamepad.buttons) | (mouse & 0x300F);
          }
          // P3: the gameplay tables' source state, edges and pad overlay, for player one only.
          // Host keyboard and mouse actions belong to that slot; users 1..3 are not polled with
          // anything attached and must not receive a copy of player one's input. The overlay is
          // applied here - after General Mapping and after the native pointer's pulses - so it is
          // the last word on the two controls that cross the native gameplay boundary: the
          // Controller table's own buttons are removed while it routes, and Start carries the
          // direct Pause action (plan D3/D4).
          if (IsGameplayUser(user_index)) {
            const gameplay_input::PadOverlay overlay = context->ObserveGameplay(
                physical, gates, effective, gameplay, REXCVAR_GET(input_source_trace));
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
    if (has_table) {
      REXLOG_INFO(
          "remap: {} user rebind(s) from the profile; every other control answers to its "
          "default pad button and keyboard key (--launcher_profile <path> names another file)",
          bound);
    } else {
      REXLOG_INFO(
          "remap: no Controller Settings profile to read, so the pad keeps the SDK's own reporting and Guide "
          "is kept out of the state the guest sees");
    }

    // P1's evidence line (plan §6 P1): the effective switches and their sources, then what each
    // gameplay table would run. P2 reads the gates at poll time; this line remains the one place a
    // standalone boot names them - the same line a Controller Settings launch prints.
    const GameplaySwitches switches = ReadGameplaySwitches();
    REXLOG_INFO(
        "gameplay mapping: keyboard_support={} ({}) mouse_ui_nav={} ({}) "
        "gameplay_mapping_controller={} ({}) gameplay_mapping_keyboard_mouse={} ({})",
        switches.keyboard_support ? "on" : "off",
        CvarSourceName(rex::cvar::GetFlagSource("keyboard_support")),
        switches.mouse_support ? "on" : "off",
        CvarSourceName(rex::cvar::GetFlagSource("mouse_ui_nav")),
        switches.controller_mapping ? "on" : "off",
        CvarSourceName(rex::cvar::GetFlagSource("gameplay_mapping_controller")),
        switches.keyboard_mouse_mapping ? "on" : "off",
        CvarSourceName(rex::cvar::GetFlagSource("gameplay_mapping_keyboard_mouse")));
    if (!kGameplayDispatchReady) {
      // The one place a stored switch would not do what its name says: the direct dispatch is
      // held off in this build, so routing through the tables would stop input without
      // replacing it.
      REXLOG_INFO(
          "gameplay mapping: this build holds the direct dispatch off, so both gameplay "
          "switches are inert; General Mapping and the native preset behave as before (the "
          "stored values above are preserved)");
    } else {
      REXLOG_INFO(
          "gameplay mapping: direct dispatch on (plan P3) - a table whose switch is on drives "
          "the six actions; the Controller table also bypasses the native preset and owns the "
          "pad control(s) {:#06x} in the delivered state",
          context->controller_buttons);
    }
    // P2's provenance line: where each source kind is read from. `pad` is the physical drivers
    // (never the merged state), key/mouse are SDL's own raw state, and the mouse is gameplay-only
    // because the native pointer owns it in menus.
    REXLOG_INFO(
        "input sources: pad=physical-drivers-only keyboard=SDL-state mouse=SDL-state "
        "(gameplay only) focus=required; MnK legacy={}; source trace={} ({})",
        rex::cvar::GetFlagByName("mnk_mode") == "true"
            ? (switches.keyboard_support ? "on (merged into the guest pad, never a `pad:` gameplay "
                                           "source)"
                                         : "off (keyboard_support is off, so the driver is not "
                                           "installed)")
            : "off",
        REXCVAR_GET(input_source_trace) ? "on" : "off",
        CvarSourceName(rex::cvar::GetFlagSource("input_source_trace")));
    const auto& controller_actions = context->controller_actions;
    const auto& keyboard_mouse_actions = context->keyboard_mouse_actions;
    REXLOG_INFO(
        "gameplay mapping: [{}] {} user row(s), {} action(s) firing of {}; [{}] {} user row(s), "
        "{} action(s) firing of {}",
        gameplay::TableKindSection(gameplay::TableKind::kController),
        controller_actions.BoundCount(), controller_actions.EffectiveBoundCount(),
        gameplay::kActionCount,
        gameplay::TableKindSection(gameplay::TableKind::kKeyboardMouse),
        keyboard_mouse_actions.BoundCount(), keyboard_mouse_actions.EffectiveBoundCount(),
        gameplay::kActionCount);
    for (const gameplay::TableKind kind : gameplay::TableKinds()) {
      const gameplay::Table& table = kind == gameplay::TableKind::kController
                                       ? controller_actions
                                       : keyboard_mouse_actions;
      // A row this table keeps but will not apply: said out loud, because a binding that does
      // nothing is otherwise indistinguishable from a bug.
      for (const gameplay::Table::RejectedRow& rejected : table.Rejected()) {
        REXLOG_WARN("gameplay mapping: [{}] row '{} = {}' is kept and not applied: {}",
                    gameplay::TableKindSection(kind), rejected.row.key, rejected.row.value,
                    rejected.reason);
      }
      // The all-empty scheme, which is a real thing to want and a terrible thing to hit by
      // accident - the same warning the `[remap]` table gets above.
      if (table.BoundCount() > 0 && !table.AnySourceSet()) {
        REXLOG_WARN(
            "gameplay mapping: the profile binds every action in [{}] to nothing, so that "
            "table will not fire anything",
            gameplay::TableKindSection(kind));
      }
    }
    return system;
  };
}

}  // namespace rb_blitz::input
