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
};
// Logical (96 DPI) units, shared by drawing and the event listener. Keep the
// player low in the right margin, alongside the guest's left/center prompts.
inline Layout PlayerLayout(float width, float height) {
  if (width < 320 || height < 240) return {};
  const float s = std::min({height / 720.f, width / 960.f, 1.6f});
  const float right = width - 24 * s, bottom = height - 24 * s;
  Layout l{{right - 332 * s, bottom - 100 * s, right, bottom},
           {right - 320 * s, bottom - 88 * s, right - 244 * s, bottom - 12 * s},
           {},
           s};
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
    if (!visible || epoch != epoch_ || song != song_ ||
        layout.panel.left != layout_.panel.left ||
        layout.panel.top != layout_.panel.top ||
        layout.panel.right != layout_.panel.right ||
        layout.panel.bottom != layout_.panel.bottom)
      armed_ = -1;
    layout_ = layout;
    epoch_ = epoch;
    song_ = song;
    visible_ = visible;
    enabled_ = enabled;
  }
  // Cancellation retains ownership of a down already swallowed by the player,
  // so its release cannot fall through after a modal/screen/focus boundary.
  void Cancel() {
    armed_ = -1;
    visible_ = false;
  }
  PointerResult Route(PointerEvent event, MenuPoint p, int button = 0) {
    const bool hit = visible_ && layout_.panel.Contains(p);
    PointerResult result{hit || swallowed_ != 0, {}};
    if (event == PointerEvent::kDown && hit) {
      const unsigned bit = 1u << button;
      if (!(swallowed_ & bit)) {
        swallowed_ |= bit;
        if (button == 0)
          for (int i = 0; i < 3; ++i)
            if (enabled_[i] && layout_.controls[i].Contains(p)) armed_ = i;
      }
    }
    if (event == PointerEvent::kMove && armed_ >= 0 &&
        !layout_.controls[armed_].Contains(p))
      armed_ = -1;
    if (event == PointerEvent::kUp) {
      if (button == 0 && armed_ >= 0 && hit && enabled_[armed_] &&
          layout_.controls[armed_].Contains(p))
        result.clicked = armed_;
      swallowed_ &= ~(1u << button);
      if (button == 0) armed_ = -1;
    }
    return result;
  }
  int armed() const { return armed_; }

 private:
  Layout layout_{};
  uint64_t epoch_ = 0;
  size_t song_ = 0;
  bool visible_ = false;
  std::array<bool, 3> enabled_{};
  int armed_ = -1;
  unsigned swallowed_ = 0;
};
}  // namespace rb_blitz::ui::music
