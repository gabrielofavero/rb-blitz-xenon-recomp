// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::input {

struct MenuPoint { float x = 0, y = 0; };
// The presented rectangle inside a `w` x `h` frame: the part of the window the guest's frame
// occupies once the presenter has applied the video mode's aspect and (optionally) the overscan
// safe area. MenuPointer and MenuPointToWindow share it, so a pointer and an overlay drawn from
// the same guest coordinates can never disagree about where the guest's frame is.
struct MenuPresentRect { int width = 0, height = 0; };
inline MenuPresentRect MenuPresentedRect(int w, int h, int aspect_w, int aspect_h,
                                         bool letterbox, bool overscan, int safe_x, int safe_y) {
  if (w <= 0 || h <= 0 || aspect_w <= 0 || aspect_h <= 0) return {};
  auto scale = [](int value, int numerator, int denominator) {
    return int((int64_t(value) * numerator + denominator / 2) / denominator);
  };
  int pw = w, ph = h;
  if (int64_t(w) * aspect_h > int64_t(h) * aspect_w) {
    int safe = overscan && safe_y > 0 && safe_y < 100 ? safe_y : 100;
    ph = scale(w, aspect_h, aspect_w);
    if (int64_t(ph) * safe > int64_t(h) * 100) {
      ph = scale(h, 100, safe);
      if (letterbox) pw = scale(h * 100, aspect_w, aspect_h * safe);
    }
  } else {
    int safe = overscan && safe_x > 0 && safe_x < 100 ? safe_x : 100;
    pw = scale(h, aspect_w, aspect_h);
    if (int64_t(pw) * safe > int64_t(w) * 100) {
      pw = scale(w, 100, safe);
      if (letterbox) ph = scale(w * 100, aspect_h, aspect_w * safe);
    }
  }
  return {pw, ph};
}
// Invert the presenter's centered aspect/overscan rectangle in physical pixels.
inline MenuPoint MenuPointer(float x, float y, int w, int h, int aspect_w, int aspect_h,
                             bool letterbox, bool overscan, int safe_x, int safe_y) {
  const MenuPresentRect rect =
      MenuPresentedRect(w, h, aspect_w, aspect_h, letterbox, overscan, safe_x, safe_y);
  if (rect.width <= 0 || rect.height <= 0) return {NAN, NAN};
  return {(x - (w - rect.width) / 2) / rect.width,
          (y - (h - rect.height) / 2) / rect.height};
}
// The forward mapping, for a host overlay that draws *over* the guest's own UI: the window point
// a normalized guest point lands on, in whatever units the caller passes as `w`/`h` (ImGui's
// `DisplaySize`, so the result is usable with a draw list directly). Same rectangle as
// MenuPointer; `MenuPointer(MenuPointToWindow(p, ...), ...)` returns `p` within a pixel.
inline MenuPoint MenuPointToWindow(MenuPoint point, int w, int h, int aspect_w, int aspect_h,
                                   bool letterbox, bool overscan, int safe_x, int safe_y) {
  const MenuPresentRect rect =
      MenuPresentedRect(w, h, aspect_w, aspect_h, letterbox, overscan, safe_x, safe_y);
  if (rect.width <= 0 || rect.height <= 0) return {NAN, NAN};
  return {point.x * float(rect.width) + (w - rect.width) * .5f,
          point.y * float(rect.height) + (h - rect.height) * .5f};
}
struct MenuBounds {
  float left = 0, top = 0, right = 0, bottom = 0;
  bool Contains(MenuPoint p) const {
    return std::isfinite(p.x) && std::isfinite(p.y) &&
           p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
  }
};
struct MenuRow {
  MenuPoint center;
  int index = -1;
  bool enabled = false;
  std::optional<MenuBounds> bounds;
};

// Coordinates are normalized through the camera that actually drew the list.
// Disabled rows participate in the hit test so hovering one cannot select a neighbor.
inline int HitMenuRow(MenuPoint p, const MenuBounds& bounds,
                      const std::vector<MenuRow>& rows) {
  if (!bounds.Contains(p)) return -1;
  float distance = std::numeric_limits<float>::infinity();
  const MenuRow* closest = nullptr;
  for (const auto& row : rows) {
    if (row.bounds && !row.bounds->Contains(p)) continue;
    float dx = p.x - row.center.x, dy = p.y - row.center.y;
    float d = dx * dx + dy * dy;
    if (d < distance) { distance = d; closest = &row; }
  }
  return closest && closest->enabled ? closest->index : -1;
}

