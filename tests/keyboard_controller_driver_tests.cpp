// SPDX-License-Identifier: GPL-2.0-only
// Exercise the real SDL driver under the same capture suppression as a modal.
// No native window is opened and no game/UI is navigated.
#include "check.h"

#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/ui/keyboard_input_controller.h>
#include <rex/ui/window_sdl.h>

namespace {
class Context final : public rex::ui::WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};
}  // namespace

int main() {
  using namespace rex::input;
  rb_blitz::test::BeginCase("Modal B reads physical SDL input while guest input is suppressed");
  Context context;
  rex::ui::WindowSDL window(context, "Unopened keyboard test", 1280, 720);
  sdl::SDLInputDriver driver(nullptr, 0);
  CHECK_EQ(driver.Setup(), 0);
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  driver.OnWindowAvailable(&window);
  bool captured = true;
  driver.set_is_active_callback([&] { return !captured; });

  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.name = "Keyboard modal regression pad";
  const auto instance = SDL_AttachVirtualJoystick(&desc);
  CHECK_TRUE(instance != 0);
  auto* joystick = SDL_OpenJoystick(instance);
  CHECK_TRUE(joystick != nullptr);
  SDL_UpdateJoysticks();
  SDL_PumpEvents();
  CHECK_TRUE(SDL_IsGamepad(instance));
  std::vector<DeviceInfo> devices;
  driver.EnumerateDevices(devices);
  DeviceId device = DeviceId::kInvalid;
  for (const auto& info : devices) {
    if (info.name == desc.name) device = info.id;
  }
  CHECK_TRUE(device != DeviceId::kInvalid);
  if (device != DeviceId::kInvalid && joystick) {
    rex::ui::KeyboardInputController cancel;
    X_INPUT_STATE state{};
    // Prime B's opening edge from a neutral physical sample.
    CHECK_EQ(driver.GetDeviceStateForUI(device, &state), 0);
    CHECK_FALSE(cancel.Update({true, true, 0}).cancel);
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_EAST, true));
    SDL_UpdateJoysticks();
    SDL_PumpEvents();

    CHECK_EQ(driver.GetDeviceState(device, &state), 0);
    CHECK_EQ(state.gamepad.buttons, 0);
    CHECK_EQ(driver.GetDeviceStateForUI(device, &state), 0);
    CHECK_TRUE((state.gamepad.buttons & X_INPUT_GAMEPAD_B) != 0);
    const auto buttons = (state.gamepad.buttons & X_INPUT_GAMEPAD_B)
                             ? rex::ui::KeyboardControllerSample::Cancel : 0;
    CHECK_TRUE(cancel.Update({true, true, static_cast<uint16_t>(buttons)}).cancel);
    // The UI read must not enable guest input as a side effect.
    CHECK_EQ(driver.GetDeviceState(device, &state), 0);
    CHECK_EQ(state.gamepad.buttons, 0);
    captured = false;
    CHECK_EQ(driver.GetDeviceState(device, &state), 0);
    CHECK_TRUE((state.gamepad.buttons & X_INPUT_GAMEPAD_B) != 0);
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_MISC1, true));
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, true));
    SDL_UpdateJoysticks();
    SDL_PumpEvents();
    CHECK_EQ(driver.GetDeviceState(device, &state), 0);
    CHECK_FALSE((state.gamepad.buttons & X_INPUT_GAMEPAD_GUIDE) != 0);
    CHECK_FALSE((state.gamepad.buttons & X_INPUT_GAMEPAD_Y) != 0);
    uint8_t available = 0, held = 0;
    CHECK_TRUE(driver.GetExtraButtonState(device, &available, &held));
    CHECK_TRUE((available & (1u << 0)) && (available & (1u << 3)));
    CHECK_TRUE((held & (1u << 0)) && (held & (1u << 3)));
    CHECK_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_MISC1, false));
    SDL_UpdateJoysticks();
    SDL_PumpEvents();
    CHECK_TRUE(driver.GetExtraButtonState(device, &available, &held));
    CHECK_FALSE((held & (1u << 0)) != 0);
  }
  if (joystick) SDL_CloseJoystick(joystick);
  if (instance) SDL_DetachVirtualJoystick(instance);
  SDL_PumpEvents();
  rex::ui::UIEvent closing(&window);
  static_cast<rex::ui::WindowListener&>(driver).OnClosing(closing);
  SDL_Quit();
  return rb_blitz::test::Finish();
}
