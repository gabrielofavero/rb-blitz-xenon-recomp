// SPDX-License-Identifier: GPL-2.0-only
// Retail guest ABI and validation: docs/engine/input-flow.md.
#include "input/native_menu.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <bit>
#include <mutex>
#include <string>

REXCVAR_DECLARE(bool, mouse_ui_nav);
REXCVAR_DEFINE_BOOL(mouse_native_trace, false, "Diagnostics", "Trace native menu geometry and selection");

REX_EXTERN(__imp__sub_824B2958); // UIManager::Poll
REX_EXTERN(__imp__sub_824E5020); // UIListDir::Draw
REX_EXTERN(sub_824B1980);       // UIManager::FocusComponent
REX_EXTERN(sub_824C1938);       // UIList::SetSelected
REX_EXTERN(sub_824C1F08);       // UIList::ScrollComplete
REX_EXTERN(sub_824C7220);       // UIList::CalcBoundingBox
REX_EXTERN(sub_821E14A8);       // Multiply(Vector3, Transform)
REX_EXTERN(sub_821FF4A0);       // RndCam::WorldToScreen
REX_EXTERN(sub_824E3008);       // UIListState::Selected (logical index)

namespace rb_blitz::input {
namespace {
uint32_t U32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}
float F32(uint8_t* base, uint32_t address) { return std::bit_cast<float>(U32(base, address)); }
void Float(uint8_t* base, uint32_t address, float value) {
  rex::memory::store_and_swap<uint32_t>(base + address, std::bit_cast<uint32_t>(value));
}
std::string ObjectName(uint8_t* base, uint32_t object) {
  if (!object) return {};
  auto name = U32(base, object + 32);
  if (!name) return {};
  const char* text = reinterpret_cast<char*>(base + name);
  return std::string(text, strnlen(text, 96));
}
uint32_t ObjectBase(uint8_t* base, uint32_t object) {
  return object ? object + 4 + U32(base, U32(base, object + 4) + 4) : 0;
}
std::mutex mutex;
MenuMouseQueue queue;
bool window_focused = false;
bool menu_available = false;
uint32_t current_screen = 0, current_focus = 0;

struct Geometry {
  uint32_t list = 0, frame = 0;
  MenuBounds bounds;
  std::vector<MenuRow> rows;
};
thread_local Geometry geometry;
thread_local uint32_t frame_number = 0;

// All side calls use isolated registers and reserve space below the guest frame.
// Output buffers are above that call's linkage area and survive nested guest calls.
struct GuestCall {
  rex::CallFrame frame;
  uint32_t scratch;
  explicit GuestCall(PPCContext& ctx) : frame(ctx) {
    frame.ctx.r1.u32 -= 256;
    scratch = frame.ctx.r1.u32 + 128;
  }
};
uint32_t Focus(PPCContext& ctx, uint8_t* base, uint32_t manager) {
  GuestCall call(ctx); call.frame.ctx.r3.u32 = manager;
  sub_824B1980(call.frame, base);
  return call.frame.ctx.r3.u32;
}
void Context(PPCContext& ctx, uint8_t* base, uint32_t manager) {
  const uint32_t screen = U32(base, manager + 44);
  const uint32_t focus = screen ? Focus(ctx, base, manager) : 0;
  const uint32_t panel = screen ? U32(base, screen + 52) : 0;
  const std::string name = ObjectName(base, screen);
  const std::string panel_name = ObjectName(base, ObjectBase(base, panel));
  // game_screen owns pause_panel too; its focused panel distinguishes a live song
  // from pause and its nested menus. A transition is never an input destination.
  const bool allowed = screen && U32(base, manager + 16) == 0 &&
      (name != "game_screen" || panel_name == "pause_panel");
  std::lock_guard lock(mutex);
  if ((screen != current_screen || focus != current_focus || allowed != menu_available) &&
      REXCVAR_GET(mouse_native_trace))
    REXLOG_INFO("native_mouse: screen={} panel={} focus={:08X} available={}", name, panel_name, focus, allowed);
  current_screen = screen; current_focus = focus; menu_available = allowed;
  queue.Context(screen, focus, allowed && window_focused && MouseSupportEnabled());
}
void Select(PPCContext& ctx, uint8_t* base, MenuPoint point) {
  // The pointer from the last draw is valid only for the same focused component
  // in the immediately preceding frame. Never dereference a retired screen's list.
  if (!geometry.list || geometry.frame + 1 != frame_number ||
      current_focus != geometry.list) return;
  const int row = HitMenuRow(point, geometry.bounds, geometry.rows);
  if (REXCVAR_GET(mouse_native_trace)) REXLOG_INFO("native_mouse: hit={},{} row={} bounds={},{},{},{}", point.x, point.y, row, geometry.bounds.left, geometry.bounds.top, geometry.bounds.right, geometry.bounds.bottom);
  if (row < 0) return;
  GuestCall call(ctx);
  auto& c = call.frame.ctx;
  c.r3.u32 = geometry.list + 176;
  sub_824E3008(c, base);
  if (c.r3.s32 == row) return;
  c.r3.u32 = geometry.list; c.r4.s32 = row; c.r5.s32 = -1;
  sub_824C1938(c, base);
  // Refresh providers and send the engine's scroll-complete notification, without
  // issuing intermediate directional presses or animating through intervening rows.
  c.r3.u32 = geometry.list + 84; c.r4.u32 = geometry.list + 176;
  sub_824C1F08(c, base);
  if (REXCVAR_GET(mouse_native_trace)) REXLOG_INFO("native_mouse: selected row={} list={:08X}", row, geometry.list);
}
}

