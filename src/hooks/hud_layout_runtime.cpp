// SPDX-License-Identifier: GPL-2.0-only
// Apply the HUD edge rule while WorldXfm is resolving a live transform, before
// its caller consumes the result. See docs/settings/graphics.md part 3 §9.2.7.
#include "hooks/hud_layout_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "hooks/hud_layout.h"
#include "hooks/render_settings.h"
#include "platform/guest_memory.h"
#include "input/touch_controls.h"
#include "input/native_menu.h"

REX_EXTERN(__imp__sub_821F13D0);  // RndTransformable::WorldXfm
REX_EXTERN(sub_821F13D0);
REX_EXTERN(sub_821F0300);        // SetWorldDirty, propagates to transform children
REX_EXTERN(__imp__sub_821F65C8); // RndDir::DrawShowing (RndDrawable view)
REX_EXTERN(__imp__sub_82226CD8); // RndGroup child draw traversal (RndDrawable view)
REX_EXTERN(__imp__sub_823FF258); // Hmx::Object::SetProperty(Symbol, DataNode)

REXCVAR_DEFINE_BOOL(hud_reanchor, true, "Graphics",
                    "Re-anchor the in-song HUD on non-16:9 frames during world-transform "
                    "resolution, independently of HUD element visibility.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(hud_reanchor_apply, true, "Graphics",
                    "Place the six measured HUD anchors; their children inherit the shift.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(hud_reanchor_hud_half_height, 240.0, "Graphics",
                      "HUD scene half-height in authored units (240, measured 2026-10-17).")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(hud_reanchor_probe, false, "Diagnostics",
                    "Log a bounded sample of HUD world-transform placements.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
// Accept old configuration/command-line flags without restoring a heap sweep.
REXCVAR_DEFINE_BOOL(hud_reanchor_scan, false, "Diagnostics",
                    "Retired HUD heap-scan diagnostic; accepted for compatibility, no work.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {
constexpr uint32_t kImageBegin = 0x82000000;
constexpr uint32_t kImageEnd = 0x82A40000;
constexpr uint32_t kHeapBegin = 0x40000000;
constexpr uint32_t kHeapEnd = 0x7F000000;
constexpr uint32_t kWorldX = 0x78;
constexpr uint32_t kParent = 0x94;
constexpr uint32_t kConstraint = 0xB8;
constexpr int kMaxParents = 32;

uint32_t U32(const uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}

bool HeapRange(uint32_t address, uint32_t bytes) {
  return address >= kHeapBegin && uint64_t(address) + bytes <= kHeapEnd;
}

// These are live objects supplied by WorldXfm, not speculative heap candidates.
// Follow the same virtual-base conversion as the guest constructors: the table
// at +4 gives the displacement from +4 to Hmx::Object. No heap/page-state cache
// survives an unload, and no heap allocation query is made on this path.
uint32_t ObjectBase(const uint8_t* base, uint32_t object) {
  if (!HeapRange(object, 8)) return 0;
  const uint32_t table = U32(base, object + 4);
  if (table < kImageBegin || table > kImageEnd - 8) return 0;
  const int32_t adjust = int32_t(U32(base, table + 4));
  if (adjust < -0x1000 || adjust > 0x1000) return 0;
  const int64_t header = int64_t(object) + 4 + adjust;
  return header >= kHeapBegin && header + 40 <= kHeapEnd ? uint32_t(header) : 0;
}

bool StringRange(uint32_t address, size_t bytes) {
  return HeapRange(address, uint32_t(bytes)) ||
         (address >= kImageBegin && uint64_t(address) + bytes <= kImageEnd);
}

bool NameIs(const uint8_t* base, uint32_t address, std::string_view expected) {
  if (!StringRange(address, expected.size() + 1)) return false;
  // The name is an owned string of a live Hmx::Object; stop at a mismatch/NUL.
  return std::strncmp(reinterpret_cast<const char*>(base + address),
                      expected.data(), expected.size() + 1) == 0;
}

bool HudPath(const uint8_t* base, uint32_t address) {
  constexpr char path[] = "track/hud.milo";
  if (!StringRange(address, sizeof(path))) return false;
  // +0x64/+0x6C are file-path fields only on PanelDir. For other ObjectDirs
  // these words can be unrelated data: validate this speculative string with
  // VirtualQuery directly, never the expensive guest heap QueryRegionInfo.
  // There is no permanent positive/negative page cache to go stale on reload.
#if defined(_WIN32)
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(base + address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
      base + address + sizeof(path) >
          static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize) return false;
#else
  if (!rb_blitz::platform::GuestRangeReadable(base + address, sizeof(path))) return false;
#endif
  return std::memcmp(base + address, path, sizeof(path)) == 0;
}

bool InHud(const uint8_t* base, uint32_t header) {
  uint32_t dir = U32(base, header + 36);
  for (int depth = 0; depth < kMaxParents && HeapRange(dir, 0x70); ++depth) {
    const uint32_t object = ObjectBase(base, dir);
    if (object == 0) return false;
    if (HudPath(base, U32(base, dir + 0x64)) || HudPath(base, U32(base, dir + 0x6C)) ||
        (HeapRange(object, 0x70) &&
         (HudPath(base, U32(base, object + 0x64)) ||
          HudPath(base, U32(base, object + 0x6C))))) {
      return true;
    }
    const uint32_t parent = U32(base, object + 36);
    if (parent == dir) return false;
    dir = parent;
  }
  return false;
}

struct Anchor {
  std::string_view name;
  uint32_t vtable;
  int32_t transform_offset;  // from Hmx::Object, measured in §9.2.5
  int side;
  bool portrait_only = false;
};
constexpr auto kAnchors = std::to_array<Anchor>({
    {"star_board",          0x8201C784, -0x114,  1},
    {"band_meter_hud",      0x8201C784, -0x114, -1},
    {"letter_collect_hud",  0x8201C784, -0x114, -1},
    {"remedial_help.grp",   0x82024C4C, -0x108, -1},
    {"dynamic_leaderboard", 0x82018EA4, -0x1B4,  1},
    {"score.lbl",           0x8203A95C,  0x07C,  1},
    {"timer.lbl",           0x8203A95C,  0x07C,  0, true},
    {"streak.lbl",          0x8203A95C,  0x07C,  0, true},
    {"inputs.lbl",          0x8203A95C,  0x07C,  0, true},
    {"pb.lbl",              0x8203A95C,  0x07C,  0, true},
    {"progress_bar.sld",     0,           0,      0, true},
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
    {"touch_pause.grp",     0x82024C4C, -0x108, -1},
#endif
});

bool TouchAnchor(const Anchor& anchor) {
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
  return anchor.name == "touch_pause.grp";
#else
  return false;
#endif
}
bool TouchHudEnabled() {
#if defined(RB_BLITZ_IOS)
  return true;
#elif defined(RB_BLITZ_TOUCH_HUD)
  return rb_blitz::input::touch::PreviewEnabled();
#else
  return false;
#endif
}

bool PortraitHud();
const Anchor* AnchorFor(const uint8_t* base, uint32_t transformable) {
  const uint32_t header = ObjectBase(base, transformable);
  if (!header) return nullptr;
  const uint32_t vtable = U32(base, header);
  for (const auto& anchor : kAnchors) {
    if (anchor.portrait_only && !PortraitHud()) continue;
    // Script-created slider subclasses share the live WorldXfm ABI but do not
    // share AppLabel's object offset. Its owned name and HUD ancestry identify
    // it here; no speculative conversion from a header is attempted.
    if (anchor.vtable && (vtable != anchor.vtable ||
        int64_t(header) + anchor.transform_offset != transformable)) continue;
    if (NameIs(base, U32(base, header + 32), anchor.name) && InHud(base, header)) {
      return &anchor;
    }
  }
  return nullptr;
}

uint32_t HudWidth() {
  const auto width = rb_blitz::settings::BootPortraitWidth();
  return width ? width : rb_blitz::settings::BootRenderWidth();
}
uint32_t HudHeight() {
  const auto height = rb_blitz::settings::BootPortraitHeight();
  return height ? height : rb_blitz::settings::BootRenderHeight();
}
// Portrait uses compact, separate HUD slots in the guest's 480-unit scene.
// Resolve from the fresh authored world matrix, so animations/reloads never
// accumulate a scale or translation. Named children inherit the parent's scale.
bool PortraitHud() {
  const auto width = HudWidth();
  return width != 0 && width < HudHeight();
}
const Anchor* InheritedAnchor(const uint8_t* base, uint32_t transformable) {
  uint32_t current = transformable;
  for (int depth = 0; depth < kMaxParents; ++depth) {
    const auto constraint = U32(base, current + kConstraint);
    if (constraint != 0 && constraint != 1 && constraint != 2 && constraint != 10) break;
    const auto parent = U32(base, current + kParent);
    if (!HeapRange(parent, 0xC0) || parent == current || parent == transformable) break;
    if (const auto* anchor = AnchorFor(base, parent)) return anchor;
    current = parent;
  }
  return nullptr;
}
float PortraitScale(const Anchor& anchor, float half_width) {
  float scale = .5f;
  if (anchor.name == "band_meter_hud" || anchor.name == "dynamic_leaderboard")
    scale = .65f;
  if (anchor.name == "progress_bar.sld") scale = .8f;
  if (anchor.name == "streak.lbl") scale = 1.2f;
  if (anchor.name == "timer.lbl" || anchor.name == "inputs.lbl")
    scale = .7f;
  return scale * std::min(1.f, half_width / 120.f);
}
void PlacePortraitHud(uint8_t* base, uint32_t transformable, const Anchor& anchor) {
  const float half_width = 240.f * HudWidth() /
                           HudHeight();
  const float scale = PortraitScale(anchor, half_width);
  const auto* inherited = InheritedAnchor(base, transformable);
  const float relative_scale = inherited && !TouchAnchor(*inherited)
      ? scale / PortraitScale(*inherited, half_width) : scale;
  for (uint32_t row = 0; row < 3; ++row)
    for (uint32_t column = 0; column < 3; ++column) {
      const auto address = transformable + 72 + row * 16 + column * 4;
      const float value = std::bit_cast<float>(U32(base, address));
      rex::memory::store_and_swap<uint32_t>(base + address,
          std::bit_cast<uint32_t>(value * relative_scale));
    }
  if (anchor.name == "progress_bar.sld") {
    // Screen right is +X and screen up is +Z. Rotate the fresh world basis
    // clockwise around Y, including the authored slider tilt and its children.
    for (uint32_t row = 0; row < 3; ++row) {
      const auto address = transformable + 72 + row * 16;
      const float old_x = std::bit_cast<float>(U32(base, address));
      const float old_z = std::bit_cast<float>(U32(base, address + 8));
      rex::memory::store_and_swap<uint32_t>(base + address,
          std::bit_cast<uint32_t>(old_z));
      rex::memory::store_and_swap<uint32_t>(base + address + 8,
          std::bit_cast<uint32_t>(-old_x));
    }
  }
  float x = 0, z = 0;
  if (anchor.name == "star_board") { x = half_width - 24; z = 210; }
  else if (anchor.name == "score.lbl") { x = 0; z = 210; }
  else if (anchor.name == "band_meter_hud") { x = -half_width + 115 * scale; z = 0; }
  else if (anchor.name == "letter_collect_hud") { x = 0; z = 110; }
  else if (anchor.name == "dynamic_leaderboard") { x = half_width * (2.f / 3.f); z = 0; }
  else if (anchor.name == "timer.lbl") { x = 0; z = -202; }
  else if (anchor.name == "streak.lbl") { x = -half_width + 12; z = -80; }
  else if (anchor.name == "inputs.lbl") { x = -half_width + 12; z = -160; }
  else if (anchor.name == "pb.lbl") {
    // Ultimate parks this animated label at local x=900 when it is hidden.
    // Preserve that state rather than dragging the offscreen key into view.
    const float local_x = std::bit_cast<float>(U32(base, transformable + 0x38));
    bool preview = false;
#if defined(RB_BLITZ_TOUCH_HUD) && !defined(RB_BLITZ_IOS)
    preview = rb_blitz::input::touch::PreviewEnabled();
#endif
    x = local_x > 100 && !preview ? half_width + 900 : half_width - 12;
    z = 70;
  }
  else if (anchor.name == "progress_bar.sld") { x = -84 * scale; z = -212; }
  else if (anchor.name == "remedial_help.grp") { x = 0; z = -190; }
  rex::memory::store_and_swap<uint32_t>(base + transformable + kWorldX,
                                       std::bit_cast<uint32_t>(x));
  rex::memory::store_and_swap<uint32_t>(base + transformable + kWorldX + 8,
                                       std::bit_cast<uint32_t>(z));
}

// Ordinary transform constraints compose the parent's world translation. Avoid
// shifting a named child twice when it already inherits a placed HUD anchor.
// Stop when the guest no longer uses that parent (billboard/target constraints).
int InheritedSide(const uint8_t* base, uint32_t transformable) {
  uint32_t current = transformable;
  for (int depth = 0; depth < kMaxParents; ++depth) {
    const uint32_t constraint = U32(base, current + kConstraint);
    if (constraint != 0 && constraint != 1 && constraint != 2 && constraint != 10) return 0;
    const uint32_t parent = U32(base, current + kParent);
    if (!HeapRange(parent, 0xC0) || parent == current || parent == transformable) return 0;
    if (const auto* anchor = AnchorFor(base, parent)) return anchor->side;
    current = parent;
  }
  return 0;
}
double ActiveDelta() {
  if (!REXCVAR_GET(hud_reanchor) || !REXCVAR_GET(hud_reanchor_apply)) return 0.0;
  const double delta = rb_blitz::settings::HudShiftUnits(
      HudWidth(), HudHeight()) *
      REXCVAR_GET(hud_reanchor_hud_half_height);
  return std::isfinite(delta) ? delta : 0.0;
}

// A drawn group can be submitted independently of its owning ObjectDir (the
// Ultimate personal-best path adds dynamic_leaderboard's draw.grp to hud.grp).
// Identify the live transformable using the same measured class offsets; the
// group itself need not have an anchor name.
uint32_t TransformForHeader(const uint8_t* base, uint32_t header) {
  if (!header) return 0;
  const uint32_t vtable = U32(base, header);
  for (const auto& known : kAnchors) {
    if (vtable != known.vtable) continue;
    const uint32_t transformable = uint32_t(int64_t(header) + known.transform_offset);
    return HeapRange(transformable, 0xC0) && ObjectBase(base, transformable) == header
               ? transformable : 0;
  }
  return 0;
}

void PrepareAnchorDraw(PPCContext& ctx, uint8_t* base, uint32_t transformable,
                       const Anchor& anchor) {
  // A group and each of its descendants may submit the same owning anchor.
  // WorldXfm is an evaluator, not a clean-cache getter: even with +189 clear it
  // recomputes the matrix and calls the class's virtual world-update method.
  // Do that once for unchanged live state, rather than at every child draw.
  // Never trust an address alone across an unload/reload. Compare the current
  // transform (including local/world matrices, parent, constraint and dirty byte)
  // and its parent's world cache against the exact result we prepared.
  struct Prepared {
    const uint8_t* base = nullptr;
    uint32_t transformable = 0;
    double delta = 0;
    std::array<uint8_t, 0xC0> state{};
    std::array<uint8_t, 64> parent_world{};
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
    bool touch_mode = false;
    rb_blitz::input::touch::Layout touch_layout;
#endif
  };
  static thread_local std::array<Prepared, 16> prepared{};
  static thread_local size_t next_prepared = 0;
  const double delta = ActiveDelta();
  const uint32_t parent = U32(base, transformable + kParent);
  const bool has_parent = HeapRange(parent, 0xC0);
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
  const bool touch_mode = TouchAnchor(anchor) && rb_blitz::input::touch::TouchInputActive() &&
                          rb_blitz::input::touch::ButtonsEnabled();
  const auto touch_layout = touch_mode ? rb_blitz::input::touch::CurrentLayout()
                                       : rb_blitz::input::touch::Layout{};
#endif
  Prepared* previous = nullptr;
  for (auto& entry : prepared) {
    if (entry.base != base || entry.transformable != transformable) continue;
    previous = &entry;
    if (
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
        entry.touch_mode == touch_mode && entry.touch_layout == touch_layout &&
#endif
        !base[transformable + 189] &&
        (!has_parent || !base[parent + 189]) &&
        entry.delta == delta &&
        std::memcmp(entry.state.data(), base + transformable, entry.state.size()) == 0 &&
        (!has_parent || std::memcmp(entry.parent_world.data(), base + parent + 72,
                                   entry.parent_world.size()) == 0)) return;
    break;
  }
  // These bits affect logging only.
  static thread_local std::array<bool, kAnchors.size()> reported{};
  bool& seen = reported[size_t(&anchor - kAnchors.data())];
  const uint32_t before = seen ? 0 : U32(base, transformable + kWorldX);
  rex::CallFrame call(ctx);
  call.ctx.r3.u32 = transformable;
  sub_821F13D0(call.ctx, base);
  if (!previous) previous = &prepared[next_prepared++ % prepared.size()];
  previous->base = base;
  previous->transformable = transformable;
  previous->delta = delta;
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
  previous->touch_mode = touch_mode;
  previous->touch_layout = touch_layout;
#endif
  std::memcpy(previous->state.data(), base + transformable, previous->state.size());
  const uint32_t resolved_parent = U32(base, transformable + kParent);
  if (HeapRange(resolved_parent, 0xC0))
    std::memcpy(previous->parent_world.data(), base + resolved_parent + 72,
                previous->parent_world.size());
  if (!seen) {
    seen = true;
    const uint32_t after = U32(base, transformable + kWorldX);
    float from, to;
    std::memcpy(&from, &before, sizeof(from));
    std::memcpy(&to, &after, sizeof(to));
    REXLOG_INFO("hud_reanchor: pre-draw '{}' world x {:.3f} -> {:.3f} ({:08X})",
                anchor.name, from, to, transformable);
  }
}

// Clean matrices bypass WorldXfm in the guest's draw paths. Prepare every HUD
// anchor in the drawn group's transform ancestry, from parent to child. This
// also covers separately submitted comparison art whose anchor dir never draws.
// Only eligible anchors' live parent chains are visited; repeated preparations
// reuse the content-validated snapshots above.
void PrepareTransformAncestry(PPCContext& ctx, uint8_t* base, uint32_t current) {
  std::array<uint32_t, kMaxParents> chain{};
  size_t count = 0;
  while (HeapRange(current, 0xC0) && count < chain.size()) {
    for (size_t i = 0; i < count; ++i) {
      if (chain[i] == current) return; // malformed cycle, never resolve it recursively
    }
    chain[count++] = current;
    const uint32_t constraint = U32(base, current + kConstraint);
    if (constraint != 0 && constraint != 1 && constraint != 2 && constraint != 10) break;
    current = U32(base, current + kParent);
  }
  while (count != 0) {
    const uint32_t transformable = chain[--count];
    if (const Anchor* anchor = AnchorFor(base, transformable)) {
      PrepareAnchorDraw(ctx, base, transformable, *anchor);
    }
  }
}
// A draw group's ownership and transform parent are independent. Its members
// can belong to the comparison dir and inherit that dir's transform even when
// draw.grp itself has no transform parent. Prepare named owning dirs as well.
void PrepareHudDraw(PPCContext& ctx, uint8_t* base) {
  if (ActiveDelta() == 0.0 && !TouchHudEnabled()) return;
  const uint32_t header = ObjectBase(base, ctx.r3.u32);
  if (!header) return;
  // These hooks also see the entire 3D venue. Only the named HUD entry points
  // and Ultimate's explicitly submitted comparison draw.grp need preparation.
  // In particular, don't walk every model group's ObjectDir ancestry looking
  // for a HUD that isn't there.
  const uint32_t vtable = U32(base, header);
  const uint32_t name = U32(base, header + 32);
  bool named_anchor = false;
  for (const auto& anchor : kAnchors) {
    if (vtable == anchor.vtable && NameIs(base, name, anchor.name)) {
      named_anchor = true;
      break;
    }
  }
  if (!named_anchor &&
      (vtable != 0x82024C4C || !NameIs(base, name, "draw.grp"))) return;
  const uint32_t transformable = TransformForHeader(base, header);
  if (named_anchor) {
    PrepareTransformAncestry(ctx, base, transformable);
    return;
  }
  // The authored comparison route supplies either its named owning directory
  // or a direct transform parent. Unrelated draw.grp objects must also take a
  // constant-time rejection path, without a scene-wide ancestry walk.
  const uint32_t dir = U32(base, header + 36);
  const uint32_t owner = HeapRange(dir, 8) ? TransformForHeader(base, ObjectBase(base, dir)) : 0;
  if (owner && AnchorFor(base, owner)) PrepareTransformAncestry(ctx, base, owner);
  const uint32_t parent = transformable ? U32(base, transformable + kParent) : 0;
  if (HeapRange(parent, 0xC0) && AnchorFor(base, parent))
    PrepareTransformAncestry(ctx, base, parent);
}
}  // namespace

void rb_blitz::hooks::PrepareHudLabel(PPCContext& ctx, uint8_t* base, uint32_t label) {
  if (!PortraitHud()) return;
  const uint32_t header = ObjectBase(base, label);
  if (!header) return;
  const uint32_t transformable = TransformForHeader(base, header);
  if (transformable && AnchorFor(base, transformable))
    PrepareTransformAncestry(ctx, base, transformable);
}

bool rb_blitz::hooks::HudReanchorWorld(uint8_t* base, uint32_t transformable) {
  const double delta = ActiveDelta();
  if (delta == 0.0 && !TouchHudEnabled()) return false;
  const Anchor* anchor = AnchorFor(base, transformable);
  if (!anchor) return false;
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
  if (TouchAnchor(*anchor) && rb_blitz::input::touch::TouchInputActive() &&
      rb_blitz::input::touch::ButtonsEnabled()) {
    const auto layout = rb_blitz::input::touch::CurrentLayout();
    if (layout.width > 0 && layout.height > 0) {
      const auto& rect = layout.pause;
      const float height = float(HudHeight());
      const float width = float(HudWidth());
      const float aspect = width > 0 && height > 0 ? width / height : 16.f / 9.f;
      const float x = ((rect.x + rect.width / 2) / layout.width - .5f) * aspect * 480;
      const float z = (.5f - (rect.y + rect.height / 2) / layout.height) * 480;
      const float scale = rect.height * 480 / (layout.height * 54);
      // Preserve the native group transform and all animated children; only
      // map the disc's centre and diameter to the same safe-area hit rectangle.
      for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column) {
          const auto address = transformable + 72 + row * 16 + column * 4;
          const auto value = std::bit_cast<float>(U32(base, address));
          rex::memory::store_and_swap<uint32_t>(base + address,
              std::bit_cast<uint32_t>(value * scale));
        }
      rex::memory::store_and_swap<uint32_t>(base + transformable + kWorldX,
                                           std::bit_cast<uint32_t>(x));
      rex::memory::store_and_swap<uint32_t>(base + transformable + kWorldX + 8,
                                           std::bit_cast<uint32_t>(z));
      return true;
    }
  }
#endif
  if (PortraitHud() && !TouchAnchor(*anchor)) {
    PlacePortraitHud(base, transformable, *anchor);
    return true;
  }
  const double correction = double(anchor->side - InheritedSide(base, transformable)) * delta;
  if (correction == 0.0) return true;
  const uint32_t bits = U32(base, transformable + kWorldX);
  float authored;
  std::memcpy(&authored, &bits, sizeof(authored));
  if (!std::isfinite(authored)) return false;
  const float placed = float(double(authored) + correction);
  if (!std::isfinite(placed)) return false;
  uint32_t placed_bits;
  std::memcpy(&placed_bits, &placed, sizeof(placed_bits));
  rex::memory::store_and_swap<uint32_t>(base + transformable + kWorldX, placed_bits);
  if (REXCVAR_GET(hud_reanchor_probe)) {
    // Diagnostic counters affect logging only; placement keeps no instance state.
    static thread_local std::array<unsigned, kAnchors.size()> samples{};
    if (samples[size_t(anchor - kAnchors.data())]++ < 8) {
      REXLOG_INFO("hud_reanchor: '{}' world x {:.3f} -> {:.3f} during WorldXfm ({:08X})",
                  anchor->name, authored, placed, transformable);
    }
  }
  return true;
}

