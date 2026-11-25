// SPDX-License-Identifier: GPL-2.0-only
// Retail guest ABI and validation: docs/engine/input-flow.md.
#include "input/native_menu.h"
#include "input/native_mesh.h"
#include "input/touch_scroll.h"
#include "hooks/hud_layout_runtime.h"
#include "input/inline_help.h"
#include "input/dialog_prompts.h"
#include "input/last_device.h"
#include "input/start_prompt.h"
#include "input/calibration_text.h"
#include "diag/shader_trace.h"
#include "input/remap.h"
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
#include "input/touch_controls.h"
#endif
#include <rex/ppc/hook.h>
#include <rex/ppc/call.h>
#include <rex/utility.h>
#include <rex/memory/endian.h>
#include "input/touch_controls.h"
#include "settings/runtime_settings.h"
#include <rex/diagnostics/compile_trace.h>
#include "input/title_input.h"
#include "hooks/help_observer.h"
#include "hooks/menu_filter.h"
#include "hooks/action_hint_probe.h"
#include "ui/input_font_scene.h"
#include "ui/achievements_overlay.h"
#include "ui/portrait_menu.h"
#include "hooks/portrait_layout.h"
#include "hooks/render_settings.h"
#include "ui/settings_screen.h"
#include "loading/load_trace.h"
#include "loading/crash_recovery.h"
#include "hooks/content_progress.h"
#include <bit>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <array>
#include <unordered_map>
#include <unordered_set>




REX_EXTERN(__imp__sub_824B2958); // UIManager::Poll
REX_EXTERN(__imp__sub_824E5020); // UIListDir::Draw
REX_EXTERN(__imp__sub_824E6DC8); // UIListDir::BuildDrawState
REX_EXTERN(sub_824B1980);       // UIManager::FocusComponent
REX_EXTERN(sub_824C1938);       // UIList::SetSelected
REX_EXTERN(sub_824C1F08);       // UIList::ScrollComplete
REX_EXTERN(sub_824C1DE8);       // UIList::ScrollStart
REX_EXTERN(__imp__sub_824E07A8); // UIListWidget::CalcXfm
REX_EXTERN(__imp__sub_824E9A90); // UIListLabelElement::Draw
REX_EXTERN(__imp__sub_824E0900); // UIListWidget::DrawMesh
REX_EXTERN(__imp__sub_824BFEF0); // UIListMeshElement::Draw
REX_EXTERN(sub_821E14A8);       // Multiply(Vector3, Transform)
REX_EXTERN(sub_821FF4A0);       // RndCam::WorldToScreen
REX_EXTERN(sub_824E3008);       // UIListState::Selected (logical index)
REX_EXTERN(sub_824C0C08);       // UIList::NumData (dispatches to the current provider)
REX_EXTERN(sub_82402DE8);       // ObjectDir::FindObject
REX_EXTERN(sub_821F13D0);       // RndTransformable::WorldXfm
REX_EXTERN(__imp__sub_824BC668); // UILabel::DrawShowing (label +260)
REX_EXTERN(sub_824BA9F8);       // UILabel::LabelStyle
REX_EXTERN(sub_824BAF98);       // UILabel::Update (text rectangle and style layout)
REX_EXTERN(sub_824BC7D0);       // UILabel::SetText
REX_EXTERN(__imp__sub_824BC7D0);
REX_EXTERN(__imp__sub_824DC2F0); // UISlider drawable traversal
REX_EXTERN(__imp__sub_8229FD60); // RndText: cumulative UTF-16 advances during normal layout

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
uint64_t window_focus_revision = 0;
std::optional<MenuPoint> menu_pointer;
bool menu_available = false;
bool game_screen_active = false;
bool gameplay_resolution_active = false;
bool portrait_splash_active = false;
uint32_t current_screen = 0, current_focus = 0;
uint32_t current_panel = 0;
thread_local uint32_t current_panel_dir = 0, current_input_scope = 0;
thread_local MenuDialogScope dialog_scope = MenuDialogScope::kNone;
thread_local uint32_t setting_prompt_label = 0;
thread_local std::unordered_map<uint32_t, std::vector<DialogPromptSpan>> dialog_prompts;
uint64_t screen_generation = 0;
// The current screen's guest object name, used by native mouse navigation.
std::string current_screen_name;
bool PortraitStoreActive() {
  return ui::screen::CurrentMenuConfiguration().rhythmverse_row && ui::PortraitStoreScreen(current_screen_name);
}

// The focused list the previous frame's draw identified (see DrawList). Read by the help caption
// observer; 0 when no list has been drawn as the focused component yet.
uint32_t focused_list_drawn = 0;
uint32_t music_list_screen = 0, music_list_frame = 0, observed_menu_frame = 0;

struct Geometry {
  uint32_t list = 0, frame = 0;
  bool horizontal = false;
  MenuBounds bounds;
  std::vector<MenuRow> rows;
  std::optional<MenuBounds> left_arrow, right_arrow;
  // The focused row's logical data index, preserved while the viewport scrolls.
  int selected = -1;
};
thread_local Geometry geometry;
// Shared gesture state is protected by mutex; guest draw state stays thread local.
struct TouchTarget {
  uint32_t list = 0, frame = 0;
  MenuBounds bounds;
  float pitch = 0;
  int first = 0, maximum = 0;
};
TouchTarget touch_target;
TouchListScroll touch_scroll;
bool touch_captured = false;
thread_local uint32_t touch_draw_list = 0;
thread_local float touch_draw_position = 0;
double TouchTime() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void ResetTouchScroll() {
  touch_target = {};
  touch_scroll.Reset();
  touch_captured = false;
}
struct PowerupInfoTarget {
  uint32_t screen = 0, focus = 0, frame = 0;
  MenuBounds bounds;
  MenuPoint row;
};
thread_local std::array<PowerupInfoTarget, 3> powerup_info_targets;
thread_local uint32_t powerup_info_screen = 0, powerup_info_frame = 0;
thread_local uint32_t frame_number = 0;
struct HeaderGeometry { uint32_t screen = 0, focus = 0, frame = 0; MenuBounds bounds; };
thread_local std::array<HeaderGeometry, 4> header_geometry;
thread_local std::vector<MenuHintTarget> hint_targets;
struct FooterDraw {
  uint32_t owner;
  std::vector<InlineHelpEntry> entries;
  size_t index, count;
};
thread_local const FooterDraw* drawing_footer = nullptr;
thread_local MenuFooterTouchPress footer_touch_press;
thread_local int clicked_header = -1;
thread_local uint32_t header_click_frame = 0;
thread_local std::string current_panel_name;
thread_local std::string current_panel_scene;
// The last row a draw positively identified as selected. The guest sets the selected
// *slot* to -1 when a scroll left the selection outside the band, so the slot cannot
// be the anchor; this survives that and is reset on every screen or focus change.
thread_local int last_selected_row = -1;
// The mouse's hover ownership and the unhover lift (MouseHighlight in native_menu.h).
// Only a hover the mouse itself set may be lifted when the pointer leaves it; a
// controller's own selection is never touched by mouse motion.
thread_local MouseHighlight mouse_highlight;
thread_local MenuTouchHighlight touch_highlight;
// The scroll-aware hover (MouseScrollFollow in native_menu.h): a scroll the mouse applied moves
// the rows, so the row that comes to rest under the pointer is caught once the burst has drained
// and the drawn frame shows the new view.
thread_local MouseScrollFollow scroll_follow;
struct SliderGeometry {
  uint32_t focus = 0, screen = 0, frame = 0;
  MenuBounds bounds;
};
thread_local std::array<SliderGeometry, 2> slider_geometries;
thread_local int slider_drag = -1;
thread_local uint32_t slider_drag_screen = 0;
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
  PPCContext ctx;
  PPCContext& parent;
  uint32_t scratch;
  explicit GuestCall(PPCContext& caller, uint8_t* base, uint32_t bytes = 256)
      : ctx(caller), parent(caller) {
    rex::ppc::OpenCallFrame(ctx, base, bytes);
    scratch = ctx.r1.u32 + 128;
  }
  ~GuestCall() { parent.fpscr = ctx.fpscr; }
};