bool MouseSupportEnabled() { return REXCVAR_GET(mouse_ui_nav); }
bool MouseMenuAvailable() {
  std::lock_guard lock(mutex);
  return MouseSupportEnabled() && menu_available && window_focused;
}
void MouseMenuFocus(bool focused) {
  std::lock_guard lock(mutex);
  window_focused = focused;
  queue.Context(current_screen, current_focus, focused && menu_available && MouseSupportEnabled());
}
void QueueMenuMouse(MouseAction action, MenuPoint point) {
  std::lock_guard lock(mutex);
  if (MouseSupportEnabled()) queue.Push(action, point);
}
void QueueMenuWheel(int delta) {
  std::lock_guard lock(mutex);
  if (MouseSupportEnabled()) queue.Wheel(delta);
}
uint16_t MenuMousePadState() {
  std::lock_guard lock(mutex);
  return MouseSupportEnabled() ? queue.pulse() : 0;
}

void PollMouse(PPCContext& ctx, uint8_t* base, uint32_t manager) {
  ++frame_number;
  Context(ctx, base, manager);
  std::optional<MenuMouseEvent> event;
  { std::lock_guard lock(mutex); event = queue.NextFrame(); }
  if (!event) return;
  const auto screen = current_screen, focus = current_focus;
  if (event->action == MouseAction::kMove || event->action == MouseAction::kAccept)
    Select(ctx, base, event->point);
  // A selection notification can itself replace a screen, so recheck before A.
  Context(ctx, base, manager);
  std::lock_guard lock(mutex);
  if (queue.enabled() && screen == current_screen && focus == current_focus) queue.Pulse(event->action);
}

