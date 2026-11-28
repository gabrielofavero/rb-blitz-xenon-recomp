// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <optional>

#include "input/native_menu.h"

namespace rb_blitz::ui::music {
using input::MenuBounds;
using input::MenuPoint;

struct Layout {
  MenuBounds panel, cover;
  std::array<MenuBounds, 3> controls{};
  float scale = 0;
  bool compact = false;
  MenuBounds content;
  float expanded_top = 0, expanded_cover_size = 0;
  std::array<MenuBounds, 3> expanded_controls{};

  MenuBounds Panel(float expansion) const {
    auto bounds = panel;
    if (compact) bounds.top = std::lerp(panel.top, expanded_top, expansion);
    return bounds;
  }
  MenuBounds Handle(float expansion) const {
    const float center = (content.left + content.right) * .5f;
    const float top = Panel(expansion).top;
    return {center - 66 * scale, top, center + 66 * scale, top + 30 * scale};
  }
};

// Logical window units, shared by drawing and pointer routing. Center the
// landscape anchor in a viewport no wider than 21:9.
inline Layout PlayerLayout(float width, float height, MenuBounds safe = {}) {
  if (width < (width < height ? 160 : 320) || height < 240) return {};
  Layout l;
  if (width < height) {
    safe.left = std::clamp(safe.left, 0.f, width);
    safe.right = std::clamp(safe.right, safe.left, width);
    safe.top = std::clamp(safe.top, 0.f, height);
    safe.bottom = std::clamp(safe.bottom, safe.top, height);
    if (safe.right - safe.left < 120 || safe.bottom - safe.top < 200)
      safe = {0, 0, width, height};
    const float available_width = safe.right - safe.left;
    const float width_scale = available_width < 320 ? available_width / 320
        : std::clamp(available_width / 402, 1.f, 1.6f);
    const float s = std::min(width_scale, (safe.bottom - safe.top) / 485);
    const float center = (safe.left + safe.right) * .5f;
    const float content_width = std::min(available_width - 56 * s, 440 * s);
    const float right = safe.right - 12 * s;
    l.scale = s;
    l.compact = true;
    l.panel = {0, safe.bottom - 88 * s, width, height};
    l.content = {center - content_width * .5f, safe.top,
                 center + content_width * .5f, safe.bottom};
    const float left = safe.left + 16 * s;
    const float thumb_size = available_width >= 320 ? 44 * s : 0;
    l.cover = {left, l.panel.top + 27 * s, left + thumb_size,
               l.panel.top + 27 * s + thumb_size};
    for (int i = 0; i < 3; ++i)
      l.controls[i] = {right - (144 - 48 * i) * s, l.panel.top + 26 * s,
                       right - (100 - 48 * i) * s, l.panel.top + 70 * s};

    const float max_cover = std::min({content_width, available_width * .75f, 288 * s});
    const float details_and_bottom_padding = 396 * s;
    l.expanded_top = std::max(safe.top + 20 * s, std::min(height * .18f,
                              safe.bottom - max_cover - details_and_bottom_padding));
    l.expanded_cover_size = std::min(max_cover,
        safe.bottom - l.expanded_top - details_and_bottom_padding);
    const float control_top = l.expanded_top + l.expanded_cover_size + 150 * s;
    for (int i = 0; i < 3; ++i) {
      const float x = center + (i - 1) * 67 * s;
      l.expanded_controls[i] = {x - 25 * s, control_top, x + 25 * s, control_top + 50 * s};
    }
    return l;
  }
  const float s = std::min({height / 720.f, width / 960.f, 1.6f});
  const float right = (width + std::min(width, height * (21.f / 9))) * .5f - 24 * s;
  const float bottom = height - 24 * s;
  l.scale = s;
  l.panel = {right - 332 * s, bottom - 100 * s, right, bottom};
  l.cover = {right - 320 * s, bottom - 88 * s, right - 244 * s, bottom - 12 * s};
  for (int i = 0; i < 3; ++i)
    l.controls[i] = {right - (230 - 74 * i) * s, bottom - 37 * s,
                     right - (164 - 74 * i) * s, bottom - 9 * s};
  return l;
}

inline MenuPoint LogicalPointer(float x, float y, float physical_w,
                                float physical_h, float logical_w,
                                float logical_h) {
  if (physical_w <= 0 || physical_h <= 0) return {NAN, NAN};
  return {x * logical_w / physical_w, y * logical_h / physical_h};
}

enum class PointerEvent { kMove, kDown, kUp, kWheel };
struct PointerResult {
  bool consumed = false;
  std::optional<int> clicked;
};

class PointerRouter {
 public:
  void Present(Layout layout, uint64_t epoch, size_t song, bool visible,
               std::array<bool, 3> enabled) {
    if (!visible || epoch != epoch_ || !layout.compact || !layout_.compact) {
      open_ = false;
      expansion_ = drag_expansion_ = 0;
    }
    if (!visible || epoch != epoch_ || !SameBounds(layout.panel, layout_.panel) ||
        !SameBounds(layout.content, layout_.content)) {
      armed_ = -1;
      dragging_ = dismissing_ = false;
    }
    if (song != song_) armed_ = -1;
    layout_ = layout;
    epoch_ = epoch;
    song_ = song;
    visible_ = visible;
    enabled_ = enabled;
  }
  // Keep ownership of a swallowed down across a screen change so its release
  // cannot activate the guest. A fresh down supersedes any stale ownership.
  void Cancel() {
    Present({}, 0, 0, false, {});
    passed_ = 0;
  }

