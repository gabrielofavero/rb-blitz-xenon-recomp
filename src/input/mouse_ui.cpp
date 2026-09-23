// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/input/mouse_ui.h for why the mouse is translated into left-stick
// presses, and src/input/ui_nav.h for the translation itself. This file is the
// device side: the window events that fill the stepper in, the pad state the
// guest polls out, and the hook that installs the whole thing.

#include "input/mouse_ui.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/ui/window.h>

REXCVAR_DEFINE_BOOL(mouse_ui_nav, true, "Input",
                    "Move the guest's menu selection by moving the mouse, and press A/B with the "
                    "left/right button, without an ImGui overlay having the pointer");
REXCVAR_DEFINE_DOUBLE(mouse_ui_row_fraction, 0.0417, "Input",
                      "Mouse motion that moves the menu selection by one row, as a fraction of "
                      "the window's height: 32 px at 768, the top of the range the menus measured "
                      "(27-40 px a row), leaving the song list's 96 to the wheel")
    .range(0.01, 0.25);
REXCVAR_DEFINE_DOUBLE(mouse_ui_press_ms, 24.0, "Input",
                      "How long one menu step holds the stick, in milliseconds: a step the "
                      "guest's frame cannot see is a row that never moves")
    .range(1.0, 1000.0);
REXCVAR_DEFINE_DOUBLE(mouse_ui_release_ms, 24.0, "Input",
                      "How long one menu step releases the stick before the next one, in "
                      "milliseconds: the next row only moves if the guest sees this gap")
    .range(0.0, 1000.0);

namespace rb_blitz::input {

using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::InputDriver;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;
using rex::X_RESULT;
using rex::X_STATUS;

namespace {

// Used only until an interval has been measured. 2.4 ms is the cadence the guest
// log shows here, ~420 reads of this device a second.
constexpr double kAssumedPollIntervalMs = 2.4;

// A gap longer than this is a stall - a breakpoint, a resize, a sleeping
// process - and not a cadence, so it is dropped instead of being smoothed in.
constexpr double kMaxPollIntervalMs = 500.0;

// Enough smoothing to ignore one short gap between two reads without taking
// seconds to follow a real change of cadence.
constexpr double kPollIntervalSmoothing = 0.125;

// What a millisecond pulse is worth in polls at the cadence being measured. The
// stepper clamps what this returns: only the arithmetic belongs here.
int PollsForMilliseconds(double milliseconds, double poll_interval_ms) {
  const double interval = poll_interval_ms > 0.0 ? poll_interval_ms : kAssumedPollIntervalMs;
  return static_cast<int>(std::lround(milliseconds / interval));
}

// The pulser's numbering, named once so that the mouse handlers and the
// guest-side mapping cannot drift apart: index 0 is the left button, which is the
// guest's A, and 1 the right, which is B.
constexpr int kLeftClick = 0;
constexpr int kRightClick = 1;

}  // namespace

MouseUiInputDriver::~MouseUiInputDriver() {
  // The window outlives the driver and would otherwise keep calling into it.
  DetachFromWindow();
}

X_STATUS MouseUiInputDriver::Setup() {
  return X_STATUS_SUCCESS;
}

void MouseUiInputDriver::OnWindowAvailable(rex::ui::Window* window) {
  if (!window) {
    return;
  }
  {
    std::lock_guard lock(state_mutex_);
    attached_window_ = window;
    UpdateRowPixels();
    REXLOG_INFO("mouse_ui: a menu row is {} px at {}x{}", nav_.row_pixels(),
                window->GetActualPhysicalWidth(), window->GetActualPhysicalHeight());
  }
  window->AddInputListener(this, window_z_order());
  window->AddListener(this);
}

void MouseUiInputDriver::OnClosing(rex::ui::UIEvent&) {
  DetachFromWindow();
}

// Safe to call from any thread.
void MouseUiInputDriver::DetachFromWindow() {
  rex::ui::Window* window = attached_window_;
  if (!window) {
    return;
  }
  window->app_context().CallInUIThreadSynchronous([this, window] {
    {
      std::lock_guard lock(state_mutex_);
      attached_window_ = nullptr;
    }
    window->RemoveInputListener(this);
    window->RemoveListener(this);
  });
}

bool MouseUiInputDriver::IsEnabled() const {
  return REXCVAR_GET(mouse_ui_nav);
}

void MouseUiInputDriver::UpdateRowPixels() {
  rex::ui::Window* window = attached_window_;
  if (!window) {
    return;
  }
  const uint32_t height = window->GetActualPhysicalHeight();
  // Zero is documented for a window whose surface has not been given a size yet;
  // until there is a height to scale by, the stepper's own default is a better
  // answer than a row of no pixels.
  if (height == 0) {
    return;
  }
  nav_.SetRowPixels(REXCVAR_GET(mouse_ui_row_fraction) * static_cast<double>(height));
}

void MouseUiInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  // Disabled means no device at all, so it never occupies the guest user slot
  // it shares with the keyboard emulation.
  if (!IsEnabled()) {
    return;
  }
  DeviceInfo info;
  info.id = kDevice;
  info.name = "Mouse (menu navigation)";
  // What routes it to guest user 0 - see SlotAssignment::OnDevicesChanged.
  info.synthetic = true;
  out.push_back(info);
}