void CaptureGeometry(PPCContext& ctx, uint8_t* base) {
  if (!MouseSupportEnabled() || ctx.r9.u32 != 0) return;
  const uint32_t list = ctx.r5.u32 - 176;
  if (U32(base, list + 8) != 1) return; // UIComponent::kFocused
  const uint32_t camera = U32(base, 0x8284A108); // RndCam::sCurrent
  if (!camera) return;
  const uint32_t draw = ctx.r4.u32, transform = ctx.r7.u32;
  const uint32_t first = U32(base, draw + 56), end = U32(base, draw + 60);
  if (end < first || (end - first) % 44 || end - first > 44 * 256) return;
  Geometry next; next.list = list; next.frame = frame_number;
  next.bounds = {1e10f, 1e10f, -1e10f, -1e10f};
  GuestCall call(ctx); auto& c = call.frame.ctx;
  const uint32_t vector = call.scratch + 32, projected = call.scratch + 48;
  auto project = [&](uint32_t world) {
    c.r3.u32 = camera; c.r4.u32 = world; c.r5.u32 = projected;
    sub_821FF4A0(c, base);
    return MenuPoint{F32(base, projected), F32(base, projected + 4)};
  };
  c.r3.u32 = list; c.r4.u32 = call.scratch;
  sub_824C7220(c, base);
  for (unsigned corner = 0; corner < 8; ++corner) {
    for (unsigned axis = 0; axis < 3; ++axis)
      Float(base, vector + axis * 4, F32(base, call.scratch + axis * 4 + ((corner & (1 << axis)) ? 16 : 0)));
    auto p = project(vector);
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;
    next.bounds.left = std::min(next.bounds.left, p.x); next.bounds.right = std::max(next.bounds.right, p.x);
    next.bounds.top = std::min(next.bounds.top, p.y); next.bounds.bottom = std::max(next.bounds.bottom, p.y);
  }
  for (uint32_t row = first; row < end; row += 44) {
    if (!base[row] || F32(base, row + 20) < .5f) continue;
    c.r3.u32 = row + 4; c.r4.u32 = transform; c.r5.u32 = vector;
    sub_821E14A8(c, base);
    auto p = project(vector);
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
    if (REXCVAR_GET(mouse_native_trace) && geometry.list != list)
      REXLOG_INFO("native_mouse: origin row={} point={},{} world={},{},{}", U32(base, row + 36), p.x, p.y, F32(base, vector), F32(base, vector+4), F32(base, vector+8));
    next.rows.push_back({p, int(U32(base, row + 36)), U32(base, row + 24) != 2});
  }
  if (next.rows.empty()) return;
  // CalcBoundingBox includes the list origin and its layout-space widget bounds,
  // which do not follow each rendered row's translation. Use it only across the
  // list; along the list, hit cells come from the actual drawn row positions.
  float min_x = 1e10f, min_y = 1e10f, max_x = -1e10f, max_y = -1e10f;
  for (const auto& row : next.rows) {
    min_x = std::min(min_x, row.center.x); max_x = std::max(max_x, row.center.x);
    min_y = std::min(min_y, row.center.y); max_y = std::max(max_y, row.center.y);
  }
  const bool horizontal = max_x - min_x > max_y - min_y;
  const float extent = horizontal ? max_x - min_x : max_y - min_y;
  const float half_cell = next.rows.size() > 1 ? extent / (next.rows.size() - 1) * .5f : .025f;
  if (horizontal) {
    next.bounds.left = min_x - half_cell; next.bounds.right = max_x + half_cell;
    for (auto& row : next.rows) row.center.y = (next.bounds.top + next.bounds.bottom) * .5f;
  } else {
    next.bounds.top = min_y - half_cell; next.bounds.bottom = max_y + half_cell;
    for (auto& row : next.rows) row.center.x = (next.bounds.left + next.bounds.right) * .5f;
  }
  if (REXCVAR_GET(mouse_native_trace) && geometry.list != list) {
    REXLOG_INFO("native_mouse: geometry list={:08X} focus={:08X} bounds={},{},{},{}", list, current_focus,
        next.bounds.left, next.bounds.top, next.bounds.right, next.bounds.bottom);
    for (const auto& row : next.rows)
      REXLOG_INFO("native_mouse: row={} center={},{} enabled={}", row.index, row.center.x, row.center.y, row.enabled);
  }
  geometry = std::move(next);
}
} // namespace rb_blitz::input

REX_HOOK_RAW(sub_824B2958) {
  const auto manager = ctx.r3.u32;
  rb_blitz::input::PollMouse(ctx, base, manager);
  __imp__sub_824B2958(ctx, base);
  rb_blitz::input::Context(ctx, base, manager);
}
REX_HOOK_RAW(sub_824E5020) {
  rb_blitz::input::CaptureGeometry(ctx, base);
  __imp__sub_824E5020(ctx, base);
}
