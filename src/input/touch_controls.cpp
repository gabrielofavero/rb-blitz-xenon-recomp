// SPDX-License-Identifier: GPL-2.0-only
#include "input/touch_controls.h"
#include <rex/ui/imgui_dialog.h>
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(touch_buttons, true, "Input", "Show gameplay buttons while touch is the active input.");
REXCVAR_DEFINE_BOOL(touch_shake_deploy, true, "Input", "Rapidly shake the device to deploy a ready power-up.");

#if defined(RB_BLITZ_IOS)
#include <atomic>
#include <mutex>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <rex/logging.h>
#include <rex/kernel/xam/module.h>
#include <rex/ui/flags.h>
#include <rex/ui/window.h>
#include "input/last_device.h"
#include "input/input_capture.h"
#include "input/native_menu.h"
#include "input/remap.h"
#include "ui/achievements_overlay.h"

namespace rb_blitz::input::touch {
namespace {
static_assert(static_cast<uint64_t>(SDL_MOUSE_TOUCHID) == UINT64_MAX);
static_assert(static_cast<uint64_t>(SDL_PEN_TOUCHID) == UINT64_MAX - 1);
std::mutex g_mutex;
Gestures g_gestures;
ShakeDetector g_shake;
Rect g_lane_powerup;
uint64_t g_lane_timestamp = 0;
MouseGate g_mouse_gate;
std::atomic<bool> g_foreground{true};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_powerup_ready{false};
Layout g_layout;
bool Available() {
  return g_foreground && GameScreenActive() && HostInputFocused() &&
         !rex::kernel::xam::xeXamIsUIActive() && !ui::achievements::Active() && !capture::Armed();
}
void UpdateGestures(bool active) {
  g_gestures.SetActive(active);
  g_gestures.SetPowerupAvailable(active && g_powerup_ready);
  const auto now = SDL_GetTicksNS();
  const auto rect = g_lane_timestamp && now - g_lane_timestamp < 100'000'000
      ? g_lane_powerup : Rect{};
  const auto to_window = [&](float x, float y) {
    return MenuPointToWindow({x, y}, int(g_layout.width), int(g_layout.height),
        REXCVAR_GET(video_mode_width), REXCVAR_GET(video_mode_height),
        REXCVAR_GET(present_letterbox), REXCVAR_GET(present_allow_overscan_cutoff),
        REXCVAR_GET(present_safe_area_x), REXCVAR_GET(present_safe_area_y));
  };
  const auto first = to_window(rect.x, rect.y);
  const auto last = to_window(rect.x + rect.width, rect.y + rect.height);
  g_gestures.SetLanePowerup({first.x, first.y, last.x - first.x, last.y - first.y});
}
class Controls final : public rex::ui::ImGuiDialog {
 public:
  Controls(rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Window*()> window)
      : ImGuiDialog(drawer), window_(std::move(window)) {
    if (!SDL_AddEventWatch(Watch, this))
      REXLOG_ERROR("touch_controls: could not register touch events: {}", SDL_GetError());
    sensor_initialized_ = SDL_InitSubSystem(SDL_INIT_SENSOR);
    if (sensor_initialized_) {
      int count = 0;
      auto* ids = SDL_GetSensors(&count);
      for (int i = 0; i < count && !sensor_; ++i)
        if (SDL_GetSensorTypeForID(ids[i]) == SDL_SENSOR_ACCEL) sensor_ = SDL_OpenSensor(ids[i]);
      SDL_free(ids);
    }
    sensor_id_ = sensor_ ? SDL_GetSensorID(sensor_) : 0;
    if (!sensor_) REXLOG_WARN("touch_controls: shake deployment unavailable: {}", SDL_GetError());
    REXLOG_INFO("touch_controls: fixed left/right taps, horizontal swipes and pause enabled; notes fire on down; inverted swipes forgive their starting wrong press");
  }
  ~Controls() override {
    SDL_RemoveEventWatch(Watch, this);
    if (sensor_) SDL_CloseSensor(sensor_);
    if (sensor_initialized_) SDL_QuitSubSystem(SDL_INIT_SENSOR);
    CancelGameplay();
  }
 private:
  static bool SDLCALL Watch(void* data, SDL_Event* event) {
    auto& self = *static_cast<Controls*>(data);
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || event->type == SDL_EVENT_DID_ENTER_BACKGROUND) {
      g_foreground = false;
      CancelGameplay();
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
      g_foreground = true;
    }
    if (event->type == SDL_EVENT_SENSOR_UPDATE && event->sensor.which == self.sensor_id_) {
      const bool active = Available() && TouchInputActive() && REXCVAR_GET(touch_shake_deploy);
      std::lock_guard lock(g_mutex);
      if (!active || !g_powerup_ready) { g_shake.Reset(); return true; }
      const auto& sample = event->sensor;
      UpdateGestures(active);
      if (g_shake.Observe(sample.timestamp, sample.data[0], sample.data[1], sample.data[2]))
        g_gestures.DeployPowerup();
      return true;
    }
    Phase phase;
    switch (event->type) {
      case SDL_EVENT_FINGER_DOWN: phase = Phase::kDown; break;
      case SDL_EVENT_FINGER_MOTION: phase = Phase::kMove; break;
      case SDL_EVENT_FINGER_UP: phase = Phase::kUp; break;
      case SDL_EVENT_FINGER_CANCELED: phase = Phase::kCancel; break;
      default: return true;
    }
    const auto& finger = event->tfinger;
    // SDL can also synthesize finger events from a physical mouse or pen.
    // Those devices retain their own bindings and must never trigger gestures.
    if (!IsDirectTouch(finger.touchID)) return true;
    if (!self.window_id_ || finger.windowID != self.window_id_) return true;
    const bool active = Available();
    {
      std::lock_guard lock(g_mutex);
      UpdateGestures(active);
      if (active && phase == Phase::kDown) g_mouse_gate.Capture();
      g_gestures.Observe({finger.touchID, finger.fingerID, phase, finger.x, finger.y});
    }
    // Menu touches also establish the source before a song's HUD appears.
    if (phase == Phase::kDown && g_foreground && HostInputFocused()) last_device::Touch();
    return true;
  }
  void OnDraw(ImGuiIO&) override {
    auto* window = window_();
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_Window* sdl_window = windows && count == 1 ? windows[0] : nullptr;
    SDL_free(windows);
    int width = 0, height = 0;
    SDL_Rect safe{};
    if (sdl_window) {
      SDL_GetWindowSize(sdl_window, &width, &height);
      SDL_GetWindowSafeArea(sdl_window, &safe);
    }
    window_id_ = sdl_window ? SDL_GetWindowID(sdl_window) : 0;
    const Layout layout = MakeLayout(float(width), float(height),
        {float(safe.x), float(safe.y), float(safe.w), float(safe.h)});
    const bool active = window && window->HasFocus() && Available() && window_id_;
    {
      std::lock_guard lock(g_mutex);
      g_layout = layout;
      auto hit_layout = layout;
      if (!ButtonsEnabled()) { hit_layout.pause = {}; hit_layout.powerup = {}; }
      g_gestures.SetLayout(hit_layout);
      UpdateGestures(active);
      if (!active || !TouchInputActive() || !REXCVAR_GET(touch_shake_deploy)) g_shake.Reset();
    }
    g_visible = active && layout.pause.width > 0;
  }
  std::function<rex::ui::Window*()> window_;
  std::atomic<SDL_WindowID> window_id_{0};
  SDL_Sensor* sensor_ = nullptr;
  SDL_SensorID sensor_id_ = 0;
  bool sensor_initialized_ = false;
};
}
Actions SampleGameplay(bool active) {
  std::lock_guard lock(g_mutex);
  g_gestures.SetActive(active && g_foreground);
  return g_gestures.Sample();
}
void CancelGameplay() {
  g_visible = false;
  std::lock_guard lock(g_mutex);
  g_gestures.Cancel(); g_shake.Reset(); g_lane_powerup = {}; g_lane_timestamp = 0;
}
void PublishLanePowerup(Rect rect) {
  std::lock_guard lock(g_mutex);
  g_lane_powerup = rect;
  g_lane_timestamp = SDL_GetTicksNS();
}
Layout CurrentLayout() { std::lock_guard lock(g_mutex); return g_layout; }
bool TouchInputActive() { return last_device::Current().using_touch; }
bool ButtonsEnabled() { return REXCVAR_GET(touch_buttons); }
void PublishPowerupReady(bool ready) {
  static std::atomic<bool> observed{false};
  const bool changed = g_powerup_ready.exchange(ready) != ready;
  if (!ready && changed) { std::lock_guard lock(g_mutex); g_shake.Reset(); }
  if (!observed.exchange(true) || changed)
    REXLOG_INFO("touch_hud: native power-up deployable={}", ready);
}
bool PowerupReady() { return g_powerup_ready; }
bool ControlsVisible() { return g_visible && g_foreground && TouchInputActive() && ButtonsEnabled(); }
std::vector<GameplayEvent> DrainGameplay() {
  std::lock_guard lock(g_mutex);
  return g_gestures.DrainGameplay();
}
bool ConsumeMouse(bool is_touch, Phase phase, bool gameplay) {
  std::lock_guard lock(g_mutex);
  return g_mouse_gate.Consume(is_touch, phase, gameplay);
}
std::unique_ptr<rex::ui::ImGuiDialog> CreateControls(
    rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Window*()> window) {
  return std::make_unique<Controls>(drawer, std::move(window));
}
}
#else
namespace rb_blitz::input::touch {
Actions SampleGameplay(bool) { return {}; }
void CancelGameplay() {}
std::vector<GameplayEvent> DrainGameplay() { return {}; }
bool ConsumeMouse(bool, Phase, bool) { return false; }
Layout CurrentLayout() { return {}; }
bool ControlsVisible() { return false; }
bool TouchInputActive() { return false; }
bool ButtonsEnabled() { return REXCVAR_GET(touch_buttons); }
void PublishPowerupReady(bool) {}
void PublishLanePowerup(Rect) {}
bool PowerupReady() { return false; }
std::unique_ptr<rex::ui::ImGuiDialog> CreateControls(
    rex::ui::ImGuiDrawer*, std::function<rex::ui::Window*()>) { return {}; }
}
#endif