extern "C" REX_FUNC(sub_821F13D0) {
  const uint32_t transformable = ctx.r3.u32;
  const bool active = ActiveDelta() != 0.0 || TouchHudEnabled();
  std::array<uint8_t, 64> previous{};
  if (active) std::memcpy(previous.data(), base + transformable + 72, previous.size());
  __imp__sub_821F13D0(ctx, base);
  if (!rb_blitz::hooks::HudReanchorWorld(base, transformable)) return;
  // Children may already have clean world caches from loading. If the parent's
  // resolved world changed, invalidate those caches before any child consumes
  // them. Clear only this parent's dirty flag: its matrix is already resolved,
  // so a child's WorldXfm must not recursively resolve the parent again.
  bool changed = false;
  if (active) {
    // Transform has three floats in each 16-byte row; ignore padding bytes.
    for (size_t row = 0; row < 4; ++row) {
      changed |= std::memcmp(previous.data() + row * 16,
                             base + transformable + 72 + row * 16, 12) != 0;
    }
  }
  if (changed) {
    rex::CallFrame call(ctx);
    call.ctx.r3.u32 = transformable;
    sub_821F0300(call.ctx, base);
    base[transformable + 189] = 0;
  }
}

extern "C" REX_FUNC(sub_821F65C8) {
  PrepareHudDraw(ctx, base);
  __imp__sub_821F65C8(ctx, base);
}

