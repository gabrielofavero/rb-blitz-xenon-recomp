// SPDX-License-Identifier: GPL-2.0-only
#include "input/mouse_ui.h"
#include "input/native_menu.h"
#include "input/remap.h"
#include "input/source_gates.h"
#include "input/title_input.h"

#include <cstring>
#include <limits>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <rex/cvar.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/ui/flags.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

REXCVAR_DEFINE_BOOL(mouse_ui_nav, true, "Input",
    "Native menu mouse: point to select, left/right click A/B, wheel up/down. "
    "Gameplay pointer capture follows the plan's D3 rule, not this switch alone.");

// Defined in src/input/native_menu.cpp; read here for the button-event trace only.
REXCVAR_DECLARE(bool, mouse_native_trace);

namespace rb_blitz::input {
using rex::X_STATUS;
using rex::X_RESULT;
namespace {
// The host's raw device state, written by the window thread only (the physical sampler reads it
// from the guest input thread). SDL documents `SDL_GetMouseState` and `SDL_GetKeyboardFocus` as
// main-thread calls, so the sampler must not ask SDL itself - it reads these instead.
std::atomic<uint32_t> g_mouse_buttons{0};
std::atomic<bool> g_window_focused{false};
// The wheel detents seen since the guest-side sampler last consumed them (P2 of the
// interface/gameplay mapping plan): positive away from the player, negative toward. Recorded
// whether or not Mouse Support is on, exactly like `g_mouse_buttons` - the sampler applies the
// gates, and a capture must be able to bind a detent while the device is disabled.
std::atomic<int32_t> g_wheel_detents{0};
// SDL's own answer to "does a window have keyboard focus", sampled on the main thread (SDL
// documents the query as main-thread only). Kept beside `g_window_focused` because the two can
// disagree, and a sampler that sees no keys has to be able to tell that case apart.
std::atomic<bool> g_sdl_keyboard_focused{false};

// The keyboard latch. P2's live arm measured SDL's global keyboard array as blind in this
// process - `SDL_GetKeyboardFocus()` null and every probe key up, while the window's own key
// *events* kept working (the MnK emulation navigates the menus with them). The sampler therefore
// reads this latch, which is fed by the very events the SDK dispatches to every window listener,
// instead of SDL's state array: the same source MnK proves on every acceptance run, and the shape
// plan D5 asks for (physical input sampled from raw device events).
std::array<std::atomic<bool>, SDL_SCANCODE_COUNT> g_keys_down;
// P7's capture needs the *name* of the key that was just pressed, which `HostKeyDown(name)`
// cannot answer (it asks about one name at a time). Every key-down is therefore also appended to
// a small serial-numbered ring; the filter consumes what arrived since its last observation, so a
// tap that begins and ends between two polls is still one event.
std::atomic<uint32_t> g_any_key_down{0};

struct KeyEvent {
  uint64_t serial = 0;
  std::string name;
};
std::mutex g_key_events_mutex;
std::deque<KeyEvent> g_key_events;
uint64_t g_key_event_serial = 0;

void PushKeyEvent(const std::string_view name) {
  std::lock_guard lock(g_key_events_mutex);
  KeyEvent event;
  event.serial = ++g_key_event_serial;
  event.name = std::string(name);
  g_key_events.push_back(std::move(event));
  while (g_key_events.size() > 32) {
    g_key_events.pop_front();
  }
}

// The SDL keycode for a Windows virtual key, for the keys a binding can reasonably name. Letters
// and digits are the ASCII offset every platform agrees on; the rest is the punctuation and
// navigation set the shipped tables use, plus the F-keys. Unknown keys latch nothing, which is
// the honest answer rather than a guessed scancode.
SDL_Keycode KeycodeForVirtualKey(rex::ui::VirtualKey key) {
  const uint16_t vk = static_cast<uint16_t>(key);
  if (vk >= 'A' && vk <= 'Z') {
    return SDLK_A + (vk - 'A');
  }
  if (vk >= '0' && vk <= '9') {
    return SDLK_0 + (vk - '0');
  }
  if (vk >= uint16_t(rex::ui::VirtualKey::kF1) &&
      vk < uint16_t(rex::ui::VirtualKey::kF1) + 12) {
    return SDLK_F1 + (vk - uint16_t(rex::ui::VirtualKey::kF1));
  }
  switch (key) {
    case rex::ui::VirtualKey::kMediaNextTrack: return SDLK_MEDIA_NEXT_TRACK;
    case rex::ui::VirtualKey::kMediaPrevTrack: return SDLK_MEDIA_PREVIOUS_TRACK;
    case rex::ui::VirtualKey::kMediaPlayPause: return SDLK_MEDIA_PLAY_PAUSE;
    case rex::ui::VirtualKey::kSpace:
      return SDLK_SPACE;
    case rex::ui::VirtualKey::kReturn:
      return SDLK_RETURN;
    case rex::ui::VirtualKey::kEscape:
      return SDLK_ESCAPE;
    case rex::ui::VirtualKey::kTab:
      return SDLK_TAB;
    case rex::ui::VirtualKey::kBack:
      return SDLK_BACKSPACE;
    case rex::ui::VirtualKey::kLeft:
      return SDLK_LEFT;
    case rex::ui::VirtualKey::kRight:
      return SDLK_RIGHT;
    case rex::ui::VirtualKey::kUp:
      return SDLK_UP;
    case rex::ui::VirtualKey::kDown:
      return SDLK_DOWN;
    case rex::ui::VirtualKey::kHome:
      return SDLK_HOME;
    case rex::ui::VirtualKey::kEnd:
      return SDLK_END;
    case rex::ui::VirtualKey::kPrior:
      return SDLK_PAGEUP;
    case rex::ui::VirtualKey::kNext:
      return SDLK_PAGEDOWN;
    case rex::ui::VirtualKey::kInsert:
      return SDLK_INSERT;
    case rex::ui::VirtualKey::kDelete:
      return SDLK_DELETE;
    case rex::ui::VirtualKey::kShift:
      return SDLK_LSHIFT;
    case rex::ui::VirtualKey::kControl:
      return SDLK_LCTRL;
    case rex::ui::VirtualKey::kMenu:
      return SDLK_LALT;
    case rex::ui::VirtualKey::kOem1:
      return SDLK_SEMICOLON;
    case rex::ui::VirtualKey::kOemComma:
      return SDLK_COMMA;
    case rex::ui::VirtualKey::kOemPeriod:
      return SDLK_PERIOD;
    case rex::ui::VirtualKey::kOem2:
      return SDLK_SLASH;
    case rex::ui::VirtualKey::kOem3:
      return SDLK_GRAVE;
    case rex::ui::VirtualKey::kOem4:
      return SDLK_LEFTBRACKET;
    case rex::ui::VirtualKey::kOem5:
      return SDLK_BACKSLASH;
    case rex::ui::VirtualKey::kOem6:
      return SDLK_RIGHTBRACKET;
    case rex::ui::VirtualKey::kOem7:
      return SDLK_APOSTROPHE;
    case rex::ui::VirtualKey::kOemMinus:
      return SDLK_MINUS;
    case rex::ui::VirtualKey::kOemPlus:
      return SDLK_EQUALS;
    default:
      break;
  }
  return SDLK_UNKNOWN;
}

void LatchKey(rex::ui::VirtualKey key, bool down) {
  const SDL_Keycode keycode = KeycodeForVirtualKey(key);
  if (keycode == SDLK_UNKNOWN) {
    return;
  }
  const SDL_Scancode scancode = SDL_GetScancodeFromKey(keycode, nullptr);
  if (scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_SCANCODE_COUNT) {
    const bool was = g_keys_down[scancode].exchange(down, std::memory_order_relaxed);
    if (down && !was) {
      g_any_key_down.fetch_add(1, std::memory_order_relaxed);
      if (const char* name = SDL_GetScancodeName(scancode); name != nullptr && name[0] != 0) {
        PushKeyEvent(name);
      }
    } else if (!down && was) {
      g_any_key_down.fetch_sub(1, std::memory_order_relaxed);
    }
  }
}

void ClearKeys() {
  for (auto& down : g_keys_down) {
    if (down.exchange(false, std::memory_order_relaxed)) {
      g_any_key_down.fetch_sub(1, std::memory_order_relaxed);
    }
  }
}
uint32_t MouseMask(rex::ui::MouseEvent::Button button) {
  switch (button) {
    case rex::ui::MouseEvent::Button::kLeft:
      return SDL_BUTTON_MASK(SDL_BUTTON_LEFT);
    case rex::ui::MouseEvent::Button::kRight:
      return SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);
    case rex::ui::MouseEvent::Button::kMiddle:
      return SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE);
    case rex::ui::MouseEvent::Button::kX1:
      return SDL_BUTTON_MASK(SDL_BUTTON_X1);
    case rex::ui::MouseEvent::Button::kX2:
      return SDL_BUTTON_MASK(SDL_BUTTON_X2);
    case rex::ui::MouseEvent::Button::kNone:
      break;
  }
  return 0;
}
class MousePad final : public rex::input::InputDriver {
 public:
  MousePad() : InputDriver(nullptr, 0) {}
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x4D4F5553);
  rex::X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    // P2 (plan D2): the synthetic connection follows *either* host gate, not just the mouse.
    // A keyboard-only configuration with no pad must stay connected - the guest's GetState
    // reports not-connected before the state filter runs when nothing enumerates - and a
    // mouse-only one must too. The device contributes no pad state of its own; it exists for
    // connectivity and for the menu pulses the filter merges after the user's remap. It is
    // synthetic, so the SDK parks it past every physical ordinal and rumble ignores it.
    if (!HostDeviceConnected({KeyboardSupportEnabled(), MouseSupportEnabled()})) return;
    rex::input::DeviceInfo info;
    info.id = kDevice; info.name = "Host input (native menus)"; info.synthetic = true;
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
  void EndDrag(bool cancel = true) {
    if (!dragging_) return;
    dragging_ = false;
    if (cancel) QueueMenuScrollbar(MouseAction::kScrollbarUp, {NAN, NAN});
    if (window_) window_->ReleaseMouse();
  }
  void Detach() {
    if (!window_) return;
    EndDrag();
    window_->RemoveInputListener(this); window_->RemoveListener(this);
    window_ = nullptr; g_mouse_buttons = 0; g_window_focused = false; ClearKeys();
    g_wheel_detents = 0;
    MouseMenuFocus(false);
  }
  void OnClosing(rex::ui::UIEvent&) override { Detach(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    EndDrag();
    // A lost window has no held buttons or keys from the host's point of view: the releases that
    // happen outside it never reach this listener, so the latches must not stay pressed.
    g_mouse_buttons = 0;
    g_window_focused = false;
    ClearKeys();
    g_wheel_detents = 0;
    MouseMenuFocus(false);
  }
  void OnGotFocus(rex::ui::UISetupEvent&) override { g_window_focused = true; MouseMenuFocus(true); }
  void OnKeyDown(rex::ui::KeyEvent& e) override { LatchKey(e.virtual_key(), true); }
  void OnKeyUp(rex::ui::KeyEvent& e) override { LatchKey(e.virtual_key(), false); }
  void OnDraw(ImGuiIO& io) override {
    if (!window_) {
      window_ = provider_();
      if (!window_) return;
      // Above the keyboard's mouse bindings (0), below the SDK dialogs.
      window_->AddInputListener(this, 1); window_->AddListener(this);
      g_window_focused = window_->HasFocus();
      MouseMenuFocus(g_window_focused);
    }
    MouseMenuFocus(window_->HasFocus() && !io.WantCaptureMouse);
    if (!MouseMenuAvailable() || io.WantCaptureMouse ||
        (dragging_ && (CurrentScreen() != drag_screen_ || CurrentFocus() != drag_focus_))) EndDrag();
    // The focus facts are read on the main thread where SDL allows them, and per frame because
    // this listener attaches after the window exists (so the initial focus has no OnGotFocus).
    g_window_focused = window_->HasFocus();
    g_sdl_keyboard_focused = SDL_GetKeyboardFocus() != nullptr;
    // The raw mask is the event latch, not `SDL_GetMouseState`: P2's live arm measured SDL's
    // device state blind in this process (both the key and the mouse arrays read zero while the
    // events keep arriving), so re-reading it here would erase a press the events just recorded.
    // Focus loss clears the latch; a press that never delivers its up is the same kind of stale
    // input the native queue discards on a boundary.
    // P2: the cursor rule is one pure function (source_gates.h), so "the cursor stays hidden in
    // gameplay" is a test rather than a reading of this expression. A live song has no native
    // menu to point at, and no overlay asked for capture, so nothing is shown.
    const bool visible = CursorVisible(MouseSupportEnabled(), MouseMenuAvailable(), io.WantCaptureMouse);
    const auto visibility = visible ? rex::ui::Window::CursorVisibility::kVisible
                                    : rex::ui::Window::CursorVisibility::kHidden;
    auto* window = window_;
    window->app_context().CallInUIThread([window, visibility] {
      window->SetCursorVisibility(visibility);
      // SDL owns a process-wide cursor; keep it synchronized after startup too.
      if (visibility == rex::ui::Window::CursorVisibility::kVisible) SDL_ShowCursor();
      else SDL_HideCursor();
    });
  }
  std::optional<MenuPoint> Point(const rex::ui::MouseEvent& e, bool clip = true) const {
    if (!window_) return {};
    auto p = MenuPointer(float(e.x()), float(e.y()),
        window_->GetActualPhysicalWidth(), window_->GetActualPhysicalHeight(),
        REXCVAR_GET(video_mode_width), REXCVAR_GET(video_mode_height),
        REXCVAR_GET(present_letterbox), REXCVAR_GET(present_allow_overscan_cutoff),
        REXCVAR_GET(present_safe_area_x), REXCVAR_GET(present_safe_area_y));
    if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
        (clip && !MenuBounds{0, 0, 1, 1}.Contains(p))) return {};
    return p;
  }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!MouseSupportEnabled()) return;
    const bool moved = e.x() != last_x_ || e.y() != last_y_;
    if (auto p = Point(e, !dragging_)) {
      if (dragging_) QueueMenuScrollbar(MouseAction::kScrollbarDrag, *p);
      else if (moved) QueueMenuMouse(MouseAction::kMove, *p);
    } else if (moved && !dragging_) {
      // A move that lands outside the game rectangle (pillarbox, overscan cutoff) has left
      // every row: the guest must not keep hovering one. A point that no bounds can contain
      // stands in for the departure, and a later inside move coalesces back onto it.
      const float miss = std::numeric_limits<float>::quiet_NaN();
      QueueMenuMouse(MouseAction::kMove, {miss, miss});
    }
    last_x_ = e.x(); last_y_ = e.y();
    e.set_handled(true);
  }
  void OnMouseDown(rex::ui::MouseEvent& e) override {
    // The raw mask is device state, so it is recorded whatever the gates say: the sampler applies
    // the gates, and a mouse button recorded while Mouse Support is off must not look "never
    // pressed" once it is turned on.
    g_mouse_buttons.fetch_or(MouseMask(e.button()));
    if (REXCVAR_GET(mouse_native_trace))
      REXLOG_INFO("native_mouse: button={} down mask={:02X}", static_cast<int>(e.button()),
                  g_mouse_buttons.load());
    if (!MouseSupportEnabled()) return;
    if (auto p = Point(e)) {
      if (e.button() == rex::ui::MouseEvent::Button::kLeft) {
        // The gutter belongs to the scrollbar: a press there must not click the row,
        // and a miss just outside the strip must not press A either.
        if (MouseMenuRailHit(*p)) {
          EndDrag();
          dragging_ = true;
          drag_screen_ = CurrentScreen(); drag_focus_ = CurrentFocus();
          window_->CaptureMouse();
          QueueMenuScrollbar(MouseAction::kScrollbarDown, *p);
        } else if (!MouseMenuScrollZone(*p)) QueueMenuMouse(MouseAction::kAccept, *p);
      }
      if (e.button() == rex::ui::MouseEvent::Button::kRight) QueueMenuMouse(MouseAction::kBack, *p);
    }
    e.set_handled(true);
  }
  void OnMouseUp(rex::ui::MouseEvent& e) override {
    g_mouse_buttons.fetch_and(~MouseMask(e.button()));
    if (REXCVAR_GET(mouse_native_trace))
      REXLOG_INFO("native_mouse: button={} up mask={:02X}", static_cast<int>(e.button()),
                  g_mouse_buttons.load());
    if (dragging_ && e.button() == rex::ui::MouseEvent::Button::kLeft) {
      const float miss = std::numeric_limits<float>::quiet_NaN();
      QueueMenuScrollbar(MouseAction::kScrollbarUp, Point(e, false).value_or(MenuPoint{miss, miss}));
      EndDrag(false);
    }
    if (!MouseSupportEnabled()) return;
    e.set_handled(true);
  }
  void OnMouseWheel(rex::ui::MouseEvent& e) override {
    // Device state first, gate second: the sampler applies the gates, and a capture binds a
    // detent while Mouse Support is off.
    const float scroll = e.scroll_y();
    if (scroll > 0.0f) {
      g_wheel_detents.fetch_add(1, std::memory_order_relaxed);
    } else if (scroll < 0.0f) {
      g_wheel_detents.fetch_add(-1, std::memory_order_relaxed);
    }
    if (!MouseSupportEnabled()) return;
    // The point rides the detent: the guest re-catches the row that lands under it when the
    // scroll settles (MouseScrollFollow in native_menu.h).
    if (auto p = Point(e)) QueueMenuWheel(e.scroll_y(), *p);
    e.set_handled(true);
  }
  std::function<rex::ui::Window*()> provider_;
  rex::ui::Window* window_ = nullptr;
  int last_x_ = -1, last_y_ = -1;
  bool dragging_ = false;
  uint32_t drag_screen_ = 0, drag_focus_ = 0;
};
}

