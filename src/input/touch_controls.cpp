// SPDX-License-Identifier: GPL-2.0-only
#include "input/touch_controls.h"
#include <rex/ui/imgui_dialog.h>
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(touch_buttons, true, "Input", "Show gameplay buttons while touch is the active input.");

#if defined(RB_BLITZ_IOS)
#include <atomic>
#include <mutex>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <rex/logging.h>
#include <rex/kernel/xam/module.h>
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
MouseGate g_mouse_gate;
std::atomic<bool> g_foreground{true};
std::atomic<bool> g_visible{false};
Layout g_layout;
bool Available() {
  return g_foreground && GameScreenActive() && HostInputFocused() &&
         !rex::kernel::xam::xeXamIsUIActive() && !ui::achievements::Active() && !capture::Armed();
}
class Controls final : public rex::ui::ImGuiDialog {
 public:
  Controls(rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Window*()> window)
      : ImGuiDialog(drawer), window_(std::move(window)) {
    if (!SDL_AddEventWatch(Watch, this))
      REXLOG_ERROR("touch_controls: could not register touch events: {}", SDL_GetError());
    REXLOG_INFO("touch_controls: fixed left/right taps, horizontal swipes and pause enabled; notes fire on down; inverted swipes forgive their starting wrong press");
  }
  ~Controls() override { SDL_RemoveEventWatch(Watch, this); CancelGameplay(); }
 private:
  static bool SDLCALL Watch(void* data, SDL_Event* event) {
    auto& self = *static_cast<Controls*>(data);
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || event->type == SDL_EVENT_DID_ENTER_BACKGROUND) {
      g_foreground = false;
      CancelGameplay();
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
      g_foreground = true;
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
      g_gestures.SetActive(active);
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
      g_gestures.SetLayout(layout);
      g_gestures.SetActive(active);
    }
    g_visible = active && layout.pause.width > 0;
  }
  std::function<rex::ui::Window*()> window_;
  std::atomic<SDL_WindowID> window_id_{0};
};
}
Actions SampleGameplay(bool active) {
  std::lock_guard lock(g_mutex);
  g_gestures.SetActive(active && g_foreground);
  return g_gestures.Sample();
}
void CancelGameplay() {
  g_visible = false;
  std::lock_guard lock(g_mutex); g_gestures.Cancel();
}
Layout CurrentLayout() { std::lock_guard lock(g_mutex); return g_layout; }
bool TouchInputActive() { return last_device::Current().using_touch; }
bool ButtonsEnabled() { return REXCVAR_GET(touch_buttons); }
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
std::unique_ptr<rex::ui::ImGuiDialog> CreateControls(
    rex::ui::ImGuiDrawer*, std::function<rex::ui::Window*()>) { return {}; }
}
#endif
