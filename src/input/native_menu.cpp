// SPDX-License-Identifier: GPL-2.0-only
// Retail guest ABI and validation: docs/engine/input-flow.md.
#include "input/native_menu.h"
#include "diag/shader_trace.h"
#include "input/remap.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/kernel/xam/module.h>
#include "hooks/help_observer.h"
#include "loading/load_trace.h"
#include "loading/crash_recovery.h"
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
REX_EXTERN(sub_824C0C08);       // UIList::NumData (dispatches to the current provider)
REX_EXTERN(sub_82402DE8);       // ObjectDir::FindObject
REX_EXTERN(sub_821F13D0);       // RndTransformable::WorldXfm
REX_EXTERN(__imp__sub_824BC668); // UILabel::DrawShowing (label +260)
REX_EXTERN(sub_824BA9F8);       // UILabel::LabelStyle

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
bool game_screen_active = false;
uint32_t current_screen = 0, current_focus = 0;
// The current screen's guest object name, used by native mouse navigation.
std::string current_screen_name;
// The focused list the previous frame's draw identified (see DrawList). Read by the help caption
// observer; 0 when no list has been drawn as the focused component yet.
uint32_t focused_list_drawn = 0;
uint32_t music_list_screen = 0, music_list_frame = 0, observed_menu_frame = 0;

