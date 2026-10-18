// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the title's input system (src/input/title_input.h). Mirrors the SDK's
// CreateDefaultInputSystem, with the MnK gate and the physical-driver registry.

#include "input/title_input.h"

#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/flags.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/input/state_merge.h>
#include <rex/logging.h>
#include <rex/platform.h>
#if REX_PLATFORM_WIN32
#include <rex/input/xinput/xinput_input_driver.h>
#endif

namespace rb_blitz::input {
namespace {

using rex::X_RESULT;
using rex::X_STATUS;

// The physical drivers of the input system currently installed. Written once when the system is
// built and read by the state filter afterwards; the drivers are owned by that system, and the
// filter is owned by it too, so the pointers cannot outlive their use.
std::mutex g_drivers_mutex;
std::vector<rex::input::InputDriver*> g_physical_drivers;

bool KeyboardSupportOn() { return rex::cvar::GetFlagByName("keyboard_support") != "false"; }

}  // namespace

std::unique_ptr<rex::system::IInputSystem> CreateTitleInputSystem(bool tool_mode) {
  auto input = std::make_unique<rex::input::InputSystem>(nullptr);
  std::vector<rex::input::InputDriver*> physical;

  if (!tool_mode) {
    const std::string backend = REXCVAR_GET(input_backend);
    if (backend == "xinput") {
#if REX_PLATFORM_WIN32
      auto xinput_driver = std::make_unique<rex::input::xinput::XinputInputDriver>(nullptr, 0);
      if (xinput_driver->Setup() == X_STATUS_SUCCESS) {
        physical.push_back(xinput_driver.get());
        input->AddDriver(std::move(xinput_driver));
        REXLOG_INFO("title_input: pads come from XInput (a physical pad only, for `pad:` sources)");
      } else {
        REXLOG_WARN("title_input: the XInput backend did not set up; no physical pad driver");
      }
#else
      REXLOG_WARN("title_input: input_backend=xinput on a platform without XInput");
#endif
    }

    if (backend == "sdl") {
      auto sdl_driver = std::make_unique<rex::input::sdl::SDLInputDriver>(nullptr, 0);
      if (sdl_driver->Setup() == X_STATUS_SUCCESS) {
        physical.push_back(sdl_driver.get());
        input->AddDriver(std::move(sdl_driver));
        REXLOG_INFO("title_input: pads come from SDL (a physical pad only, for `pad:` sources)");
      } else {
        REXLOG_WARN("title_input: the SDL backend did not set up; no physical pad driver");
      }
    }

    // The legacy keyboard/mouse-to-pad emulation. It is a keyboard path, so it follows the
    // keyboard gate: absent means no synthesized pad bit can exist at all. With the gate on it is
    // installed exactly as the SDK's default system installs it (mnk_mode itself still decides
    // whether it enumerates), because every acceptance harness drives the menus through it. Its
    // state is merged into what the guest sees but is never a `pad:` gameplay source - the
    // gameplay tables and P3's dispatcher read SamplePhysicalPad instead.
    if (KeyboardSupportOn()) {
      auto mnk_driver = std::make_unique<rex::input::mnk::MnkInputDriver>(nullptr, 0);
      if (mnk_driver->Setup() == X_STATUS_SUCCESS) {
        input->AddDriver(std::move(mnk_driver));
        REXLOG_INFO(
            "title_input: legacy MnK emulation follows keyboard_support=on (mnk_mode enables it; "
            "its synthesized pad state is not a `pad:` gameplay source)");
      }
    } else {
      REXLOG_WARN(
          "title_input: legacy MnK emulation is not installed - keyboard_support is off, so no "
          "mnk_mode/keybind_* setting can synthesize pad input");
    }
  }

  // NOP driver (primary in tool mode, fallback otherwise), as the SDK's default has it.
  const uint8_t nop_index = tool_mode ? 0 : 1;
  input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr, nop_index));
  input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());

  {
    std::lock_guard lock(g_drivers_mutex);
    g_physical_drivers = std::move(physical);
  }
  return input;
}

bool SamplePhysicalPad(rex::input::X_INPUT_STATE* out_state) {
  if (out_state) {
    *out_state = {};
  }
  std::vector<rex::input::InputDriver*> drivers;
  {
    std::lock_guard lock(g_drivers_mutex);
    drivers = g_physical_drivers;
  }
  bool any = false;
  rex::input::X_INPUT_STATE merged{};
  for (rex::input::InputDriver* driver : drivers) {
    std::vector<rex::input::DeviceInfo> devices;
    driver->EnumerateDevices(devices);
    for (const rex::input::DeviceInfo& info : devices) {
      // The physical drivers enumerate physical pads only, but the flag is what "physical"
      // means, so it is the test - not which driver answered.
      if (info.synthetic) {
        continue;
      }
      rex::input::X_INPUT_STATE state{};
      if (driver->GetDeviceState(info.id, &state) != X_ERROR_SUCCESS) {
        continue;
      }
      if (!any) {
        merged = state;
        any = true;
      } else {
        rex::input::MergeInto(merged, state);
      }
    }
  }
  if (!any) {
    return false;
  }
  if (out_state) {
    *out_state = merged;
  }
  return true;
}

}  // namespace rb_blitz::input