uint32_t Focus(PPCContext& ctx, uint8_t* base, uint32_t manager) {
  GuestCall call(ctx, base); call.ctx.r3.u32 = manager;
  sub_824B1980(call.ctx, base);
  return call.ctx.r3.u32;
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
  GuestCall call(ctx, base);
  call.ctx.r3.u32 = list;
  sub_824C0C08(call.ctx, base);
  const int rows = call.ctx.r3.s32, display = int(U32(base, list + 180));
  if (rows <= 0 || display <= 0) { rail_map = {}; return false; }
  const bool changed = !rail_map.known || rail_map.list != list || rail_map.max_first != std::max(0, rows - display);
  rail_map = {true, list, std::max(0, rows - display)};
  if (changed && rb_blitz::settings::title::mouse_native_trace.Get())
    REXLOG_INFO("native_mouse: scroll rail rows={} numDisplay={} maxFirst={}", rows, display, rail_map.max_first);
  return true;
}
void Context(PPCContext& ctx, uint8_t* base, uint32_t manager) {
  const uint32_t screen = U32(base, manager + 44);
  const uint32_t focus = screen ? Focus(ctx, base, manager) : 0;
  const uint32_t panel = screen ? U32(base, screen + 52) : 0;
  const std::string name = ObjectName(base, screen);
  const std::string panel_name = ObjectName(base, ObjectBase(base, panel));
  const std::string focus_name = ObjectName(base, ObjectBase(base, focus));
  if (panel_name != current_panel_name)
    current_panel_scene = ui::screen::PanelScene(panel_name);
  const auto scope = DialogScopeFor(panel_name, focus_name);
  const uint32_t input_scope = scope == MenuDialogScope::kShortcuts ? focus :
                               scope == MenuDialogScope::kPanel ? panel : 0;
  rb_blitz::hooks::PublishActionHints();
  // game_screen owns pause_panel too; its focused panel distinguishes a live song
  // from pause and its nested menus. A transition is never an input destination.
  const bool allowed = screen && U32(base, manager + 16) == 0 &&
      (name != "game_screen" || panel_name == "pause_panel" || scope == MenuDialogScope::kPanel);
  const bool in_song = screen != 0 && name == "game_screen" && panel_name != "pause_panel" &&
      scope != MenuDialogScope::kPanel;
  rb_blitz::loading::ObserveRecoveryScreen(name, allowed);
  rb_blitz::content_progress::ObserveScreen(name, panel_name, allowed);
  rb_blitz::diag::shader_trace::ObserveScreen(name, panel_name, focus_name,
                                             U32(base, manager + 16) != 0);
  const bool changed_context = screen != current_screen || focus != current_focus || panel != current_panel ||
      input_scope != current_input_scope || allowed != menu_available;
  if (changed_context) {
    if (input_scope != current_input_scope)
      REXLOG_INFO("native_mouse: dialog context panel={} focus={} scope={:08X}", panel_name,
                  ObjectName(base, ObjectBase(base, focus)), input_scope);
    // A panel-entry refresh may replace the splash provider's row array before
    // its first draw. Never hit-test or dispatch against indices captured from
    // the preceding screen or list generation.
    geometry = {};
    powerup_info_targets = {};
    powerup_info_screen = powerup_info_frame = 0;
    header_geometry = {};
    hint_targets.clear();
    footer_touch_press.Reset();
    clicked_header = -1;
    MenuRail stale; PublishRail(stale);
    last_selected_row = -1;
    rail_map = {};
    rail_drag.Reset();
    mouse_highlight.Reset();
    touch_highlight.Reset();
    scroll_follow.Reset();
    touch_draw_list = 0;
    // P2 (plan D5): a screen or focus change is a boundary the gameplay tables must not carry
    // held state across. The gameplay holds are dropped without an edge on the next poll, so a
    // press or release from the old context can never arrive late in the new one.
    InvalidateGameplayInput();
    if (rb_blitz::settings::title::mouse_native_trace.Get())
      REXLOG_INFO("native_mouse: screen={} panel={} scene={} focus={:08X} available={}",
                  name, panel_name, current_panel_scene, focus, allowed);
  }
  std::lock_guard lock(mutex);
  if (changed_context) ResetTouchScroll();
  if (screen != current_screen) ++screen_generation;
  current_screen = screen; current_focus = focus; menu_available = allowed;
  current_panel = panel;
  current_panel_dir = panel ? U32(base, panel + 52) : 0;
  current_input_scope = input_scope;
  dialog_scope = scope;
  observed_menu_frame = frame_number;
  if (current_screen_name != name) current_screen_name = name;
  game_screen_active = in_song;
  gameplay_resolution_active = GameplayResolutionScreen(name, panel_name, focus_name);
  portrait_splash_active = ui::PortraitSplashScreen(name, panel_name, current_panel_scene) &&
      (!ui::PortraitStoreScreen(name) || ui::screen::CurrentMenuConfiguration().rhythmverse_row);
  current_panel_name = panel_name;
  queue.Context(screen, focus, allowed && window_focused && MouseSupportEnabled() &&
                !SystemUIActive() && !ui::achievements::Active(), panel);
}
// UIListState::Selected - the list's live selection - read with isolated registers like
// every other side call (the same read the hit test's ordering uses).
int SelectedRow(PPCContext& ctx, uint8_t* base, uint32_t list) {
  GuestCall call(ctx, base);
  call.ctx.r3.u32 = list + 176;
  sub_824E3008(call.ctx, base);
  return call.ctx.r3.s32;
}
// Returns whether the point named a row of the focused list. When it did not, the mouse's
// own hover ends: a click that does not land on an element cannot press anything, and the
// highlight leaves the frame while the list still shows the row the mouse set (owner rule,
// 2026-10-16 - MouseHighlight in native_menu.h). The guest's selected row itself is never
// written, so the album panel and the controller keep their anchor.
bool Select(PPCContext& ctx, uint8_t* base, MenuPoint point, bool press = false) {
  // The pointer from the last draw is valid only for the same focused component
  // in the immediately preceding frame. Never dereference a retired screen's list.
  if (!geometry.list || geometry.frame + 1 != frame_number ||
      current_focus != geometry.list) return false;
  const int row = HitMenuRow(point, geometry.bounds, geometry.rows);
  if (last_device::Current().using_touch) {
    mouse_highlight.Reset();
    if (press && row >= 0) touch_highlight.Press(geometry.list, row);
    else touch_highlight.Move(geometry.list, row);
  }
  if (rb_blitz::settings::title::mouse_native_trace.Get()) REXLOG_INFO("native_mouse: hit={},{} row={} bounds={},{},{},{}", point.x, point.y, row, geometry.bounds.left, geometry.bounds.top, geometry.bounds.right, geometry.bounds.bottom);
  if (row < 0) {
    const auto left = mouse_highlight.PointerLeft(geometry.list, SelectedRow(ctx, base, geometry.list));
    if (left != MouseHighlight::Leave::kIgnored && rb_blitz::settings::title::mouse_native_trace.Get())
      REXLOG_INFO("native_mouse: unhover list={:08X} highlight={}", geometry.list,
                  left == MouseHighlight::Leave::kLifted ? "off" : "kept");
    return false;
  }
  if (!last_device::Current().using_touch) mouse_highlight.Hover(geometry.list, row);
  const int selected = SelectedRow(ctx, base, geometry.list);
  if (selected == row) return true;
  const int direction = row > selected ? 1 : -1;
  GuestCall call(ctx, base);
  auto& c = call.ctx;
  c.r3.u32 = geometry.list; c.r4.s32 = row; c.r5.u32 = U32(base, geometry.list + 224);
  sub_824C1938(c, base);
  // Refresh providers and send the engine's scroll-complete notification, without
  // issuing intermediate directional presses or animating through intervening rows.
  c.r3.u32 = geometry.list + 84; c.r4.u32 = geometry.list + 176;
  c.r5.s32 = direction; c.r6.u32 = 0;
  sub_824C1DE8(c, base);
  c.r3.u32 = geometry.list + 84; c.r4.u32 = geometry.list + 176;
  sub_824C1F08(c, base);
  if (rb_blitz::settings::title::mouse_native_trace.Get()) REXLOG_INFO("native_mouse: selected row={} list={:08X}", row, geometry.list);
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
  if (left != MouseHighlight::Leave::kIgnored && rb_blitz::settings::title::mouse_native_trace.Get())
    REXLOG_INFO("native_mouse: unhover list={:08X} highlight={} (scroll)", list,
                left == MouseHighlight::Leave::kLifted ? "off" : "kept");
  scroll_follow.Arm(pointer);
  const int direction = first > int(current) ? 1 : -1;
  GuestCall call(ctx, base);
  auto& c = call.ctx;
  c.r3.u32 = list; c.r4.s32 = row; c.r5.u32 = uint32_t(first);
  sub_824C1938(c, base);
  c.r3.u32 = list + 84; c.r4.u32 = list + 176; c.r5.s32 = direction; c.r6.u32 = 0;
  sub_824C1DE8(c, base);
  c.r3.u32 = list + 84; c.r4.u32 = list + 176;
  sub_824C1F08(c, base);
  if (rb_blitz::settings::title::mouse_native_trace.Get())
    REXLOG_INFO("native_mouse: scrollbar first={} target={} row={} dir={}", current, first, row, direction);
  return true;
}
std::optional<MouseAction> PresetArrowAt(PPCContext& ctx, uint8_t* base, MenuPoint point);
}