struct Geometry {
  uint32_t list = 0, frame = 0;
  bool horizontal = false;
  MenuBounds bounds;
  std::vector<MenuRow> rows;
  // The focused row's logical data index, preserved while the viewport scrolls.
  int selected = -1;
};
thread_local Geometry geometry;
thread_local uint32_t frame_number = 0;
struct HeaderGeometry { uint32_t screen = 0, focus = 0, frame = 0; MenuBounds bounds; };
thread_local std::array<HeaderGeometry, 4> header_geometry;
thread_local int clicked_header = -1;
thread_local uint32_t header_click_frame = 0;
thread_local std::string current_panel_name;
// The last row a draw positively identified as selected. The guest sets the selected
// *slot* to -1 when a scroll left the selection outside the band, so the slot cannot
// be the anchor; this survives that and is reset on every screen or focus change.
thread_local int last_selected_row = -1;
// The mouse's hover ownership and the unhover lift (MouseHighlight in native_menu.h).
// Only a hover the mouse itself set may be lifted when the pointer leaves it; a
// controller's own selection is never touched by mouse motion.
thread_local MouseHighlight mouse_highlight;
// The scroll-aware hover (MouseScrollFollow in native_menu.h): a scroll the mouse applied moves
// the rows, so the row that comes to rest under the pointer is caught once the burst has drained
// and the drawn frame shows the new view.
thread_local MouseScrollFollow scroll_follow;
thread_local MenuRailDrag rail_drag;

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
struct RailMap {
  bool known = false;
  uint32_t list = 0;
  int max_first = 0;
};
thread_local RailMap rail_map;
// Resolve how far the focused list can scroll: provider NumData minus the visible row
// count, using the list's own virtual dispatch. Providers and their contents can change
// without changing the focused component (search results, sorting, store refreshes).
bool EnsureRailMap(PPCContext& ctx, uint8_t* base, uint32_t list) {
  GuestCall call(ctx);
  call.frame.ctx.r3.u32 = list;
  sub_824C0C08(call.frame, base);
  const int rows = call.frame.ctx.r3.s32, display = int(U32(base, list + 180));
  if (rows <= 0 || display <= 0) { rail_map = {}; return false; }
  const bool changed = !rail_map.known || rail_map.list != list || rail_map.max_first != std::max(0, rows - display);
  rail_map = {true, list, std::max(0, rows - display)};
  if (changed && REXCVAR_GET(mouse_native_trace))
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
  const bool in_song = screen != 0 && name == "game_screen" && panel_name != "pause_panel";
  rb_blitz::loading::ObserveRecoveryScreen(name, allowed);
  rb_blitz::content_progress::ObserveScreen(name, panel_name, allowed);
  rb_blitz::diag::shader_trace::ObserveScreen(name, panel_name, ObjectName(base, ObjectBase(base, focus)),
                                             U32(base, manager + 16) != 0);
  if (screen != current_screen || focus != current_focus || allowed != menu_available) {
    header_geometry = {};
    clicked_header = -1;
    MenuRail stale; PublishRail(stale);
    last_selected_row = -1;
    rail_map = {};
    rail_drag.Reset();
    mouse_highlight.Reset();
    scroll_follow.Reset();
    // P2 (plan D5): a screen or focus change is a boundary the gameplay tables must not carry
    // held state across. The gameplay holds are dropped without an edge on the next poll, so a
    // press or release from the old context can never arrive late in the new one.
    InvalidateGameplayInput();
    if (REXCVAR_GET(mouse_native_trace))
      REXLOG_INFO("native_mouse: screen={} panel={} focus={:08X} available={}", name, panel_name, focus, allowed);
  }
  std::lock_guard lock(mutex);
  current_screen = screen; current_focus = focus; menu_available = allowed;
  observed_menu_frame = frame_number;
  if (current_screen_name != name) current_screen_name = name;
  game_screen_active = in_song;
  current_panel_name = panel_name;
  queue.Context(screen, focus, allowed && window_focused && MouseSupportEnabled() &&
                !rex::kernel::xam::xeXamIsUIActive());
}
// UIListState::Selected - the list's live selection - read with isolated registers like
// every other side call (the same read the hit test's ordering uses).
int SelectedRow(PPCContext& ctx, uint8_t* base, uint32_t list) {
  GuestCall call(ctx);
  call.frame.ctx.r3.u32 = list + 176;
  sub_824E3008(call.frame.ctx, base);
  return call.frame.ctx.r3.s32;
}
// Returns whether the point named a row of the focused list. When it did not, the mouse's
// own hover ends: a click that does not land on an element cannot press anything, and the
// highlight leaves the frame while the list still shows the row the mouse set (owner rule,
// 2026-10-16 - MouseHighlight in native_menu.h). The guest's selected row itself is never
// written, so the album panel and the controller keep their anchor.
bool Select(PPCContext& ctx, uint8_t* base, MenuPoint point) {
  // The pointer from the last draw is valid only for the same focused component
  // in the immediately preceding frame. Never dereference a retired screen's list.
  if (!geometry.list || geometry.frame + 1 != frame_number ||
      current_focus != geometry.list) return false;
  const int row = HitMenuRow(point, geometry.bounds, geometry.rows);
  if (REXCVAR_GET(mouse_native_trace)) REXLOG_INFO("native_mouse: hit={},{} row={} bounds={},{},{},{}", point.x, point.y, row, geometry.bounds.left, geometry.bounds.top, geometry.bounds.right, geometry.bounds.bottom);
  if (row < 0) {
    const auto left = mouse_highlight.PointerLeft(geometry.list, SelectedRow(ctx, base, geometry.list));
    if (left != MouseHighlight::Leave::kIgnored && REXCVAR_GET(mouse_native_trace))
      REXLOG_INFO("native_mouse: unhover list={:08X} highlight={}", geometry.list,
                  left == MouseHighlight::Leave::kLifted ? "off" : "kept");
    return false;
  }
  mouse_highlight.Hover(geometry.list, row);
  const int selected = SelectedRow(ctx, base, geometry.list);
  if (selected == row) return true;
  const int direction = row > selected ? 1 : -1;
  GuestCall call(ctx);
  auto& c = call.frame.ctx;
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
// Move only the viewport, keep the guest's selection row, then notify the providers the
// way the guest's own scroll does. `first` is clamped to the list's own last page; an
// applied scroll ends the hover it owned and arms the scroll-aware catch under `pointer`
// (MouseHighlight/MouseScrollFollow in native_menu.h): the row the pointer was on is
// sliding away, so the highlight lifts and then lands wherever the rows stop.
bool ScrollListTo(PPCContext& ctx, uint8_t* base, uint32_t list, int first, MenuPoint pointer) {
  const int row = geometry.selected >= 0 ? geometry.selected : last_selected_row;
  if (row < 0) return false;
  if (rail_map.known && rail_map.list == list) first = std::clamp(first, 0, rail_map.max_first);
  else first = std::max(0, first);
  const uint32_t current = U32(base, list + 224);
  if (uint32_t(first) == current) return false;
  const auto left = mouse_highlight.PointerLeft(list, row);
  if (left != MouseHighlight::Leave::kIgnored && REXCVAR_GET(mouse_native_trace))
    REXLOG_INFO("native_mouse: unhover list={:08X} highlight={} (scroll)", list,
                left == MouseHighlight::Leave::kLifted ? "off" : "kept");
  scroll_follow.Arm(pointer);
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
  return true;
}
std::optional<MouseAction> PresetArrowAt(PPCContext& ctx, uint8_t* base, MenuPoint point);
}

