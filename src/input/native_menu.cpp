// SPDX-License-Identifier: GPL-2.0-only
// Retail guest ABI and validation: docs/engine/input-flow.md.
#include "input/native_menu.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include "hooks/help_observer.h"
#include "hooks/content_progress.h"
#include <bit>
#include <cstring>
#include <mutex>
#include <string>
#include <array>
#include <unordered_map>

REXCVAR_DECLARE(bool, mouse_ui_nav);
REXCVAR_DEFINE_BOOL(mouse_native_trace, false, "Diagnostics", "Trace native menu geometry and selection");

REX_EXTERN(__imp__sub_824B2958); // UIManager::Poll
REX_EXTERN(__imp__sub_824E5020); // UIListDir::Draw
REX_EXTERN(sub_824B1980);       // UIManager::FocusComponent
REX_EXTERN(sub_824C1938);       // UIList::SetSelected
REX_EXTERN(sub_824C1F08);       // UIList::ScrollComplete
REX_EXTERN(sub_824C1DE8);       // UIList::ScrollStart
REX_EXTERN(__imp__sub_824E07A8); // UIListWidget::CalcXfm
REX_EXTERN(__imp__sub_824E9A90); // UIListLabelElement::Draw
REX_EXTERN(__imp__sub_824E0900); // UIListWidget::DrawMesh
REX_EXTERN(__imp__sub_823F0118); // RndMesh::OnSync (before CPU vertices are released)
REX_EXTERN(__imp__sub_824BFEF0); // UIListMeshElement::Draw
REX_EXTERN(sub_821E14A8);       // Multiply(Vector3, Transform)
REX_EXTERN(sub_821FF4A0);       // RndCam::WorldToScreen
REX_EXTERN(sub_824E3008);       // UIListState::Selected (logical index)
REX_EXTERN(sub_822DAE10);       // UIListProvider::NumData (vtable slot 10 in Blitz)

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
  if (!object) return 0;
  const auto table = U32(base, object + 4);
  return table ? object + 4 + U32(base, table + 4) : 0;
}
std::mutex mutex;
MenuMouseQueue queue;
bool window_focused = false;
bool menu_available = false;
uint32_t current_screen = 0, current_focus = 0;
// The focused list the previous frame's draw identified (see DrawList). Read by the help caption
// observer; 0 when no list has been drawn as the focused component yet.
uint32_t focused_list_drawn = 0;

struct Geometry {
  uint32_t list = 0, frame = 0;
  bool horizontal = false;
  MenuBounds bounds;
  std::vector<MenuRow> rows;
  // Captured for the scrollbar mapping: the focused row's data index and the row pitch
  // the wheel's step size follows.
  int selected = -1;
  float pitch = 0;
};
thread_local Geometry geometry;
thread_local uint32_t frame_number = 0;
// The last row a draw positively identified as selected. The guest sets the selected
// *slot* to -1 when a scroll left the selection outside the band, so the slot cannot
// be the anchor; this survives that and is reset on every screen or focus change.
thread_local int last_selected_row = -1;
// The list the mouse itself last hovered a row on (0 = none). Only a hover the mouse
// set may be cleared when the pointer leaves it; a controller's own selection is never
// touched by mouse motion.
thread_local uint32_t mouse_hover_list = 0;