enum class MouseAction { kMove, kAccept, kBack, kUp, kDown, kLeft, kRight,
                         kScrollbarDown, kScrollbarDrag, kScrollbarUp };
struct MenuMouseEvent { MouseAction action; MenuPoint point; };

// Small lists retain precise single-row scrolling. Large song libraries scale to
// roughly one percent per notch, capped at one visible page.
inline int LibraryWheelRows(int total, int display) {
  if (total <= 0 || display <= 0) return 1;
  return std::clamp(1 + (total - 1) / 100, 1, display);
}

inline int LibraryHeaderForScreen(std::string_view screen) {
  if (screen == "song_select_screen") return 0;
  if (screen == "store_menu_screen" || screen == "store_browser_screen") return 1;
  if (screen == "inbox_screen") return 2;
  if (screen == "recommendation_screen") return 3;
  return -1;
}

inline int LibraryHeaderForLabel(std::string_view name) {
  if (name == "main_my_songs.lbl") return 0;
  if (name == "main_store.lbl") return 1;
  if (name == "main_inbox.lbl") return 2;
  if (name == "main_Home.lbl") return 3;
  return -1;
}

// Hover ownership and the unhover lift (owner rule, 2026-10-16). Hover selects a row; when the
// pointer leaves the rows the frame the player sees drops the highlight too - but only the hover
// the mouse itself owns. The engine's selected row is never written: the lift strips the
// highlight from the *draw* of the list while it still shows the row the mouse set (see DrawList
// in native_menu.cpp), and it ends the moment a *different* row is positively selected (a
// controller press, a wheel detent on a list whose wheel steps the selection, a screen change),
// so a pad's own highlight is never hidden. A scroll that takes the lifted row out of the band
// reports no selected row at all, which is not a selection change: the lift holds until the
// scroll-aware catch replaces it (MouseScrollFollow below) or another selection arrives. Pure so
// the tests can pin the rule without a guest.
struct MouseHighlight {
  // What a pointer-leave did, for the trace and the tests.
  enum class Leave { kIgnored, kKept, kLifted };
  uint32_t owner_list = 0;   // The list whose selection the mouse set (0 = none).
  int owner_row = -1;        // The row it set it to.
  uint32_t lifted_list = 0;  // The list drawing unhighlighted after the pointer left (0 = none).
  int lifted_row = -1;

  void Hover(uint32_t list, int row) {
    owner_list = list; owner_row = row;
    lifted_list = 0; lifted_row = -1;
  }
  // The pointer left `list`'s rows, which still show `current`. Only the row the mouse itself
  // set is lifted; ownership ends either way.
  Leave PointerLeft(uint32_t list, int current) {
    if (owner_list != list) return Leave::kIgnored;
    const bool owned = owner_row == current;
    owner_list = 0; owner_row = -1;
    if (!owned) return Leave::kKept;
    lifted_list = list; lifted_row = current;
    return Leave::kLifted;
  }
  // Whether `list` still draws unhighlighted: while the lifted row is still its selection. Any
  // other selection a draw can name (a controller press, a wheel detent where the wheel steps
  // the selection) ends the lift and the highlight returns; a scroll that pushed the lifted row
  // out of the band reports no selected row at all (`current` < 0), which is the same selection -
  // the lift holds until the catch lands or a real selection change arrives.
  bool Lifted(uint32_t list, int current) {
    if (lifted_list != list) return false;
    if (current >= 0 && lifted_row != current) { lifted_list = 0; lifted_row = -1; return false; }
    return true;
  }
  void Reset() { owner_list = 0; owner_row = -1; lifted_list = 0; lifted_row = -1; }
};