bool MouseSupportEnabled() { return true; }
bool AnswerMenuHeaderQuery(std::string_view path, bool* answer) {
  constexpr std::string_view prefix = "rbbz-mouse/header/";
  if (!path.starts_with(prefix)) return false;
  *answer = false;
  const auto suffix = path.substr(prefix.size());
  const auto slash = suffix.rfind('/');
  if (slash == std::string_view::npos || slash + 2 != suffix.size() ||
      suffix.back() < '0' || suffix.back() > '3') return true;
  std::lock_guard lock(mutex);
  if (queue.enabled() && MouseSupportEnabled() && header_click_frame == frame_number &&
      suffix.substr(0, slash) == current_panel_name && clicked_header == suffix.back() - '0') {
    *answer = true;
    clicked_header = -1;
  }
  return true;
}
bool MouseMenuAvailable() {
  std::lock_guard lock(mutex);
  return MouseSupportEnabled() && menu_available && window_focused;
}
void MouseMenuFocus(bool focused) {
  std::lock_guard lock(mutex);
  if (focused != window_focused && REXCVAR_GET(mouse_native_trace))
    REXLOG_INFO("native_mouse: window focus={} queue={}", focused, focused && menu_available && MouseSupportEnabled());
  window_focused = focused;
  queue.Context(current_screen, current_focus, focused && menu_available && MouseSupportEnabled() &&
                !rex::kernel::xam::xeXamIsUIActive());
  // P2 (plan D5): losing the window drops every gameplay hold. The physical layer already
  // reports nothing while unfocused; this makes the *edge* disappear too, so a key released
  // behind another window cannot be delivered as one the guest acted on.
  if (!focused) InvalidateGameplayInput();
}
void QueueMenuMouse(MouseAction action, MenuPoint point) {
  std::lock_guard lock(mutex);
  if (MouseSupportEnabled()) queue.Push(action, point);
}
void QueueMenuWheel(int delta, MenuPoint point) {
  std::lock_guard lock(mutex);
  if (MouseSupportEnabled()) queue.Wheel(delta, point);
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
bool MenuScreenIs(std::string_view name) {
  std::lock_guard lock(mutex);
  return current_screen_name == name;
}
bool MainMenuMusicInputAvailable() {
  std::lock_guard lock(mutex);
  return menu_available && current_screen_name == "splash_screen" &&
         focused_list_drawn != 0 && focused_list_drawn == current_focus &&
         music_list_screen == current_screen &&
         (music_list_frame == observed_menu_frame || music_list_frame + 1 == observed_menu_frame);
}
bool GameScreenActive() {
  std::lock_guard lock(mutex);
  return game_screen_active;
}

void PollMouse(PPCContext& ctx, uint8_t* base, uint32_t manager) {
  ++frame_number;
  clicked_header = -1;
  Context(ctx, base, manager);
  std::optional<MenuMouseEvent> event;
  const bool fresh = geometry.list && geometry.list == current_focus && geometry.frame + 1 == frame_number;
  const auto rail = RailSnapshot();
  const bool railed = fresh && !geometry.horizontal && rail.valid;
  // Retail UIListState rejects another directional input while the viewport is
  // animating (firstShowing != targetShowing). Keep that detent queued instead - except
  // on a railed list, where the wheel advances a plain target and needs no hold.
  const bool can_scroll = !fresh || railed || U32(base, current_focus + 224) == U32(base, current_focus + 228);
  bool idle = false, enabled = false;
  {
    std::lock_guard lock(mutex);
    event = queue.NextFrame(can_scroll);
    idle = !queue.Pending();
    enabled = queue.enabled();
  }
  if (!enabled) rail_drag.Reset();
  // The scroll-aware hover's landing (MouseScrollFollow in native_menu.h): the scroll has been
  // applied and drawn (a fresh frame carries the new view), the burst has drained and the
  // viewport is at rest, so the row under the pointer is caught - or the hover stays empty (the
  // lift the scroll started keeps it that way when no row landed there). A disabled queue means
  // discarded input, so the catch is dropped instead of landing late.
  if (!event && idle && fresh &&
      U32(base, current_focus + 224) == U32(base, current_focus + 228)) {
    if (auto point = scroll_follow.Take()) {
      if (enabled) {
        if (REXCVAR_GET(mouse_native_trace))
          REXLOG_INFO("native_mouse: scroll follow point={},{}", point->x, point->y);
        Select(ctx, base, *point);
      }
    }
  }
  if (!event) return;
  // Header clicks precede the focused list's hit test and never become A.
  // Only a label actually drawn in the last frame can receive the click.
  if (event->action == MouseAction::kAccept && LibraryHeaderForScreen(current_screen_name) >= 0) {
    for (int i = 0; i < int(header_geometry.size()); ++i) {
      const auto& header = header_geometry[i];
      if (header.screen != current_screen || header.focus != current_focus ||
          header.frame + 1 != frame_number || !header.bounds.Contains(event->point)) continue;
      Select(ctx, base, event->point); // retire the list's mouse hover
      scroll_follow.Reset();
      if (i != LibraryHeaderForScreen(current_screen_name)) {
        clicked_header = i;
        header_click_frame = frame_number;
        if (REXCVAR_GET(mouse_native_trace))
          REXLOG_INFO("native_mouse: header click screen={} panel={} target={}",
                      current_screen_name, current_panel_name, i);
      }
      return;
    }
  }
  if (event->action == MouseAction::kAccept) {
    if (const auto arrow = PresetArrowAt(ctx, base, event->point)) {
      // The art belongs to the guest's preset page. A controller D-pad pulse
      // reaches its own left/right handler, including its normal wrap behavior.
      Select(ctx, base, event->point); // retire any mouse-owned list hover
      scroll_follow.Reset();
      event->action = *arrow;
    }
  }
  // Scrollbar traffic drives the list's view directly (no pad pulse, no horizontal
  // remap), so it is handled before the directional path below.
  if (event->action == MouseAction::kScrollbarDown || event->action == MouseAction::kScrollbarDrag ||
      event->action == MouseAction::kScrollbarUp) {
    if (event->action == MouseAction::kScrollbarDown) {
      rail_drag.Reset();
      const int anchor = geometry.selected >= 0 ? geometry.selected : last_selected_row;
      if (railed && anchor >= 0 && EnsureRailMap(ctx, base, current_focus)) {
        const int target = rail_drag.Begin(current_focus, event->point.y, rail,
                                          int(U32(base, current_focus + 228)), rail_map.max_first);
        if (rail_drag.active) ScrollListTo(ctx, base, current_focus, target, event->point);
      }
      if (REXCVAR_GET(mouse_native_trace))
        REXLOG_INFO("native_mouse: gutter press y={:.3f} armed={}", event->point.y, rail_drag.active);
    } else {
      // Apply the release position too: a fast drag may finish between guest frames.
      if (rail_drag.active && rail_drag.list == current_focus && railed &&
          std::isfinite(event->point.y) && EnsureRailMap(ctx, base, current_focus)) {
        ScrollListTo(ctx, base, rail_drag.list,
                     rail_drag.Target(event->point.y, rail, rail_map.max_first), event->point);
      }
      if (event->action == MouseAction::kScrollbarUp) rail_drag.Reset();
    }
    return;
  }
  // The wheel scrolls a railed list's view instead of stepping its selection (owner
  // rule, 2026-10-16): a scrollbar moves the view; selection is a controller behaviour.
  // Large song libraries advance farther per notch, up to one visible page.
  if ((event->action == MouseAction::kUp || event->action == MouseAction::kDown) && railed &&
      EnsureRailMap(ctx, base, current_focus) && rail_map.max_first > 0) {
    const int base_first = int(U32(base, current_focus + 228));
    const int display = int(U32(base, current_focus + 180));
    const int step = current_screen_name == "song_select_screen" ?
        LibraryWheelRows(rail_map.max_first + display, display) : 1;
    ScrollListTo(ctx, base, current_focus, base_first + (event->action == MouseAction::kDown ? step : -step),
                 event->point);
    return;
  }
  const auto screen = current_screen, focus = current_focus;
  if (geometry.list == focus && geometry.frame + 1 == frame_number && geometry.horizontal) {
    if (event->action == MouseAction::kUp) event->action = MouseAction::kLeft;
    if (event->action == MouseAction::kDown) event->action = MouseAction::kRight;
  }
  if (event->action == MouseAction::kMove || event->action == MouseAction::kAccept) {
    // A later pointer event is where a pending scroll catch must land (MouseScrollFollow).
    scroll_follow.Pointer(event->point);
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
  // The mouse's unhover frame: the shared highlight mesh is skipped while this draw runs.
  bool suppress = false;
};
thread_local DrawCapture* drawing = nullptr;

// Static retail meshes discard their CPU vertices after upload. Keep only their
// local bounds, including the packed 36-byte vertex representation loaded from Milo.
// The GPU allocation identifies the resource; a reused guest address cannot reuse
// an old box without passing through OnSync again.
struct MeshBox { std::array<float, 3> low, high; uint32_t resource = 0; };
std::mutex mesh_mutex;
std::unordered_map<uint32_t, MeshBox> mesh_boxes;
// ObjectDir stores Hmx::Object pointers; OnSync supplies the corresponding RndMesh
// pointer. Keep that conversion alongside the resource-validated vertex bounds.
std::unordered_map<uint32_t, uint32_t> mesh_objects;
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
      if (mesh_boxes.size() >= 65536) { mesh_boxes.clear(); mesh_objects.clear(); }
      mesh_boxes[mesh] = *box;
      if (const auto object = ObjectBase(base, mesh)) mesh_objects[object] = mesh;
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
  // These sibling meshes may be queried before their own draw updates the world
  // transform. Resolve parent transforms before projecting their bounds.
  if (base[mesh + 253]) {
    c.r3.u32 = mesh + 64;
    sub_821F13D0(c, base);
  }
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

uint32_t SceneMesh(PPCContext& ctx, uint8_t* base, uint32_t dir, const char* name) {
  GuestCall call(ctx);
  std::memcpy(base + call.scratch, name, std::strlen(name) + 1);
  auto& c = call.frame.ctx;
  c.r3.u32 = dir; c.r4.u32 = call.scratch; c.r5.u32 = 0; c.r6.u32 = 0;
  sub_82402DE8(c, base);
  const auto object = c.r3.u32;
  std::lock_guard lock(mesh_mutex);
  const auto it = mesh_objects.find(object);
  if (it == mesh_objects.end() || ObjectBase(base, it->second) != object ||
      ObjectName(base, object) != name) return 0;
  return it->second;
}

namespace {
std::optional<MouseAction> PresetArrowAt(PPCContext& ctx, uint8_t* base, MenuPoint point) {
  if (current_screen_name != "controller_config_screen" &&
      current_screen_name != "controller_config_pause_screen") return {};
  // UIPanel +0x34 owns the loaded PanelDir, whose authored arrow meshes are
  // named in controller_config.milo_xbox. Resolve only on a click so placement
  // and aspect changes use the scene's current transforms at minimal cost.
  const uint32_t panel = U32(base, current_screen + 52);
  const uint32_t dir = panel ? U32(base, panel + 52) : 0;
  const uint32_t left = dir ? SceneMesh(ctx, base, dir, "arrow_lt.mesh") : 0;
  const uint32_t right = dir ? SceneMesh(ctx, base, dir, "arrow_rt.mesh") : 0;
  auto left_bounds = left ? ProjectMesh(ctx, base, left) : std::nullopt;
  auto right_bounds = right ? ProjectMesh(ctx, base, right) : std::nullopt;
  auto hit = HitPresetArrow(point, left_bounds, right_bounds);
  // The static meshes do not always yield a usable projected box. The authored
  // panel keeps both arrows at these guest coordinates; MenuPointer already
  // accounts for the window's size and presentation rectangle. These narrow
  // targets keep a click on the visible art from falling through to Confirm.
  if (!hit) hit = HitPresetArrow(point, MenuBounds{.365f, .31f, .378f, .35f},
                                 MenuBounds{.622f, .31f, .635f, .35f});
  REXLOG_INFO("native_mouse: preset click point={},{} panel={:08X} dir={:08X} meshes={:08X}/{:08X} projected={}/{} action={}",
              point.x, point.y, panel, dir, left, right, left_bounds.has_value(),
              right_bounds.has_value(), hit ? (*hit == MouseAction::kLeft ? "left" : "right") : "confirm");
  return hit;
}
}

MenuRail MeasureRail(PPCContext& ctx, uint8_t* base, uint32_t list, const Geometry& rows) {
  if (rows.horizontal || rows.rows.empty()) return {};
  const auto object = ObjectBase(base, list);
  const auto dir = object ? U32(base, object + 36) : 0;
  if (!dir) return {};
  const auto track_mesh = SceneMesh(ctx, base, dir, "rail.mesh");
  const auto thumb_mesh = SceneMesh(ctx, base, dir, "scroller.mesh");
  if (!track_mesh || !thumb_mesh) return {};
  const auto track = ProjectMesh(ctx, base, track_mesh);
  const auto thumb = ProjectMesh(ctx, base, thumb_mesh);
  if (!track || !thumb) return {};
  // A panel may also contain another focused list (shortcuts, leaderboards). Its
  // sibling scrollbar belongs only to the row band immediately to its left.
  // Text can occupy much less than the full row when its highlight is lifted, so
  // don't impose a maximum gap based on the current labels' right edge.
  if (track->left < rows.bounds.right - .025f ||
      track->bottom < rows.bounds.top || track->top > rows.bounds.bottom ||
      !EnsureRailMap(ctx, base, list) || rail_map.max_first <= 0) return {};
  const float height = thumb->bottom - thumb->top;
  if (!(height > 0) || track->bottom - track->top <= height) return {};
  // Keep a narrow native rail easy to grab, using both rendered meshes' widths.
  const float center = (thumb->left + thumb->right) * .5f;
  return {std::min({track->left, thumb->left, center - .006f}), track->top,
          std::max({track->right, thumb->right, center + .006f}), track->bottom, true, height, thumb->top};
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

void CaptureHeader(PPCContext& ctx, uint8_t* base, uint32_t label) {
  if (!menu_available || LibraryHeaderForScreen(current_screen_name) < 0) return;
  const int index = LibraryHeaderForLabel(ObjectName(base, ObjectBase(base, label)));
  const auto camera = U32(base, 0x8284A108);
  if (index < 0 || !camera) return;
  const float x = F32(base, label + 144), z = F32(base, label + 148);
  const float width = F32(base, label + 152), height = F32(base, label + 156);
  if (!(width > 0) || height == 0) return;
  GuestCall call(ctx); auto& c = call.frame.ctx;
  c.r3.u32 = label; c.r4.u32 = 0;
  sub_824BA9F8(c, base);
  // DrawShowing skips fully transparent label styles; those are not hit targets.
  if (!(F32(base, c.r3.u32 + 36) > 0)) return;
  // Resolve the same virtual RndTransformable base used by UIListLabelElement::Draw.
  c.r3.u32 = label + 72 + U32(base, U32(base, label + 72) + 16);
  sub_821F13D0(c, base);
  const auto transform = c.r3.u32;
  const auto local = call.scratch, world = local + 16, projected = local + 32;
  MenuBounds bounds{INFINITY, INFINITY, -INFINITY, -INFINITY};
  for (unsigned corner = 0; corner < 4; ++corner) {
    Float(base, local, x + (corner & 1 ? width : 0));
    Float(base, local + 4, 0);
    Float(base, local + 8, z + (corner & 2 ? height : 0));
    c.r3.u32 = local; c.r4.u32 = transform; c.r5.u32 = world;
    sub_821E14A8(c, base);
    c.r3.u32 = camera; c.r4.u32 = world; c.r5.u32 = projected;
    sub_821FF4A0(c, base);
    const float px = F32(base, projected), py = F32(base, projected + 4);
    if (!std::isfinite(px) || !std::isfinite(py)) return;
    bounds.left = std::min(bounds.left, px); bounds.right = std::max(bounds.right, px);
    bounds.top = std::min(bounds.top, py); bounds.bottom = std::max(bounds.bottom, py);
  }
  header_geometry[index] = {current_screen, current_focus, frame_number, bounds};
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
    music_list_screen = current_screen;
    music_list_frame = frame_number;
  }
  // The capture runs whatever the mouse preference says: the mouse hit test needs it, and so does
  // the settings overlay's row text (P1), which must work on a pad-only machine too. Only the
  // mouse-specific use of the result (Select, the hover lift, the wheel's rail) reads the
  // preference - the queue is disabled when it is off, so none of those ever runs.
  if (ctx.r9.u32 || list != current_focus || !menu_available) {
    __imp__sub_824E5020(ctx, base); return;
  }
  DrawCapture capture;
  capture.result.list = list; capture.result.frame = frame_number;
  capture.first = U32(base, ctx.r4.u32 + 56); capture.end = U32(base, ctx.r4.u32 + 60);
  const auto display = U32(base, ctx.r4.u32 + 48);
  for (auto row = capture.first; row < capture.end; row += 44)
    if (U32(base, row + 32) == display) capture.selected = U32(base, row + 36);
  // The mouse's unhover (owner rule, 2026-10-16; MouseHighlight in native_menu.h) is
  // cosmetic and per-draw: while the list still shows the row the pointer left, this
  // frame draws without a highlight. The element records are a per-draw local the caller
  // frees right after DrawWidgets, so nothing here survives the frame, and the captured
  // row geometry above was read from the real selected slot. A selection change the mouse
  // did not make ends the lift instead, so a controller's own highlight is never hidden.
  if (mouse_highlight.lifted_list == list) {
    if (!mouse_highlight.Lifted(list, SelectedRow(ctx, base, list))) {
      if (REXCVAR_GET(mouse_native_trace))
        REXLOG_INFO("native_mouse: highlight back list={:08X}", list);
    } else {
      // The element the engine marked highlighted drops to active (+24: 1 -> 0), and the
      // draw-state's highlight display slot reads -1, which makes highlight-only slots
      // (drawstate +48) skip. The shared highlight mesh is skipped in the DrawMesh hook
      // via capture.suppress. Nothing else in the draw uses these fields.
      for (auto row = capture.first; row < capture.end; row += 44)
        if (U32(base, row + 24) == 1)
          rex::memory::store_and_swap<uint32_t>(base + row + 24, 0);
      rex::memory::store_and_swap<uint32_t>(base + ctx.r4.u32 + 48, uint32_t(-1));
      capture.suppress = true;
    }
  }
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
  PublishRail(MeasureRail(ctx, base, list, geometry));

}
} // namespace rb_blitz::input

REX_HOOK_RAW(sub_824B2958) {
  const auto manager = ctx.r3.u32;
  rb_blitz::input::PollMouse(ctx, base, manager);
  __imp__sub_824B2958(ctx, base);
  rb_blitz::loading::ObserveLoadFrame(base, manager);
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
REX_HOOK_RAW(sub_824BC668) {
  const auto label = ctx.r3.u32 - 260;
  __imp__sub_824BC668(ctx, base);
  rb_blitz::input::CaptureHeader(ctx, base, label);
}
REX_HOOK_RAW(sub_824E0900) {
  using namespace rb_blitz::input;
  const auto mesh = ctx.r4.u32;
  const bool bar = drawing && !ctx.r8.u32 && U32(base, U32(base, ctx.r3.u32) + 100) == 0x824E9098;
  // The mouse's unhover frame draws no bar and captures no cell: the cell would be the
  // bar's own projection, and the hover that selects a row again restores both in the same
  // frame the pointer lands (`UIListHighlight::Draw`, at vtable +100). The mesh's transform
  // is left at its resting value; UIListHighlight::Draw restores it after every draw anyway.
  if (bar && drawing->suppress) return;
  __imp__sub_824E0900(ctx, base);
  if (bar) drawing->highlight = ProjectMesh(ctx, base, mesh);
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
