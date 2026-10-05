// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the pad remap (src/input/remap.h). See src/launcher/remap.h for what a
// binding means; this file is only the device side of it.

#include "input/remap.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <SDL3/SDL.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>

#include "launcher/profile.h"
#include "launcher/profile_path.h"
#include "launcher/remap.h"

REXCVAR_DEFINE_STRING(launcher_profile, "", "Runtime",
                      "Path to the launcher's profile, when the launcher started this process; "
                      "empty resolves the file the launcher itself would write");

namespace rb_blitz::input {
namespace {

using rb_blitz::launcher::ProfileLoadResult;
using rb_blitz::launcher::remap::Source;
using rb_blitz::launcher::remap::SourceKind;

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
  rb_blitz::launcher::remap::Table table;

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

// The profile the launcher would have written, resolved the way the launcher resolves it: an
// explicit path, then the environment, then the settings-folder pointer, then the portable
// marker and the app data folder. The order is not restated here - the launcher's own resolver
// is what answers it.
std::optional<rb_blitz::launcher::Profile> ReadLauncherProfile() {
  rb_blitz::launcher::ProfilePathInputs inputs =
      rb_blitz::launcher::ProfilePathInputsFromEnvironment(rex::filesystem::GetExecutableFolder());
  inputs.command_line_value = REXCVAR_GET(launcher_profile);

  const std::filesystem::path path = rb_blitz::launcher::ResolveProfilePath(inputs);
  if (path.empty()) {
    return std::nullopt;
  }
  ProfileLoadResult loaded = rb_blitz::launcher::LoadProfile(path);
  if (!loaded.usable()) {
    // A profile this build cannot read is not a reason to refuse to start: the pad is left as
    // the SDK reports it, and the launcher is where the user is told about the file.
    REXLOG_WARN("remap: ignoring {}, which does not parse: {}", path.string(), loaded.error);
    return std::nullopt;
  }
  return std::move(loaded.profile);
}

}  // namespace

void InstallPadRemap(rex::RuntimeConfig& config) {
  // Tool mode has no window, no pad and no guest.
  if (config.tool_mode) {
    return;
  }

  const std::optional<rb_blitz::launcher::Profile> profile = ReadLauncherProfile();
  if (!profile) {
    return;
  }
  // Held by the filter, which the input system copies: this frame's table is not what it reads
  // from.
  auto context = std::make_shared<RemapContext>();
  context->table = rb_blitz::launcher::remap::Table::FromRows(profile->remap);
  if (context->table.BoundCount() == 0) {
    REXLOG_INFO("remap: the launcher profile binds no buttons, the pad is untouched");
    return;
  }
  if (!context->table.AnySourceSet()) {
    // A table of empty bindings is a pad that does nothing, which is a real thing to want and a
    // terrible thing to hit by accident. It is honoured, and said out loud.
    REXLOG_WARN(
        "remap: the launcher profile binds {} control(s) to nothing, so no pad button will "
        "reach the game",
        context->table.BoundCount());
  }
  const std::size_t bound = context->table.BoundCount();

  std::function<std::unique_ptr<rex::system::IInputSystem>(bool)> backend;
  if (config.input_factory) {
    backend = std::move(config.input_factory);
  } else {
    backend = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
  }

  config.input_factory = [backend = std::move(backend), context = std::move(context),
                          bound](bool tool_mode) {
    std::unique_ptr<rex::system::IInputSystem> system = backend(tool_mode);
    if (tool_mode || !system) {
      return system;
    }
    // The same cast mouse_ui makes, and for the same reason: this is the one input system the
    // SDK and this project ship, and the filter is the seam the SDK grew for exactly this.
    static_cast<rex::input::InputSystem*>(system.get())
        ->SetStateFilter([context](uint32_t /*user_index*/, rex::input::X_INPUT_STATE& state) {
          namespace remap = rb_blitz::launcher::remap;
          const remap::PadState pad{state.gamepad.buttons, state.gamepad.left_trigger,
                                    state.gamepad.right_trigger};
          remap::PadState out;
          remap::Apply(context->table, pad, &SourceDownThunk, context.get(), &out);
          state.gamepad.buttons = out.buttons;
          // An unbound trigger keeps its analog travel: Apply copies the state it was handed
          // before it rewrites anything, so only a bound one is driven to an end.
          state.gamepad.left_trigger = out.left_trigger;
          state.gamepad.right_trigger = out.right_trigger;
        });
    REXLOG_INFO(
        "remap: {} pad control(s) rebound by the launcher profile (--launcher_profile <path> "
        "names another file)",
        bound);
    return system;
  };
}

}  // namespace rb_blitz::input
