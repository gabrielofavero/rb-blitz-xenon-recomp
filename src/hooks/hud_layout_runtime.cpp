// SPDX-License-Identifier: GPL-2.0-only
// Apply the HUD edge rule while WorldXfm is resolving a live transform, before
// its caller consumes the result. See docs/settings/graphics.md part 3 §9.2.7.
#include "hooks/hud_layout_runtime.h"

#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

#include <windows.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "hooks/hud_layout.h"
#include "hooks/render_settings.h"

REX_EXTERN(__imp__sub_821F13D0);  // RndTransformable::WorldXfm
REX_EXTERN(sub_821F13D0);
REX_EXTERN(sub_821F0300);        // SetWorldDirty, propagates to transform children
REX_EXTERN(__imp__sub_821F65C8); // RndDir::DrawShowing (RndDrawable view)
REX_EXTERN(__imp__sub_82226CD8); // RndGroup child draw traversal (RndDrawable view)

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
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(base + address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
      base + address + sizeof(path) >
          static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize) return false;
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
};
constexpr std::array<Anchor, 6> kAnchors{{
    {"star_board",          0x8201C784, -0x114,  1},
    {"band_meter_hud",      0x8201C784, -0x114, -1},
    {"letter_collect_hud",  0x8201C784, -0x114, -1},
    {"remedial_help.grp",   0x82024C4C, -0x108, -1},
    {"dynamic_leaderboard", 0x82018EA4, -0x1B4,  1},
    {"score.lbl",           0x8203A95C,  0x07C,  1},
}};

const Anchor* AnchorFor(const uint8_t* base, uint32_t transformable) {
  const uint32_t header = ObjectBase(base, transformable);
  if (!header) return nullptr;
  const uint32_t vtable = U32(base, header);
  for (const auto& anchor : kAnchors) {
    if (vtable != anchor.vtable ||
        int64_t(header) + anchor.transform_offset != transformable) continue;
    if (NameIs(base, U32(base, header + 32), anchor.name) && InHud(base, header)) {
      return &anchor;
    }
  }
  return nullptr;
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
      rb_blitz::settings::BootRenderWidth(), rb_blitz::settings::BootRenderHeight()) *
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
  // These bits affect logging only; no instance or placement state survives.
  static thread_local std::array<bool, kAnchors.size()> reported{};
  bool& seen = reported[size_t(&anchor - kAnchors.data())];
  const uint32_t before = seen ? 0 : U32(base, transformable + kWorldX);
  rex::CallFrame call(ctx);
  call.ctx.r3.u32 = transformable;
  sub_821F13D0(call.ctx, base);
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
// Only the live parent chain is visited: no object search or persistent cache.
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
  if (ActiveDelta() == 0.0) return;
  const uint32_t header = ObjectBase(base, ctx.r3.u32);
  if (!header) return;
  std::array<uint32_t, kMaxParents> owners{};
  size_t count = 0;
  uint32_t dir = U32(base, header + 36);
  while (HeapRange(dir, 8) && count < owners.size()) {
    const uint32_t object = ObjectBase(base, dir);
    if (!object) break;
    owners[count++] = TransformForHeader(base, object);
    const uint32_t parent = U32(base, object + 36);
    if (parent == dir) break;
    dir = parent;
  }
  while (count != 0) {
    const uint32_t transformable = owners[--count];
    if (transformable && AnchorFor(base, transformable)) {
      PrepareTransformAncestry(ctx, base, transformable);
    }
  }
  PrepareTransformAncestry(ctx, base, TransformForHeader(base, header));
}
}  // namespace

bool rb_blitz::hooks::HudReanchorWorld(uint8_t* base, uint32_t transformable) {
  const double delta = ActiveDelta();
  if (delta == 0.0) return false;
  const Anchor* anchor = AnchorFor(base, transformable);
  if (!anchor) return false;
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
  const bool active = ActiveDelta() != 0.0;
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
  PrepareHudDraw(ctx, base);
  __imp__sub_82226CD8(ctx, base);
}