extern "C" REX_FUNC(sub_82226CD8) {
  const auto header = ObjectBase(base, ctx.r3.u32);
  if (header && U32(base, header) == 0x82024C4C) {
    const auto name = U32(base, header + 32);
    if (NameIs(base, name, "powerup.grp"))
      rb_blitz::input::CaptureLanePowerup(ctx, base, U32(base, header + 36));
#if defined(RB_BLITZ_IOS) || defined(RB_BLITZ_TOUCH_HUD)
    if (NameIs(base, name, "touch_pause.grp") && InHud(base, header) &&
        !rb_blitz::input::touch::ControlsVisible()) return;
#endif
  }
  PrepareHudDraw(ctx, base);
  __imp__sub_82226CD8(ctx, base);
}

extern "C" REX_FUNC(sub_823FF258) {
  // Observe the live provider write used by the native power-up manager
  // (8239139C), including thresholds below a full energy meter. No heap scan,
  // guest side call, or saved pointer survives a song unload.
  const uint32_t object = ctx.r3.u32, symbol = ctx.r4.u32, value = ctx.r5.u32;
  const bool readiness = HeapRange(object, 40) && HeapRange(value, 8) &&
      NameIs(base, symbol, "powerup_can_deploy") &&
      NameIs(base, U32(base, object + 32), "powerups_state.ep") &&
      (U32(base, value + 4) & 0x1F) == 0;
  const bool ready = readiness && U32(base, value) != 0;
  __imp__sub_823FF258(ctx, base);
  if (readiness) rb_blitz::input::touch::PublishPowerupReady(ready);
}
