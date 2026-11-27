// SPDX-License-Identifier: GPL-2.0-only
#include "input/touch_controls.h"
#include <rex/ui/ui_system.h>
#include "settings/runtime_settings.h"






namespace rb_blitz::input::touch {
bool PreviewEnabled() { return rb_blitz::settings::title::touch_preview.Get(); }
}

#include <atomic>
#include <mutex>
#include <optional>
#include <set>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <rex/logging/log_category.h>
#include "input/title_input.h"
#include "input/last_device.h"
#include "input/input_capture.h"
#include "input/native_menu.h"
#include "hooks/render_settings.h"
#include "settings/portrait_render_target.h"
#include "input/touch_timing.h"
#include "input/remap.h"
#include "ui/achievements_overlay.h"

namespace rb_blitz::input::touch {
namespace {
static_assert(static_cast<uint64_t>(SDL_MOUSE_TOUCHID) == UINT64_MAX);
static_assert(static_cast<uint64_t>(SDL_PEN_TOUCHID) == UINT64_MAX - 1);
std::mutex g_mutex;
Gestures g_gestures;
CalibrationTaps g_calibration;
uint64_t g_calibration_updated_ns = 0;
MotionDetector g_motion;
PowerupTarget g_powerup_target;
Rect g_lane_powerup;
uint64_t g_lane_timestamp = 0;
MouseGate g_mouse_gate;
std::atomic<bool> g_foreground{true};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_powerup_ready{false};
Layout g_layout;
bool Available() {
  return g_foreground && GameScreenActive() && HostInputFocused() &&
         !SystemUIActive() && !ui::achievements::Active() && !capture::Armed();
}
MenuPoint LanePointToWindow(float x, float y) {
  using namespace rb_blitz::settings;
  const auto source = PresentationPointerSource(
      {int32_t(BootRenderWidth()), int32_t(BootRenderHeight())},
      {ReadSdkSetting<int32_t>("ui.video_mode_width"), ReadSdkSetting<int32_t>("ui.video_mode_height")},
      ReadSdkSetting<bool>("gpu.xenos.present_source_aspect"));
  return MenuPointToWindow({x, y}, int(g_layout.width), int(g_layout.height), source.width, source.height,
      ReadSdkSetting<bool>("gpu.xenos.present_letterbox"),
      ReadSdkSetting<bool>("gpu.xenos.present_allow_overscan_cutoff"),
      ReadSdkSetting<int32_t>("gpu.xenos.present_safe_area_x"),
      ReadSdkSetting<int32_t>("gpu.xenos.present_safe_area_y"));
}

void UpdateGestures(bool active) {
  g_gestures.SetActive(active);
  g_gestures.SetInvertedLaneSwipe(rb_blitz::settings::title::touch_lane_swipe.Get() != "normal");
  g_gestures.SetPowerupAvailable(active && g_powerup_ready);
  const auto now = SDL_GetTicksNS();
  const auto rect = g_lane_timestamp && now - g_lane_timestamp < 100'000'000
      ? g_lane_powerup : Rect{};
  const auto first = LanePointToWindow(rect.x, rect.y);
  const auto last = LanePointToWindow(rect.x + rect.width, rect.y + rect.height);
  g_powerup_target.SetTarget({first.x, first.y, last.x - first.x, last.y - first.y},
                            active && g_powerup_ready && rect.width > 0);
}
PointerResult ObserveLanePointer(const FingerEvent& event) {
  const auto result = g_powerup_target.Observe(event);
  if (result.deploy) g_gestures.DeployPowerup();
  return result;
}
class Controls final : public rex::ui::DialogBase {
 public:
  explicit Controls(rex::ui::IUISystem& ui) : ui_(ui) {
    if (!SDL_AddEventWatch(Watch, this))
      REXLOG_ERROR("touch_controls: could not register touch events: {}", SDL_GetError());
    // Motion sensors are needed by mobile play and the explicit desktop preview.
#if defined(RB_BLITZ_IOS)
    const bool motion_enabled = true;
#else
    const bool motion_enabled = PreviewEnabled();
#endif
    sensor_initialized_ = motion_enabled && SDL_InitSubSystem(SDL_INIT_SENSOR);
    if (sensor_initialized_) {
      int count = 0;
      auto* ids = SDL_GetSensors(&count);
      for (int i = 0; i < count && !sensor_; ++i)
        if (SDL_GetSensorTypeForID(ids[i]) == SDL_SENSOR_ACCEL) sensor_ = SDL_OpenSensor(ids[i]);
      SDL_free(ids);
    }
    sensor_id_ = sensor_ ? SDL_GetSensorID(sensor_) : 0;
    if (motion_enabled && !sensor_) REXLOG_WARN("touch_controls: motion deployment unavailable: {}", SDL_GetError());
    REXLOG_INFO("touch_controls: native lane power-up pointer enabled; mobile taps, swipes and pause use direct touch");
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
      CancelMenuTouch();
      std::lock_guard lock(self.menu_mutex_);
      self.menu_fingers_.clear();
      self.menu_primary_.reset();
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
      g_foreground = true;
    }
    if (event->type == SDL_EVENT_SENSOR_UPDATE && event->sensor.which == self.sensor_id_) {
      const auto mode = ParseMotionMode(rb_blitz::settings::title::touch_motion_deploy.Get());
      const bool active = Available() && TouchInputActive() && mode != MotionMode::kNone;
      std::lock_guard lock(g_mutex);
      if (!active || !g_powerup_ready) { g_motion.Reset(); return true; }
      const auto& sample = event->sensor;
      UpdateGestures(active);
      if (g_motion.Observe(mode, sample.timestamp, sample.data[0], sample.data[1], sample.data[2]))
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
    if (phase == Phase::kCancel) CancelMenuTouch();
    const bool active = Available();
    if (phase == Phase::kDown && g_foreground && HostInputFocused()) last_device::Touch();
    {
      std::lock_guard menu_lock(self.menu_mutex_);
      const auto key = std::pair<uint64_t, uint64_t>{finger.touchID, finger.fingerID};
      if (phase == Phase::kDown) {
        if (self.menu_fingers_.empty()) self.menu_primary_ = key;
        self.menu_fingers_.insert(key);
      }
      bool menu_captured = false;
      if (self.menu_primary_ == key) {
        std::lock_guard lock(g_mutex);
        const auto now = SDL_GetTicksNS();
        if (!g_foreground || !HostInputFocused() || SystemUIActive() ||
            ui::achievements::Active() || !g_calibration_updated_ns ||
            InputAgeNs(g_calibration_updated_ns, now) > 100'000'000)
          g_calibration.SetTarget(0);
        menu_captured = g_calibration.Observe(phase, finger.timestamp ? finger.timestamp : now);
      }
      if (!menu_captured && self.menu_primary_ == key && !active && g_foreground && HostInputFocused()) {
        const auto menu_phase = phase == Phase::kDown ? MenuTouchPhase::kDown :
            phase == Phase::kMove ? MenuTouchPhase::kMove :
            phase == Phase::kUp ? MenuTouchPhase::kUp : MenuTouchPhase::kCancel;
        const auto layout = CurrentLayout();
        const float x = finger.x * layout.width, y = finger.y * layout.height;
        const auto point = MenuWindowPointer(x, y, int(layout.width), int(layout.height));
        const auto now = SDL_GetTicksNS();
        const double age = finger.timestamp && now > finger.timestamp ?
            double(now - finger.timestamp) * 1e-9 : 0;
        menu_captured = QueueMenuTouch(menu_phase, point, x, y, age);
      }
      if (phase == Phase::kDown && self.menu_primary_ == key) {
        std::lock_guard lock(g_mutex);
        g_mouse_gate.MenuBegin(menu_captured);
      }
      if (phase == Phase::kUp || phase == Phase::kCancel) {
        self.menu_fingers_.erase(key);
        if (self.menu_primary_ == key) self.menu_primary_.reset();
      }
    }
    {
      std::lock_guard lock(g_mutex);
      UpdateGestures(active);
      const bool target = ObserveLanePointer({finger.touchID, finger.fingerID, phase,
          finger.x * g_layout.width, finger.y * g_layout.height}).consumed;
#if defined(RB_BLITZ_IOS)
      const bool touch_gameplay = true;
#else
      const bool touch_gameplay = PreviewEnabled();
#endif
      if (active && phase == Phase::kDown && (target || touch_gameplay)) g_mouse_gate.Capture();
      if (!target && touch_gameplay)
        g_gestures.Observe({finger.touchID, finger.fingerID, phase, finger.x, finger.y,
            finger.timestamp ? finger.timestamp : SDL_GetTicksNS()});
    }
    return true;
  }
  bool Draw(ImGuiIO&) override {
    auto native = ui_.GetNativeWindow();
    auto* sdl_window = native ? static_cast<SDL_Window*>(native->sdl) : nullptr;
    int width = 0, height = 0;
    SDL_Rect safe{};
    if (sdl_window) {
      SDL_GetWindowSize(sdl_window, &width, &height);
      SDL_GetWindowSafeArea(sdl_window, &safe);
    }
    window_id_ = sdl_window ? SDL_GetWindowID(sdl_window) : 0;
    const Layout layout = MakeLayout(float(width), float(height),
        {float(safe.x), float(safe.y), float(safe.w), float(safe.h)});
    const bool active = ui_.IsFocused() && Available() && window_id_;
    if (CurrentLayout() != layout) CancelMenuTouch();
    {
      std::lock_guard lock(g_mutex);
      if (g_layout != layout) g_powerup_target.Cancel();
      g_layout = layout;
      auto hit_layout = layout;
      if (!ButtonsEnabled()) hit_layout.pause = {};
      g_gestures.SetLayout(hit_layout);
      UpdateGestures(active);
      if (!active || !TouchInputActive() || ParseMotionMode(rb_blitz::settings::title::touch_motion_deploy.Get()) == MotionMode::kNone)
        g_motion.Reset();
    }
    g_visible = active && layout.pause.width > 0;
    return true;
  }
  rex::ui::IUISystem& ui_;
  std::atomic<SDL_WindowID> window_id_{0};
  SDL_Sensor* sensor_ = nullptr;
  SDL_SensorID sensor_id_ = 0;
  bool sensor_initialized_ = false;
  std::mutex menu_mutex_;
  std::set<std::pair<uint64_t, uint64_t>> menu_fingers_;
  std::optional<std::pair<uint64_t, uint64_t>> menu_primary_;
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
  g_gestures.Cancel(); g_motion.Reset(); g_powerup_target.Cancel();
  g_calibration.Cancel();
  g_calibration_updated_ns = 0;
  g_lane_powerup = {}; g_lane_timestamp = 0;
}
void PublishLanePowerup(Rect rect) {
  std::lock_guard lock(g_mutex);
  g_lane_powerup = rect;
  g_lane_timestamp = SDL_GetTicksNS();
}
Layout CurrentLayout() { std::lock_guard lock(g_mutex); return g_layout; }
bool TouchInputActive() {
#if defined(RB_BLITZ_IOS)
  return PreviewEnabled() || last_device::Current().using_touch;
#else
  return PreviewEnabled() || (rb_blitz::settings::title::mouse_as_touch.Get() &&
                              last_device::Current().using_touch);
#endif
}
bool ButtonsEnabled() { return rb_blitz::settings::title::touch_buttons.Get(); }
void PublishPowerupReady(bool ready) {
  static std::atomic<bool> observed{false};
  const bool changed = g_powerup_ready.exchange(ready) != ready;
  if (!ready && changed) {
    std::lock_guard lock(g_mutex);
    g_motion.Reset();
    g_powerup_target.SetTarget({}, false);
  }
  if (!observed.exchange(true) || changed)
    REXLOG_INFO("touch_hud: native power-up deployable={}", ready);
}
bool PowerupReady() { return PreviewEnabled() || g_powerup_ready; }
bool LanePowerupAvailable() {
  if (!Available() || !g_powerup_ready) return false;
  std::lock_guard lock(g_mutex);
  const auto now = SDL_GetTicksNS();
  return g_lane_timestamp && now - g_lane_timestamp < 100'000'000 &&
      g_lane_powerup.width > 0 && g_lane_powerup.height > 0;
}
bool ConsumeLanePointer(Phase phase, float x, float y) {
  const bool active = Available();
  std::lock_guard lock(g_mutex);
  UpdateGestures(active);
  const auto point = LanePointToWindow(x, y);
  return ObserveLanePointer({UINT64_MAX, 0, phase, point.x, point.y}).consumed;
}
bool ControlsVisible() { return g_visible && g_foreground && TouchInputActive() && ButtonsEnabled(); }
std::vector<GameplayEvent> DrainGameplay() {
  std::lock_guard lock(g_mutex);
  return g_gestures.DrainGameplay();
}
void PublishCalibrationTarget(uint32_t target) {
  std::lock_guard lock(g_mutex);
  g_calibration.SetTarget(target);
  g_calibration_updated_ns = SDL_GetTicksNS();
}
std::vector<uint64_t> DrainCalibrationTaps(uint32_t target) {
  std::lock_guard lock(g_mutex);
  return g_calibration.Drain(target);
}
bool ConsumeMouse(bool is_touch, Phase phase, bool gameplay) {
  std::lock_guard lock(g_mutex);
  return g_mouse_gate.Consume(is_touch, phase, gameplay && TouchInputActive());
}
std::unique_ptr<rex::ui::IDialog> CreateControls(rex::ui::IUISystem& ui) {
  return std::make_unique<Controls>(ui);
}
}
