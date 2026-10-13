// SPDX-License-Identifier: GPL-2.0-only
#include "input/mouse_ui.h"
#include "input/native_menu.h"

#include <cstring>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <rex/cvar.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/ui/flags.h>
#include <rex/ui/window.h>

REXCVAR_DEFINE_BOOL(mouse_ui_nav, true, "Input",
    "Native menu mouse: point to select, left/right click A/B, wheel up/down. "
    "Hidden and inactive during gameplay; available again when paused.");

namespace rb_blitz::input {
using rex::X_STATUS;
using rex::X_RESULT;
namespace {
class MousePad final : public rex::input::InputDriver {
 public:
  MousePad() : InputDriver(nullptr, 0) {}
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x4D4F5553);
  rex::X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    if (!MouseSupportEnabled()) return;
    rex::input::DeviceInfo info;
    info.id = kDevice; info.name = "Mouse (native menus)"; info.synthetic = true;
    out.push_back(info);
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id,
                               rex::input::X_INPUT_STATE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    auto pulse = MenuMousePadState();
    if (pulse != last_) { ++packet_; last_ = pulse; }
    if (out) {
      *out = {};
      out->packet_number = packet_;
    }
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                      rex::input::X_INPUT_CAPABILITIES* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out) { *out = {}; out->type = 1; out->sub_type = 1;
      out->gamepad.buttons = 0x3000; out->gamepad.thumb_lx = 32767; out->gamepad.thumb_ly = 32767; }
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId id,
                                   rex::input::X_INPUT_VIBRATION*) override {
    return id == kDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                                   rex::input::X_INPUT_KEYSTROKE*) override {
    return id == kDevice ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }
 private:
  uint16_t last_ = 0;
  uint32_t packet_ = 1;
};

class MouseCursor final : public rex::ui::ImGuiDialog,
                          public rex::ui::WindowInputListener,
                          public rex::ui::WindowListener {
 public:
  MouseCursor(rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Window*()> provider)
      : ImGuiDialog(drawer), provider_(std::move(provider)) {}
  ~MouseCursor() override { Detach(); }
 private:
  void Detach() {
    if (!window_) return;
    window_->RemoveInputListener(this); window_->RemoveListener(this);
    window_ = nullptr; MouseMenuFocus(false);
  }
  void OnClosing(rex::ui::UIEvent&) override { Detach(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override { MouseMenuFocus(false); }
  void OnGotFocus(rex::ui::UISetupEvent&) override { MouseMenuFocus(true); }
  void OnDraw(ImGuiIO& io) override {
    if (!window_) {
      window_ = provider_();
      if (!window_) return;
      // Above the keyboard's mouse bindings (0), below the SDK dialogs.
      window_->AddInputListener(this, 1); window_->AddListener(this);
      MouseMenuFocus(window_->HasFocus());
    }
    MouseMenuFocus(window_->HasFocus() && !io.WantCaptureMouse);
    const auto visibility = (MouseMenuAvailable() || (MouseSupportEnabled() && io.WantCaptureMouse)) ? rex::ui::Window::CursorVisibility::kVisible
                                                : rex::ui::Window::CursorVisibility::kHidden;
    auto* window = window_;
    window->app_context().CallInUIThread([window, visibility] {
      window->SetCursorVisibility(visibility);
      // SDL owns a process-wide cursor; keep it synchronized after startup too.
      if (visibility == rex::ui::Window::CursorVisibility::kVisible) SDL_ShowCursor();
      else SDL_HideCursor();
    });
  }
  std::optional<MenuPoint> Point(const rex::ui::MouseEvent& e) const {
    if (!window_) return {};
    auto p = MenuPointer(float(e.x()), float(e.y()),
        window_->GetActualPhysicalWidth(), window_->GetActualPhysicalHeight(),
        REXCVAR_GET(video_mode_width), REXCVAR_GET(video_mode_height),
        REXCVAR_GET(present_letterbox), REXCVAR_GET(present_allow_overscan_cutoff),
        REXCVAR_GET(present_safe_area_x), REXCVAR_GET(present_safe_area_y));
    if (!MenuBounds{0, 0, 1, 1}.Contains(p)) return {};
    return p;
  }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!MouseSupportEnabled()) return;
    if (auto p = Point(e); p && (e.x() != last_x_ || e.y() != last_y_))
      QueueMenuMouse(MouseAction::kMove, *p);
    last_x_ = e.x(); last_y_ = e.y();
    e.set_handled(true);
  }
  void OnMouseDown(rex::ui::MouseEvent& e) override {
    if (!MouseSupportEnabled()) return;
    if (auto p = Point(e)) {
      if (e.button() == rex::ui::MouseEvent::Button::kLeft) QueueMenuMouse(MouseAction::kAccept, *p);
      if (e.button() == rex::ui::MouseEvent::Button::kRight) QueueMenuMouse(MouseAction::kBack, *p);
    }
    e.set_handled(true);
  }
  void OnMouseUp(rex::ui::MouseEvent& e) override {
    if (MouseSupportEnabled()) e.set_handled(true);
  }
  void OnMouseWheel(rex::ui::MouseEvent& e) override {
    if (!MouseSupportEnabled()) return;
    if (Point(e)) QueueMenuWheel(e.scroll_y());
    e.set_handled(true);
  }
  std::function<rex::ui::Window*()> provider_;
  rex::ui::Window* window_ = nullptr;
  int last_x_ = -1, last_y_ = -1;
};
}

void InstallMouseUiNavigation(rex::RuntimeConfig& config) {
  if (config.tool_mode) return;
  auto backend = config.input_factory ? std::move(config.input_factory)
                                     : REX_INPUT_BACKEND(rex::input::CreateDefaultInputSystem);
  config.input_factory = [backend = std::move(backend)](bool tool_mode) {
    auto system = backend(tool_mode);
    if (system && !tool_mode)
      static_cast<rex::input::InputSystem*>(system.get())->AddDriver(std::make_unique<MousePad>());
    return system;
  };
}
std::unique_ptr<rex::ui::ImGuiDialog> CreateMouseMenuCursor(
    rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Window*()> window) {
  return std::make_unique<MouseCursor>(drawer, std::move(window));
}
}
