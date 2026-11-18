// SPDX-License-Identifier: GPL-2.0-only
// Physical modal sampling through the real selected SDL/HID public services.
#include "check.h"
#include <filesystem>
#include <vector>
#include <SDL3/SDL.h>
#include <rex/app/app.h>
#include <rex/apu/audio_system.h>
#include <rex/cli.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/hid/title_input.h>
#include <rex/ui/keyboard_input_controller.h>
#include <rex/ui/ui_system.h>

namespace {
class InputApp final : public rex::app::App {
 public:
  InputApp() : App("keyboard_physical_test") {}
 private:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& registry) override {
    REX_TRY_VOID(root.Set(std::filesystem::current_path()));
    REX_TRY_VOID(GetUI().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetInput().GetSettings().drivers.SetText(rex::SettingLayer::kTitle, "sdl3"));
    // Keep the virtual regression pad on user 0 even when real pads are connected.
    REX_TRY_VOID(GetInput().GetSettings().shared_pads.SetText(rex::SettingLayer::kTitle, "true"));
    REX_TRY_VOID(GetAudio().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    REX_TRY_VOID(GetGraphics().GetSettings().driver.SetText(rex::SettingLayer::kTitle, "none"));
    return registry.SetText(rex::SettingLayer::kTitle, "log.file", "false");
  }
};
void Pump() { SDL_UpdateJoysticks(); SDL_PumpEvents(); }
}

int main() {
  using namespace rex::hid;
  rb_blitz::test::BeginCase("Modal B reads physical SDL input while guest input is suppressed");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  InputApp app;
  CHECK_TRUE(app.Init(rex::CLIArgList::Parse(std::vector<std::string>{})).has_value());
  auto* physical = REX_INTERFACE(ITitleInput);
  CHECK_TRUE(physical != nullptr);
  if (!physical) { app.Shutdown(); return rb_blitz::test::Finish(); }
  auto& input = app.GetInput();
  rex::InputEvent focus{.type = rex::InputEvent::Type::kFocusGained};
  input.OnInputEvent(focus);
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
  desc.name = "Keyboard modal regression pad";
  const auto instance = SDL_AttachVirtualJoystick(&desc);
  CHECK_TRUE(instance != 0);
  auto* joystick = SDL_OpenJoystick(instance);
  CHECK_TRUE(joystick != nullptr);
  Pump();
  CHECK_TRUE(SDL_IsGamepad(instance));
  input.AcquireUIBlock();
  rex::ui::KeyboardInputController cancel;
  CHECK_TRUE(physical->GetPhysicalState(true).has_value());
  CHECK_FALSE(cancel.Update({true, true, 0}).cancel);
  if (joystick) {
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, true));
    Pump();
    const auto guest = input.GetState(0);
    CHECK_TRUE(guest.has_value());
    CHECK_EQ(guest->state.buttons, 0);
    const auto modal = physical->GetPhysicalState(true);
    CHECK_TRUE(modal.has_value());
    CHECK_TRUE((modal->buttons & kPadB) != 0);
    const auto buttons = (modal->buttons & kPadB) ? rex::ui::KeyboardControllerSample::Cancel : 0;
    CHECK_TRUE(cancel.Update({true, true, static_cast<uint16_t>(buttons)}).cancel);
    CHECK_EQ(input.GetState(0)->state.buttons, 0);
    input.ReleaseUIBlock();
    // A held modal dismissal is consumed until release, so it cannot also
    // cancel the guest screen underneath the keyboard.
    CHECK_EQ(input.GetState(0)->state.buttons, 0);
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, false));
    Pump();
    CHECK_EQ(input.GetState(0)->state.buttons, 0);
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, true));
    Pump();
    CHECK_TRUE((input.GetState(0)->state.buttons & kPadB) != 0);
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_MISC1, true));
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, true));
    Pump();
    const auto state = input.GetState(0);
    CHECK_TRUE(state.has_value());
    CHECK_FALSE((state->state.buttons & (kPadGuide | kPadY)) != 0);
    const auto extra = physical->GetPhysicalExtraButtons(true);
    CHECK_TRUE((extra.available & (1u << 0)) && (extra.available & (1u << 3)));
    CHECK_TRUE((extra.held & (1u << 0)) && (extra.held & (1u << 3)));
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_MISC1, false));
    Pump();
    CHECK_FALSE((physical->GetPhysicalExtraButtons(true).held & (1u << 0)) != 0);
  } else {
    input.ReleaseUIBlock();
  }
  if (joystick) SDL_CloseJoystick(joystick);
  if (instance) SDL_DetachVirtualJoystick(instance);
  Pump();
  CHECK_EQ(app.Shutdown(), 0);
  return rb_blitz::test::Finish();
}
