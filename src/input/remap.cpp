// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the pad remap (src/input/remap.h). See src/profile/remap.h for what a
// binding means; this file is only the device side of it.

#include "input/remap.h"
#include "input/native_menu.h"
#include "input/pad_packet.h"

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

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

// The mouse buttons the grammar names, with SDL's mask for each.
struct MouseButton {
  const char* name;
  uint32_t mask;
};

constexpr MouseButton kMouseButtons[] = {
    {"left", SDL_BUTTON_MASK(SDL_BUTTON_LEFT)},
    {"right", SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)},
    {"middle", SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)},
    {"x1", SDL_BUTTON_MASK(SDL_BUTTON_X1)},
    {"x2", SDL_BUTTON_MASK(SDL_BUTTON_X2)},
};

// Everything a source resolves against, kept alive by the filter that holds it.
struct RemapContext {
  rb_blitz::profile::remap::Table table;
  // The two gameplay-mapping tables (src/profile/gameplay_remap.h). Loaded here, with the same
  // profile read as `[remap]`, so every consumer sees one configuration; neither is consulted by
  // the rewrite yet - P2 reads them for the source gates and P3 for the direct dispatch. Their
  // kinds are fixed at construction, because a table with no profile rows still answers from its
  // own compiled defaults.
  rb_blitz::profile::gameplay_remap::Table controller_actions{
      rb_blitz::profile::gameplay_remap::TableKind::kController};
  rb_blitz::profile::gameplay_remap::Table keyboard_mouse_actions{
      rb_blitz::profile::gameplay_remap::TableKind::kKeyboardMouse};
  std::mutex packet_mutex;
  std::array<PadPacket, 4> packets;

  bool SourceDown(const Source& source) const {
    switch (source.kind) {
      case SourceKind::kPad:
        // A pad source is answered from the pad's own state inside the rewrite, so it never
        // reaches here.
        return false;
      case SourceKind::kKey: {
        int count = 0;
        const bool* keys = SDL_GetKeyboardState(&count);
        if (keys == nullptr) {
          return false;
        }
        // Resolved per call rather than cached: a table names a handful of keys and this is a
        // walk of SDL's own static table, which is nothing next to a frame.
        const SDL_Scancode scancode = SDL_GetScancodeFromName(source.name.c_str());
        if (scancode == SDL_SCANCODE_UNKNOWN || scancode >= count) {
          return false;
        }
        return keys[scancode];
      }
      case SourceKind::kMouse:
        if (MouseSupportEnabled()) return false;
        for (const MouseButton& button : kMouseButtons) {
          if (source.name == button.name) {
            return (SDL_GetMouseState(nullptr, nullptr) & button.mask) != 0;
          }
        }
        return false;
    }
    return false;
  }
};

bool SourceDownThunk(void* context, const Source& source) {
  return static_cast<const RemapContext*>(context)->SourceDown(source);
}

}  // namespace

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

  std::function<std::unique_ptr<rex::system::IInputSystem>(bool)> backend;
  if (config.input_factory) {
    backend = std::move(config.input_factory);
  } else {
    backend = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
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
          const remap::PadState pad{state.gamepad.buttons,     state.gamepad.left_trigger,
                                    state.gamepad.right_trigger, state.gamepad.thumb_lx,
                                    state.gamepad.thumb_ly,      state.gamepad.thumb_rx,
                                    state.gamepad.thumb_ry};
          remap::PadState out;
          if (has_table) {
            remap::Apply(context->table, pad, &SourceDownThunk, context.get(), &out);
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
    // gameplay table would run. Nothing consumes the switches yet, so this is the one place a
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