bool MouseSupportEnabled() { return true; }
MenuPoint MenuWindowPointer(float x, float y, int width, int height) {
  const bool source_aspect = settings::ReadSdkSetting<bool>("gpu.xenos.present_source_aspect");
  const int frame_height = settings::BootRenderHeight();
  const int frame_width = settings::BootRenderWidth();
  return MenuPointer(x, y, width, height,
      source_aspect ? (frame_width ? frame_width : frame_height * 16 / 9) : settings::ReadSdkSetting<int32_t>("ui.video_mode_width"),
      source_aspect ? frame_height : settings::ReadSdkSetting<int32_t>("ui.video_mode_height"),
      settings::ReadSdkSetting<bool>("gpu.xenos.present_letterbox"),
      settings::ReadSdkSetting<bool>("gpu.xenos.present_allow_overscan_cutoff"),
      settings::ReadSdkSetting<int32_t>("gpu.xenos.present_safe_area_x"),
      settings::ReadSdkSetting<int32_t>("gpu.xenos.present_safe_area_y"));
}
bool AnswerMenuHeaderQuery(std::string_view path, bool* answer) {
  constexpr std::string_view prefix = "rbbz-mouse/header/";
  if (!path.starts_with(prefix)) return false;
  *answer = false;
  const auto suffix = path.substr(prefix.size());
  const auto slash = suffix.rfind('/');
  if (slash == std::string_view::npos || slash + 2 != suffix.size() ||
      suffix.back() < '0' || suffix.back() > '3') return true;
  const int target = suffix.back() - '0';
  const auto visibility = rb_blitz::menu_filter::SnapshotRuntimeVisibility();
  if ((target == 1 && !visibility.rhythmverse) ||
      (target == 2 && !visibility.messages) ||
      (target == 3 && !visibility.recommendations)) return true;
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
  if (focused != window_focused) ++window_focus_revision;
  if (focused != window_focused && rb_blitz::settings::title::mouse_native_trace.Get())
    REXLOG_INFO("native_mouse: window focus={} queue={}", focused, focused && menu_available && MouseSupportEnabled());
  window_focused = focused;
  if (!focused) { menu_pointer.reset(); ResetTouchScroll(); }
  queue.Context(current_screen, current_focus, focused && menu_available && MouseSupportEnabled() &&
                !SystemUIActive() && !ui::achievements::Active(), current_panel);
  // P2 (plan D5): losing the window drops every gameplay hold. The physical layer already
  // reports nothing while unfocused; this makes the *edge* disappear too, so a key released
  // behind another window cannot be delivered as one the guest acted on.
  if (!focused) InvalidateGameplayInput();
}
void QueueMenuMouse(MouseAction action, MenuPoint point) {
  std::lock_guard lock(mutex);
  touch_scroll.Reset(); touch_captured = false;
  if (action == MouseAction::kMove || action == MouseAction::kAccept) menu_pointer = point;
  if (MouseSupportEnabled()) queue.Push(action, point);
}
void QueueMenuWheel(int delta, MenuPoint point) {
  std::lock_guard lock(mutex);
  touch_scroll.Reset(); touch_captured = false;
  menu_pointer = point;
  if (MouseSupportEnabled()) queue.Wheel(delta, point);
}
void QueueMenuScrollbar(MouseAction action, MenuPoint point) {
  std::lock_guard lock(mutex);
  touch_scroll.Reset(); touch_captured = false;
  if (MouseSupportEnabled()) queue.Scrollbar(action, point);
}
void CancelMenuTouch() {
  std::lock_guard lock(mutex);
  touch_scroll.Reset(); touch_captured = false;
}
bool QueueMenuTouch(MenuTouchPhase phase, MenuPoint point, float x, float y, double age_seconds) {
  std::lock_guard lock(mutex);
  const double now = TouchTime() - std::max(0.0, age_seconds);
  if (phase == MenuTouchPhase::kCancel || !queue.enabled()) {
    const bool consumed = touch_captured;
    touch_scroll.Reset(); touch_captured = false;
    return consumed;
  }
  if (phase == MenuTouchPhase::kDown) {
    if (!touch_target.list || touch_target.list != current_focus ||
        touch_target.frame + 1 < observed_menu_frame ||
        !(touch_target.pitch > 0) || !touch_target.bounds.Contains(point)) {
      touch_scroll.Reset(); touch_captured = false;
      return false;
    }
    touch_scroll.Begin({x, y, point.y / touch_target.pitch}, touch_target.first,
                       touch_target.maximum, now);
    touch_captured = true;
    menu_pointer = point;
    return true;
  }
  if (!touch_captured) return phase == MenuTouchPhase::kMove && touch_scroll.OwnsView();
  const TouchScrollPoint at{x, y, point.y / touch_target.pitch};
  menu_pointer = point;
  if (phase == MenuTouchPhase::kMove) touch_scroll.Move(at, now);
  else if (phase == MenuTouchPhase::kUp) {
    if (touch_scroll.End(at, now) && touch_target.bounds.Contains(point))
      queue.Push(MouseAction::kAccept, point);
    touch_captured = false;
  }
  return true;
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
uint64_t MenuScreenGeneration() {
  std::lock_guard lock(mutex);
  return screen_generation;
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
bool GameplayResolutionActive() {
  std::lock_guard lock(mutex);
  return gameplay_resolution_active;
}
bool PortraitSplashActive() {
  std::lock_guard lock(mutex);
  return portrait_splash_active;
}
bool PortraitSettingsActive() {
  std::lock_guard lock(mutex);
  return rb_blitz::settings::InterfacePortraitEnabled() &&
         (ui::PortraitSettingsScene(current_panel_scene) || ui::PortraitControlsPanel(current_panel_name) ||
          current_panel_name == "av_options_panel" || current_panel_name == "credits_panel");
}
bool PortraitConnectionActive() {
  std::lock_guard lock(mutex);
  return rb_blitz::settings::InterfacePortraitEnabled() && current_panel_name == "server_connect_panel";
}
bool PortraitCalibrationActive() {
  std::lock_guard lock(mutex);
  return rb_blitz::settings::InterfacePortraitEnabled() &&
         (current_panel_name == "cal_audio_panel" || current_panel_name == "cal_visuals_panel" ||
          current_panel_name == "cal_hw_audio_panel" || current_panel_name == "cal_hw_visuals_panel");
}
bool CalibrationMenuActive() {
  std::lock_guard lock(mutex);
  return current_screen_name.rfind("cal_", 0) == 0;
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
  const int touch_row = touch_highlight.list == current_focus && current_focus
      ? SelectedRow(ctx, base, current_focus) : -1;
  bool idle = false, enabled = false;
  bool touching_view = false;
  std::optional<float> touch_position;
  {
    std::lock_guard lock(mutex);
    if (!queue.enabled() || !last_device::Current().using_touch || !fresh ||
        touch_target.list != current_focus) {
      touch_scroll.Reset(); touch_captured = false;
    }
    touch_scroll.Advance(TouchTime());
    if (touch_scroll.OwnsView()) {
      touch_position = touch_scroll.Position();
      touching_view = touch_scroll.Pressed() || touch_scroll.Velocity() != 0;
      if (touching_view) touch_highlight.Reset();
    }
    // Focus may be lost and regained while the guest is suspended. Do not
    // turn the release of that abandoned touch into a click on resume.
    static thread_local uint64_t touch_focus_revision = 0;
    if (touch_focus_revision != window_focus_revision) {
      touch_highlight.Reset();
      touch_focus_revision = window_focus_revision;
    }
    if (touch_highlight.list) {
      if (!fresh || !menu_pointer) touch_highlight.Reset();
      else touch_highlight.Move(geometry.list, HitMenuRow(*menu_pointer, geometry.bounds, geometry.rows));
    }
    if (queue.enabled() && touch_highlight.TakeAccept(last_device::Current().using_touch,
        current_focus, touch_row, (HostMouseButtons() & 1) != 0)) queue.Pulse(MouseAction::kAccept);
    if (const auto button = footer_touch_press.Poll(queue.enabled(), (HostMouseButtons() & 1) != 0))
      queue.PulseButton(button);
    event = queue.NextFrame(can_scroll);
    idle = !queue.Pending();
    enabled = queue.enabled();
  }
  touch_draw_list = touch_position ? current_focus : 0;
  if (touch_position) {
    touch_draw_position = *touch_position;
    const MenuPoint miss{NAN, NAN};
    ScrollListTo(ctx, base, current_focus, int(std::floor(*touch_position)), miss);
    // Touch never reselects the row that happens to slide under a resting finger.
    scroll_follow.Reset();
  }
  if (!enabled) { rail_drag.Reset(); touch_highlight.Reset(); }
  if (!enabled || !(HostMouseButtons() & 1) || slider_drag_screen != current_screen)
    slider_drag = -1;
  // The scroll-aware hover's landing (MouseScrollFollow in native_menu.h): the scroll has been
  // applied and drawn (a fresh frame carries the new view), the burst has drained and the
  // viewport is at rest, so the row under the pointer is caught - or the hover stays empty (the
  // lift the scroll started keeps it that way when no row landed there). A disabled queue means
  // discarded input, so the catch is dropped instead of landing late.
  if (!event && idle && fresh &&
      U32(base, current_focus + 224) == U32(base, current_focus + 228)) {
    if (auto point = scroll_follow.Take()) {
      if (enabled) {
        if (rb_blitz::settings::title::mouse_native_trace.Get())
          REXLOG_INFO("native_mouse: scroll follow point={},{}", point->x, point->y);
        Select(ctx, base, *point);
      }
    }
  }
  if (!event) return;
  const bool powerup_info = powerup_info_screen == current_screen &&
      powerup_info_frame + 1 == frame_number;
  // Footer labels act on the existing selection. Handle them before row hit
  // testing, which would otherwise reject a footer click as empty space.
  // Touch-generated left presses use this same queue as physical mouse clicks.
  if (event->action == MouseAction::kAccept) {
    if (!powerup_info) for (const auto& target : powerup_info_targets) {
      if (target.screen != current_screen || target.focus != current_focus ||
          target.frame + 1 != frame_number || !target.bounds.Contains(event->point)) continue;
      if (Select(ctx, base, target.row)) {
        touch_highlight.Reset();
        scroll_follow.Reset();
        std::lock_guard lock(mutex);
        if (queue.enabled()) queue.PulseButton(0x8000);
      }
      return;
    }
    if (const auto button = HitMenuHint(event->point, hint_targets,
                                        current_screen, current_focus, frame_number, current_input_scope)) {
      scroll_follow.Reset();
      REXLOG_INFO("native_mouse: legend click screen={} point={:.4f},{:.4f} button={:04X}",
                  current_screen_name, event->point.x, event->point.y, button);
      if (last_device::Current().using_touch && PortraitSettingsActive()) {
        footer_touch_press.Begin(event->point, button);
        return;
      }
      std::lock_guard lock(mutex);
      if (queue.enabled()) queue.PulseButton(button);
      return;
    }
    // One line per press, bounded to the first 24 misses in a screen. This is
    // useful on devices where we cannot attach a debugger to inspect hit areas.
    static thread_local uint32_t traced_screen = 0;
    static thread_local unsigned misses = 0;
    if (traced_screen != current_screen) { traced_screen = current_screen; misses = 0; }
    if (event->point.y > .8f && misses++ < 24)
      REXLOG_INFO("native_mouse: legend miss screen={} point={:.4f},{:.4f} targets={} drawn_frame={} poll_frame={}",
                  current_screen_name, event->point.x, event->point.y, hint_targets.size(),
                  hint_targets.empty() ? 0 : hint_targets.back().frame, frame_number);
  }
  if (powerup_info) return;
  // Sliders are UIComponents, with no UIListState to select or scroll. Route
  // through their own directional handlers so their animation and value agree.
  const auto focus_name = ObjectName(base, ObjectBase(base, current_focus));
  const bool scale_page = current_panel_name == "settings_scale_panel" ||
                          current_panel_name == "settings_scale_pause_panel";
  if (scale_page && (event->action == MouseAction::kAccept ||
      (event->action == MouseAction::kMove && (HostMouseButtons() & 1)))) {
    if (event->action == MouseAction::kAccept) slider_drag = -1;
    for (size_t index = 0; index < slider_geometries.size(); ++index) {
      const auto& slider = slider_geometries[index];
      if (!slider.focus || slider.screen != current_screen || slider.frame + 1 != frame_number) continue;
      const auto bounds = SliderTouchBounds(slider.bounds, ui::CurrentPortraitViewport().height);
      if (event->action == MouseAction::kMove && slider_drag != int(index)) continue;
      const int step = event->action == MouseAction::kAccept
          ? SliderStepAt(event->point, bounds) : SliderDragStepAt(event->point, bounds);
      if (step < 0 || U32(base, slider.focus + 132) != 6) continue;
      slider_drag = int(index);
      slider_drag_screen = current_screen;
      // UISlider's native current/step fields (824DC048). Its normal draw updates
      // the pod; the panel commits changed values once per poll, including drags.
      rex::memory::store_and_swap<uint32_t>(base + slider.focus + 128, uint32_t(step));
      if (slider.focus != current_focus) {
        std::lock_guard lock(mutex);
        if (queue.enabled()) queue.Pulse(index == 0 ? MouseAction::kUp : MouseAction::kDown);
      }
      return;
    }
    return;
  }
  if (IsSettingsWheel(focus_name)) {
    if (!scale_page && event->action == MouseAction::kUp) event->action = MouseAction::kRight;
    else if (!scale_page && event->action == MouseAction::kDown) event->action = MouseAction::kLeft;
    else if (event->action == MouseAction::kAccept) {
      // The guest's audio scene places the endpoints at these normalized
      // coordinates; MenuPointer accounts for window size, crop and overscan.
      const auto& slider = slider_geometries[focus_name == "sound_effects_slider.sld" ? 0 : 1];
      const auto bounds = slider.focus == current_focus && slider.screen == current_screen &&
          slider.frame + 1 == frame_number ? slider.bounds : MenuBounds{.284f, .86f, .716f, .97f};
      const int step = SliderStepAt(event->point, bounds);
      if (step < 0) return;
      std::lock_guard lock(mutex);
      // Clamp at the lower stop before advancing. Each pulse is acknowledged
      // by the existing pad queue, including a release between equal directions.
      for (int i = 0; i < 5; ++i) queue.Push(MouseAction::kLeft);
      for (int i = 0; i < step; ++i) queue.Push(MouseAction::kRight);
      return;
    }
    if (event->action == MouseAction::kMove) return;
    std::lock_guard lock(mutex);
    if (queue.enabled()) queue.Pulse(event->action);
    return;
  }
  // Header clicks precede the focused list's hit test and never become A.
  // Only a label actually drawn in the last frame can receive the click.
  if (!current_input_scope && event->action == MouseAction::kAccept &&
      LibraryHeaderForScreen(current_screen_name) >= 0) {
    for (int i = 0; i < int(header_geometry.size()); ++i) {
      const auto& header = header_geometry[i];
      if (header.screen != current_screen || header.focus != current_focus ||
          header.frame + 1 != frame_number || !header.bounds.Contains(event->point)) continue;
      Select(ctx, base, event->point); // retire the list's mouse hover
      scroll_follow.Reset();
      if (i != LibraryHeaderForScreen(current_screen_name)) {
        clicked_header = i;
        header_click_frame = frame_number;
        if (rb_blitz::settings::title::mouse_native_trace.Get())
          REXLOG_INFO("native_mouse: header click screen={} panel={} target={}",
                      current_screen_name, current_panel_name, i);
      }
      return;
    }
  }
  if (!current_input_scope && event->action == MouseAction::kAccept) {
    if (fresh && current_screen_name == "powerup_select_screen") {
      if (const auto arrow = HitMenuArrow(event->point, geometry.left_arrow, geometry.right_arrow)) {
        // The guest only draws arrows when more icons are available. Page the
        // viewport independently of the hovered/equipped item; never send A.
        if (can_scroll && EnsureRailMap(ctx, base, current_focus)) {
          const int first = int(U32(base, current_focus + 228));
          const int display = int(U32(base, current_focus + 180));
          ScrollListTo(ctx, base, current_focus,
                       MenuArrowPage(first, display, rail_map.max_first, *arrow), event->point);
        }
        return;
      }
    }
    if (const auto arrow = PresetArrowAt(ctx, base, event->point)) {
      // The art belongs to the guest's preset page. A controller D-pad pulse
      // reaches its own left/right handler, including its normal wrap behavior.
      Select(ctx, base, event->point); // retire any mouse-owned list hover
      scroll_follow.Reset();
      event->action = *arrow;
    }
  }
  // A dialog consumes empty-space clicks. Its own captured list still supports
  // direct row selection; a dialog with no list never falls through to Confirm.
  if (current_input_scope && !fresh && event->action != MouseAction::kBack) return;
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
      if (rb_blitz::settings::title::mouse_native_trace.Get())
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
    const bool on_row = Select(ctx, base, event->point, event->action == MouseAction::kAccept);
    // A click only presses what the mouse is on: on a screen whose list tracks the mouse,
    // an unhovered click (empty space, or a scroll-gutter miss) must not press A (owner
    // rule, 2026-10-16). Screens without a captured list - the title's own prompt, for
    // one - keep their at-principle A, which is how they are started.
    if (event->action == MouseAction::kAccept && !on_row && geometry.list == current_focus &&
        geometry.frame + 1 == frame_number)
      return;
    // Touch activates on release after a visible pressed frame. A move off the
    // original row cancels the press instead of activating a different option.
    if (event->action == MouseAction::kAccept && on_row && last_device::Current().using_touch)
      return;
  }
  // A selection notification can itself replace a screen, so recheck before A.
  Context(ctx, base, manager);
  std::lock_guard lock(mutex);
  if (queue.enabled() && screen == current_screen && focus == current_focus) queue.Pulse(event->action);
}

struct DrawCapture {
  uint32_t resource = 0, widget = 0;
  std::unordered_set<uint32_t> painted_rows;
  Geometry result;
  uint32_t first = 0, end = 0, row = 0;
  int selected = -1;
  std::optional<MenuBounds> highlight;
  // A mouse leave or idle touch draws active rows without the shared highlight.
  bool suppress = false;
  bool touch = false;
};
thread_local DrawCapture* drawing = nullptr;

bool PortraitPowerupFooterRow(uint8_t* base) {
  return drawing && drawing->row && settings::InterfacePortraitEnabled() &&
      current_screen_name == "powerup_select_screen" &&
      ObjectName(base, ObjectBase(base, drawing->result.list)) == "powerup_select.lst" &&
      U32(base, drawing->row + 36) == 3;
}


uint32_t SceneObject(PPCContext& ctx, uint8_t* base, uint32_t dir, const char* name) {
  GuestCall call(ctx, base);
  std::memcpy(base + call.scratch, name, std::strlen(name) + 1);
  auto& c = call.ctx;
  c.r3.u32 = dir; c.r4.u32 = call.scratch; c.r5.u32 = 0; c.r6.u32 = 0;
  sub_82402DE8(c, base);
  return c.r3.u32;
}

bool PortraitMenuHintsAvailable(PPCContext& ctx, uint8_t* base, uint32_t owner) {
  if (!settings::InterfacePortraitEnabled()) return false;
  const auto header = ObjectBase(base, owner);
  const auto name = ObjectName(base, header);
  if (current_screen_name == "song_select_screen" && name == "pop_up_help.ihp") return false;
  if (name == "continue.ihp" || name == "back.ihp" || name == "skip.ihp") return false;
  const auto dir = header ? U32(base, header + 36) : 0;
  return dir && SceneObject(ctx, base, dir, "portrait_caption_black.color");
}

std::vector<InlineHelpEntry> PortraitMenuHintEntries(PPCContext& ctx, uint8_t* base, uint32_t owner) {
  const auto entries = ReadInlineHelpEntries(owner,
      [base](uint32_t address) { return U32(base, address); });
  if (!settings::InterfacePortraitEnabled()) return entries;
  const auto header = ObjectBase(base, owner);
  const auto dir = header ? U32(base, header + 36) : 0;
  if (dir && SceneObject(ctx, base, dir, "portrait_powerups.color") &&
      ObjectName(base, header) == "help.ihp") {
    auto ordered = entries;
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
      return a.action == 2 && b.action != 2;
    });
    return ordered;
  }
  if (dir && SceneObject(ctx, base, dir, "portrait_songs.color") &&
      ObjectName(base, header) == "InlineHelp.ihp" && current_screen_name == "song_select_screen") {
    std::vector<InlineHelpEntry> visible;
    for (const auto action : {2u, 3u, 1u})
      for (const auto& entry : entries) if (entry.action == action) visible.push_back(entry);
    return visible;
  }
  if (!dir || !SceneObject(ctx, base, dir, "portrait_calibration_footer.color")) return entries;
  return CalibrationFooterEntries(entries, [base](const auto& entry) {
    return InlineHelpCaptionToken(entry, [base](uint32_t address) { return U32(base, address); },
        [base](uint32_t address) {
          const char* token = address ? reinterpret_cast<const char*>(base + address) : "";
          return std::string_view(token, strnlen(token, 64));
        });
  });
}