// The scroll-aware hover (owner rule, 2026-10-16). A scrolled list's wheel, thumb drag and track
// click move the *view*, so the rows slide under a stationary pointer; the frame the player sees
// must let the highlight go where the pointer now points. The scroll the mouse applied ends the
// hover it owned (the row it set is sliding away from the pointer - the MouseHighlight lift
// keeps the frame unhighlighted), and the row that comes to rest under the pointer is caught
// again exactly as a pointer move would catch it; no row there leaves the hover empty. Armed by
// every applied scroll with that scroll's pointer, re-targeted by any later pointer event, and
// taken once on the first quiet frame whose drawn geometry already shows the new view (one
// detent per frame would otherwise catch a row it is about to scroll away again). Pure so the
// tests can pin the rule without a guest.
struct MouseScrollFollow {
  bool armed = false;
  MenuPoint point;
  void Arm(MenuPoint p) { armed = true; point = p; }
  // A later pointer event is where the catch must land instead.
  void Pointer(MenuPoint p) { if (armed) point = p; }
  // The pending catch, taken once; empty when no scroll is waiting to be followed.
  std::optional<MenuPoint> Take() {
    if (!armed) return {};
    armed = false;
    return point;
  }
  void Reset() { armed = false; }
};

// The scrollbar strip and thumb come from the scene's projected meshes. The thumb
// travels between the rail's ends as the first visible row walks 0..maxFirst.
struct MenuRail {
  float left = 0, top = 0, right = 0, bottom = 0;
  bool valid = false;
  float thumb_height = 0;
  float thumb_top = NAN;
};
inline float RailPitch(const std::vector<MenuRow>& rows) {
  std::vector<float> ys;
  for (const auto& row : rows) if (row.bounds) ys.push_back((row.bounds->top + row.bounds->bottom) * .5f);
  if (ys.size() < 3) return 0;
  std::sort(ys.begin(), ys.end());
  float pitch = 0;
  for (size_t i = 1; i < ys.size(); ++i) pitch = std::max(pitch, ys[i] - ys[i - 1]);
  return pitch > 0 ? pitch : 0;
}
// Thumb travel per unit of firstShowing over a rail of `rail_height` whose thumb is
// `thumb_height` tall: the thumb crosses (rail - thumb) in max_first steps.
inline float RailPerFirst(float rail_height, float thumb_height, int max_first) {
  const float travel = rail_height - thumb_height;
  return max_first > 0 && travel > 0 ? travel / float(max_first) : 0;
}
// The firstShowing whose thumb top sits at `y`.
inline int RailFirstFromY(float y, float rail_top, float per_first, int max_first) {
  if (!std::isfinite(y) || !(per_first > 0) || max_first <= 0) return 0;
  return int(std::lround(std::clamp((y - rail_top) / per_first, 0.0f, float(max_first))));
}
// Whether `y` is on the thumb (with a small grab margin) for a view at `first`.
inline bool RailYOnThumb(float y, float rail_top, float per_first, float thumb_height, int first) {
  const float top = rail_top + per_first * float(first);
  return y >= top - .004f && y <= top + thumb_height + .004f;
}

// A thumb grab preserves the point held inside it. A track press centers the thumb
// at the pointer immediately, and can continue straight into a drag.
struct MenuRailDrag {
  bool active = false;
  uint32_t list = 0;
  float grab = 0;
  void Reset() { *this = {}; }
  int Target(float y, const MenuRail& rail, int max_first) const {
    return RailFirstFromY(y - grab, rail.top,
                         RailPerFirst(rail.bottom - rail.top, rail.thumb_height, max_first), max_first);
  }
  int Begin(uint32_t owner, float y, const MenuRail& rail, int first, int max_first) {
    const float per = RailPerFirst(rail.bottom - rail.top, rail.thumb_height, max_first);
    active = rail.valid && per > 0 && std::isfinite(y);
    list = active ? owner : 0;
    const float thumb_top = rail.top + per * float(first);
    const float drawn_top = std::isfinite(rail.thumb_top) ? rail.thumb_top : thumb_top;
    const bool on_thumb = y >= drawn_top - .004f && y <= drawn_top + rail.thumb_height + .004f;
    grab = on_thumb ? y - thumb_top : rail.thumb_height * .5f;
    return active ? Target(y, rail, max_first) : first;
  }
};