uint32_t HostMouseButtons() { return g_mouse_buttons.load(std::memory_order_relaxed); }
int32_t HostTakeMouseWheel() {
  return g_wheel_detents.exchange(0, std::memory_order_relaxed);
}
bool HostInputFocused() { return g_window_focused.load(std::memory_order_relaxed); }
bool HostSdlKeyboardFocused() {
  return g_sdl_keyboard_focused.load(std::memory_order_relaxed);
}
bool HostKeyDown(std::string_view name) {
  const SDL_Scancode scancode = SDL_GetScancodeFromName(std::string(name).c_str());
  if (scancode == SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT) {
    return false;
  }
  if (g_keys_down[scancode].load(std::memory_order_relaxed)) {
    return true;
  }
  // SDL's own array is still consulted as a fallback for a key that was already held when this
  // listener attached (a boot started with a key down); reading the array is documented as safe
  // from any thread even though the query that this file calls on the main thread is not.
  int count = 0;
  const bool* keys = SDL_GetKeyboardState(&count);
  return keys != nullptr && scancode < count && keys[scancode];
}
bool HostAnyKeyDown() { return g_any_key_down.load(std::memory_order_relaxed) != 0; }
void HostKeyEventsSince(uint64_t* serial, std::vector<std::string>* names) {
  if (serial == nullptr || names == nullptr) {
    return;
  }
  std::lock_guard lock(g_key_events_mutex);
  for (const KeyEvent& event : g_key_events) {
    if (event.serial > *serial) {
      names->push_back(event.name);
      *serial = event.serial;
    }
  }
  // A ring that wrapped past the caller's serial leaves nothing to replay: the next press is the
  // capture's input, and inventing the lost events would capture a key nobody pressed now.
  if (*serial < g_key_event_serial && g_key_events.empty()) {
    *serial = g_key_event_serial;
  }
}

void InstallMouseUiNavigation(rex::RuntimeConfig& config) {
  if (config.tool_mode) return;
  auto backend = config.input_factory
                     ? std::move(config.input_factory)
                     : REX_INPUT_BACKEND(rb_blitz::input::CreateTitleInputSystem);
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