// The gutter the window thread may arm a drag in: published per draw so input that
// arrives between frames still sees the last confirmed strip, never a retired list.
std::mutex rail_mutex;
MenuRail menu_rail;
MenuRail RailSnapshot() { std::lock_guard lock(rail_mutex); return menu_rail; }
void PublishRail(const MenuRail& rail) { std::lock_guard lock(rail_mutex); menu_rail = rail; }

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
// The song library's rail, measured 2026-10-16 against the list state (calibration in
// docs/engine/input-flow.md): the thumb is one row pitch tall (kThumbHeight) and its top
// walks from kRailTop to kRailBottom - kThumbHeight as firstShowing walks 0..maxFirst.
constexpr float kRailTop = .2175f, kRailBottom = .8348f, kThumbHeight = .0624f;
constexpr uint32_t kNumDataFn = 0x822DAE10u;  // UIListProvider::NumData (vtable slot 10)
struct RailMap {
  bool known = false;
  uint32_t list = 0;
  int max_first = 0;
};
thread_local RailMap rail_map;
float RailPerFirstNow() {
  return RailPerFirst(kRailBottom - kRailTop, kThumbHeight, rail_map.max_first);
}
// Resolve how far the focused list can scroll: provider NumData minus the visible row
// count. The vtable slot is checked against the recompiled address before calling, and a
// refused answer leaves the gutter inert rather than guessing.
bool EnsureRailMap(PPCContext& ctx, uint8_t* base, uint32_t list) {
  if (rail_map.known && rail_map.list == list) return true;
  rail_map = {};
  const auto provider = U32(base, list + 208);
  const auto vtable = provider ? U32(base, provider) : 0;
  if (!vtable || U32(base, vtable + 10 * 4) != kNumDataFn) return false;
  GuestCall call(ctx);
  call.frame.ctx.r3.u32 = provider;
  sub_822DAE10(call.frame, base);
  const int rows = call.frame.ctx.r3.s32, display = int(U32(base, list + 180));
  if (rows <= 0 || display <= 0) return false;
  rail_map = {true, list, std::max(0, rows - display)};
  if (REXCVAR_GET(mouse_native_trace))
    REXLOG_INFO("native_mouse: scroll rail rows={} numDisplay={} maxFirst={}", rows, display, rail_map.max_first);
  return true;
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
  rb_blitz::content_progress::ObserveScreen(name, panel_name, allowed);
  if (screen != current_screen || focus != current_focus || allowed != menu_available) {
    MenuRail stale; PublishRail(stale);
    last_selected_row = -1;
    rail_map = {};
    mouse_hover_list = 0;
    if (REXCVAR_GET(mouse_native_trace))
      REXLOG_INFO("native_mouse: screen={} panel={} focus={:08X} available={}", name, panel_name, focus, allowed);
  }
  std::lock_guard lock(mutex);
  current_screen = screen; current_focus = focus; menu_available = allowed;
  queue.Context(screen, focus, allowed && window_focused && MouseSupportEnabled());
}
// Returns whether the point named a row of the focused list. When it did not, a hover
// the mouse owns is cleared: the guest's selected *slot* drops to -1 (its own
// no-selection encoding), so a later click on empty space cannot act on a stale hover.
// The selected *row* stays as the controller's anchor.
bool Select(PPCContext& ctx, uint8_t* base, MenuPoint point) {
  // The pointer from the last draw is valid only for the same focused component
  // in the immediately preceding frame. Never dereference a retired screen's list.
  if (!geometry.list || geometry.frame + 1 != frame_number ||
      current_focus != geometry.list) return false;
  const int row = HitMenuRow(point, geometry.bounds, geometry.rows);
  if (REXCVAR_GET(mouse_native_trace)) REXLOG_INFO("native_mouse: hit={},{} row={} bounds={},{},{},{}", point.x, point.y, row, geometry.bounds.left, geometry.bounds.top, geometry.bounds.right, geometry.bounds.bottom);
  if (row < 0) {
    // The mouse left whatever it was on: the hover it owned is cleared, so a click that
    // does not land on an element cannot press anything (owner rule, 2026-10-16). The
    // guest's selection highlight itself cannot be cleared without selecting another row
    // - the engine derives the slot from the selected row every update - so the
    // highlighted row and the album panel keep the retail selection while nothing is
    // hovered, exactly like a desktop list keeps its selected item.
    if (mouse_hover_list == geometry.list && REXCVAR_GET(mouse_native_trace))
      REXLOG_INFO("native_mouse: unhover list={:08X}", geometry.list);
    mouse_hover_list = 0;
    return false;
  }
  mouse_hover_list = geometry.list;
  GuestCall call(ctx);
  auto& c = call.frame.ctx;
  c.r3.u32 = geometry.list + 176;
  sub_824E3008(c, base);
  if (c.r3.s32 == row) return true;
  const int direction = row > c.r3.s32 ? 1 : -1;
  c.r3.u32 = geometry.list; c.r4.s32 = row; c.r5.u32 = U32(base, geometry.list + 224);
  sub_824C1938(c, base);
  // Refresh providers and send the engine's scroll-complete notification, without
  // issuing intermediate directional presses or animating through intervening rows.
  c.r3.u32 = geometry.list + 84; c.r4.u32 = geometry.list + 176;
  c.r5.s32 = direction; c.r6.u32 = 0;
  sub_824C1DE8(c, base);
  c.r3.u32 = geometry.list + 84; c.r4.u32 = geometry.list + 176;
  sub_824C1F08(c, base);
  if (REXCVAR_GET(mouse_native_trace)) REXLOG_INFO("native_mouse: selected row={} list={:08X}", row, geometry.list);
  return true;
}
// One live gutter drag. `first` is the view target frozen when the press landed; the
// selection is never moved by the scrollbar, so no row rides along with it.
struct RailDrag {
  bool active = false, moved = false;
  uint32_t list = 0;
  int first = 0;
  float y = 0;
};
thread_local RailDrag rail_drag;
// Move only the viewport, keep the guest's selection row, then notify the providers the
// way the guest's own scroll does. `first` is clamped to the list's own last page.
void ScrollListTo(PPCContext& ctx, uint8_t* base, uint32_t list, int first) {
  const int row = geometry.selected >= 0 ? geometry.selected : last_selected_row;
  if (row < 0) return;
  if (rail_map.known && rail_map.list == list) first = std::clamp(first, 0, rail_map.max_first);
  else first = std::max(0, first);
  const uint32_t current = U32(base, list + 224);
  if (uint32_t(first) == current) return;
  const int direction = first > int(current) ? 1 : -1;
  GuestCall call(ctx);
  auto& c = call.frame.ctx;
  c.r3.u32 = list; c.r4.s32 = row; c.r5.u32 = uint32_t(first);
  sub_824C1938(c, base);
  c.r3.u32 = list + 84; c.r4.u32 = list + 176; c.r5.s32 = direction; c.r6.u32 = 0;
  sub_824C1DE8(c, base);
  c.r3.u32 = list + 84; c.r4.u32 = list + 176;
  sub_824C1F08(c, base);
  if (REXCVAR_GET(mouse_native_trace))
    REXLOG_INFO("native_mouse: scrollbar first={} target={} row={} dir={}", current, first, row, direction);
}
}

