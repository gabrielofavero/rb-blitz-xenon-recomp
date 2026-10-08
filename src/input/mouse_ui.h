// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Mouse support for the guest menus, as a synthetic pad rather than as a
// pointer.
//
// The guest has no cursor: nothing in the game maps screen coordinates to a
// list row, and the menus are read straight off the pad, one left-stick press
// per row (docs/history/bringup-log.md, 2026-09-22). So "hovering with the mouse" is
// implemented the only way the guest can perceive it: the row the pointer is over
// is worked out from the guest's own frames, and the pad is pulsed until the
// guest's highlight is on that row. src/input/ui_nav.h holds the arithmetic and the
// argument for it; this file is the device side.
//
// Two things have to be true for a hover to be exact, and making both of them true
// is what this file is for. The pointer has to be read in the pixels the highlight
// is measured in, which means mapping a mouse event's client coordinates through
// the geometry the presenter scales and centres the guest's image with; and the
// presses have to be timed by the guest's frames rather than by a clock, which means
// reading the guest's own output back and handing it to the aligner - every frame
// while a hover is being decided, since what a press is measured against has to be
// the screen as it is now, and every frame that changed the rest of the time. That
// readback is a copy of the whole frontbuffer and conversion of it in software, so it
// runs at a few hertz while nothing is being aligned and as fast as the frames arrive
// while a hover is.
//
// When frames cannot be read at all - no presenter (another graphics backend), a
// window whose guest output has not been captured yet, or mouse look, which freezes
// the pointer's position and reports only deltas - the mouse falls back to what it
// did before: a row of travel is a row, in either direction. There is no wheel.
// (The hover path answers a frame older than the pointer's rest by waiting for a
// newer one rather than by pressing: a press measured against a frame from before
// the last move the guest made measures two moves and turns the pitch into two rows.)
//
// Clicks are queued behind all of that rather than reported the moment the button
// moves - UiClickPulser in src/input/ui_nav.h is the why - so that A lands on the
// row the hand is on rather than on whichever row the presses were passing
// through.
//
// The driver is appended to the host's input system in RbBlitzApp::OnPreSetup,
// so it works with whatever backend the user selected and needs no SDK patch.
// It is a separate device: the merge in the SDK ORs buttons and takes the
// larger thumb magnitude, so this never fights a real pad, and a real pad takes
// over the moment it is deflected harder. Nothing here captures the cursor:
// whether it is shown is the app's rule, not the driver's - RbBlitzApp hides it
// when `mouse_ui_nav` is off, because then there is no guest pointer for it to
// aim and nothing in the window is waiting for it (rb_blitz_app.h).

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/input/input_driver.h>
#include <rex/runtime.h>
#include <rex/ui/presenter.h>
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

  // `presenter_provider` is asked for the presenter of the guest's output,
  // lazily and from the frame thread; it may return nullptr for as long as the
  // graphics backend has no presenter, which is why it is a callback and not a
  // pointer. Pass {} when there is nothing to read frames from - the mouse still
  // navigates, it just cannot measure the screen.
  explicit MouseUiInputDriver(std::function<rex::ui::Presenter*()> presenter_provider)
      : InputDriver(nullptr, 0), presenter_provider_(std::move(presenter_provider)) {}
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

  // WindowListener
  void OnClosing(rex::ui::UIEvent& e) override;
  void OnGotFocus(rex::ui::UISetupEvent& e) override;
  void OnLostFocus(rex::ui::UISetupEvent& e) override;
  void OnResize(rex::ui::UISetupEvent& e) override;
  void OnDpiChanged(rex::ui::UISetupEvent& e) override;

 private:
  bool IsEnabled() const;
  void DetachFromWindow();

  // Retimes the travel fallback to the current window height, so that a row of
  // travel is the same fraction of the menu whatever size the window is. Callers
  // hold state_mutex_.
  void UpdateRowPixels();

  // Recomputes the mapping between the pointer's pixels and the guest's, and
  // re-aims the aligner at the position the pointer was last seen at rather than
  // leaving a target in pixels that no longer mean anything. Callers hold
  // state_mutex_; returns whether a hover can be aligned at all now.
  bool RefreshMapping();

  // The frame thread: reads the guest's own output back, hands every frame that
  // changed to the aligner, detects a guest output size change, and writes whatever
  // the probe and hover-log cvars ask for.
  void FrameThreadMain();
  void StartFrameThread();
  void StopFrameThread();

  // Guest thread. One line per finished burst when the hover log is on.
  void LogBurstReport(const MenuHoverAligner::Report& report);

  // Frame thread, from the aligner: one line per press. A press that moved two rows
  // rather than one and a press that moved nothing are the same outcome otherwise.
  void LogStep(const MenuHoverAligner::StepLog& step);

  // Steady-clock milliseconds, the time base the aligner is told about.
  static double NowMs();

  // Only the UI thread writes it, so only guest thread access needs the lock.
  rex::ui::Window* attached_window_ = nullptr;

  // Set once at construction and only read afterwards, so no lock.
  std::function<rex::ui::Presenter*()> presenter_provider_;

  // The frame thread and what it publishes for the other side: the size of the
  // frames the guest is drawing and when one was last seen to have changed, so the
  // guest thread can tell whether there is anything to align with yet.
  std::thread frame_thread_;
  std::atomic<bool> frame_stop_{false};
  std::atomic<bool> frame_started_{false};
  std::atomic<int> guest_frame_width_{0};
  std::atomic<int> guest_frame_height_{0};
  std::atomic<int64_t> guest_frame_seen_ms_{0};

  std::mutex state_mutex_;

  // Written by the mouse handlers on the UI thread, read by GetDeviceState on
  // the guest thread, hence the lock. The clicks are the other half of the same
  // bridge, so they share it and the alignment: a click is held back until the
  // selection has stopped moving.
  UiNavStepper nav_;
  UiClickPulser clicks_;
  MenuHoverAligner aligner_;

  // Where the pointer was last seen, in the client area, and the mapping that was
  // in force then - kept so that a resize or a guest resolution change can re-aim
  // the aligner at the same spot instead of dropping the hover. The client size is
  // written by the UI thread with the window events so that the guest thread never
  // has to ask the window for it.
  bool have_pointer_ = false;
  double pointer_x_ = 0.0;
  double pointer_y_ = 0.0;
  int client_width_ = 0;
  int client_height_ = 0;
  GuestImageMapping mapping_;

  // Whether the frame thread should be reading as fast as the guest draws rather
  // than at the idle rate. Written by the guest thread (from the aligner's own
  // answer) and read by the frame thread, which is also what wakes it out of its
  // five-millisecond slices.
  std::atomic<bool> align_wanted_{false};

  // Whether the last motion event went to the aligner or to the travel fallback,
  // so that switching between them starts a new gesture rather than joining the
  // old one.
  bool using_aligner_ = false;

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
  // millisecond pulses the fallback's cvars describe into the poll counts the
  // stepper works in - see UiNavStepper::SetPulsePolls. The aligner does not need
  // it: its pulses are gated on the guest's frames.
  bool has_poll_time_ = false;
  double last_poll_ms_ = 0.0;
  double poll_interval_ms_ = 0.0;

  // Guest thread only. A click waiting for the selection to arrive under the
  // pointer, and the moment it stops waiting whatever happens: an alignment that
  // cannot finish must not swallow the click.
  bool click_waiting_ = false;
  double click_wait_started_ms_ = 0.0;

  std::atomic<bool> has_focus_{true};
};

// Wraps whatever input backend the runtime config already names with one that
// also has the mouse navigation driver, so this composes with the backend
// selection (`input_backend`, `mnk_mode`) instead of replacing it. Call from
// ReXApp::OnPreSetup: the runtime reads input_factory after that hook returns.
// A no-op in tool mode, which has no window to listen to.
// `presenter_provider` is only used by the screen measuring code; pass {} if
// frames cannot be read.
void InstallMouseUiNavigation(rex::RuntimeConfig& config,
                              std::function<rex::ui::Presenter*()> presenter_provider = {});
}  // namespace rb_blitz::input