InlineHelpPlacement PortraitMenuHintPlacement(PPCContext& ctx, uint8_t* base, uint32_t owner,
                                             const InlineHelpEntry& entry, size_t index, size_t count) {
  const auto header = ObjectBase(base, owner);
  const auto dir = header ? U32(base, header + 36) : 0;
  const bool calibration = dir && SceneObject(ctx, base, dir, "portrait_calibration_footer.color");
  const auto caption = InlineHelpCaptionToken(entry,
      [base](uint32_t address) { return U32(base, address); },
      [base](uint32_t address) {
        const char* text = address ? reinterpret_cast<const char*>(base + address) : "";
        return std::string_view(text, strnlen(text, 64));
      });
  return CalibrationFooterPlacement(calibration, caption, index, count);
}

void DrawMenuHints(PPCContext& ctx, uint8_t* base, uint32_t owner, size_t index, size_t count,
                    void (*draw)(PPCContext&, uint8_t*)) {
  const FooterDraw footer{owner, ReadInlineHelpEntries(owner,
      [base](uint32_t address) { return U32(base, address); }), index, count};
  const auto previous = drawing_footer;
  drawing_footer = &footer;
  rex::ScopeGuard restore([&] { drawing_footer = previous; });
  draw(ctx, base);
}

bool DrawPortraitFooterSurface(PPCContext& ctx, uint8_t* base, uint32_t dir,
                               const std::string& stem, size_t index, size_t count, bool reflow,
                               bool visible = true) {
  const auto portrait_draw_fit = hooks::PortraitDrawFit();
  bool hovered = false;
  for (bool border : {true, false}) {
    const auto name = std::string(stem) + (border ? "_border.mesh" : ".mesh");
    const auto mesh = SceneMesh(ctx, base, dir, name.c_str());
    if (!mesh) continue;
    const bool song_control = !reflow && stem.rfind("portrait_", 0) == 0 &&
        (stem == "portrait_sort" || stem == "portrait_search" || stem == "portrait_header_previous" || stem == "portrait_header_next");
    if (reflow || song_control) {
      const auto aspect = ui::screen::CurrentMenuConfiguration().portrait_aspect;
      ui::PortraitCalibrationAction slot{};
      if (song_control) {
        const auto layout = ui::PortraitSongs(aspect);
        const bool arrow = stem.rfind("portrait_header_", 0) == 0;
        const bool left = stem == "portrait_sort" || stem == "portrait_header_previous";
        slot = {(left ? -1.f : 1.f) * (arrow ? layout.width / 2 - ui::kPortraitSongArrowInset : layout.width / 4 + 2),
                arrow ? layout.tabs_z : layout.tools_z,
                arrow ? ui::kPortraitSongArrowTargetSize : layout.width / 2 - 3,
                arrow ? ui::kPortraitSongArrowTargetSize : 27.f};
      } else slot = SceneObject(ctx, base, dir, "portrait_songs.color")
          ? ui::PortraitSongsFooter(aspect, index, count) : ui::PortraitFooterAction(aspect, index, count);
      const auto world = rex::ppc::Call<uint32_t>(ctx, base, sub_821F13D0, mesh + 64);
      const float width = (slot.width + (border ? 1.4f : 0)) * portrait_draw_fit.scale;
      const float height = (slot.height + (border ? 1.4f : 0)) * portrait_draw_fit.scale;
      Float(base, world, width / 300);
      Float(base, world + 24, height / 30);
      Float(base, world + 48, portrait_draw_fit.X(slot.x) - width / 2);
      Float(base, world + 52, border ? .2f : .1f);
      Float(base, world + 56, portrait_draw_fit.Z(slot.z) + height / 2);
    }
    if (const auto bounds = ProjectMesh(ctx, base, mesh)) {
      std::lock_guard lock(mutex);
      hovered = MenuFooterHovered(menu_pointer, *bounds, queue.enabled(),
          HostPointerIsTouch(), (HostMouseButtons() & 1) != 0);
      if (queue.enabled() && footer_touch_press.Draw(*bounds)) hovered = true;
    }
    // Header meshes only define a generous hit region; the native arrows draw themselves.
    if (!visible) continue;
    const auto material = U32(base, mesh + 292);
    std::array<uint32_t, 4> prior{};
    const auto dirty = [&] {
      rex::memory::store_and_swap<uint32_t>(base + material + 520, U32(base, material + 520) | 1);
    };
    if (hovered && !border && material) {
      for (size_t i = 0; i < prior.size(); ++i) prior[i] = U32(base, material + 44 + uint32_t(i) * 4);
      const auto color = SceneObject(ctx, base, dir, "portrait_action.color")
          ? ui::LoadingButtonColor(false, true) : ui::FooterButtonColor(false, true);
      for (size_t i = 0; i < color.size(); ++i) Float(base, material + 44 + uint32_t(i) * 4, color[i]);
      dirty();
    }
    rex::ScopeGuard restore([&] {
      if (hovered && !border && material) {
        for (size_t i = 0; i < prior.size(); ++i)
          rex::memory::store_and_swap<uint32_t>(base + material + 44 + uint32_t(i) * 4, prior[i]);
        dirty();
      }
    });
    if (auto* draw = rex::ppc::ResolveFunction(U32(base, U32(base, mesh) + 24)))
      rex::ppc::Call<void>(ctx, base, *draw, mesh);
  }
  return hovered;
}

void DrawPresetArrows(PPCContext& ctx, uint8_t* base, uint32_t label) {
  const bool store = PortraitStoreActive() && current_screen_name != "store_info_screen";
  const bool songs = current_screen_name == "song_select_screen" || store;
  if (!settings::InterfacePortraitEnabled() ||
      (current_screen_name != "controller_config_screen" &&
       current_screen_name != "controller_config_pause_screen" && !songs)) return;
  const auto header = ObjectBase(base, label);
  if (ObjectName(base, header) != (songs ? (store ? "portrait_tab_store.lbl" : "portrait_tab_songs.lbl") : "IndexNum.lbl")) return;
  const auto dir = U32(base, header + 36);
  if (!songs && !SceneObject(ctx, base, dir, "portrait_preset_arrows.grp")) return;
  for (bool left : {true, false}) {
    const std::string stem = songs ? (left ? "portrait_header_previous" : "portrait_header_next") :
                                    (left ? "portrait_previous" : "portrait_next");
    const bool hovered = DrawPortraitFooterSurface(ctx, base, dir, stem, 0, 0, false, !songs);
    if (songs && !current_input_scope) {
      const auto mesh = SceneMesh(ctx, base, dir, (stem + ".mesh").c_str());
      if (const auto bounds = mesh ? ProjectMesh(ctx, base, mesh) : std::nullopt) {
        std::lock_guard lock(mutex);
        hint_targets.push_back({current_screen, current_focus, frame_number, *bounds,
                                uint16_t(left ? 0x0100 : 0x0200), 0});
      }
    }
    if (songs) continue; // The background panel draws and animates the retail header arrows.
    for (bool glow : {false, true}) {
      const auto name = std::string(left ? "arrow_lt" : "arrow_rt") + (glow ? "_glow.mesh" : ".mesh");
      const auto mesh = SceneMesh(ctx, base, dir, name.c_str());
      if (!mesh) continue;
      const auto material = U32(base, mesh + 292);
      std::array<uint32_t, 3> prior{};
      const auto dirty = [&] {
        rex::memory::store_and_swap<uint32_t>(base + material + 520, U32(base, material + 520) | 1);
      };
      if (hovered && material) {
        for (size_t i = 0; i < prior.size(); ++i) {
          prior[i] = U32(base, material + 44 + uint32_t(i) * 4);
          Float(base, material + 44 + uint32_t(i) * 4, 0);
        }
        dirty();
      }
      rex::ScopeGuard restore([&] {
        if (hovered && material) {
          for (size_t i = 0; i < prior.size(); ++i)
            rex::memory::store_and_swap<uint32_t>(base + material + 44 + uint32_t(i) * 4, prior[i]);
          dirty();
        }
      });
      if (auto* draw = rex::ppc::ResolveFunction(U32(base, U32(base, mesh) + 24)))
        rex::ppc::Call<void>(ctx, base, *draw, mesh);
    }
  }
}