// Guest-frame pulses, independent of how many times XInput is polled per frame.
// Both the press and release must reach a pad poll before advancing: UI frames
// can run while the guest's separate input thread is delayed.
// Every screen/focus boundary discards queued input, including partial wheel ticks.
class MenuMouseQueue {
 public:
  void Context(uint32_t screen, uint32_t focus, bool enabled) {
    if (screen != screen_ || focus != focus_ || enabled != enabled_) Reset();
    screen_ = screen; focus_ = focus; enabled_ = enabled;
  }
  void Reset() {
    events_.clear(); scrollbar_.clear(); wheel_ = 0;
    if (pulse_) release_pending_ = true;
    pulse_ = 0; frames_ = 0; sampled_ = false;
  }
  bool enabled() const { return enabled_; }
  // Drag frames coalesce to the latest pointer position; presses and releases are
  // never dropped, so a click that begins and ends inside one UI frame still lands.
  void Scrollbar(MouseAction action, MenuPoint point) {
    if (!enabled_) return;
    if (action == MouseAction::kScrollbarDrag && !scrollbar_.empty() &&
        scrollbar_.back().action == MouseAction::kScrollbarDrag)
      scrollbar_.back().point = point;
    else if (scrollbar_.size() < 8)
      scrollbar_.push_back({action, point});
  }
  void Push(MouseAction action, MenuPoint point = {}) {
    if (!enabled_) return;
    if (action == MouseAction::kMove && !events_.empty() &&
        events_.back().action == MouseAction::kMove) events_.back().point = point;
    else if (events_.size() < 128) events_.push_back({action, point});
  }
  void Wheel(int delta, MenuPoint point) {
    if (!enabled_) return;
    // A direction reversal must not pay off the previous fractional detent.
    if ((delta > 0 && wheel_ < 0) || (delta < 0 && wheel_ > 0)) wheel_ = 0;
    wheel_ = std::clamp(wheel_ + delta, -120 * 64, 120 * 64);
    while (wheel_ >= 120) { Push(MouseAction::kUp, point); wheel_ -= 120; }
    while (wheel_ <= -120) { Push(MouseAction::kDown, point); wheel_ += 120; }
  }
  // Whether anything the user queued is still waiting: the scroll-aware hover's catch waits for
  // the drain (a detent applied after a catch would re-anchor on the row it left behind).
  bool Pending() const { return !events_.empty() || !scrollbar_.empty(); }
  std::optional<MenuMouseEvent> NextFrame(bool can_scroll = true) {
    if (!enabled_) return {};
    // Scrollbar traffic bypasses the pad pulse gates: it drives the list directly
    // and must keep flowing while a press pulse is still waiting for its release.
    if (!scrollbar_.empty()) {
      auto event = scrollbar_.front(); scrollbar_.pop_front();
      return event;
    }
    if (frames_ > 0) { --frames_; return {}; }
    if (pulse_) {
      if (sampled_) { pulse_ = 0; sampled_ = false; release_pending_ = true; }
      return {}; // at least one complete release frame
    }
    if (release_pending_) {
      if (!sampled_) return {};
      release_pending_ = false;
    }
    if (events_.empty()) return {};
    const auto action = events_.front().action;
    if (!can_scroll && (action == MouseAction::kUp || action == MouseAction::kDown)) return {};
    auto event = events_.front(); events_.pop_front();
    return event;
  }
  void Pulse(MouseAction action) {
    // Directions are D-pad bits (up 1, down 2, left 4, right 8) rather than stick
    // travel: the guest steps a list once per press, so one wheel detent is one
    // native step and no analog threshold or repeat rule can drop or multiply it.
    pulse_ = action == MouseAction::kAccept ? 0x1000 :
             action == MouseAction::kBack ? 0x2000 :
             action == MouseAction::kUp ? 1 : action == MouseAction::kDown ? 2 :
             action == MouseAction::kLeft ? 4 : action == MouseAction::kRight ? 8 : 0;
    frames_ = pulse_ ? 1 : 0; // two guest frames down, then one frame released
    sampled_ = false;
  }
  uint16_t pulse() const { return enabled_ ? pulse_ : 0; }
  // Only the final player-one pad filter acknowledges delivery. The synthetic
  // driver's connectivity/packet queries must not consume the pulse.
  uint16_t SamplePad() { sampled_ = true; return pulse(); }
 private:
  uint32_t screen_ = 0, focus_ = 0;
  bool enabled_ = false;
  int wheel_ = 0, frames_ = 0;
  uint16_t pulse_ = 0;
  bool sampled_ = false, release_pending_ = false;
  std::deque<MenuMouseEvent> events_, scrollbar_;
};

