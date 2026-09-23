// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Mouse support for the guest menus, as a synthetic pad rather than as a
// pointer.
//
// The guest has no cursor: nothing in the game maps screen coordinates to a
// list row, and the menus are read straight off the pad, one left-stick press
// per row (docs/bringup-log.md, 2026-09-22). So "hovering with the mouse" is
// implemented the only way the guest can perceive it: pointer travel is
// converted into the same discrete left-stick presses the keyboard bindings
// produce, which is what makes moving the mouse over a list move the selection.
// The arithmetic lives in src/input/ui_nav.h, which is SDK-free and unit tested.
//
// What the pointer's position cannot do is say which row is selected: the guest
// is handed a composited frame and exposes no hit test, so the offset between the
// pointer and the highlight is unobservable - and, because this bridge can only
// add motion to it, impossible to alter. Pointing at a row therefore moves the
// selection by the distance the pointer travelled, not to the row under the
// pointer. What the driver can do is make that distance mean what the guest's
// layout means: a row of travel is a fraction of the window's height, scaled on
// every resize and DPI change, so the selection keeps pace with the pointer at any
// window size instead of gaining or losing a row per gesture.
//
// Clicks are queued behind that travel rather than reported the moment the button
// moves - UiClickPulser in src/input/ui_nav.h is the why - so that A lands on the
// row the hand stopped on rather than on whichever row the walk was passing
// through.
//
// The driver is appended to the host's input system in RbBlitzApp::OnPreSetup,
// so it works with whatever backend the user selected and needs no SDK patch.
// It is a separate device: the merge in the SDK ORs buttons and takes the
// larger thumb magnitude, so this never fights a real pad, and a real pad takes
// over the moment it is deflected harder. Nothing here captures or hides the
// cursor - the ImGui overlays need it - and the SDK's own opt-in mouse look
// (mnk_mouse) still works: with SetRelativeMouseMode on, x/y stop moving and the
// driver falls back to the reported deltas.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

#include <rex/input/input_driver.h>
#include <rex/runtime.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window_listener.h>

#include "input/ui_nav.h"

namespace rex::ui {
class Window;
}

namespace rb_blitz::input {

class MouseUiInputDriver final : public rex::input::InputDriver,
                                public rex::ui::WindowInputListener,
                                public rex::ui::WindowListener {
 public:
  // A single device, so its handle is a constant. Distinct from the MnK
  // driver's so the two can never be confused for one another.
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x4D4F5553);

  MouseUiInputDriver() : InputDriver(nullptr, 0) {}
  ~MouseUiInputDriver() override;

  rex::X_STATUS Setup() override;

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override;
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out_state) override;
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t flags,
                                      rex::input::X_INPUT_CAPABILITIES* out_caps) override;
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId id,
                                   rex::input::X_INPUT_VIBRATION* vibration) override;
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t flags,
                                   rex::input::X_INPUT_KEYSTROKE* out_keystroke) override;

  void OnWindowAvailable(rex::ui::Window* window) override;

  // WindowInputListener
  void OnMouseDown(rex::ui::MouseEvent& e) override;
  void OnMouseUp(rex::ui::MouseEvent& e) override;
  void OnMouseMove(rex::ui::MouseEvent& e) override;
  void OnMouseWheel(rex::ui::MouseEvent& e) override;

  // WindowListener
  void OnClosing(rex::ui::UIEvent& e) override;
  void OnGotFocus(rex::ui::UISetupEvent& e) override;
  void OnLostFocus(rex::ui::UISetupEvent& e) override;
  void OnResize(rex::ui::UISetupEvent& e) override;
  void OnDpiChanged(rex::ui::UISetupEvent& e) override;

 private:
  bool IsEnabled() const;
  void DetachFromWindow();

  // Retimes the stepper to the current window height, so that a row of travel is
  // the same fraction of the menu whatever size the window is. Callers hold
  // state_mutex_.
  void UpdateRowPixels();

  // Only the UI thread writes it, so only guest thread access needs the lock.
  rex::ui::Window* attached_window_ = nullptr;

  std::mutex state_mutex_;

  // Written by the mouse handlers on the UI thread, read by GetDeviceState on
  // the guest thread, hence the lock. The clicks are half of the same bridge as
  // the travel, so they share the lock and the pulse timing: a click is held back
  // until the rows the pointer queued before it have been delivered.
  UiNavStepper nav_;
  UiClickPulser clicks_;

  // Also UI thread only. The last absolute position reported, so that motion can
  // be told apart from rotation-in-place when the pointer is locked.
  bool have_motion_event_ = false;
  double last_event_x_ = 0.0;
  double last_event_y_ = 0.0;

  // Guest thread only. The packet number moves with the state, like a real
  // pad's, so the guest can tell a repeat poll from a new one.
  uint16_t last_buttons_ = 0;
  int16_t last_lx_ = 0;
  int16_t last_ly_ = 0;
  uint32_t packet_number_ = 1;

  // Guest thread only. How often the guest reads this device is what turns the
  // millisecond pulse its cvars describe into the poll counts the stepper works
  // in - see UiNavStepper::SetPulsePolls for why a pulse is a length of time and
  // not a number of polls.
  bool has_poll_time_ = false;
  std::chrono::steady_clock::time_point last_poll_time_{};
  double poll_interval_ms_ = 0.0;

  std::atomic<bool> has_focus_{true};
};

// Wraps whatever input backend the runtime config already names with one that
// also has the mouse navigation driver, so this composes with the backend
// selection (`input_backend`, `mnk_mode`) instead of replacing it. Call from
// ReXApp::OnPreSetup: the runtime reads input_factory after that hook returns.
// A no-op in tool mode, which has no window to listen to.
void InstallMouseUiNavigation(rex::RuntimeConfig& config);

}  // namespace rb_blitz::input