void CaptureSlider(PPCContext& ctx, uint8_t* base, uint32_t slider) {
  const auto name = ObjectName(base, ObjectBase(base, slider));
  if (!IsSettingsWheel(name)) return;
  const auto resource = U32(base, slider + 92);
  if (!resource) return;
  const auto mesh = SceneMesh(ctx, base, resource, "slider.mesh");
  if (!mesh) return;
  const auto bounds = ProjectMesh(ctx, base, mesh);
  if (!bounds || bounds->right <= bounds->left || bounds->bottom <= bounds->top) return;
  slider_geometries[name == "sound_effects_slider.sld" ? 0 : 1] = {slider, current_screen, frame_number, *bounds};
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
  if (rb_blitz::settings::InterfacePortraitEnabled()) {
    const auto previous = dir ? SceneMesh(ctx, base, dir, "portrait_previous.mesh") : 0;
    const auto next = dir ? SceneMesh(ctx, base, dir, "portrait_next.mesh") : 0;
    if (previous) left_bounds = ProjectMesh(ctx, base, previous);
    if (next) right_bounds = ProjectMesh(ctx, base, next);
  }
  auto hit = HitMenuArrow(point, left_bounds, right_bounds);
  if (!hit && rb_blitz::settings::InterfacePortraitEnabled()) {
    const float width = ui::PortraitSplash(ui::screen::CurrentMenuConfiguration().portrait_aspect).width;
    const float half = 19.f / width;
    hit = HitMenuArrow(point,
        MenuBounds{.14f - half, .5f - 129.f / 480, .14f + half, .5f - 91.f / 480},
        MenuBounds{.86f - half, .5f - 129.f / 480, .86f + half, .5f - 91.f / 480});
  }
  // The static meshes do not always yield a usable projected box. The authored
  // panel keeps both arrows at these guest coordinates; MenuPointer already
  // accounts for the window's size and presentation rectangle. These narrow
  // targets keep a click on the visible art from falling through to Confirm.
  if (!hit && !rb_blitz::settings::InterfacePortraitEnabled())
    hit = HitMenuArrow(point, MenuBounds{.365f, .31f, .378f, .35f},
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

void DrawPortraitSongRow(PPCContext& ctx, uint8_t* base) {
  if (!drawing || !drawing->row || ctx.r7.u32 ||
      !settings::InterfacePortraitEnabled() ||
      (current_screen_name != "song_select_screen" && !PortraitStoreActive())) return;
  const auto label = U32(base, ctx.r3.u32 + 8);
  const auto widget = drawing->widget;
  if (PortraitStoreActive()) {
    const auto name = ObjectName(base, ObjectBase(base, label));
    if (name != "filter.lbl" && name != "song.lbl" && name != "album.lbl" && name != "group.lbl" &&
        name != "group_center.lbl" && name != "list.lbl") return;
  }
  if (!label || drawing->painted_rows.contains(drawing->row)) return;
  const auto text = U32(base, label + 80);
  if (!text || !base[text]) return;
  const bool selected = !drawing->suppress && U32(base, drawing->row + 24) == 1;
  const bool heading = U32(base, drawing->row + 24) == 2 || U32(base, drawing->row + 28) == 2;
  const auto dir = drawing->resource;
  const auto mesh = SceneMesh(ctx, base, dir, heading ? "portrait_header.mesh" :
                              selected ? "portrait_selected.mesh" : "portrait_row.mesh");
  if (!mesh) {
    static thread_local std::unordered_set<uint32_t> missing;
    if (settings::title::mouse_native_trace.Get() && missing.insert(dir).second)
      REXLOG_INFO("native_mouse: portrait row mesh unavailable dir={:08X} widget={:08X} label={:08X}",
                  dir, widget, label);
    return;
  }
  // Providers copy their own fit settings onto label instances. Restore wrapping
  // on the live label as well as the authored template before measuring/drawing.
  if (U32(base, label + 96) != 0) {
    rex::memory::store_and_swap<uint32_t>(base + label + 96, 0);
    base[label + 178] = 1;
  }
  if (F32(base, label + 88) != ui::kPortraitSongRowHeight - 5) {
    Float(base, label + 88, ui::kPortraitSongRowHeight - 5);
    base[label + 178] = 1;
  }
  if (base[label + 178]) {
    GuestCall update(ctx, base);
    rex::ppc::Call<void>(update.ctx, base, sub_824BAF98, label, uint32_t(0));
  }
  drawing->painted_rows.insert(drawing->row);
  // UIListLabelElement receives the label's final transform after provider
  // spacing, animation and portrait fitting. Derive the row rectangle from
  // that same transform, retaining the guest's own mesh, material and draw.
  std::array<float, 16> matrix{};
  for (size_t i = 0; i < matrix.size(); ++i) matrix[i] = F32(base, ctx.r4.u32 + uint32_t(i * 4));
  GuestCall call(ctx, base);
  const auto world = rex::ppc::Call<uint32_t>(call.ctx, base, sub_821F13D0, mesh + 64);
  std::array<uint8_t, 64> prior{};
  std::memcpy(prior.data(), base + world, prior.size());
  const float height = heading ? 30.f : ui::kPortraitSongRowHeight;
  // The button template's vertices lie in XY; labels lie in XZ.
  for (size_t axis = 0; axis < 3; ++axis) {
    Float(base, world + uint32_t(axis * 4), matrix[axis] * 420.f / 300);
    Float(base, world + uint32_t(16 + axis * 4), matrix[8 + axis] * height / 30);
    Float(base, world + uint32_t(32 + axis * 4), -matrix[4 + axis]);
  }
  for (size_t axis = 0; axis < 3; ++axis)
    Float(base, world + uint32_t(48 + axis * 4),
          matrix[12 + axis] - 14 * matrix[axis] + height / 2 * matrix[8 + axis]);
  const auto material = U32(base, mesh + 292);
  const float alpha = material ? F32(base, material + 56) : 1.f;
  const auto dirty = [&] {
    if (material) rex::memory::store_and_swap<uint32_t>(base + material + 520, U32(base, material + 520) | 1);
  };
  if (material) { Float(base, material + 56, alpha * float(ctx.f1.f64)); dirty(); }
  rex::ScopeGuard restore([&] {
    std::memcpy(base + world, prior.data(), prior.size());
    if (material) { Float(base, material + 56, alpha); dirty(); }
  });
  if (auto* draw = rex::ppc::ResolveFunction(U32(base, U32(base, mesh) + 24)))
    rex::ppc::Call<void>(call.ctx, base, *draw, mesh);
  if (const auto bounds = ProjectMesh(call.ctx, base, mesh)) CaptureRow(base, *bounds);
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
  GuestCall call(ctx, base); auto& c = call.ctx;
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

std::optional<MenuBounds> ProjectLabel(PPCContext& ctx, uint8_t* base, uint32_t label) {
  const auto camera = U32(base, 0x8284A108);
  if (!camera) return {};
  const float x = F32(base, label + 144), z = F32(base, label + 148);
  const float width = F32(base, label + 152), height = F32(base, label + 156);
  if (!(width > 0) || height == 0) return {};
  GuestCall call(ctx, base); auto& c = call.ctx;
  c.r3.u32 = label; c.r4.u32 = 0;
  sub_824BA9F8(c, base);
  // DrawShowing skips fully transparent label styles; those are not hit targets.
  if (!(F32(base, c.r3.u32 + 36) > 0)) return {};
  // Resolve the same virtual RndTransformable base used by UIListLabelElement::Draw.
  const auto transformable = label + 72 + U32(base, U32(base, label + 72) + 16);
  const auto transform = WorldTransformForDraw(transformable,
      [base](uint32_t address) { return base[address]; },
      [&](uint32_t object) {
        c.r3.u32 = object;
        sub_821F13D0(c, base);
        return c.r3.u32;
      });
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
    if (!std::isfinite(px) || !std::isfinite(py)) return {};
    bounds.left = std::min(bounds.left, px); bounds.right = std::max(bounds.right, px);
    bounds.top = std::min(bounds.top, py); bounds.bottom = std::max(bounds.bottom, py);
  }
  return bounds;
}
void DrawMenuHintLabel(PPCContext& ctx, uint8_t* base, uint32_t label) {
  const auto* footer = drawing_footer;
  const auto entry = footer ? std::find_if(footer->entries.begin(), footer->entries.end(),
      [label](const auto& e) { return e.label == label; }) : std::vector<InlineHelpEntry>::const_iterator{};
  if (!footer || entry == footer->entries.end()) { __imp__sub_824BC668(ctx, base); return; }
  const auto header = ObjectBase(base, footer->owner);
  const auto dir = header ? U32(base, header + 36) : 0;
  const auto black_color = dir ? SceneObject(ctx, base, dir, "portrait_caption_black.color") : 0;
  if (!black_color) { __imp__sub_824BC668(ctx, base); return; }
  const auto index = footer->index + size_t(entry - footer->entries.begin());
  const auto name = ObjectName(base, header);
  if (current_screen_name == "song_select_screen" && name == "pop_up_help.ihp") {
    __imp__sub_824BC668(ctx, base); return;
  }
  const bool independent = name == "continue.ihp" || name == "back.ihp" || name == "skip.ihp";
  const std::string stem = independent ? "portrait_" + name.substr(0, name.size() - 4) :
      ui::PortraitFooterSurface(index, footer->count);
  const bool hovered = DrawPortraitFooterSurface(ctx, base, dir, stem, index, footer->count, !independent);
  const bool black = hovered || (current_screen_name == "song_select_screen" && index == 2) ||
      (current_screen_name == "powerup_select_screen" && name == "help.ihp" && index == 1) ||
      current_screen_name == "splash_screen" ||
      current_panel_name == "server_connect_panel" || current_screen_name == "loading_screen";
  const auto styles = U32(base, label + 180), styles_end = U32(base, label + 184);
  if (!black || !styles || styles_end < styles || styles_end - styles < 44) {
    __imp__sub_824BC668(ctx, base); return;
  }
  // Only the primary caption changes color. Its alternate icon style keeps
  // the controller/key artwork's original colors, including while hovered.
  const auto prior_color = U32(base, styles + 12);
  const auto text_style = rex::ppc::Call<uint32_t>(ctx, base, sub_824BA9F8, label, uint32_t(0));
  std::array<uint32_t, 3> prior_rgb{};
  for (size_t i = 0; i < prior_rgb.size(); ++i) {
    prior_rgb[i] = U32(base, text_style + 24 + uint32_t(i) * 4);
    Float(base, text_style + 24 + uint32_t(i) * 4, 0);
  }
  rex::memory::store_and_swap<uint32_t>(base + styles + 12, black_color);
  rex::ScopeGuard restore([&] {
    rex::memory::store_and_swap<uint32_t>(base + styles + 12, prior_color);
    for (size_t i = 0; i < prior_rgb.size(); ++i)
      rex::memory::store_and_swap<uint32_t>(base + text_style + 24 + uint32_t(i) * 4, prior_rgb[i]);
  });
  __imp__sub_824BC668(ctx, base);
}

std::string_view CalibrationToken(uint8_t* base, uint32_t label) {
  const auto address = U32(base, label + 164);
  if (!address) return {};
  const char* token = reinterpret_cast<const char*>(base + address);
  return {token, strnlen(token, 64)};
}

void UpdateCalibrationText(PPCContext& ctx, uint8_t* base, uint32_t label) {
  if (current_screen_name.rfind("cal_", 0) != 0) return;
  const auto token = CalibrationToken(base, label);
  const bool result = token == "lag_success";
  if (!result && token != "cal_audio_desc_pad" && token != "cal_video_desc_pad") return;
  const auto address = U32(base, label + 80);
  if (!address) return;
  const char* chars = reinterpret_cast<const char*>(base + address);
  const std::string_view actual(chars, strnlen(chars, 1024));
  if (actual.size() == 1024) return;
  thread_local std::unordered_map<std::string, CalibrationPromptText> prompts;
  const bool touch = last_device::Current().using_touch;
  const bool portrait = settings::InterfacePortraitEnabled();
  const auto desired = prompts[std::string(token)].Update(actual,
      result ? touch && portrait : touch || portrait, result);
  if (desired == actual || desired.size() >= 1024) return;
  GuestCall call(ctx, base, 1280);
  const auto text = call.ctx.r1.u32 + 128;
  std::memcpy(base + text, desired.c_str(), desired.size() + 1);
  call.ctx.r3.u32 = label; call.ctx.r4.u32 = text; call.ctx.r5.u32 = 0;
  __imp__sub_824BC7D0(call.ctx, base);
}

void DrawAlignedMenuLabel(PPCContext& ctx, uint8_t* base, uint32_t label) {
  DrawPresetArrows(ctx, base, label);
  const bool portrait = settings::InterfacePortraitEnabled();
  const auto name = ObjectName(base, ObjectBase(base, label));
  const bool songs = portrait && (current_screen_name == "song_select_screen" || PortraitStoreActive());
  const bool powerups = portrait && current_screen_name == "powerup_select_screen";
  const bool powerup_info_control = powerups && name.starts_with("portrait_powerup_info_") &&
      name.ends_with(".lbl");
  const bool powerup_name = powerups && name == "name.lbl";
  if (powerup_name) { powerup_info_screen = current_screen; powerup_info_frame = frame_number; }
  if (powerup_info_control) {
    const auto dir = U32(base, ObjectBase(base, label) + 36);
    const auto stem = name.substr(0, name.size() - 4);
    const auto index = size_t(name[name.size() - 5] - '0');
    DrawPortraitFooterSurface(ctx, base, dir, stem, 0, 0, false);
    if (index < powerup_info_targets.size() && geometry.list == current_focus &&
        geometry.frame == frame_number && U32(base, current_focus + 224) == U32(base, current_focus + 228)) {
      auto rows = geometry.rows;
      std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.center.x < b.center.x; });
      const auto mesh = SceneMesh(ctx, base, dir, (stem + ".mesh").c_str());
      if (const auto bounds = mesh ? ProjectMesh(ctx, base, mesh) : std::nullopt; bounds && index < rows.size())
        powerup_info_targets[index] = {current_screen, current_focus, frame_number, *bounds, rows[index].center};
    }
  }
  if (songs && last_device::Current().using_touch &&
      (name == "shell_arrow_left.lbl" || name == "shell_arrow_rt.lbl")) return;
  const bool song_tool = songs && current_screen_name == "song_select_screen" && (name == "sorting.lbl" || name == "portrait_search.lbl");
  const int song_tab = songs ? LibraryHeaderForLabel(name) : -1;
  if (song_tab >= 0 && !SceneObject(ctx, base, U32(base, ObjectBase(base, label) + 36),
                                   "portrait_songs.color")) return;
  if (song_tab >= 0 && song_tab != (PortraitStoreActive() ? 1 : 0)) return;
  if (song_tool) {
    const auto dir = U32(base, ObjectBase(base, label) + 36);
    const std::string stem = name == "sorting.lbl" ? "portrait_sort" : "portrait_search";
    DrawPortraitFooterSurface(ctx, base, dir, stem, 0, 0, false);
    if (!current_input_scope) {
      const auto mesh = SceneMesh(ctx, base, dir, (stem + ".mesh").c_str());
      if (const auto bounds = mesh ? ProjectMesh(ctx, base, mesh) : std::nullopt) {
        std::lock_guard lock(mutex);
        hint_targets.push_back({current_screen, current_focus, frame_number, *bounds,
                               uint16_t(name == "sorting.lbl" ? 0x20 : 0x10), 0});
      }
    }
  }
  const bool title = portrait && name == "header.lbl" &&
      ui::PortraitSplashScreen(current_screen_name, current_panel_name, current_panel_scene);
  const bool success = portrait && current_screen_name.rfind("cal_", 0) == 0 &&
      CalibrationToken(base, label) == "lag_success";
  const auto token = portrait ? CalibrationToken(base, label) : std::string_view{};
  const bool instruction = portrait && current_screen_name.rfind("cal_", 0) == 0 &&
      (token == "cal_audio_desc_pad" || token == "cal_video_desc_pad");
  const bool powerup_description = portrait && current_screen_name == "powerup_select_screen" &&
      name == "powerup_desc.lbl";
  std::optional<float> calibration_z;
  if (portrait && current_screen_name.rfind("cal_", 0) == 0) {
    for (const auto& [label_name, anchor] : {
        std::pair{"audio_title.lbl", 117.f}, {"video_title.lbl", 117.f},
        {"cal_instructions.lbl", -128.f}, {"cal_reps.lbl", -105.f},
        {"cal_setting.lbl", -153.f}, {"cal_countdown.lbl", ui::kPortraitCalibrationMeterZ},
        {"failed.lbl", -112.f}}) {
      if (name == label_name) { calibration_z = anchor; break; }
    }
  }
  const auto* footer = portrait ? drawing_footer : nullptr;
  const auto entry = footer ? std::find_if(footer->entries.begin(), footer->entries.end(),
      [label](const auto& e) { return e.label == label; }) : std::vector<InlineHelpEntry>::const_iterator{};
  const bool action = footer && entry != footer->entries.end() &&
      !(songs && ObjectName(base, ObjectBase(base, footer->owner)) == "pop_up_help.ihp");
  if (!title && !success && !instruction && !powerup_description && !powerup_name && !powerup_info_control && !calibration_z && !action && !song_tool && song_tab < 0) {
    DrawMenuHintLabel(ctx, base, label); return;
  }
  if (base[label + 178])
    rex::ppc::Call<void>(ctx, base, sub_824BAF98, label, uint32_t(0));
  const float width = F32(base, label + 152), height = F32(base, label + 156);
  if (!(width > 0) || !std::isfinite(height)) { DrawMenuHintLabel(ctx, base, label); return; }
  const auto ink = InlineHelpGlyphBounds(label,
      [base](uint32_t address) { return U32(base, address); },
      [base](uint32_t address) { return F32(base, address); });
  const float visible_width = ink ? (*ink)[2] - (*ink)[0] : width;
  const float visible_height = ink ? (*ink)[3] - (*ink)[1] : std::abs(height);
  const auto transformable = label + 72 + U32(base, U32(base, label + 72) + 16);
  const auto world = WorldTransformForDraw(transformable,
      [base](uint32_t address) { return base[address]; },
      [&](uint32_t object) { return rex::ppc::Call<uint32_t>(ctx, base, sub_821F13D0, object); });
  std::array<uint8_t, 64> prior;
  std::memcpy(prior.data(), base + world, prior.size());
  rex::ScopeGuard restore([&] { std::memcpy(base + world, prior.data(), prior.size()); });
  float x = F32(base, world + 48), z = F32(base, world + 56);
  const auto portrait_draw_fit = hooks::PortraitDrawFit();
  if (song_tool || song_tab >= 0) {
    const auto aspect = ui::screen::CurrentMenuConfiguration().portrait_aspect;
    const auto layout = ui::PortraitSongs(aspect);
    const auto fit = portrait_draw_fit;
    float available = layout.width / 2 - 10;
    float desired_height = 12;
    if (song_tool) {
      x = fit.X((name == "sorting.lbl" ? -1.f : 1.f) * (layout.width / 4 + 2));
      z = fit.Z(layout.tools_z);
    } else {
      x = fit.X(0);
      z = fit.Z(layout.tabs_z);
      available = layout.width - 76; desired_height = 17;
    }
    const float scale = std::min(available * fit.scale / std::max(visible_width, 1.f),
                                  desired_height * fit.scale / std::max(visible_height, 1.f));
    for (uint32_t row = 0; row < 3; ++row)
      for (uint32_t column = 0; column < 3; ++column)
        Float(base, world + row * 16 + column * 4, row == column ? scale : 0);
  }
  if (calibration_z) { x = portrait_draw_fit.X(0); z = portrait_draw_fit.Z(*calibration_z); }
  if (title) {
    x = portrait_draw_fit.X(0);
    z = portrait_draw_fit.Z(current_screen_name == "powerup_select_screen"
        ? ui::PortraitPowerups(ui::screen::CurrentMenuConfiguration().portrait_aspect).title_z
        : ui::kPortraitTitleCenter);
  }
  if (instruction) {
    x = portrait_draw_fit.X(0); z = portrait_draw_fit.Z(ui::kPortraitCalibrationInstructionsZ);
    const float scale = CalibrationTextFit(std::abs(F32(base, world)) * visible_width,
        std::abs(F32(base, world + 40)) * visible_height,
        ui::PortraitSplash(ui::screen::CurrentMenuConfiguration().portrait_aspect).width * .84f * portrait_draw_fit.scale,
        ui::kPortraitCalibrationInstructionsHeight * portrait_draw_fit.scale);
    for (uint32_t row = 0; row < 3; ++row)
      for (uint32_t column = 0; column < 3; ++column)
        Float(base, world + row * 16 + column * 4, F32(base, world + row * 16 + column * 4) * scale);
  }
  if (powerup_description || powerup_name || powerup_info_control) {
    const auto layout = ui::PortraitPowerups(ui::screen::CurrentMenuConfiguration().portrait_aspect);
    if (!powerup_info_control) {
      x = portrait_draw_fit.X(0);
      z = portrait_draw_fit.Z(powerup_name ? layout.name_z : layout.description_z);
    }
    const float scale = CalibrationTextFit(std::abs(F32(base, world)) * visible_width,
        std::abs(F32(base, world + 40)) * visible_height,
        (powerup_info_control ? 56 * layout.choices_scale : layout.width - 20) * portrait_draw_fit.scale,
        (powerup_name ? 30 : powerup_info_control ? 14 : layout.description_height) * portrait_draw_fit.scale);
    for (uint32_t row = 0; row < 3; ++row)
      for (uint32_t column = 0; column < 3; ++column)
        Float(base, world + row * 16 + column * 4, F32(base, world + row * 16 + column * 4) * scale);
  }
  if (success) {
    x = portrait_draw_fit.X(0); z = portrait_draw_fit.Z(0);
    const float available = ui::PortraitSplash(ui::screen::CurrentMenuConfiguration().portrait_aspect).width *
                            .84f * portrait_draw_fit.scale;
    const float measured = std::abs(F32(base, world)) * visible_width;
    const float scale = std::min(last_device::Current().using_touch ? 2.f : 1.65f,
                                 available / std::max(measured, 1.f));
    for (uint32_t row = 0; row < 3; ++row)
      for (uint32_t column = 0; column < 3; ++column)
        Float(base, world + row * 16 + column * 4, F32(base, world + row * 16 + column * 4) * scale);
  }
  if (action) {
    const auto header = ObjectBase(base, footer->owner);
    const auto owner_name = ObjectName(base, header);
    const auto dir = header ? U32(base, header + 36) : 0;
    // Apply only to scenes with authored button surfaces. Other InlineHelp
    // widgets retain their original placement and text layout.
    if (!dir || !SceneObject(ctx, base, dir, "portrait_caption_black.color")) {
      DrawMenuHintLabel(ctx, base, label); return;
    }
    if (owner_name == "continue.ihp" || owner_name == "back.ihp" || owner_name == "skip.ihp") {
      const auto owner_transform = footer->owner + 4 + U32(base, U32(base, footer->owner + 4) + 16);
      const auto owner_world = rex::ppc::Call<uint32_t>(ctx, base, sub_821F13D0, owner_transform);
      x = F32(base, owner_world + 48); z = F32(base, owner_world + 56);
    } else {
      const auto aspect = ui::screen::CurrentMenuConfiguration().portrait_aspect;
      const auto index = footer->index + size_t(entry - footer->entries.begin());
      const bool song_footer = SceneObject(ctx, base, dir, "portrait_songs.color") != 0;
      const auto slot = song_footer
          ? ui::PortraitSongsFooter(aspect, index, footer->count)
          : ui::PortraitFooterAction(aspect, index, footer->count);
      x = portrait_draw_fit.X(slot.x); z = portrait_draw_fit.Z(slot.z);
      if (song_footer) {
        const float scale = std::min(1.f, slot.width * .92f * portrait_draw_fit.scale /
            std::max(std::abs(F32(base, world)) * visible_width, 1.f));
        for (uint32_t row = 0; row < 3; ++row)
          for (uint32_t column = 0; column < 3; ++column)
            Float(base, world + row * 16 + column * 4, F32(base, world + row * 16 + column * 4) * scale);
      }
    }
  }
  // UILabel's alignment includes font bearings and mixed icon baselines. Center
  // its measured rectangle through the live basis, including multiline text.
  float cx = F32(base, label + 144) + width / 2;
  float cz = F32(base, label + 148) + height / 2;
  // Padding, font bearings and unused icon styles are not visible text. Use
  // the drawn quads for calibration descriptions as well as footer captions.
  if (ink) {
    cx = ((*ink)[0] + (*ink)[2]) / 2;
    cz = ((*ink)[1] + (*ink)[3]) / 2;
  }
  Float(base, world + 48, x - F32(base, world) * cx - F32(base, world + 32) * cz);
  Float(base, world + 56, z - F32(base, world + 8) * cx - F32(base, world + 40) * cz);
  if (song_tab >= 0 && menu_available && !current_input_scope)
    if (const auto bounds = ProjectLabel(ctx, base, label))
      header_geometry[song_tab] = {current_screen, current_focus, frame_number, *bounds};
  DrawMenuHintLabel(ctx, base, label);
}