X_RESULT MouseUiInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                                   X_INPUT_CAPABILITIES* out_caps) {
  if (!IsEnabled() || id != kDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;
    out_caps->sub_type = 0x01;
    out_caps->flags = 0;
    // Only A, B and the left stick are ever produced, but the caps mirror the
    // keyboard emulation's: a guest that sizes its input off the capability
    // mask must not conclude this pad is a stripped-down one.
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
    out_caps->vibration.left_motor_speed = 0xFFFF;
    out_caps->vibration.right_motor_speed = 0xFFFF;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MouseUiInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  if (!IsEnabled() || id != kDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // How fast the guest is reading this device decides how many polls a press has
  // to last to be one the guest can see. See UiNavStepper::SetPulsePolls.
  const auto now = std::chrono::steady_clock::now();
  if (has_poll_time_) {
    const double interval_ms =
        std::chrono::duration<double, std::milli>(now - last_poll_time_).count();
    if (interval_ms > 0.0 && interval_ms <= kMaxPollIntervalMs) {
      poll_interval_ms_ = poll_interval_ms_ > 0.0
                              ? poll_interval_ms_ * (1.0 - kPollIntervalSmoothing) +
                                    interval_ms * kPollIntervalSmoothing
                              : interval_ms;
    }
  }
  last_poll_time_ = now;
  has_poll_time_ = true;

  const int press_polls = PollsForMilliseconds(REXCVAR_GET(mouse_ui_press_ms), poll_interval_ms_);
  const int release_polls =
      PollsForMilliseconds(REXCVAR_GET(mouse_ui_release_ms), poll_interval_ms_);

  int16_t lx = 0;
  int16_t ly = 0;
  uint16_t buttons = 0;
  {
    std::lock_guard lock(state_mutex_);
    nav_.SetPulsePolls(press_polls, release_polls);
    clicks_.SetPulsePolls(press_polls, release_polls);
    if (is_active() && has_focus_) {
      nav_.Poll(&lx, &ly);
      // A click only goes through once the guest has caught up with the pointer,
      // so that it lands on the row the hand stopped on.
      const uint16_t pressed = clicks_.Poll(nav_.HasPendingRows());
      if (pressed & UiClickPulser::MaskFor(kLeftClick)) {
        buttons |= rex::input::X_INPUT_GAMEPAD_A;
      }
      if (pressed & UiClickPulser::MaskFor(kRightClick)) {
        buttons |= rex::input::X_INPUT_GAMEPAD_B;
      }
    } else {
      // Travel and clicks gathered while an overlay owned the pointer, or while
      // the window was in the background, are dropped rather than replayed into
      // the guest once it is in charge again.
      nav_.Reset();
      clicks_.Reset();
    }
  }

  // A real pad only advances the packet number when the state really moved, and
  // the guest uses that to tell a fresh reading from a repeat of the last one.
  if (buttons != last_buttons_ || lx != last_lx_ || ly != last_ly_) {
    last_buttons_ = buttons;
    last_lx_ = lx;
    last_ly_ = ly;
    ++packet_number_;
  }

  if (out_state) {
    std::memset(out_state, 0, sizeof(*out_state));
    out_state->packet_number = packet_number_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.thumb_lx = lx;
    out_state->gamepad.thumb_ly = ly;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MouseUiInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  if (!IsEnabled() || id != kDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MouseUiInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags,
                                                X_INPUT_KEYSTROKE* out_keystroke) {
  if (!IsEnabled() || id != kDevice) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  // Buttons are reported as pad state, not as keystrokes: XInputGetKeystroke is
  // what the guest uses for text entry, and the mouse produces no text.
  return X_ERROR_EMPTY;
}

void MouseUiInputDriver::OnMouseDown(rex::ui::MouseEvent& e) {
  if (!IsEnabled() || !has_focus_ || !is_active()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  // A click usually changes the screen, so the next position the pointer is
  // reported at is the start of a new gesture rather than travel from wherever it
  // was when the mouse was last over a menu.
  nav_.ForgetPointer();
  switch (e.button()) {
    case rex::ui::MouseEvent::Button::kLeft:
      clicks_.OnButtonDown(kLeftClick);
      break;
    case rex::ui::MouseEvent::Button::kRight:
      clicks_.OnButtonDown(kRightClick);
      break;
    default:
      break;
  }
}

void MouseUiInputDriver::OnMouseUp(rex::ui::MouseEvent& e) {
  if (!IsEnabled()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  switch (e.button()) {
    case rex::ui::MouseEvent::Button::kLeft:
      clicks_.OnButtonUp(kLeftClick);
      break;
    case rex::ui::MouseEvent::Button::kRight:
      clicks_.OnButtonUp(kRightClick);
      break;
    default:
      break;
  }
}

void MouseUiInputDriver::OnMouseMove(rex::ui::MouseEvent& e) {
  if (!IsEnabled() || !has_focus_ || !is_active()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  UpdateRowPixels();
  const double x = static_cast<double>(e.x());
  const double y = static_cast<double>(e.y());
  if (have_motion_event_ && e.dx() != 0.0f && x == last_event_x_ && y == last_event_y_) {
    // SetRelativeMouseMode, which mouse look turns on, freezes x/y and reports
    // only deltas; reading those as a position would leave navigation dead for as
    // long as mouse look is on.
    nav_.OnPointerMotion(static_cast<double>(e.dx()), static_cast<double>(e.dy()));
  } else {
    // Absolute positions rather than deltas: a delta is a sum of whatever the
    // platform rounded each event to, which drifts against a fixed scale.
    nav_.OnPointer(x, y);
  }
  have_motion_event_ = true;
  last_event_x_ = x;
  last_event_y_ = y;
}

void MouseUiInputDriver::OnMouseWheel(rex::ui::MouseEvent& e) {
  if (!IsEnabled() || !has_focus_ || !is_active()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  nav_.OnWheel(e.scroll_x(), e.scroll_y());
}

void MouseUiInputDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  has_focus_ = true;
  // The pointer may have moved over other windows while this one was unfocused,
  // and none of that travel is meant for the menu.
  std::lock_guard lock(state_mutex_);
  nav_.ForgetPointer();
}

void MouseUiInputDriver::OnLostFocus(rex::ui::UISetupEvent&) {
  has_focus_ = false;
  // A button released while unfocused never delivers its up event, and neither
  // does a click whose press has not been emitted yet: nothing in flight here is
  // meant for the menu.
  std::lock_guard lock(state_mutex_);
  clicks_.Reset();
  nav_.Reset();
}

void MouseUiInputDriver::OnResize(rex::ui::UISetupEvent&) {
  // A row is a fraction of the window's height, so resizing changes the pixels it
  // is measured in, and anything already queued was queued in the old ones.
  std::lock_guard lock(state_mutex_);
  UpdateRowPixels();
  nav_.Reset();
}

void MouseUiInputDriver::OnDpiChanged(rex::ui::UISetupEvent&) {
  // The same pixels now mean a different distance on the glass.
  std::lock_guard lock(state_mutex_);
  UpdateRowPixels();
  nav_.Reset();
}

void InstallMouseUiNavigation(rex::RuntimeConfig& config) {
  // Tool mode has no window and no guest to steer.
  if (config.tool_mode) {
    return;
  }

  std::function<std::unique_ptr<rex::system::IInputSystem>(bool)> backend;
  if (config.input_factory) {
    backend = std::move(config.input_factory);
  } else {
    backend = REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
  }

  config.input_factory = [backend = std::move(backend)](bool tool_mode) {
    std::unique_ptr<rex::system::IInputSystem> system = backend(tool_mode);
    if (tool_mode || !system) {
      return system;
    }
    // ReXApp casts this interface to InputSystem on the way to AttachWindow, so
    // anything else could not have worked anyway; CreateDefaultInputSystem, the
    // only backend the SDK and this project ship, returns one.
    static_cast<rex::input::InputSystem*>(system.get())
        ->AddDriver(std::make_unique<MouseUiInputDriver>());
    REXLOG_INFO("mouse_ui: the mouse navigates the menus (a row of travel per {} of the window's "
                "height, {} + {} ms per step, --no-mouse_ui_nav to disable)",
                REXCVAR_GET(mouse_ui_row_fraction), REXCVAR_GET(mouse_ui_press_ms),
                REXCVAR_GET(mouse_ui_release_ms));
    return system;
  };
}

}  // namespace rb_blitz::input