bool MouseSupportEnabled() { return REXCVAR_GET(mouse_ui_nav); }
bool MouseMenuAvailable() {
  std::lock_guard lock(mutex);
  return MouseSupportEnabled() && menu_available && window_focused;
}
void MouseMenuFocus(bool focused) {
  std::lock_guard lock(mutex);
  if (focused != window_focused && REXCVAR_GET(mouse_native_trace))
    REXLOG_INFO("native_mouse: window focus={} queue={}", focused, focused && menu_available && MouseSupportEnabled());
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
void QueueMenuScrollbar(MouseAction action, MenuPoint point) {
  std::lock_guard lock(mutex);
  if (MouseSupportEnabled()) queue.Scrollbar(action, point);
}
bool MouseMenuRailHit(MenuPoint point) {
  const auto rail = RailSnapshot();
  return MouseSupportEnabled() && rail.valid &&
      MenuBounds{rail.left, rail.top, rail.right, rail.bottom}.Contains(point);
}
bool MouseMenuScrollZone(MenuPoint point) {
  // The rail plus a small margin: a click here belongs to the scrollbar even when it
  // misses the strip, so it must not fall through to the A press (a rail-adjacent click
  // used to start the selected song).
  const auto rail = RailSnapshot();
  return MouseSupportEnabled() && rail.valid &&
      MenuBounds{rail.left - .022f, rail.top - .025f, rail.right + .020f, rail.bottom + .025f}.Contains(point);
}
uint16_t MenuMousePadState() {
  std::lock_guard lock(mutex);
  return MouseSupportEnabled() ? queue.pulse() : 0;
}
uint16_t SampleMenuMousePadState() {
  std::lock_guard lock(mutex);
  const auto pulse = queue.SamplePad();
  return MouseSupportEnabled() ? pulse : 0;
}

uint32_t CurrentScreen() {
  std::lock_guard lock(mutex);
  return current_screen;
}
uint32_t CurrentFocus() {
  std::lock_guard lock(mutex);
  return current_focus;
}
uint32_t FocusedListDrawn() {
  std::lock_guard lock(mutex);
  return focused_list_drawn;
}
bool MenuAvailable() {
  std::lock_guard lock(mutex);
  return menu_available;
}

void PollMouse(PPCContext& ctx, uint8_t* base, uint32_t manager) {
  ++frame_number;
  Context(ctx, base, manager);
  std::optional<MenuMouseEvent> event;
  const bool fresh = geometry.list && geometry.list == current_focus && geometry.frame + 1 == frame_number;
  const bool railed = fresh && !geometry.horizontal && geometry.pitch > 0 && RailSnapshot().valid;
  // Retail UIListState rejects another directional input while the viewport is
  // animating (firstShowing != targetShowing). Keep that detent queued instead - except
  // on a railed list, where the wheel advances a plain target and needs no hold.
  const bool can_scroll = !fresh || railed || U32(base, current_focus + 224) == U32(base, current_focus + 228);
  { std::lock_guard lock(mutex); event = queue.NextFrame(can_scroll); }
  if (!event) return;
  // Scrollbar traffic drives the list's view directly (no pad pulse, no horizontal
  // remap), so it is handled before the directional path below.
  if (event->action == MouseAction::kScrollbarDown || event->action == MouseAction::kScrollbarDrag ||
      event->action == MouseAction::kScrollbarUp) {
    if (event->action == MouseAction::kScrollbarDown) {
      rail_drag = {};
      // Anchor on the pending target: the thumb's drawn position follows it, and the
      // drag then moves the view port row-for-row with the cursor.
      const int anchor = geometry.selected >= 0 ? geometry.selected : last_selected_row;
      if (fresh && railed && anchor >= 0 && EnsureRailMap(ctx, base, current_focus))
        rail_drag = {true, false, current_focus, int(U32(base, current_focus + 228)), event->point.y};
      if (REXCVAR_GET(mouse_native_trace))
        REXLOG_INFO("native_mouse: gutter press y={:.3f} armed={}", event->point.y, rail_drag.active);
    } else if (event->action == MouseAction::kScrollbarDrag) {
      const float per = RailPerFirstNow();
      if (rail_drag.active && rail_drag.list == current_focus && fresh && railed && per > 0) {
        const int delta = int(std::lround((event->point.y - rail_drag.y) / per));
        ScrollListTo(ctx, base, rail_drag.list,
                     std::clamp(rail_drag.first + delta, 0, rail_map.max_first));
        rail_drag.moved = true;
      }
    } else {
      // A press that never moved slides the thumb to the cursor unless it already sat on
      // the thumb (which is a grab), the way a track click lands in a native scrollbar.
      const bool on_thumb = RailYOnThumb(rail_drag.y, kRailTop, RailPerFirstNow(), kThumbHeight, rail_drag.first);
      if (rail_drag.active && rail_drag.list == current_focus && fresh && railed &&
          !rail_drag.moved && !on_thumb)
        ScrollListTo(ctx, base, rail_drag.list, RailFirstFromY(rail_drag.y, kRailTop, RailPerFirstNow(), rail_map.max_first));
      rail_drag = {};
    }
    return;
  }
  // The wheel scrolls a railed list's view instead of stepping its selection (owner
  // rule, 2026-10-16): a scrollbar moves the view; selection is a controller behaviour.
  // One detent is one list row (firstShowing counts rows), so the sensitivity is the
  // same per detent on any library - the fraction of the list it covers shrinks with
  // the library, the way a desktop scrollbar gets more granular with more content.
  if ((event->action == MouseAction::kUp || event->action == MouseAction::kDown) && railed &&
      EnsureRailMap(ctx, base, current_focus) && rail_map.max_first > 0) {
    const int base_first = int(U32(base, current_focus + 228));
    ScrollListTo(ctx, base, current_focus, base_first + (event->action == MouseAction::kDown ? 1 : -1));
    return;
  }
  const auto screen = current_screen, focus = current_focus;
  if (geometry.list == focus && geometry.frame + 1 == frame_number && geometry.horizontal) {
    if (event->action == MouseAction::kUp) event->action = MouseAction::kLeft;
    if (event->action == MouseAction::kDown) event->action = MouseAction::kRight;
  }
  if (event->action == MouseAction::kMove || event->action == MouseAction::kAccept) {
    const bool on_row = Select(ctx, base, event->point);
    // A click only presses what the mouse is on: on a screen whose list tracks the mouse,
    // an unhovered click (empty space, or a scroll-gutter miss) must not press A (owner
    // rule, 2026-10-16). Screens without a captured list - the title's own prompt, for
    // one - keep their at-principle A, which is how they are started.
    if (event->action == MouseAction::kAccept && !on_row && geometry.list == current_focus &&
        geometry.frame + 1 == frame_number)
      return;
  }
  // A selection notification can itself replace a screen, so recheck before A.
  Context(ctx, base, manager);
  std::lock_guard lock(mutex);
  if (queue.enabled() && screen == current_screen && focus == current_focus) queue.Pulse(event->action);
}

struct DrawCapture {
  Geometry result;
  uint32_t first = 0, end = 0, row = 0;
  int selected = -1;
  std::optional<MenuBounds> highlight;
};
thread_local DrawCapture* drawing = nullptr;

// Static retail meshes discard their CPU vertices after upload. Keep only their
// local bounds, including the packed 36-byte vertex representation loaded from Milo.
// The GPU allocation identifies the resource; a reused guest address cannot reuse
// an old box without passing through OnSync again.
struct MeshBox { std::array<float, 3> low, high; uint32_t resource = 0; };
std::mutex mesh_mutex;
std::unordered_map<uint32_t, MeshBox> mesh_boxes;
void SyncMesh(PPCContext& ctx, uint8_t* base) {
  const auto mesh = ctx.r3.u32, owner = U32(base, mesh + 324);
  std::optional<MeshBox> box;
  if (owner == mesh) {
    auto vertices = U32(base, mesh + 256), count = U32(base, mesh + 260);
    uint32_t stride = 96;
    if (!count) { vertices = U32(base, mesh + 380); count = U32(base, mesh + 384); stride = 36; }
    // UI resources are small; avoid scanning large gameplay meshes on upload.
    if (vertices && count && count <= 4096) {
      MeshBox b{{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
      bool valid = true;
      for (uint32_t i = 0; i < count; ++i) for (unsigned axis = 0; axis < 3; ++axis) {
        const auto value = F32(base, vertices + i * stride + axis * 4);
        valid &= std::isfinite(value);
        b.low[axis] = std::min(b.low[axis], value);
        b.high[axis] = std::max(b.high[axis], value);
      }
      if (valid) box = b;
    }
  }
  __imp__sub_823F0118(ctx, base);
  if (owner == mesh) {
    std::lock_guard lock(mesh_mutex);
    if (box) {
      box->resource = U32(base, mesh + 412);
      if (mesh_boxes.size() >= 65536) mesh_boxes.clear();
      mesh_boxes[mesh] = *box;
    } else if (auto it = mesh_boxes.find(mesh); it != mesh_boxes.end() &&
               it->second.resource != U32(base, mesh + 412)) mesh_boxes.erase(it);
  }
}
std::optional<MenuBounds> ProjectMesh(PPCContext& ctx, uint8_t* base, uint32_t mesh) {
  const auto camera = U32(base, 0x8284A108), owner = U32(base, mesh + 324);
  if (!camera || !owner) return {};
  MeshBox box;
  {
    std::lock_guard lock(mesh_mutex);
    const auto it = mesh_boxes.find(owner);
    if (it == mesh_boxes.end() || it->second.resource != U32(base, owner + 412)) return {};
    box = it->second;
  }
  GuestCall call(ctx); auto& c = call.frame.ctx;
  const auto local = call.scratch, world = local + 16, projected = local + 32;
  MenuBounds b{INFINITY, INFINITY, -INFINITY, -INFINITY};
  for (unsigned corner = 0; corner < 8; ++corner) {
    for (unsigned axis = 0; axis < 3; ++axis)
      Float(base, local + axis * 4, corner & (1 << axis) ? box.high[axis] : box.low[axis]);
    c.r3.u32 = local; c.r4.u32 = mesh + 136; c.r5.u32 = world;
    sub_821E14A8(c, base);
    c.r3.u32 = camera; c.r4.u32 = world; c.r5.u32 = projected;
    sub_821FF4A0(c, base);
    const auto x = F32(base, projected), y = F32(base, projected + 4);
    if (!std::isfinite(x) || !std::isfinite(y)) return {};
    b.left = std::min(b.left, x); b.right = std::max(b.right, x);
    b.top = std::min(b.top, y); b.bottom = std::max(b.bottom, y);
  }
  return b;
}
void CaptureRow(uint8_t* base, MenuBounds bounds) {
  if (!drawing || !drawing->row) return;
  const auto row = drawing->row;
  if (F32(base, row + 20) < .5f) return;
  auto& result = drawing->result;
  const int index = U32(base, row + 36);
  auto it = std::find_if(result.rows.begin(), result.rows.end(), [index](const MenuRow& r) { return r.index == index; });
  if (it == result.rows.end()) {
    result.rows.push_back({{}, index, U32(base, row + 24) != 2 && U32(base, row + 28) != 2, bounds});
  } else {
    bounds.left = std::min(bounds.left, it->bounds->left); bounds.right = std::max(bounds.right, it->bounds->right);
    bounds.top = std::min(bounds.top, it->bounds->top); bounds.bottom = std::max(bounds.bottom, it->bounds->bottom);
    it->bounds = bounds;
  }
}

void CaptureLabel(PPCContext& ctx, uint8_t* base) {
  if (!drawing || !drawing->row || ctx.r7.u32) return;
  const auto row = drawing->row;
  const auto label = U32(base, ctx.r3.u32 + 8);
  const auto camera = U32(base, 0x8284A108);
  if (!label || !camera || F32(base, row + 20) < .5f) return;
  // Retail UILabel stores its current text rectangle at +144. The stock
  // UIList bounding-box path omits the final slot transform; the rendering
  // path provides that exact transform in r4, including provider adjustments.
  const float x = F32(base, label + 144), z = F32(base, label + 148);
  const float width = F32(base, label + 152), height = F32(base, label + 156);
  if (!(width > 0) || height == 0) return;
  GuestCall call(ctx); auto& c = call.frame.ctx;
  const auto local = call.scratch, world = local + 16, projected = local + 32;
  MenuBounds bounds{1e10f, 1e10f, -1e10f, -1e10f};
  for (unsigned corner = 0; corner < 4; ++corner) {
    Float(base, local, x + (corner & 1 ? width : 0));
    Float(base, local + 4, 0);
    Float(base, local + 8, z + (corner & 2 ? height : 0));
    c.r3.u32 = local; c.r4.u32 = ctx.r4.u32; c.r5.u32 = world;
    sub_821E14A8(c, base);
    c.r3.u32 = camera; c.r4.u32 = world; c.r5.u32 = projected;
    sub_821FF4A0(c, base);
    const auto px = F32(base, projected), py = F32(base, projected + 4);
    if (!std::isfinite(px) || !std::isfinite(py)) return;
    bounds.left = std::min(bounds.left, px); bounds.right = std::max(bounds.right, px);
    bounds.top = std::min(bounds.top, py); bounds.bottom = std::max(bounds.bottom, py);
  }
  CaptureRow(base, bounds);
}
// Defined after DrawList; declared here because the draw is what discovers a new list.
void DrawList(PPCContext& ctx, uint8_t* base) {
  const uint32_t list = ctx.r5.u32 - 176;
  // Record the focused list's identity for the help caption observer (src/hooks/help_observer.cpp)
  // before the mouse-specific early-outs: this is the only place a list is *positively* identified
  // as the focused component by the engine itself, and the observer must not depend on the mouse
  // navigation being enabled.
  if (!ctx.r9.u32 && list != 0 && list == current_focus && menu_available) {
    std::lock_guard lock(mutex);
    focused_list_drawn = list;
  }
  if (!MouseSupportEnabled() || ctx.r9.u32 || list != current_focus || !menu_available) {
    __imp__sub_824E5020(ctx, base); return;
  }
  DrawCapture capture;
  capture.result.list = list; capture.result.frame = frame_number;
  capture.first = U32(base, ctx.r4.u32 + 56); capture.end = U32(base, ctx.r4.u32 + 60);
  const auto display = U32(base, ctx.r4.u32 + 48);
  for (auto row = capture.first; row < capture.end; row += 44)
    if (U32(base, row + 32) == display) capture.selected = U32(base, row + 36);
  auto previous = drawing; drawing = &capture;
  __imp__sub_824E5020(ctx, base);
  drawing = previous;
  auto& result = capture.result;
  // The native highlight supplies the full button width, even when its text is
  // short. Translate its cell along the row axis; no screen-specific pitches.
  const auto selected = std::find_if(result.rows.begin(), result.rows.end(), [&](const MenuRow& r) { return r.index == capture.selected; });
  if (capture.highlight && selected != result.rows.end()) {
    float min_x = 1e10f, max_x = -1e10f, min_y = 1e10f, max_y = -1e10f;
    for (const auto& row : result.rows) {
      const auto& b = *row.bounds;
      min_x = std::min(min_x, (b.left+b.right)*.5f); max_x = std::max(max_x, (b.left+b.right)*.5f);
      min_y = std::min(min_y, (b.top+b.bottom)*.5f); max_y = std::max(max_y, (b.top+b.bottom)*.5f);
    }
    const bool horizontal = max_x - min_x > max_y - min_y;
    const auto origin = *selected->bounds;
    for (auto& row : result.rows) {
      auto& b = *row.bounds;
      auto cell = *capture.highlight;
      if (horizontal) {
        const auto offset = (b.left + b.right - origin.left - origin.right) * .5f;
        cell.left += offset; cell.right += offset;
      } else {
        const auto offset = (b.top + b.bottom - origin.top - origin.bottom) * .5f;
        cell.top += offset; cell.bottom += offset;
      }
      b.left = std::min(b.left, cell.left); b.right = std::max(b.right, cell.right);
      b.top = std::min(b.top, cell.top); b.bottom = std::max(b.bottom, cell.bottom);
    }
  }
  result.bounds = {1e10f, 1e10f, -1e10f, -1e10f};
  for (auto& row : result.rows) {
    const auto& b = *row.bounds;
    row.center = {(b.left + b.right) * .5f, (b.top + b.bottom) * .5f};
    result.bounds.left = std::min(result.bounds.left, b.left); result.bounds.right = std::max(result.bounds.right, b.right);
    result.bounds.top = std::min(result.bounds.top, b.top); result.bounds.bottom = std::max(result.bounds.bottom, b.bottom);
    if (REXCVAR_GET(mouse_native_trace) && geometry.list != list)
      REXLOG_INFO("native_mouse: row={} bounds={},{},{},{} enabled={}", row.index, b.left,b.top,b.right,b.bottom,row.enabled);
  }
  if (result.rows.size() > 1) {
    float min_x = INFINITY, max_x = -INFINITY, min_y = INFINITY, max_y = -INFINITY;
    for (const auto& row : result.rows) {
      min_x = std::min(min_x, row.center.x); max_x = std::max(max_x, row.center.x);
      min_y = std::min(min_y, row.center.y); max_y = std::max(max_y, row.center.y);
    }
    result.horizontal = max_x - min_x > max_y - min_y;
  }
  geometry = std::move(result);
  geometry.selected = capture.selected;
  if (capture.selected >= 0) last_selected_row = capture.selected;
  geometry.pitch = RailPitch(geometry.rows);
  MenuRail rail;
  float right = -INFINITY;
  for (const auto& row : geometry.rows)
    if (row.bounds) right = std::max(right, row.bounds->right);
  if (!geometry.horizontal && geometry.pitch > 0 && std::isfinite(right)) {
    // The rail's own extent, not the row band: the thumb travels inside it (measured).
    rail.left = right + .023f; rail.right = rail.left + .024f;
    rail.top = kRailTop; rail.bottom = kRailBottom;
    rail.valid = true;
  }
  PublishRail(rail);
}
} // namespace rb_blitz::input

REX_HOOK_RAW(sub_824B2958) {
  const auto manager = ctx.r3.u32;
  rb_blitz::input::PollMouse(ctx, base, manager);
  __imp__sub_824B2958(ctx, base);
  rb_blitz::input::Context(ctx, base, manager);
  // The help captions' observer (owner request, 2026-10-13): the frame's input has been
  // dispatched by now, so a selection that moved this frame is already visible and the observer
  // can publish its row's description - or retire a caption when the surface was left.
  rb_blitz::hooks::HelpObserverTick(ctx, base,
                                    {rb_blitz::input::CurrentScreen(),
                                     rb_blitz::input::CurrentFocus(),
                                     rb_blitz::input::FocusedListDrawn(),
                                     rb_blitz::input::MenuAvailable()});
}
REX_HOOK_RAW(sub_824E5020) {
  rb_blitz::input::DrawList(ctx, base);
}
REX_HOOK_RAW(sub_824E07A8) {
  using namespace rb_blitz::input;
  if (drawing) {
    const auto row = ctx.r5.u32 - 4;
    drawing->row = row >= drawing->first && row < drawing->end && (row - drawing->first) % 44 == 0 ? row : 0;
  }
  __imp__sub_824E07A8(ctx, base);
}
REX_HOOK_RAW(sub_824E9A90) {
  rb_blitz::input::CaptureLabel(ctx, base);
  __imp__sub_824E9A90(ctx, base);
}
REX_HOOK_RAW(sub_824E0900) {
  using namespace rb_blitz::input;
  const auto mesh = ctx.r4.u32;
  const bool highlight = drawing && !ctx.r8.u32 && U32(base, U32(base, ctx.r3.u32) + 100) == 0x824E9098;
  __imp__sub_824E0900(ctx, base);
  if (highlight) drawing->highlight = ProjectMesh(ctx, base, mesh);
}

REX_HOOK_RAW(sub_823F0118) {
  rb_blitz::input::SyncMesh(ctx, base);
}
REX_HOOK_RAW(sub_824BFEF0) {
  using namespace rb_blitz::input;
  const auto mesh = drawing && drawing->row && !ctx.r7.u32 && U32(base, ctx.r3.u32 + 8)
      ? U32(base, U32(base, ctx.r3.u32 + 4) + 132) : 0;
  __imp__sub_824BFEF0(ctx, base);
  if (mesh) if (auto bounds = ProjectMesh(ctx, base, mesh)) CaptureRow(base, *bounds);
}