void CaptureHeader(PPCContext& ctx, uint8_t* base, uint32_t label) {
  if (settings::InterfacePortraitEnabled() &&
      (current_screen_name == "song_select_screen" || PortraitStoreActive())) return;
  if (!menu_available || current_input_scope || LibraryHeaderForScreen(current_screen_name) < 0) return;
  const int index = LibraryHeaderForLabel(ObjectName(base, ObjectBase(base, label)));
  if (index < 0) return;
  if (const auto bounds = ProjectLabel(ctx, base, label))
    header_geometry[index] = {current_screen, current_focus, frame_number, *bounds};
}
void CaptureMenuHint(PPCContext& ctx, uint8_t* base, uint32_t label, uint32_t action,
                     uint32_t owner) {
  if (!menu_available) return;
  const auto header = ObjectBase(base, owner);
  if (dialog_scope == MenuDialogScope::kShortcuts && ObjectName(base, header) != "pop_up_help.ihp") return;
  if (dialog_scope == MenuDialogScope::kPanel &&
      (!header || U32(base, header + 36) != current_panel_dir)) return;
  const uint16_t button = MenuHintButton(action);
  if (!button) return;
  auto bounds = ProjectLabel(ctx, base, label);
  // The portrait settings footer is a full native button. Its mesh supplies
  // the hit area, so tapping beside the Back text also sends Cancel.
  if (bounds && PortraitMenuHintsAvailable(ctx, base, owner)) {
    const auto dir = header ? U32(base, header + 36) : 0;
    std::string name;
    {
      const auto entries = PortraitMenuHintEntries(ctx, base, owner);
      const auto entry = std::find_if(entries.begin(), entries.end(),
          [label](const auto& entry) { return entry.label == label; });
      if (entry == entries.end()) return;
      const auto index = size_t(entry - entries.begin());
      const auto placement = PortraitMenuHintPlacement(ctx, base, owner, *entry, index, entries.size());
      name = std::string(ui::PortraitFooterSurface(placement.index, placement.count)) + ".mesh";
    }
    const auto mesh = !name.empty() && dir ? SceneMesh(ctx, base, dir, name.c_str()) : 0;
    if (mesh) {
      if (const auto button_bounds = ProjectMesh(ctx, base, mesh)) bounds = button_bounds;
    }
  }
  if (bounds && (current_panel_name == "server_connect_panel" || current_panel_name == "splash_panel") &&
      rb_blitz::settings::InterfacePortraitEnabled()) {
    const char* name = action == 1 ? "portrait_continue.mesh" :
                       action == 2 ? "portrait_back.mesh" :
                       action == 3 ? "portrait_skip.mesh" : nullptr;
    const auto dir = header ? U32(base, header + 36) : 0;
    const auto mesh = name && dir ? SceneMesh(ctx, base, dir, name) : 0;
    if (mesh) {
      if (const auto button_bounds = ProjectMesh(ctx, base, mesh)) bounds = button_bounds;
    }
  }
  if (!bounds) {
    static thread_local std::unordered_map<uint32_t, uint32_t> rejected;
    if (rejected.size() > 128) rejected.clear();
    if (rejected[label] != current_screen) {
      rejected[label] = current_screen;
      REXLOG_INFO("native_mouse: legend has no bounds screen={} action={} label={:08X} rect={},{},{},{} camera={:08X}",
                  current_screen_name, action, label, F32(base, label + 144),
                  F32(base, label + 148), F32(base, label + 152), F32(base, label + 156),
                  U32(base, 0x8284A108));
    }
    return;
  }
  // Keep only the current draw's geometry; a hidden or cleared legend must stop
  // being clickable on the next poll even when its guest label is reused.
  if (!hint_targets.empty() && hint_targets.back().frame != frame_number)
    hint_targets.clear();
  hint_targets.push_back({current_screen, current_focus, frame_number, *bounds, button, current_input_scope});
  static thread_local std::unordered_map<uint32_t, uint32_t> traced;
  if (traced.size() > 128) traced.clear();
  if (traced[label] != current_screen) {
    traced[label] = current_screen;
    REXLOG_INFO("native_mouse: legend target screen={} action={} label={:08X} bounds={:.4f},{:.4f},{:.4f},{:.4f}",
                current_screen_name, action, label, bounds->left, bounds->top, bounds->right, bounds->bottom);
  }
}
void CaptureDialogPrompt(PPCContext& ctx, uint8_t* base, uint32_t label) {
  if (!menu_available || dialog_scope != MenuDialogScope::kPanel) return;
  const auto header = ObjectBase(base, label);
  if (!header || ObjectName(base, header) != "continue.lbl" ||
      U32(base, header + 36) != current_panel_dir) return;
  const auto it = dialog_prompts.find(label);
  if (it == dialog_prompts.end()) return;
  const auto bounds = ProjectLabel(ctx, base, label);
  if (!bounds) return;
  if (!hint_targets.empty() && hint_targets.back().frame != frame_number) hint_targets.clear();
  for (const auto& span : it->second) {
    const float width = bounds->right - bounds->left;
    MenuBounds part{bounds->left + width * span.left, bounds->top,
                    bounds->left + width * span.right, bounds->bottom};
    hint_targets.push_back({current_screen, current_focus, frame_number, part,
                            MenuHintButton(span.action), current_input_scope});
  }
  static thread_local std::unordered_map<uint32_t, uint32_t> traced;
  if (traced.size() > 128) traced.clear();
  if (traced[label] != current_input_scope) {
    traced[label] = current_input_scope;
    REXLOG_INFO("native_mouse: dialog prompt panel={} label={:08X} actions={} bounds={:.4f},{:.4f},{:.4f},{:.4f}",
                current_panel_name, label, it->second.size(), bounds->left, bounds->top,
                bounds->right, bounds->bottom);
  }
}
void UpdateCalibrationSideLabel(PPCContext& ctx, uint8_t* base, uint32_t label) {
  // These two UILabels flank the metronome. Their scene font is redirected to
  // inline_help_icons.milo; its U+0081 glyph is the live Interface Confirm tile.
  if (current_screen_name.rfind("cal_", 0) != 0) return;
  const auto name = ObjectName(base, ObjectBase(base, label));
  if (name != "green02.lbl" && name != "green1.lbl") return;
  thread_local uint64_t previous_generation = 0;
  thread_local std::unordered_set<uint32_t> updated;
  if (previous_generation != screen_generation) {
    updated.clear();
    previous_generation = screen_generation;
  }
  if (!updated.insert(label).second) return;
  GuestCall call(ctx, base);
  const auto chars = call.scratch;
  constexpr uint32_t code = rb_blitz::ui::input_font::GlyphCode(1);
  base[chars] = uint8_t(0xC0 | (code >> 6));
  base[chars + 1] = uint8_t(0x80 | (code & 0x3F));
  base[chars + 2] = 0;
  call.ctx.r3.u32 = label;
  call.ctx.r4.u32 = chars;
  call.ctx.r5.u32 = 0;
  sub_824BC7D0(call.ctx, base);
}
void UpdateTouchPrompt(PPCContext& ctx, uint8_t* base, uint32_t label) {
  // UILabel keeps its locale Symbol at +164 and its RndText UTF-8 string
  // at +80 (RndText +8). SetText(clear_token=false) preserves that Symbol.
  const uint32_t token_address = U32(base, label + 164);
  if (!token_address) return;
  const char* token = reinterpret_cast<const char*>(base + token_address);
  const std::string_view name(token, strnlen(token, 64));
  if (name.size() == 64) return;
  const uint32_t text_address = U32(base, label + 80);
  if (!text_address) return;
  const char* text = reinterpret_cast<const char*>(base + text_address);
  const std::string_view actual(text, strnlen(text, 4096));
  if (actual.size() == 4096) return;
  // Any localized button instruction can appear outside the start screens,
  // including powerup descriptions. Preserve each token's physical wording.
  thread_local std::unordered_map<std::string, StartPromptText> prompts;
  auto found = prompts.find(std::string(name));
  if (found == prompts.end()) {
    if (TouchStartPrompt(actual) == actual) return;
    found = prompts.emplace(std::string(name), StartPromptText{}).first;
  }
  auto& prompt = found->second;
  const auto desired = prompt.Update(actual, last_device::Current().using_touch);
  if (desired == actual || desired.size() >= 4096) return;
  GuestCall call(ctx, base, uint32_t(256 + desired.size()));
  const uint32_t chars = call.ctx.r1.u32 + 128;
  std::memcpy(base + chars, desired.c_str(), desired.size() + 1);
  call.ctx.r3.u32 = label;
  call.ctx.r4.u32 = chars;
  call.ctx.r5.u32 = 0;
  __imp__sub_824BC7D0(call.ctx, base);
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
  const auto list_header = ObjectBase(base, list);
  const bool belongs = dialog_scope != MenuDialogScope::kPanel ||
      (list_header && U32(base, list_header + 36) == current_panel_dir);
  const bool focused = list == current_focus && menu_available && belongs;
  const bool touch = last_device::Current().using_touch;
  const bool portrait_songs = settings::InterfacePortraitEnabled() &&
      (current_screen_name == "song_select_screen" || PortraitStoreActive());
  const bool portrait_powerups = settings::InterfacePortraitEnabled() && current_screen_name == "powerup_select_screen";
  if (ctx.r9.u32 || !list || (!focused && !touch && !portrait_songs && !portrait_powerups)) {
    __imp__sub_824E5020(ctx, base); return;
  }
  DrawCapture capture;
  capture.resource = ctx.r3.u32;
  capture.touch = touch;
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
  // Touch uses the same draw-only suppression without inheriting mouse hover;
  // only the row pressed on this page may light, until its release.
  if (touch) mouse_highlight.Reset();
  bool suppress = !focused || !touch_highlight.Draw(touch, list, SelectedRow(ctx, base, list),
                                       (HostMouseButtons() & 1) != 0);
  if (!touch && mouse_highlight.lifted_list == list) {
    if (!mouse_highlight.Lifted(list, SelectedRow(ctx, base, list))) {
      if (rb_blitz::settings::title::mouse_native_trace.Get())
        REXLOG_INFO("native_mouse: highlight back list={:08X}", list);
    } else suppress = true;
  }
  if (suppress) {
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
  auto previous = drawing; drawing = &capture;
  __imp__sub_824E5020(ctx, base);
  drawing = previous;
  if (!focused) return;
  auto& result = capture.result;
  // The native highlight supplies the full button width, even when its text is
  // short. Translate its cell along the row axis; no screen-specific pitches.
  const auto selected = std::find_if(result.rows.begin(), result.rows.end(), [&](const MenuRow& r) { return r.index == capture.selected; });
  if (capture.highlight && selected != result.rows.end() && !portrait_powerups) {
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
  // Entrance animations and window resizing can move rows after their first
  // draw. Publish a complete changed table so diagnostic consumers see the
  // same current bounds as the live hit test.
  const bool trace_geometry = rb_blitz::settings::title::mouse_native_trace.Get() &&
      (geometry.list != list || geometry.rows.size() != result.rows.size() ||
       !std::equal(result.rows.begin(), result.rows.end(), geometry.rows.begin(),
           [](const MenuRow& a, const MenuRow& b) {
             if (a.index != b.index || a.enabled != b.enabled || !a.bounds || !b.bounds)
               return false;
             constexpr float epsilon = .0001f;
             return std::abs(a.bounds->left - b.bounds->left) < epsilon &&
                    std::abs(a.bounds->top - b.bounds->top) < epsilon &&
                    std::abs(a.bounds->right - b.bounds->right) < epsilon &&
                    std::abs(a.bounds->bottom - b.bounds->bottom) < epsilon;
           }));
  for (auto& row : result.rows) {
    const auto& b = *row.bounds;
    row.center = {(b.left + b.right) * .5f, (b.top + b.bottom) * .5f};
    result.bounds.left = std::min(result.bounds.left, b.left); result.bounds.right = std::max(result.bounds.right, b.right);
    result.bounds.top = std::min(result.bounds.top, b.top); result.bounds.bottom = std::max(result.bounds.bottom, b.bottom);
    if (trace_geometry)
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
  // Library and RhythmVerse lists share the viewport route, including shortcuts without
  // a sibling rail. Publish only the focused list from this completed draw.
  const bool touch_list = MenuTouchScrollEligible(current_screen_name, dialog_scope, geometry.horizontal);
  TouchTarget target;
  if (touch_list && EnsureRailMap(ctx, base, list) && rail_map.max_first > 0) {
    const float pitch = RailPitch(geometry.rows);
    if (pitch > 0)
      target = {list, frame_number, geometry.bounds, pitch,
                int(U32(base, list + 224)), rail_map.max_first};
  }
  {
    std::lock_guard lock(mutex);
    // Keep the pitch fixed for a held finger: edge fades can temporarily omit
    // a row from hit geometry while the native draw state is interpolating.
    if (touch_captured && target.list == touch_target.list) target.pitch = touch_target.pitch;
    touch_target = target;
    touch_scroll.Limit(target.maximum);
  }
}
std::optional<float> PortraitPanelDrawAspect(uint8_t* base, uint32_t drawable) {
  if (!settings::InterfacePortraitEnabled() || !portrait_splash_active) return {};
  const auto object = ObjectBase(base, drawable);
  const auto name = ObjectName(base, object);
  const bool store_panel = PortraitStoreActive() &&
      (name == "StoreMainPanel" || name == "StoreMenuPanel" || name == "songselect" || name == "storedetails" || name == "error_general");
  if (!store_panel && (!current_panel_dir || object != ObjectBase(base, current_panel_dir))) return {};
  return ui::PortraitSplash(ui::screen::CurrentMenuConfiguration().portrait_aspect).width / 480;
}

} // namespace rb_blitz::input

REX_HOOK_RAW(sub_824B2958) {
  rex::diagnostics::compile_trace::Scope trace_poll(
      rex::diagnostics::compile_trace::Event::kGuestWork, "guest.ui_poll", 0x824B2958);
  const auto manager = ctx.r3.u32;
  rb_blitz::input::PollMouse(ctx, base, manager);
  __imp__sub_824B2958(ctx, base);
  rb_blitz::content_progress::Tick(ctx, base);
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
  trace_poll.Success();
}
REX_HOOK_RAW(sub_824DC2F0) {
  const auto slider = ctx.r3.u32 - 204;
  __imp__sub_824DC2F0(ctx, base);
  rb_blitz::input::CaptureSlider(ctx, base, slider);
}
REX_HOOK_RAW(sub_824E5020) {
  rb_blitz::input::DrawList(ctx, base);
}
REX_HOOK_RAW(sub_824E6DC8) {
  using namespace rb_blitz::input;
  const auto state = ctx.r5.u32;
  const float fraction = touch_draw_position - std::floor(touch_draw_position);
  if (!touch_draw_list || state != touch_draw_list + 176 || fraction <= 0) {
    __imp__sub_824E6DC8(ctx, base);
    return;
  }
  // UIListState +48/+52/+60 are firstShowing, targetShowing and progress.
  // Let the native builder supply its extra edge row, provider spacing and
  // fades at this fractional position. Restore navigation state before poll.
  const auto draw_state = ctx.r4.u32;
  const int selected = SelectedRow(ctx, base, touch_draw_list);
  const int anchor = selected >= 0 ? selected : last_selected_row;
  const auto target = U32(base, state + 52), progress = U32(base, state + 60);
  rex::ScopeGuard restore([&] {
    rex::memory::store_and_swap<uint32_t>(base + state + 52, target);
    rex::memory::store_and_swap<uint32_t>(base + state + 60, progress);
  });
  rex::memory::store_and_swap<uint32_t>(base + state + 52, U32(base, state + 48) + 1);
  Float(base, state + 60, fraction);
  __imp__sub_824E6DC8(ctx, base);
  // Retail interpolation advances the selected display slot at its midpoint.
  // A finger moves only the view: retain the logical selection and its draw
  // slot instead, including while it sits outside the visible band.
  rex::memory::store_and_swap<uint32_t>(base + draw_state + 48, uint32_t(-1));
  const auto first = U32(base, draw_state + 56), end = U32(base, draw_state + 60);
  for (auto row = first; row < end; row += 44) {
    const auto row_state = U32(base, row + 24);
    if (row_state == 1) rex::memory::store_and_swap<uint32_t>(base + row + 24, 0);
    if (anchor >= 0 && U32(base, row + 36) == uint32_t(anchor)) {
      rex::memory::store_and_swap<uint32_t>(base + draw_state + 48, U32(base, row + 32));
      if (row_state != 2) rex::memory::store_and_swap<uint32_t>(base + row + 24, 1);
    }
  }
}
REX_HOOK_RAW(sub_824E07A8) {
  using namespace rb_blitz::input;
  if (drawing) {
    drawing->widget = ctx.r3.u32;
    const auto row = ctx.r5.u32 - 4;
    drawing->row = row >= drawing->first && row < drawing->end && (row - drawing->first) % 44 == 0 ? row : 0;
  }
  __imp__sub_824E07A8(ctx, base);
}
REX_HOOK_RAW(sub_824E9A90) {
  // The native fourth category remains keyboard/controller reachable, while
  // its launch action is drawn and targeted by the portrait Start footer.
  using namespace rb_blitz::input;
  if (PortraitPowerupFooterRow(base)) return;
  rb_blitz::input::DrawPortraitSongRow(ctx, base);
  rb_blitz::input::CaptureLabel(ctx, base);
  __imp__sub_824E9A90(ctx, base);
}
REX_HOOK_RAW(sub_824BC668) {
  const auto label = ctx.r3.u32 - 260;
  rb_blitz::input::UpdateTouchPrompt(ctx, base, label);
  rb_blitz::hooks::PrepareHudLabel(ctx, base, label);
  rb_blitz::input::UpdateCalibrationSideLabel(ctx, base, label);
  rb_blitz::input::UpdateCalibrationText(ctx, base, label);
  rb_blitz::input::DrawAlignedMenuLabel(ctx, base, label);
  rb_blitz::input::CaptureDialogPrompt(ctx, base, label);
  rb_blitz::input::CaptureHeader(ctx, base, label);
}

REX_HOOK_RAW(sub_824BC7D0) {
  using namespace rb_blitz::input;
  const uint32_t label = ctx.r3.u32;
  const auto previous = setting_prompt_label;
  setting_prompt_label = ObjectName(base, ObjectBase(base, label)) == "continue.lbl" ? label : 0;
  if (setting_prompt_label) dialog_prompts.erase(label);
  __imp__sub_824BC7D0(ctx, base);
  setting_prompt_label = previous;
}

REX_HOOK_RAW(sub_8229FD60) {
  using namespace rb_blitz::input;
  const uint32_t label = setting_prompt_label;
  const uint32_t text_address = ctx.r4.u32, advances_address = ctx.r5.u32;
  std::u16string text;
  if (label && ctx.r3.u32 == label + 72) {
    for (size_t i = 0; i < 1024; ++i) {
      const auto code = rex::memory::load_and_swap<uint16_t>(base + text_address + i * 2);
      if (!code) break;
      text.push_back(char16_t(code));
    }
  }
  __imp__sub_8229FD60(ctx, base);
  if (text.empty() || text.size() == 1024) return;
  std::vector<float> advances;
  for (size_t i = 0; i <= text.size(); ++i)
    advances.push_back(F32(base, advances_address + uint32_t(i * 4)));
  if (dialog_prompts.size() > 128) dialog_prompts.clear();
  dialog_prompts[label] = DialogPromptSpans(text, advances);
}
REX_HOOK_RAW(sub_824E0900) {
  using namespace rb_blitz::input;
  const auto mesh = ctx.r4.u32;
  const auto transform = ctx.r7.u32;
  if (PortraitPowerupFooterRow(base)) return;
  const bool bar = drawing && !ctx.r8.u32 && U32(base, U32(base, ctx.r3.u32) + 100) == 0x824E9098;
  // The mouse's unhover frame draws no bar and captures no cell: the cell would be the
  // bar's own projection, and the hover that selects a row again restores both in the same
  // frame the pointer lands (`UIListHighlight::Draw`, at vtable +100). The mesh's transform
  // is left at its resting value; UIListHighlight::Draw restores it after every draw anyway.
  if (bar && drawing->suppress) {
    // DrawMesh would apply r7 to the shared bar. Project with that same final
    // transform so invisible touch highlights retain their full hit width.
    if (drawing->touch) drawing->highlight = ProjectMesh(ctx, base, mesh, ctx.r7.u32);
    return;
  }
  __imp__sub_824E0900(ctx, base);
  if (bar) drawing->highlight = ProjectMesh(ctx, base, mesh);
  // UIListArrow::Draw calls DrawMesh only for a visible scroll arrow, with
  // its final slot transform applied. Capture here before its caller restores
  // the mesh transform, using the camera that actually drew the list.
  if (drawing && current_screen_name == "powerup_select_screen") {
    const auto name = ObjectName(base, ObjectBase(base, mesh));
    if (name == "arrow_lt.mesh") drawing->result.left_arrow = ProjectMesh(ctx, base, mesh, transform);
    if (name == "arrow_rt.mesh") drawing->result.right_arrow = ProjectMesh(ctx, base, mesh, transform);
  }
}

REX_HOOK_RAW(sub_824BFEF0) {
  using namespace rb_blitz::input;
  const auto mesh = drawing && drawing->row && !ctx.r7.u32 && U32(base, ctx.r3.u32 + 8)
      ? U32(base, U32(base, ctx.r3.u32 + 4) + 132) : 0;
  __imp__sub_824BFEF0(ctx, base);
  if (mesh && !PortraitPowerupFooterRow(base))
    if (auto bounds = ProjectMesh(ctx, base, mesh)) CaptureRow(base, *bounds);
}