  void Animate(float seconds) {
    if (dragging_) {
      expansion_ = drag_expansion_;
      return;
    }
    const float target = open_ ? 1.f : 0.f;
    expansion_ += (target - expansion_) * (1 - std::exp(-18 * std::min(seconds, .05f)));
    if (std::abs(target - expansion_) < .005f) expansion_ = target;
  }

  std::optional<MenuBounds> HitBounds() const {
    if (!visible_) return {};
    auto bounds = layout_.Panel(expansion_);
    if (expansion_ > .5f) bounds.top = 0;
    return bounds;
  }
  bool Owns(MenuPoint p) const { return swallowed_ != 0 || (passed_ == 0 && Hit(p)); }

  PointerResult Route(PointerEvent event, MenuPoint p, int button = 0) {
    const bool hit = Hit(p);
    const unsigned bit = 1u << button;
    PointerResult result{Owns(p), {}};
    if (event == PointerEvent::kDown) {
      result.consumed = hit || (swallowed_ & ~bit);
      if (result.consumed) {
        if (!(swallowed_ & bit) || (armed_ < 0 && !dragging_ && !dismissing_)) {
          swallowed_ |= bit;
          passed_ &= ~bit;
          if (button == 0 && hit) Arm(p);
        }
      } else {
        swallowed_ &= ~bit;
        passed_ |= bit;
      }
    } else if (event == PointerEvent::kMove) {
      if (dragging_) Drag(p.y);
      if (armed_ >= 0 && !Controls()[armed_].Contains(p)) armed_ = -1;
    } else if (event == PointerEvent::kUp) {
      result.consumed = (swallowed_ & bit) || (hit && !(passed_ & bit));
      swallowed_ &= ~bit;
      passed_ &= ~bit;
      if (button == 0) {
        if (armed_ >= 0 && hit && enabled_[armed_] && Controls()[armed_].Contains(p))
          result.clicked = armed_;
        if (dragging_ && std::isfinite(p.y)) {
          Drag(p.y);
          if (drag_moved_) {
            const float travel = layout_.panel.top - layout_.expanded_top;
            const float threshold = std::min(.18f, 50 * layout_.scale / travel);
            open_ = drag_expansion_ >= (open_ ? 1 - threshold : threshold);
          }
          else if (hit) open_ = !open_;
        }
        if (dismissing_ && hit && !layout_.Panel(expansion_).Contains(p)) open_ = false;
        armed_ = -1;
        dragging_ = dismissing_ = false;
      }
    }
    return result;
  }

  int armed() const { return armed_; }
  float expansion() const { return expansion_; }
  bool active_transport() const {
    return !dragging_ && expansion_ == (open_ ? 1.f : 0.f);
  }

 private:
  bool Hit(MenuPoint p) const {
    const auto bounds = HitBounds();
    return bounds && bounds->Contains(p);
  }
  static bool SameBounds(MenuBounds a, MenuBounds b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
  }
  const std::array<MenuBounds, 3>& Controls() const {
    return open_ ? layout_.expanded_controls : layout_.controls;
  }
  void Arm(MenuPoint p) {
    if (!layout_.Panel(expansion_).Contains(p)) {
      dismissing_ = true;
      return;
    }
    if (active_transport()) {
      for (int i = 0; i < 3; ++i) {
        if (!Controls()[i].Contains(p)) continue;
        if (enabled_[i]) armed_ = i;
        return;
      }
    }
    if (layout_.compact && (layout_.Handle(expansion_).Contains(p) || expansion_ == 0)) {
      dragging_ = true;
      drag_moved_ = false;
      drag_start_y_ = p.y;
      drag_start_expansion_ = drag_expansion_ = expansion_;
    }
  }
  void Drag(float y) {
    if (!std::isfinite(y)) return;
    const float travel = drag_start_y_ - y;
    // Ignore normal tap jitter. Once it turns into a drag, reversing direction
    // moves the sheet back without changing the gesture into a tap.
    drag_moved_ |= std::abs(travel) > 6 * layout_.scale;
    if (drag_moved_)
      drag_expansion_ = std::clamp(drag_start_expansion_ +
          travel / (layout_.panel.top - layout_.expanded_top), 0.f, 1.f);
  }

  Layout layout_{};
  uint64_t epoch_ = 0;
  size_t song_ = 0;
  bool visible_ = false;
  std::array<bool, 3> enabled_{};
  int armed_ = -1;
  unsigned swallowed_ = 0, passed_ = 0;
  bool open_ = false, dragging_ = false, dismissing_ = false, drag_moved_ = false;
  float expansion_ = 0, drag_expansion_ = 0, drag_start_y_ = 0, drag_start_expansion_ = 0;
};
}  // namespace rb_blitz::ui::music
