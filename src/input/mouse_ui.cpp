// SPDX-License-Identifier: GPL-2.0-only
#include "input/mouse_ui.h"
#include "input/native_menu.h"
#include "input/remap.h"
#include "input/last_device.h"
#include "input/source_gates.h"
#include "input/title_input.h"
#include "input/touch_controls.h"
#include "ui/menu_music_player.h"

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
#include "settings/runtime_settings.h"
#include <rex/logging/log_category.h>



// Defined in src/input/native_menu.cpp; read here for the button-event trace only.


namespace rb_blitz::input {
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
SDL_Keycode KeycodeForVirtualKey(rex::VirtualKey key) {
  const uint16_t vk = static_cast<uint16_t>(key);
  if (vk >= 'A' && vk <= 'Z') {
    return SDLK_A + (vk - 'A');
  }
  if (vk >= '0' && vk <= '9') {
    return SDLK_0 + (vk - '0');
  }
  if (vk >= uint16_t(rex::VirtualKey::kF1) &&
      vk < uint16_t(rex::VirtualKey::kF1) + 12) {
    return SDLK_F1 + (vk - uint16_t(rex::VirtualKey::kF1));
  }
  switch (key) {
    case rex::VirtualKey::kMediaNextTrack: return SDLK_MEDIA_NEXT_TRACK;
    case rex::VirtualKey::kMediaPrevTrack: return SDLK_MEDIA_PREVIOUS_TRACK;
    case rex::VirtualKey::kMediaPlayPause: return SDLK_MEDIA_PLAY_PAUSE;
    case rex::VirtualKey::kSpace:
      return SDLK_SPACE;
    case rex::VirtualKey::kReturn:
      return SDLK_RETURN;
    case rex::VirtualKey::kEscape:
      return SDLK_ESCAPE;
    case rex::VirtualKey::kTab:
      return SDLK_TAB;
    case rex::VirtualKey::kBack:
      return SDLK_BACKSPACE;
    case rex::VirtualKey::kLeft:
      return SDLK_LEFT;
    case rex::VirtualKey::kRight:
      return SDLK_RIGHT;
    case rex::VirtualKey::kUp:
      return SDLK_UP;
    case rex::VirtualKey::kDown:
      return SDLK_DOWN;
    case rex::VirtualKey::kHome:
      return SDLK_HOME;
    case rex::VirtualKey::kEnd:
      return SDLK_END;
    case rex::VirtualKey::kPrior:
      return SDLK_PAGEUP;
    case rex::VirtualKey::kNext:
      return SDLK_PAGEDOWN;
    case rex::VirtualKey::kInsert:
      return SDLK_INSERT;
    case rex::VirtualKey::kDelete:
      return SDLK_DELETE;
    case rex::VirtualKey::kShift:
      return SDLK_LSHIFT;
    case rex::VirtualKey::kControl:
      return SDLK_LCTRL;
    case rex::VirtualKey::kMenu:
      return SDLK_LALT;
    case rex::VirtualKey::kOem1:
      return SDLK_SEMICOLON;
    case rex::VirtualKey::kOemComma:
      return SDLK_COMMA;
    case rex::VirtualKey::kOemPeriod:
      return SDLK_PERIOD;
    case rex::VirtualKey::kOem2:
      return SDLK_SLASH;
    case rex::VirtualKey::kOem3:
      return SDLK_GRAVE;
    case rex::VirtualKey::kOem4:
      return SDLK_LEFTBRACKET;
    case rex::VirtualKey::kOem5:
      return SDLK_BACKSLASH;
    case rex::VirtualKey::kOem6:
      return SDLK_RIGHTBRACKET;
    case rex::VirtualKey::kOem7:
      return SDLK_APOSTROPHE;
    case rex::VirtualKey::kOemMinus:
      return SDLK_MINUS;
    case rex::VirtualKey::kOemPlus:
      return SDLK_EQUALS;
    default:
      break;
  }
  return SDLK_UNKNOWN;
}

void LatchKey(rex::VirtualKey key, bool down) {
  const SDL_Keycode keycode = KeycodeForVirtualKey(key);
  if (keycode == SDLK_UNKNOWN) {
    return;
  }
  const SDL_Scancode scancode = SDL_GetScancodeFromKey(keycode, nullptr);
  if (scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_SCANCODE_COUNT) {
    const bool was = g_keys_down[scancode].exchange(down, std::memory_order_relaxed);
    if (down && !was) {
      g_any_key_down.fetch_add(1, std::memory_order_relaxed);
      if (auto changed = last_device::KeyDown())
        REXLOG_INFO("button_schemes: active {} (device keyboard)", changed->scheme);
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
uint32_t MouseMask(rex::MouseButton button) {
  switch (button) {
    case rex::MouseButton::kLeft:
      return SDL_BUTTON_MASK(SDL_BUTTON_LEFT);
    case rex::MouseButton::kRight:
      return SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);
    case rex::MouseButton::kMiddle:
      return SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE);
    case rex::MouseButton::kX1:
      return SDL_BUTTON_MASK(SDL_BUTTON_X1);
    case rex::MouseButton::kX2:
      return SDL_BUTTON_MASK(SDL_BUTTON_X2);
    case rex::MouseButton::kNone: return 0;
  }
  return 0;
}
class MouseCursor final : public rex::ui::DialogBase, public rex::ui::IUIEventListener {
 public:
  explicit MouseCursor(rex::ui::IUISystem& ui) : ui_(ui) {
    listener_ = ui_.AddEventListener(*this);
    g_window_focused = ui_.IsFocused();
    last_device::Focus(g_window_focused);
    MouseMenuFocus(g_window_focused);
  }
  ~MouseCursor() override { Detach(); }
 private:
  void EndDrag(bool cancel = true) {
    if (!dragging_) return;
    dragging_ = false;
    if (cancel) QueueMenuScrollbar(MouseAction::kScrollbarUp, {NAN, NAN});
    SDL_CaptureMouse(false);
  }
  void Detach() {
    EndLanePointer();
    EndDrag();
    ui::music::CancelPlayerPointer();
    listener_ = {}; g_mouse_buttons = 0; g_window_focused = false; ClearKeys();
    last_device::Focus(false);
    g_wheel_detents = 0;
    MouseMenuFocus(false);
  }
  void OnUIEvent(const rex::ui::UIEvent& event) override {
    // Listener guards wait for delivery; release the subscription after this callback returns.
    if (event.GetType() == rex::ui::UIEvent::Type::kClosing) { OnLostFocus(); return; }
    if (event.GetType() == rex::ui::UIEvent::Type::kFocus) {
      if (auto* window = Window()) {
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) OnGotFocus(); else OnLostFocus();
      }
    }
  }
  void OnInputEvent(rex::InputEvent& event) override {
    if (event.handled && event.type != rex::InputEvent::Type::kKeyUp &&
        event.type != rex::InputEvent::Type::kButtonUp) return;
    switch (event.type) {
      case rex::InputEvent::Type::kKeyDown: LatchKey(event.key, true); break;
      case rex::InputEvent::Type::kKeyUp: LatchKey(event.key, false); break;
      case rex::InputEvent::Type::kMotion: OnMouseMove(event); break;
      case rex::InputEvent::Type::kButtonDown: OnMouseDown(event); break;
      case rex::InputEvent::Type::kButtonUp: OnMouseUp(event); break;
      case rex::InputEvent::Type::kWheel: OnMouseWheel(event); break;
      default: break;
    }
  }
  SDL_Window* Window() const {
    auto native = ui_.GetNativeWindow();
    return native ? static_cast<SDL_Window*>(native->sdl) : nullptr;
  }
  int Width() const { int w = 0; if (auto* window = Window()) SDL_GetWindowSizeInPixels(window, &w, nullptr); return w; }
  int Height() const { int h = 0; if (auto* window = Window()) SDL_GetWindowSizeInPixels(window, nullptr, &h); return h; }
  float ToLogical() const { int logical = 0; if (auto* window = Window()) SDL_GetWindowSize(window, &logical, nullptr); const int physical = Width(); return physical ? float(logical) / physical : 1.0f; }

  void OnLostFocus() {
    EndLanePointer();
    EndDrag();
    ui::music::CancelPlayerPointer();
    // A lost window has no held buttons or keys from the host's point of view: the releases that
    // happen outside it never reach this listener, so the latches must not stay pressed.
    g_mouse_buttons = 0;
    g_window_focused = false;
    last_device::Focus(false);
    ClearKeys();
    g_wheel_detents = 0;
    MouseMenuFocus(false);
  }
  void OnGotFocus() {
    g_window_focused = true; last_device::Focus(true); MouseMenuFocus(true);
  }
  bool Draw(ImGuiIO& io) override {
    if (!ui_.HasWindow()) return true;
    MouseMenuFocus(ui_.IsFocused() && !io.WantCaptureMouse && !ui::music::PlayerPointerOver());
    if (lane_pointer_captured_ && (!touch::LanePowerupAvailable() || io.WantCaptureMouse)) EndLanePointer();
    if (!MouseMenuAvailable() || io.WantCaptureMouse ||
        (dragging_ && (CurrentScreen() != drag_screen_ || CurrentFocus() != drag_focus_))) EndDrag();
    // The focus facts are read on the main thread where SDL allows them, and per frame because
    // this listener attaches after the window exists (so the initial focus has no OnGotFocus).
    const bool focused = ui_.IsFocused();
    if (focused != g_window_focused.load()) {
      if (focused) OnGotFocus(); else OnLostFocus();
    }
    if (auto changed = last_device::Tick())
      REXLOG_INFO("button_schemes: active {} (device {})", changed->scheme,
                  changed->family == last_device::Family::kController ? "controller" :
                  changed->family == last_device::Family::kMouse ? "mouse" : "keyboard");
    g_sdl_keyboard_focused = SDL_GetKeyboardFocus() != nullptr;
    // The raw mask is the event latch, not `SDL_GetMouseState`: P2's live arm measured SDL's
    // device state blind in this process (both the key and the mouse arrays read zero while the
    // events keep arriving), so re-reading it here would erase a press the events just recorded.
    // Focus loss clears the latch; a press that never delivers its up is the same kind of stale
    // input the native queue discards on a boundary.
    // The native lane icon is a pointer target while ready, including in a live song.
    const bool lane_target = !last_device::Current().using_touch && touch::LanePowerupAvailable();
    const bool visible = CursorVisible(MouseSupportEnabled(), MouseMenuAvailable(),
                                       io.WantCaptureMouse, ui::music::PlayerPointerOver(), lane_target);
    if (Window()) {
      if (visible) SDL_ShowCursor(); else SDL_HideCursor();
    }
    return true;
  }

  std::optional<MenuPoint> Point(const rex::InputEvent& e, bool clip = true) const {
    if (!ui_.HasWindow()) return {};
    auto p = MenuPointer(float(e.x), float(e.y),
        Width(), Height(),
        rb_blitz::settings::ReadSdkSetting<int32_t>("ui.video_mode_width"), rb_blitz::settings::ReadSdkSetting<int32_t>("ui.video_mode_height"),
        rb_blitz::settings::ReadSdkSetting<bool>("gpu.xenos.present_letterbox"), rb_blitz::settings::ReadSdkSetting<bool>("gpu.xenos.present_allow_overscan_cutoff"),
        rb_blitz::settings::ReadSdkSetting<int32_t>("gpu.xenos.present_safe_area_x"), rb_blitz::settings::ReadSdkSetting<int32_t>("gpu.xenos.present_safe_area_y"));
    if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
        (clip && !MenuBounds{0, 0, 1, 1}.Contains(p))) return {};
    return p;
  }
  void OnMouseMove(rex::InputEvent& e) {
    if (touch::ConsumeMouse(e.is_touch, touch::Phase::kMove, GameScreenActive())) {
      e.handled = true;
      return;
    }
    if (auto changed = e.is_touch ? last_device::Touch() : last_device::MouseMove(e.x, e.y))
      REXLOG_INFO("button_schemes: active {} (device {})", changed->scheme,
                  e.is_touch ? "touch" : "mouse");
    if (PlayerEvent(ui::music::PointerEvent::kMove, e)) return;
    if (LaneEvent(touch::Phase::kMove, e)) return;
    if (!MouseSupportEnabled()) return;
    const bool moved = e.x != last_x_ || e.y != last_y_;
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
    last_x_ = e.x; last_y_ = e.y;
    e.handled = true;
  }
  void OnMouseDown(rex::InputEvent& e) {
    if (touch::ConsumeMouse(e.is_touch, touch::Phase::kDown, GameScreenActive())) {
      e.handled = true;
      return;
    }
    if (auto changed = e.is_touch ? last_device::Touch() : last_device::MouseButton())
      REXLOG_INFO("button_schemes: active {} (device {})", changed->scheme,
                  e.is_touch ? "touch" : "mouse");
    if (PlayerEvent(ui::music::PointerEvent::kDown, e)) return;
    if (LaneEvent(touch::Phase::kDown, e)) return;
    // The raw mask is device state, so it is recorded whatever the gates say: the sampler applies
    // the gates, and a mouse button recorded while Mouse Support is off must not look "never
    // pressed" once it is turned on.
    g_mouse_buttons.fetch_or(MouseMask(e.button));
    if (rb_blitz::settings::title::mouse_native_trace.Get())
      REXLOG_INFO("native_mouse: button={} down mask={:02X}", static_cast<int>(e.button),
                  g_mouse_buttons.load());
    if (!MouseSupportEnabled()) return;
    if (auto p = Point(e)) {
      if (e.button == rex::MouseButton::kLeft) {
        // The gutter belongs to the scrollbar: a press there must not click the row,
        // and a miss just outside the strip must not press A either.
        if (MouseMenuRailHit(*p)) {
          EndDrag();
          dragging_ = true;
          drag_screen_ = CurrentScreen(); drag_focus_ = CurrentFocus();
          SDL_CaptureMouse(true);
          QueueMenuScrollbar(MouseAction::kScrollbarDown, *p);
        } else if (!MouseMenuScrollZone(*p)) QueueMenuMouse(MouseAction::kAccept, *p);
      }
      if (e.button == rex::MouseButton::kRight) QueueMenuMouse(MouseAction::kBack, *p);
    }
    e.handled = true;
  }
  void OnMouseUp(rex::InputEvent& e) {
    if (touch::ConsumeMouse(e.is_touch, touch::Phase::kUp, GameScreenActive())) {
      e.handled = true;
      return;
    }
    g_mouse_buttons.fetch_and(~MouseMask(e.button));
    if (PlayerEvent(ui::music::PointerEvent::kUp, e)) return;
    if (LaneEvent(touch::Phase::kUp, e)) return;
    if (rb_blitz::settings::title::mouse_native_trace.Get())
      REXLOG_INFO("native_mouse: button={} up mask={:02X}", static_cast<int>(e.button),
                  g_mouse_buttons.load());
    if (dragging_ && e.button == rex::MouseButton::kLeft) {
      const float miss = std::numeric_limits<float>::quiet_NaN();
      QueueMenuScrollbar(MouseAction::kScrollbarUp, Point(e, false).value_or(MenuPoint{miss, miss}));
      EndDrag(false);
    }
    if (!MouseSupportEnabled()) return;
    e.handled = true;
  }
  void OnMouseWheel(rex::InputEvent& e) {
    if (auto changed = e.is_touch ? last_device::Touch() : last_device::MouseWheel())
      REXLOG_INFO("button_schemes: active {} (device {})", changed->scheme,
                  e.is_touch ? "touch" : "mouse");
    if (PlayerEvent(ui::music::PointerEvent::kWheel, e)) return;
    // Device state first, gate second: the sampler applies the gates, and a capture binds a
    // detent while Mouse Support is off.
    const float scroll = e.scroll_y;
    if (scroll > 0.0f) {
      g_wheel_detents.fetch_add(1, std::memory_order_relaxed);
    } else if (scroll < 0.0f) {
      g_wheel_detents.fetch_add(-1, std::memory_order_relaxed);
    }
    if (!MouseSupportEnabled()) return;
    // The point rides the detent: the guest re-catches the row that lands under it when the
    // scroll settles (MouseScrollFollow in native_menu.h).
    if (auto p = Point(e)) QueueMenuWheel(int(e.scroll_y * 120.0f), *p);
    e.handled = true;
  }
  void EndLanePointer() {
    if (!lane_pointer_captured_) return;
    const float miss = std::numeric_limits<float>::quiet_NaN();
    touch::ConsumeLanePointer(touch::Phase::kCancel, miss, miss);
    lane_pointer_captured_ = false;
    SDL_CaptureMouse(false);
  }
  bool LaneEvent(touch::Phase phase, rex::InputEvent& e) {
    if (e.is_touch || (phase != touch::Phase::kMove &&
        e.button != rex::MouseButton::kLeft)) return false;
    if (ui_.GetImGuiIO().WantCaptureMouse) phase = touch::Phase::kCancel;
    const float miss = std::numeric_limits<float>::quiet_NaN();
    const auto point = Point(e, false).value_or(MenuPoint{miss, miss});
    if (!touch::ConsumeLanePointer(phase, point.x, point.y)) return false;
    if (phase == touch::Phase::kDown && ui_.HasWindow()) {
      SDL_CaptureMouse(true);
      lane_pointer_captured_ = true;
    } else if (phase == touch::Phase::kUp || phase == touch::Phase::kCancel) {
      lane_pointer_captured_ = false;
      SDL_CaptureMouse(false);
    }
    e.handled = true;
    return true;
  }
  bool PlayerEvent(ui::music::PointerEvent type, rex::InputEvent& e) {
    if (!ui_.HasWindow()) return false;
    // The SDK drawer observes these events first but does not mark mouse events
    // handled. This explicit route runs at priority 1, ahead of native queueing
    // and the keyboard/mouse emulation at priority 0. No WantCaptureMouse lag.
    const int button=e.button==rex::MouseButton::kLeft?0:
                     e.button==rex::MouseButton::kRight?1:2;
    if (ui::music::RoutePlayerPointer(type,float(e.x),float(e.y),
            Width(),Height(),
            ToLogical(),button)) {
      EndDrag();
      MouseMenuFocus(false);
      last_x_=e.x; last_y_=e.y;
      e.handled = true;
      return true;
    }
    MouseMenuFocus(ui_.IsFocused() && !ui_.GetImGuiIO().WantCaptureMouse);
    return false;
  }
  rex::ui::IUISystem& ui_;
  rex::ScopeGuard listener_;
  int last_x_ = -1, last_y_ = -1;
  bool dragging_ = false;
  bool lane_pointer_captured_ = false;
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

std::unique_ptr<rex::ui::IDialog> CreateMouseMenuCursor(rex::ui::IUISystem& ui) {
  return std::make_unique<MouseCursor>(ui);
}
}