// Thread-safe bridge. Window events contain no guest pointers; only the UIManager
// hook reads guest objects or calls guest functions.
bool MouseSupportEnabled();
// One-shot header clicks consumed by the active panel's authored poll handler.
bool AnswerMenuHeaderQuery(std::string_view path, bool* answer);
bool MouseMenuAvailable();
void MouseMenuFocus(bool focused);
// The host's raw device state, as the window thread last saw it - for the guest-side physical
// sampler (plan P2), which must not call SDL's mouse/focus queries itself: SDL documents both as
// main-thread only. `HostMouseButtons` is `SDL_GetMouseState()`'s mask bits; `HostInputFocused` is
// the game window's own focus, not the pointer's availability (an ImGui overlay taking the
// pointer must not stop the keyboard from playing).
uint32_t HostMouseButtons();
// The wheel detents seen since the previous call: positive away from the player (`wheel_up`),
// negative toward (`wheel_down`). Consumes them, so exactly one poll sees each detent.
int32_t HostTakeMouseWheel();
bool HostInputFocused();
// SDL's own keyboard-focus answer, as the window thread last saw it. `HostInputFocused` is the
// game window's Win32 focus (tracked from the window listener); this is what SDL's keyboard state
// array is scoped to. A silent key sample has to be able to tell the two apart.
bool HostSdlKeyboardFocused();
// Whether the key the profile names is physically down, from the window's own key events (the
// latch `mouse_ui.cpp` keeps). This is the sampler's keyboard source: SDL's global state array
// was measured blind in this process (P2, 2026-10-18), while the events keep working.
bool HostKeyDown(std::string_view name);
// True while any key is physically down. P7's capture uses this to wait for the press that opened
// the listening dialog to be released before it captures anything.
bool HostAnyKeyDown();
// The key-down events since `*serial`, by SDL scancode name (`Space`, `Return`, `A`), and the new
// serial. A tap between two polls is still one event; the ring is bounded, and a caller that fell
// behind resumes at the newest event rather than replaying a stale key.
void HostKeyEventsSince(uint64_t* serial, std::vector<std::string>* names);
void QueueMenuMouse(MouseAction action, MenuPoint point);
// The wheel carries the pointer position: a detent scrolls the view under a stationary pointer,
// and the scroll-aware hover catches the row that lands there (MouseScrollFollow).
void QueueMenuWheel(int delta, MenuPoint point);
void QueueMenuScrollbar(MouseAction action, MenuPoint point);
// Whether a point is in the focused list's scrollbar gutter, as the last draw left it.
bool MouseMenuRailHit(MenuPoint point);
// The rail plus a small margin: clicks here are swallowed instead of pressing A.
bool MouseMenuScrollZone(MenuPoint point);
uint16_t MenuMousePadState();
uint16_t SampleMenuMousePadState();

// The UIManager frame state the help caption observer (src/hooks/help_observer.cpp) reads, as the
// last frame's Context() stored it. `FocusedListDrawn()` is the list the previous frame's draw
// identified as the focused component (0 when none) - the only shape the observer may read a
// selection from, because a slider has no list state at list+176. Recorded regardless of the
// mouse preference, because the captions must not depend on an input setting.
uint32_t CurrentScreen();
uint32_t CurrentFocus();
uint32_t FocusedListDrawn();
bool MenuAvailable();

// One row the focused list last drew: its logical index (the same index the mouse hit test and
// `UIListState::Selected` use) and the two rectangles the draw measured - `label`, the row's own
// text, and `cell`, the row's full clickable slot. Normalized through the camera, the coordinate
// space MenuPointer maps into; MenuPointToWindow turns them into window coordinates.
struct MenuRowGeometry {
  int index = -1;
  MenuBounds label;
  MenuBounds cell;
};

// The visible rows of the focused list, in draw order, and the screen that owned it (the guest
// object's own name, e.g. `settings_general_mapping_screen`). False when no menu list is drawn -
// a song, a transition, or before the first draw - and then the caller must draw nothing rather
// than a stale page's rows. The settings overlay (src/ui/help_overlay.cpp) reads this once per
// UI frame; the snapshot is replaced by the draw and cleared at every screen/focus boundary.
bool VisibleMenuRows(std::string* screen_name, std::vector<MenuRowGeometry>* rows);
// A live song: the screen is `game_screen` and the focused panel is not the pause panel
// (the same split `MenuAvailable` uses to let pause take the pad). False on every menu, on
// the title, the loading screens and while paused.
bool GameScreenActive();
} // namespace rb_blitz::input
