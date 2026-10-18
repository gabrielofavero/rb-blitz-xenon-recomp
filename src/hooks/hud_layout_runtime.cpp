// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Re-anchoring the in-song HUD on a frame that is not 16:9 (docs/settings/graphics.md
// part 3 §9, the owner's rule). The arithmetic and its tests live in hooks/hud_layout.h;
// this file is where the rule reaches a real HUD element.
//
// The first seam: RndTransformable::Handle (0x821F44F8) is the guest's own script handler
// for `set_local_pos`, `set_local_pos_index`, `set_trans_parent`, ... The Ultimate payload
// positions its HUD groups through `set_local_pos`, e.g.
//
//     {star_board set_local_pos 220 0 143.85}
//     {band_meter_hud set_local_pos -200.54 25 4.35}
//
// (read out of ulti/gen/ulti_game_enter.dtb, whose 4:3 branch repositions exactly these
// four top-level groups). The handler is entered with the Hmx::Object subobject in r4 and
// the DataArray message in r5; it calls OnSetLocalPos with the RndTransformable subobject,
// which is 196 bytes before the Hmx::Object header in this image, and OnSetLocalPos writes
// the local translation at transformable+56 (measured from 0x821F1678):
//
//     56: v.x   60: v.y   64: v.z
//
// So after the original handler has written the authored position we can move x by the
// rule and leave y/z alone. The dir chain check (`hud` in the object's mDir names) keeps
// this off menus and world objects, and a 16:9 boot (BootRenderWidth() == 0) returns before
// touching anything at all - the arithmetic is the identity there too.
//
// Measured limit (2026-10-17): the stock title does **not** go through this handler for
// its own HUD. A 2560x1080 boot logged 200 handler calls (ui_enter, set, get, ...) and zero
// `set_local_pos`; the stock HUD is authored scene data. A second seam, the PropAnim
// keyframe evaluator, was patched in the indirect-call table and was also never entered in
// that boot. The stock HUD is therefore targeted by `HudReanchorPresent`, a per-present
// scene pass that finds the top-level groups by name and requires their dir chain to reach
// the HUD scene. A live 2560x1080 song (2026-10-17) plays safely with the pass on, but every
// name match there is a string-table/DTA record (`dir=''`), so the HUD scene's real dir
// name is still the open measurement (docs/settings/graphics.md §9.4).
//
// The HUD is laid out in a 360-unit-tall screen space (half-height 180 units): the 16:9
// anchors above (star_board +220, dynamic_leaderboard +295) land where the 16:9 captures
// put them at about 3 px per unit on a 1080p frame. That scale is an inference from the
// anchors and the capture, not a measured camera constant, which is why
// `hud_reanchor_hud_half_height` is a cvar: the next boot can correct the one number.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <windows.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "hooks/hud_layout.h"
#include "hooks/hud_layout_runtime.h"
#include "hooks/render_settings.h"
#include "input/native_menu.h"

// The RndTransformable script handler and the transformable's translation offset inside
// the Hmx::Object subobject it is entered with (both measured in this image).
REX_EXTERN(__imp__sub_821F44F8);
// PropAnim::SetFrame (0x82237478, vtable slot 0x820269EC): tried as the stock-HUD seam;
// the probe did not enter it in the stock route, so it stays installed for the elements
// that do use it and for the next measurement.
REX_EXTERN(__imp__sub_82237478);
// The transformable's own dirty/update routine (0x821F0300): OnSetLocalPos calls it after
// writing the local translation, so every seam here must too or the draw uses the cached
// world transform.
REX_EXTERN(sub_821F0300);
extern "C" void sub_82237478(PPCContext& ctx, uint8_t* base);
namespace {
constexpr uint32_t kTransformableBeforeObject = 196;
constexpr uint32_t kPropAnimTarget = 28;
constexpr uint32_t kLocalTranslation = 56;  // v.x, then v.y at +4, v.z at +8
// The stock HUD groups (RndGroup/PanelDir) keep the local translation at +0x74 of the
// object: +0x74 v.x, +0x78 v.y, +0x7C v.z (measured 2026-10-17 from the live scene: the
// left group's x is -219.04, the star board's -37.24, the leaderboard's -295.66).
constexpr uint32_t kHudGroupTranslation = 0x74;
// RndTransformable: local translation v at +0x38, cached world translation at +0x78.
constexpr uint32_t kLocalTranslationOffset = 0x38;
constexpr uint32_t kWorldTranslationOffset = 0x78;
constexpr uint32_t kObjectNameOffset = 32;
constexpr uint32_t kObjectDirOffset = 36;
constexpr int kDirChainDepth = 8;
// The recompiled indirect-call table (REX_LOOKUP_FUNC): one host pointer per guest
// function address, indexed by (address - REX_CODE_BASE) * 2.
constexpr uint32_t kImageBase = 0x82000000;
constexpr uint32_t kImageSize = 0xA40000;
constexpr uint32_t kCodeBase = 0x82190000;
constexpr uint32_t kPropAnimSetFrame = 0x82237478;
}  // namespace

REXCVAR_DEFINE_BOOL(hud_reanchor, true, "Graphics",
                    "Re-anchor the in-song HUD on a non-16:9 frame. The elements are found "
                    "through the scene's own object hash (an `ObjectDir` lookup, ~0.2 ms once "
                    "per song) and placed by `hud_reanchor_apply`; the per-frame cost is a "
                    "handful of cached guest reads, 1-4 us (docs/settings/graphics.md part 3 "
                    "§9.2.5-9.2.7). Off means zero work: a 16:9 boot is untouched either "
                    "way.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(hud_reanchor_probe, false, "Diagnostics",
                    "Diagnostics: log the HUD scene dir, the container inventory with each "
                    "element's authored x, every element resolution and every applied shift, "
                    "plus the present hook's cost and per-element breakdown every 600 "
                    "frames. The evidence the re-anchor rule and the element table came "
                    "from.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(hud_reanchor_hud_half_height, 240.0, "Graphics",
                      "The HUD scene's half-height in its own units: 240, measured 2026-10-17 "
                      "against a capture (the left block authors x=-298.537 and sits 335.9 px "
                      "left of centre on a 540-line frame, i.e. 1.125 px per unit and 270 px "
                      "= 240 units). 2560x1080 therefore shifts by 142.2 HUD units = 160 "
                      "guest px = 320 px on a 1080p frame. 180 was the payload-derived guess "
                      "this replaces.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(hud_reanchor_apply, true, "Graphics",
                    "Place the in-song HUD elements on the side the scene authors them: the "
                    "band meter, the collectible-letter row and the lane-switch prompt travel "
                    "left, the score/star readout and the previous-run comparator travel "
                    "right, each by the frame's extra half-width in HUD units "
                    "(docs/settings/graphics.md part 3 §9.2.5). Centred elements and any "
                    "element whose class is not in the measured table are left alone.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(hud_reanchor_scan, false, "Diagnostics",
                    "Diagnostics ONLY since the object-hash lookup replaced it: sweep the heap "
                    "window around the live HUD scene and log every transformable-looking "
                    "record, to map a scene whose containers have not been read yet. "
                    "**Expensive**: each uncached page test is a heap query plus "
                    "`VirtualQuery` (~1.2 ms here), so a +-2 MiB sweep is seconds of frame "
                    "time (docs/settings/graphics.md part 3 §9.2.6). The placement never uses "
                    "it.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

uint32_t U32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}

// `GuestPtr` is only base+address arithmetic and says nothing about whether the page is
// committed; a probe that trusts it dereferences holes in the guest map. Ask the heap that
// owns the address whether the page is committed, and let the heap supply the host offset.
uint8_t* HostPtr(uint32_t address) {
  // The title's image and heap both live above 0x40000000; a smaller value read out of a
  // false candidate is an arithmetic artefact, not a guest pointer. Refusing it here also
  // keeps a malformed object record from pointing the probe at an unmapped low page.
  if (address < 0x40000000) {
    return nullptr;
  }
  auto* kernel = rex::system::kernel_state();
  auto* memory = kernel != nullptr ? kernel->memory() : nullptr;
  auto* heap = memory != nullptr ? memory->LookupHeap(address) : nullptr;
  if (heap == nullptr) {
    return nullptr;
  }
  rex::memory::HeapAllocationInfo heap_info{};
  if (!heap->QueryRegionInfo(address, &heap_info)) {
    return nullptr;
  }
  if ((heap_info.state & rex::memory::kMemoryAllocationCommit) == 0) {
    return nullptr;
  }
  uint8_t* host = memory->TranslateVirtual<uint8_t*>(address);
  // The guest page table can mark a page committed while the host allocation behind it is
  // still a placeholder; only VirtualQuery says whether dereferencing the host address is
  // safe. The probe must not fault the process while looking at a malformed object record.
  MEMORY_BASIC_INFORMATION host_info{};
  if (host == nullptr || ::VirtualQuery(host, &host_info, sizeof(host_info)) == 0 ||
      host_info.State != MEM_COMMIT ||
      (host_info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
    return nullptr;
  }
  return host;
}

bool Mapped(uint8_t* base, uint32_t address) {
  (void)base;
  return HostPtr(address) != nullptr;
}

float F32(uint8_t* base, uint32_t address) {
  const uint32_t bits = U32(base, address);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void StoreF32(uint8_t* base, uint32_t address, float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  rex::memory::store_and_swap<uint32_t>(base + address, bits);
}

// The local translation write is not visible to the renderer until the transformable's
// dirty pass runs; OnSetLocalPos does exactly this call after its write (0x821F1678).
void MarkTransformDirty(PPCContext& ctx, uint8_t* base, uint32_t transformable) {
  rex::CallFrame call(ctx);
  call.ctx.r3.u32 = transformable;
  sub_821F0300(call.ctx, base);
}

// A derived object's Hmx::Object subobject. The engine stores an RTTI-style table pointer
// at object+4 whose +4 word is the adjustment to the Hmx::Object base (the same conversion
// native_menu.cpp applies to a screen's panel). An `ObjectDir*` read out of `mDir` is a
// DERIVED pointer: reading +32/+36 on it directly reads the wrong fields, which is why the
// first live song run saw every real HUD element as `dir=''`.
uint32_t ObjectBase(uint8_t* base, uint32_t object) {
  if (object == 0 || !Mapped(base, object + 4)) {
    return 0;
  }
  const uint32_t table = U32(base, object + 4);
  if (table == 0 || !Mapped(base, table + 4)) {
    return 0;
  }
  const int32_t adjust = int32_t(U32(base, table + 4));
  const uint32_t subobject = object + 4 + uint32_t(adjust);
  return Mapped(base, subobject) ? subobject : 0;
}

// A guest object's name: Hmx::Object header +32 in this image (the same field the native
// menu and help observer read). Empty when the pointer is not a mapped, printable name.
// `object` must already be the Hmx::Object subobject (use ObjectBase for derived pointers).
std::string ObjectName(uint8_t* base, uint32_t object) {
  if (object == 0 || !Mapped(base, object + kObjectNameOffset)) {
    return {};
  }
  const uint32_t name = U32(base, object + kObjectNameOffset);
  if (name == 0) {
    return {};
  }
  const char* text = reinterpret_cast<const char*>(HostPtr(name));
  if (text == nullptr) {
    return {};
  }
  std::string out;
  for (size_t i = 0; i < 96; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == 0) {
      break;
    }
    if (c < 0x20 || c > 0x7E) {
      return {};
    }
    out.push_back(static_cast<char>(c));
  }
  return out;
}

// Defined with the scene scan below (it needs the page-commit cache). Every page test in this
// file goes through it: `Mapped` is `HostPtr`, which ends in `VirtualQuery` and measures
// ~1.2 ms on this machine, so it must never run per frame or per candidate.
bool PageCommitted(uint32_t address);
bool IsHudMiloDir(uint8_t* base, uint32_t dir);

// True when the object's directory, or one of its parents, is the scene loaded from
// `track/hud.milo`. The dir is NOT named "hud" in this title: a live song's HUD objects sit
// in a `PanelDir` whose +0x64/+0x6C fields point at that path (measured 2026-10-17, see
// docs/settings/graphics.md §9.2.4). The old literal-"hud" test rejected every real element.
bool InHudDir(uint8_t* base, uint32_t object) {
  if (!Mapped(base, object) || !Mapped(base, object + kObjectDirOffset)) {
    return false;
  }
  return IsHudMiloDir(base, U32(base, object + kObjectDirOffset));
}

// The object's dir chain, outermost last, for the probe line. A wrong dir offset shows up as
// an empty or non-scene chain here rather than as a silent no-op.
std::string DirChain(uint8_t* base, uint32_t object) {
  std::string out;
  if (!Mapped(base, object) || !Mapped(base, object + kObjectDirOffset)) {
    return out;
  }
  uint32_t dir = U32(base, object + kObjectDirOffset);
  for (int depth = 0; depth < 4; ++depth) {
    const uint32_t subobject = ObjectBase(base, dir);
    if (subobject == 0 || !Mapped(base, subobject + kObjectDirOffset)) {
      break;
    }
    const std::string name = ObjectName(base, subobject);
    if (name.empty()) {
      break;
    }
    if (!out.empty()) {
      out += "/";
    }
    out += name;
    const uint32_t parent = U32(base, subobject + kObjectDirOffset);
    if (parent == dir || parent == 0) {
      break;
    }
    dir = parent;
  }
  return out;
}

// The message's own node 1 is its symbol (`_msg->Sym(1)` in the engine's handlers).
std::string MessageSymbol(uint8_t* base, uint32_t message) {
  if (message == 0 || !Mapped(base, message)) {
    return {};
  }
  const uint32_t nodes = U32(base, message);
  if (nodes == 0 || !Mapped(base, nodes + 8)) {
    return {};
  }
  const uint32_t symbol = U32(base, nodes + 8);
  if (symbol == 0) {
    return {};
  }
  const char* text = reinterpret_cast<const char*>(HostPtr(symbol));
  if (text == nullptr) {
    return {};
  }
  std::string out;
  for (size_t i = 0; i < 96; ++i) {
    const char c = text[i];
    if (c == 0) {
      break;
    }
    if (c < 0x20 || c > 0x7E) {
      return {};
    }
    out.push_back(c);
  }
  return out;
}

// One log line per distinct position per object, so a repeated animation does not flood
// the log while a layout change is still printed in full.
struct LastPosition {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  bool valid = false;
};
std::array<LastPosition, 256> g_last_positions{};

LastPosition* PositionSlot(uint32_t object) {
  return &g_last_positions[(object >> 4) % g_last_positions.size()];
}

// Apply the owner's rule to one transformable, and say what was seen when the probe is on.
// `object` is the Hmx::Object subobject (the name/dir source); `transformable` is the
// RndTransformable whose local translation lives at +56. Both seams call this after the
// original writer has produced the authored position, so the shift is always applied to
// the authored value, never compounded onto an earlier application.
void ApplyHudRule(PPCContext& ctx, uint8_t* base, uint32_t object, uint32_t transformable,
                  const char* seam) {
  if (!Mapped(base, object) || !Mapped(base, transformable) ||
      !Mapped(base, transformable + kLocalTranslation + 8)) {
    return;
  }
  const float authored_x = F32(base, transformable + kLocalTranslation);
  const float authored_y = F32(base, transformable + kLocalTranslation + 4);
  const float authored_z = F32(base, transformable + kLocalTranslation + 8);

  const bool probe = REXCVAR_GET(hud_reanchor_probe);
  const bool hud = InHudDir(base, object);
  if (probe) {
    LastPosition* last = PositionSlot(transformable);
    if (!last->valid || last->x != authored_x || last->y != authored_y ||
        last->z != authored_z) {
      last->valid = true;
      last->x = authored_x;
      last->y = authored_y;
      last->z = authored_z;
      REXLOG_INFO("hud_reanchor: '{}' dir='{}' hud={} {} {:.3f} {:.3f} {:.3f}",
                  ObjectName(base, object), DirChain(base, object), hud, seam, authored_x,
                  authored_y, authored_z);
    }
  }

  if (!REXCVAR_GET(hud_reanchor) || !hud) {
    return;
  }
  const uint32_t width = rb_blitz::settings::BootRenderWidth();
  const uint32_t height = rb_blitz::settings::BootRenderHeight();
  // The title's own 16:9 frame (and every unforced boot) is the identity by construction;
  // this early return is the safeguard the owner asked for, not a second rule.
  if (width == 0 || height == 0) {
    return;
  }
  const double layout_units = rb_blitz::settings::HudShiftUnits(width, height);
  if (layout_units == 0.0) {
    return;
  }
  const double hud_units = layout_units * double(REXCVAR_GET(hud_reanchor_hud_half_height));
  // ShiftHudX is the same signed move the tested policy uses, including the "stop at the
  // centre" limit on a narrower frame; the epsilon is in HUD units (~1 px at 720p).
  const double moved = rb_blitz::settings::ShiftHudX(double(authored_x), hud_units, 0.5);
  if (moved == double(authored_x)) {
    return;
  }
  StoreF32(base, transformable + kLocalTranslation, float(moved));
  MarkTransformDirty(ctx, base, transformable);
  if (probe) {
    REXLOG_INFO("hud_reanchor: '{}' {:.3f} -> {:.3f} ({} delta {:.3f} HUD units)",
                ObjectName(base, object), authored_x, float(moved), seam, hud_units);
  }
}

// Read-only diagnostic: find the loaded ObjectDir named "hud" and print its object list.
// The stock HUD is placed by the scene's authored data, not by `set_local_pos`; this is the
// measurement that says where a post-load pass would have to write. **Not called any more**:
// it walks the whole heap and stalled the guest, which `hud_reanchor_scan`'s bounded scene
// window replaced. Kept because its field map is cited by the documentation.
[[maybe_unused]] void ScanHudDir(uint8_t* base) {
  for (uint32_t page = 0x40000000; page < 0x4F000000; page += 0x1000) {
    if (!Mapped(base, page)) {
      continue;
    }
    for (uint32_t a = page; a < page + 0x1000; a += 4) {
      for (uint32_t header : {a, a + kTransformableBeforeObject}) {
      if (!Mapped(base, header + kObjectNameOffset)) {
        continue;
      }
      const uint32_t name = U32(base, header + kObjectNameOffset);
      if (!Mapped(base, name)) {
        continue;
      }
      const char* text = reinterpret_cast<const char*>(HostPtr(name));
      if (text == nullptr) {
        continue;
      }
      // The HUD elements are ordinary transformables; find one by name, then read its
      // mDir to reach the scene's ObjectDir. These are the names the authored scene and
      // the Ultimate payload use for the anchored groups.
      const bool is_element =
          std::strncmp(text, "star_board", 10) == 0 ||
          std::strncmp(text, "band_meter_hud", 14) == 0 ||
          std::strncmp(text, "dynamic_leaderboard", 19) == 0 ||
          std::strncmp(text, "score", 5) == 0 ||
          std::strncmp(text, "streak_hud", 10) == 0 ||
          std::strncmp(text, "track_switch_hud", 16) == 0 ||
          std::strncmp(text, "letter_collect_hud", 18) == 0 ||
          std::strncmp(text, "blitz_meter", 11) == 0 ||
          std::strncmp(text, "hud_numbers_3d", 14) == 0;
      if (is_element) {
        const uint32_t own_vtable = U32(base, a);
        const bool own_code = own_vtable >= 0x82000000 && own_vtable < 0x82900000;
        const bool object_page = Mapped(base, a + kTransformableBeforeObject);
        const uint32_t object_vtable =
            object_page ? U32(base, a + kTransformableBeforeObject) : 0;
        const bool object_code = object_vtable >= 0x82000000 && object_vtable < 0x82900000;
        if (!own_code && !object_code) {
          continue;
        }
        const uint32_t dir = Mapped(base, header + kObjectDirOffset)
                                 ? U32(base, header + kObjectDirOffset)
                                 : 0;
        const uint32_t dir_subobject = ObjectBase(base, dir);
        const std::string dir_name =
            dir_subobject != 0 ? ObjectName(base, dir_subobject) : std::string{};
        const float x = F32(base, a + kLocalTranslation);
        const float y = F32(base, a + kLocalTranslation + 4);
        const float z = F32(base, a + kLocalTranslation + 8);
        REXLOG_INFO("hud_reanchor: scan element {:08X} header {:08X} '{}' dir={:08X} '{}' "
                    "xfm {:.3f} {:.3f} {:.3f}",
                    a, header, ObjectName(base, header), dir, dir_name, x, y, z);
        continue;
      }
      if (text[0] != 'h' || text[1] != 'u' || text[2] != 'd' || text[3] != 0) {
        continue;
      }
      // A real object has a vtable in the image at its own start or at the virtual
      // Hmx::Object base 196 bytes later; a symbol-table record with a "hud" pointer at
      // +32 has neither, and must not be walked as an ObjectDir.
      const uint32_t own_vtable = U32(base, a);
      const bool object_page = Mapped(base, a + kTransformableBeforeObject);
      const uint32_t object_vtable =
          object_page ? U32(base, a + kTransformableBeforeObject) : 0;
      const bool own_code = own_vtable >= 0x82000000 && own_vtable < 0x82900000;
      const bool object_code = object_vtable >= 0x82000000 && object_vtable < 0x82900000;
      if (!own_code && !object_code) {
        continue;
      }
      REXLOG_INFO("hud_reanchor: scan candidate {:08X} header {:08X} name 'hud'", a, header);
      static bool detailed = false;
      if (!detailed) {
        detailed = true;
        REXLOG_INFO("hud_reanchor: scan candidate {:08X} vtable={:08X} header_vtable={:08X}", a,
                    U32(base, a), U32(base, a + kTransformableBeforeObject));
        for (uint32_t off = 0; off < 0x100; off += 4) {
          const uint32_t word = U32(base, a + off);
          if (word >= 0x40000000 && word < 0x50000000 && Mapped(base, word)) {
            const std::string named = ObjectName(base, word);
            REXLOG_INFO("hud_reanchor: scan candidate +{:03X} = {:08X} '{}'", off, word, named);
          }
        }
      }
      const uint32_t begin = U32(base, a + 124);
      const uint32_t end = U32(base, a + 128);
      if (!Mapped(base, begin) || !Mapped(base, end) || end < begin || end - begin > 0x4000) {
        REXLOG_INFO("hud_reanchor: scan candidate {:08X} has no object vector", a);
        continue;
      }
      REXLOG_INFO("hud_reanchor: scan candidate {:08X} objects {:08X}..{:08X}", a, begin, end);
      for (uint32_t slot = begin; slot < end; slot += 4) {
        const uint32_t object = U32(base, slot);
        if (!Mapped(base, object)) {
          continue;
        }
        const uint32_t transformable = object - kTransformableBeforeObject;
        const bool mapped = Mapped(base, transformable);
        const float x = mapped ? F32(base, transformable + kLocalTranslation) : 0.0f;
        const float y = mapped ? F32(base, transformable + kLocalTranslation + 4) : 0.0f;
        const float z = mapped ? F32(base, transformable + kLocalTranslation + 8) : 0.0f;
        REXLOG_INFO("hud_reanchor: scan obj {:08X} '{}' dir='{}' mapped={} {:.3f} {:.3f} {:.3f}",
                    object, ObjectName(base, object), DirChain(base, object), mapped, x, y, z);
      }
      }
    }
  }
}

// The stock HUD's top-level anchored groups, read from the same payload anchors that the
// script seam uses. Each is a subdir of the HUD scene; moving its local x moves the whole
// group (gauge, labels, markers) and leaves the centred elements where they are.
//
// A song can load the HUD scene more than once (the tutorial screen owns one before the song
// starts), so a name maps to SEVERAL records and every one of them has to be moved: the first
// live song of 2026-10-17 placed the tutorial-era `band_meter_hud` only, and the song then
// drew its own copy at the old position (`hud-xfm6.log`).
constexpr int kMaxInstances = 4;

struct HudTarget {
  const char* name;
  // The direction the visible element must move on a wider frame: +1 right, -1 left, 0 a
  // centred element. The group's own x is not the element's side (the star board sits near
  // the origin but its art is at the right), so the side is the measured layout, not a sign.
  int shift_sign = 0;
  // The milo loader gives a subdir whose name already exists a numeric suffix; the `1`
  // spellings are alternates, not separate elements, so a song that only loads the authored
  // names has still found the HUD.
  bool required = true;
  // Whether the shift is applied. Only elements whose placement has been verified against a
  // capture are on: a write that moves data but not the visible art is worse than no write,
  // and one that lands on the wrong field can fault the guest (2026-10-17).
  bool place = false;
  std::array<uint32_t, kMaxInstances> headers{};
  // The vtable and name pointer recorded at match time. Validation compares these two words,
  // so the per-frame path never asks the heap API anything — a `HostPtr` (heap lookup +
  // `VirtualQuery`) per instance per frame is what made the first sliced sweep cost 8 s
  // (`hud-sweep1.log`).
  std::array<uint32_t, kMaxInstances> vtables{};
  std::array<uint32_t, kMaxInstances> name_ptrs{};
  std::array<uint32_t, kMaxInstances> transformables{};
  std::array<int32_t, kMaxInstances> transform_offsets{};
  std::array<float, kMaxInstances> last_written{};
  std::array<bool, kMaxInstances> has_last{};
  int instances = 0;
};

std::array<HudTarget, 11> g_targets{{
    // Every element the owner asked for is placed: the left cluster and the lane-switch icon
    // travel left, the score/star readout and the previous-run comparator travel right, and a
    // centred element (sign 0) would not be touched at all.
    {"star_board", 1, true, true},
    {"star_board1", 1, false, false},
    {"band_meter_hud", -1, true, true},
    {"band_meter_hud1", -1, false, false},
    // The collectible-letter row shares the band meter's side: the scene authors it at
    // x -294.108 against the meter's -298.537 (inventory dump, `hud-inv2.log`), so it is the
    // other far-left element and needs the same shift.
    {"letter_collect_hud", -1, true, true},
    {"letter_collect_hud1", -1, false, false},
    // The inactivity prompt that tells the player to switch lanes (`help_lane_switch.anim`,
    // `help_arrow_l/r.mat`, `help_bg.mesh`) is the `remedial_help.grp` group, authored at
    // x -289.338 against the band meter's -298.537 — the other far-left element.
    {"remedial_help.grp", -1, true, true},
    // The comparator against the previous run sits on the right of the track, like the score
    // and star readouts; the payload authors it at +295, the same side as the star board.
    {"dynamic_leaderboard", 1, true, true},
    {"dynamic_leaderboard1", 1, false, false},
    {"score.lbl", 1, true, true},
    {"score.lbl1", 1, false, false},
}};

bool NameIs(const char* text, const char* name) {
  const size_t length = std::strlen(name);
  return std::strncmp(text, name, length) == 0 && text[length] == '\0';
}

// Defined with the scene scan below (it needs the page-commit bitmap). Every page test in
// this file goes through it: `Mapped` is `HostPtr`, which ends in `VirtualQuery` and measures
// ~1.2 ms on this machine, so it must never run per frame or per candidate.
bool PageCommitted(uint32_t address);

// Defined with the scene scan below (it needs the page-commit bitmap). Every page test in
// this file goes through it: `Mapped` is `HostPtr`, which ends in `VirtualQuery` and measures
// ~1.2 ms on this machine, so it must never run per frame or per candidate.
bool PageCommitted(uint32_t address);

// A real scene object, as opposed to the scene's string table or a DTA script object that
// also mentions the name: the object has a code vtable in the image, its group translation
// is mapped, and its Hmx::Object mDir resolves. The dir must additionally reach the
// `track/hud.milo` scene (InHudDir).
bool ObjectShape(uint8_t* base, uint32_t header) {
  if (!PageCommitted(header) || !PageCommitted(header + kHudGroupTranslation + 8) ||
      !PageCommitted(header + kObjectDirOffset)) {
    return false;
  }
  const uint32_t vtable = U32(base, header);
  if (vtable < 0x82000000 || vtable >= 0x82A40000) {
    return false;
  }
  const uint32_t dir = U32(base, header + kObjectDirOffset);
  return dir != 0 && ObjectBase(base, dir) != 0;
}

// sub_821F0300 sets the dirty byte at +189 and then walks the child list whose sentinel is
// at +156 (each node's +8 is the child transformable, its +0 the next node). A record that
// is not really a transformable holds an arbitrary word there, and the walk then faults
// (measured 2026-10-17: `read of guest 0xBD` on a false candidate, `read of guest 0x240` on
// a real group whose +156 is not that list). Only call it when the list looks like the
// engine's own: the sentinel itself, or a mapped node whose child is mapped.
//
// Page checks go through `PageCommitted`, never `Mapped`: `Mapped` is `HostPtr`, which does
// a heap lookup plus `VirtualQuery` and measures ~1.2 ms on this machine (4.08 s for the
// 3 369 pages the old sweep touched). Twelve of those per frame already cost 16 ms a frame
// with nothing written — the per-frame lag the owner hit (`hud-phase1/2.log`).
bool ChildListSafe(uint8_t* base, uint32_t transformable) {
  const uint32_t sentinel = transformable + 156;
  if (!PageCommitted(sentinel)) {
    return false;
  }
  const uint32_t first = U32(base, sentinel);
  if (first == sentinel) {
    return true;
  }
  if (!PageCommitted(first + 8)) {
    return false;
  }
  const uint32_t child = U32(base, first + 8);
  return PageCommitted(child) && PageCommitted(child + 189);
}

bool InstanceValid(uint8_t* base, const HudTarget& target, int instance) {
  const uint32_t header = target.headers[instance];
  if (header == 0 || !PageCommitted(header) || !PageCommitted(header + kObjectNameOffset)) {
    return false;
  }
  // Two word compares against what was recorded when the record was found. The page is known
  // committed, so this cannot fault; a freed object fails the vtable or the name pointer and
  // the caller re-scans instead of writing.
  return U32(base, header) == target.vtables[instance] &&
         U32(base, header + kObjectNameOffset) == target.name_ptrs[instance];
}

// A target is present while at least one of its recorded instances still validates.
bool TargetPresent(uint8_t* base, const HudTarget& target) {
  for (int i = 0; i < target.instances; ++i) {
    if (InstanceValid(base, target, i)) {
      return true;
    }
  }
  return false;
}

bool TargetsMissing(uint8_t* base) {
  for (const auto& target : g_targets) {
    if (target.required && !TargetPresent(base, target)) {
      return true;
    }
  }
  return false;
}

// The scene pass finds the live HUD dir first (a few hundred bytes of fields), then scans
// only the scene's own heap window for the group names. The first implementation walked the
// whole 1 GiB heap and asked the heap API about EVERY 4-byte word; one pass took minutes and
// stalled the present thread (the 2026-10-17 song-start freeze, audio looping its buffer).
// The window below is swept in slices and only on the live `game_screen`.
constexpr uint32_t kHeapBegin = 0x40000000;
constexpr uint32_t kHeapEnd = 0x7F000000;  // the 64 KiB-page virtual heap
constexpr size_t kHeapPageCount = ((kHeapEnd - kHeapBegin) >> 12);

// What is known about a heap page: 0 = unvisited, 1 = committed, 2 = not committed. A page
// is queried once; every later candidate name pointer costs one array read. The virtual
// heap (0x40000000..0x7EFFFFFF) is mapped with host offset 0 (xmemory.cpp), so base+address
// is the candidate's host address once the page is known committed.
std::array<uint8_t, kHeapPageCount> g_page_state{};
uint64_t g_page_queries = 0;  // pages that needed a heap query (HostPtr)

bool PageCommitted(uint32_t address) {
  if (address < kHeapBegin || address >= kHeapEnd) {
    return false;
  }
  uint8_t& state = g_page_state[(address - kHeapBegin) >> 12];
  if (state == 0) {
    ++g_page_queries;
    state = HostPtr(address & ~uint32_t(0xFFF)) != nullptr ? 1 : 2;
  }
  return state == 1;
}

// A NUL-terminated string at `address` equals `expected`, page-checked end to end.
bool StringIs(uint8_t* base, uint32_t address, const char* expected) {
  const size_t length = std::strlen(expected);
  if (!PageCommitted(address) || (address & 0xFFFu) > 0x1000 - (length + 1)) {
    return false;
  }
  return std::memcmp(base + address, expected, length + 1) == 0;
}

// The HUD scene's dir carries the milo's own path at +0x64/+0x6C (`track/hud.milo`). That is
// the one stable, unique signature found in a live 2560x1080 song: the object is a
// `PanelDir`, and no other scene dir in the game carries this path. Walk mDir parents so a
// subdir of the HUD scene also passes. The path fields belong to the DERIVED dir object;
// both it and its Hmx::Object subobject are checked because the two differ by the virtual
// base adjustment.
bool HasHudPath(uint8_t* base, uint32_t object) {
  if (object == 0 || !Mapped(base, object + 0x6C)) {
    return false;
  }
  if (((object + 0x6C) & 0xFFFu) > 0x1000 - 4) {
    return false;
  }
  return StringIs(base, U32(base, object + 0x64), "track/hud.milo") ||
         StringIs(base, U32(base, object + 0x6C), "track/hud.milo");
}

bool IsHudMiloDir(uint8_t* base, uint32_t dir) {
  uint32_t current = dir;
  for (int depth = 0; depth < kDirChainDepth; ++depth) {
    const uint32_t subobject = ObjectBase(base, current);
    if (HasHudPath(base, current) || (subobject != 0 && HasHudPath(base, subobject))) {
      return true;
    }
    uint32_t parent = 0;
    if (Mapped(base, current + kObjectDirOffset)) {
      parent = U32(base, current + kObjectDirOffset);
    }
    if (parent == 0 && subobject != 0 && Mapped(base, subobject + kObjectDirOffset)) {
      parent = U32(base, subobject + kObjectDirOffset);
    }
    if (parent == 0 || parent == current) {
      break;
    }
    current = parent;
  }
  return false;
}

// The live HUD scene dir. Path measured 2026-10-17: the focused `game_screen` holds the
// `game_panel` at +0x34, the panel holds the game `ObjectDir` at +0x34, and one of that
// dir's fields is the `PanelDir` loaded from `track/hud.milo`. Bounded to that small field
// range; no heap walk.
uint32_t HudDirNow(uint8_t* base) {
  const uint32_t screen = rb_blitz::input::CurrentScreen();
  if (screen == 0 || !Mapped(base, screen + 0x38)) {
    return 0;
  }
  const uint32_t panel = U32(base, screen + 0x34);
  if (panel == 0 || !Mapped(base, panel + 0x38)) {
    return 0;
  }
  const uint32_t game_dir = U32(base, panel + 0x34);
  if (game_dir == 0 || !Mapped(base, game_dir)) {
    return 0;
  }
  for (uint32_t off = 0; off + 4 <= 0x300; off += 4) {
    const uint32_t word = U32(base, game_dir + off);
    if (word < kHeapBegin || word >= kHeapEnd) {
      continue;
    }
    if (IsHudMiloDir(base, word)) {
      return word;
    }
  }
  if (REXCVAR_GET(hud_reanchor_probe)) {
    REXLOG_INFO("hud_reanchor: no track/hud.milo dir under game dir {:08X} (panel {:08X})",
                game_dir, panel);
  }
  return 0;
}

// ---- The precise element lookup -----------------------------------------------------------
// The engine keeps an `ObjectDir`'s own children in a hash table (`mEntries` at dir + 0x08,
// `mSize` at +0x0C) of 8-byte `{name, object}` entries, and its child dirs in a vector at
// `+0x50` whose 0x14-byte elements hold the child `ObjectDir*` at +0x0C. Both layouts come
// from `ObjectDir::FindObject` (guest 0x82402DE8) and `ObjectDir::FindEntry` (0x824028C0) —
// `generated/default/rb_blitz_recomp.100.cpp:14795` and `rb_blitz_recomp.69.cpp:14669`.
//
// Walking those containers reads a few hundred words instead of sweeping megabytes of the
// guest arena, which measures ~2 us per 4-byte word on this machine (graphics.md §9.2.6).
// That sweep is why the earlier builds lagged and why they found nothing in a heap laid out
// differently, so this lookup replaces it entirely.
constexpr uint32_t kDirEntriesOffset = 0x08;
constexpr uint32_t kDirSizeOffset = 0x0C;
constexpr uint32_t kDirSubDirsOffset = 0x50;
constexpr uint32_t kDirSubDirsEndOffset = 0x54;
constexpr uint32_t kSubDirStride = 0x14;
constexpr uint32_t kSubDirObjectOffset = 0x0C;
constexpr int kDirSearchDepth = 3;

uint32_t FindNamedInDir(uint8_t* base, uint32_t dir, const char* name, int depth);
uint32_t AsNamedObject(uint8_t* base, uint32_t object, const char* name);

uint32_t FindNamedInDir(uint8_t* base, uint32_t dir, const char* name, int depth) {
  if (dir == 0 || depth > kDirSearchDepth || !PageCommitted(dir + kDirSubDirsEndOffset + 4)) {
    return 0;
  }
  // A child dir (the HUD elements are subdirs) is matched by its own name.
  if (depth > 0) {
    const uint32_t self = AsNamedObject(base, dir, name);
    if (self != 0) {
      return self;
    }
  }
  const uint32_t entries = U32(base, dir + kDirEntriesOffset);
  const int32_t size = int32_t(U32(base, dir + kDirSizeOffset));
  if (size > 0 && size <= 8192 && entries != 0 && PageCommitted(entries) &&
      PageCommitted(entries + uint32_t(size) * 8 - 4)) {
    for (int32_t i = 0; i < size; ++i) {
      const uint32_t entry = entries + uint32_t(i) * 8;
      const uint32_t object = U32(base, entry + 4);
      if (object == 0) {
        continue;
      }
      const uint32_t entry_name = U32(base, entry);
      if (entry_name != 0 && StringIs(base, entry_name, name)) {
        return object;
      }
    }
  }
  const uint32_t begin = U32(base, dir + kDirSubDirsOffset);
  const uint32_t end = U32(base, dir + kDirSubDirsEndOffset);
  if (begin != 0 && end > begin && (end - begin) <= 0x4000 && PageCommitted(begin) &&
      PageCommitted(end - 4)) {
    for (uint32_t slot = begin; slot + kSubDirStride <= end; slot += kSubDirStride) {
      const uint32_t sub = U32(base, slot + kSubDirObjectOffset);
      if (sub == 0) {
        continue;
      }
      const uint32_t found = FindNamedInDir(base, sub, name, depth + 1);
      if (found != 0) {
        return found;
      }
    }
  }
  return 0;
}

// The lookup hands back an `Hmx::Object*`; the layouts in `kKnownTransforms` are relative to
// that view, whose name is at +0x20. If a derived pointer ever comes back instead, the name
// check fails and the virtual-base conversion recovers the right view.
uint32_t AsNamedObject(uint8_t* base, uint32_t object, const char* name) {
  if (object == 0) {
    return 0;
  }
  if (PageCommitted(object + kObjectDirOffset) &&
      StringIs(base, U32(base, object + kObjectNameOffset), name)) {
    return object;
  }
  const uint32_t hmx = ObjectBase(base, object);
  if (hmx != 0 && hmx != object && PageCommitted(hmx + kObjectDirOffset) &&
      StringIs(base, U32(base, hmx + kObjectNameOffset), name)) {
    return hmx;
  }
  return 0;
}

uint32_t g_dir_lookups = 0;  // diagnostic counter: object-hash walks performed
// Present-hook cost counters (logged every 600 frames): what the hook spends and how many
// writes/dirty calls it actually performs, so a regression shows up as a number.
uint64_t g_present_frames = 0;
uint64_t g_present_ns = 0;
uint64_t g_present_ns_max = 0;
uint64_t g_present_ns_window = 0;
uint64_t g_apply_writes = 0;
uint64_t g_dirty_calls = 0;
uint64_t g_phase_ns[4] = {};  // 0 = preamble, 1 = apply, 2 = screen gate, 3 = missing check
uint64_t g_target_ns[16] = {};
uint64_t g_host_queries = 0;

struct PhaseTimer {
  int slot = 0;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  explicit PhaseTimer(int index) : slot(index) {}
  ~PhaseTimer() {
    g_phase_ns[slot] += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count());
  }
};

// Resolve one element through the HUD scene's own object hash. Returns true when a new
// instance was recorded. Cheap enough to run every frame until the scene exists.
bool ResolveTarget(uint8_t* base, uint32_t dir, HudTarget& target, bool probe) {
  ++g_dir_lookups;
  const uint32_t raw = FindNamedInDir(base, dir, target.name, 0);
  if (raw == 0) {
    return false;
  }
  const uint32_t header = AsNamedObject(base, raw, target.name);
  if (header == 0 || !ObjectShape(base, header) || !InHudDir(base, header)) {
    if (probe) {
      REXLOG_INFO("hud_reanchor: '{}' lookup found {:08X} but it is not a HUD scene object",
                  target.name, raw);
    }
    return false;
  }
  for (int i = 0; i < target.instances; ++i) {
    if (target.headers[i] == header) {
      return false;
    }
  }
  if (target.instances >= kMaxInstances) {
    return false;
  }
  const int instance = target.instances++;
  target.headers[instance] = header;
  target.vtables[instance] = U32(base, header);
  target.name_ptrs[instance] = U32(base, header + kObjectNameOffset);
  target.transform_offsets[instance] = 0;
  target.has_last[instance] = false;
  REXLOG_INFO("hud_reanchor: '{}' instance {} resolved by lookup: header={:08X} vt={:08X} "
              "dir='{}'",
              target.name, instance, header, U32(base, header), DirChain(base, header));
  return true;
}

// Probe-only: list the names the HUD scene's own containers hold, so the element inventory
// (which element sits on which side) is read off the scene instead of guessed. Logs one line
// per name; only reachable with `hud_reanchor_probe`, once per scene.
int32_t KnownTransformableOffset(uint8_t* base, uint32_t header);

void DumpDirInventory(uint8_t* base, uint32_t dir, int depth) {
  if (dir == 0 || depth > kDirSearchDepth || !Mapped(base, dir + kDirSubDirsEndOffset + 4)) {
    return;
  }
  const uint32_t entries = U32(base, dir + kDirEntriesOffset);
  const int32_t size = int32_t(U32(base, dir + kDirSizeOffset));
  if (size > 0 && size <= 8192 && entries != 0 && Mapped(base, entries) &&
      Mapped(base, entries + uint32_t(size) * 8 - 4)) {
    for (int32_t i = 0; i < size; ++i) {
      const uint32_t entry = entries + uint32_t(i) * 8;
      const uint32_t object = U32(base, entry + 4);
      const uint32_t entry_name = U32(base, entry);
      if (object == 0 || entry_name == 0) {
        continue;
      }
      const char* text = reinterpret_cast<const char*>(HostPtr(entry_name));
      if (text == nullptr) {
        continue;
      }
      std::string name;
      for (size_t c = 0; c < 64 && text[c] != 0; ++c) {
        const unsigned char ch = static_cast<unsigned char>(text[c]);
        if (ch < 0x20 || ch > 0x7E) {
          name.clear();
          break;
        }
        name.push_back(static_cast<char>(ch));
      }
      if (name.empty()) {
        continue;
      }
      // For a class in the measured table, print the authored translation: that is what
      // decides which side of the frame the element belongs to.
      const int32_t offset =
          Mapped(base, object) ? KnownTransformableOffset(base, object) : 0;
      if (offset != 0) {
        const uint32_t transformable = uint32_t(int32_t(object) + offset);
        const uint32_t local = transformable + kLocalTranslationOffset;
        const uint32_t world = transformable + kWorldTranslationOffset;
        const float local_x = Mapped(base, local) ? F32(base, local) : 0.0f;
        const float world_x = Mapped(base, world) ? F32(base, world) : 0.0f;
        REXLOG_INFO("hud_reanchor: inventory d{} entry '{}' xfm {:+d} local x {:.3f} world x "
                    "{:.3f}",
                    depth, name, offset, local_x, world_x);
        continue;
      }
      REXLOG_INFO("hud_reanchor: inventory d{} entry '{}' -> {:08X} vt={:08X}", depth, name,
                  object, Mapped(base, object) ? U32(base, object) : 0u);
      // A class that is not in the table yet: find its transformable the way the scene does
      // it — a child-list sentinel at +156 that points at itself, and a translation pair
      // (+0x38 local, +0x78 world) that agrees. Groups and the help prompt are what is left.
      if (name.find(".grp") != std::string::npos || name.find("help") != std::string::npos ||
          name.find("countdown") != std::string::npos) {
        for (int off = -0x300; off <= 0x100; off += 4) {
          const uint32_t candidate = uint32_t(int32_t(object) + off);
          if (!Mapped(base, candidate + kWorldTranslationOffset + 4) ||
              !ChildListSafe(base, candidate)) {
            continue;
          }
          const float local_x = F32(base, candidate + kLocalTranslationOffset);
          const float world_x = F32(base, candidate + kWorldTranslationOffset);
          if (!std::isfinite(local_x) || !std::isfinite(world_x) ||
              std::fabs(world_x) > 4000.0f || std::fabs(local_x) > 4000.0f) {
            continue;
          }
          if (std::fabs(double(local_x) - double(world_x)) > 0.01) {
            continue;
          }
          REXLOG_INFO("hud_reanchor: inventory d{} entry '{}' candidate xfm {:+d} x {:.3f} "
                      "vt={:08X}",
                      depth, name, off, world_x, U32(base, candidate));
        }
      }
    }
  }
  const uint32_t begin = U32(base, dir + kDirSubDirsOffset);
  const uint32_t end = U32(base, dir + kDirSubDirsEndOffset);
  if (begin != 0 && end > begin && (end - begin) <= 0x4000 && Mapped(base, begin) &&
      Mapped(base, end - 4)) {
    for (uint32_t slot = begin; slot + kSubDirStride <= end; slot += kSubDirStride) {
      const uint32_t sub = U32(base, slot + kSubDirObjectOffset);
      if (sub == 0) {
        continue;
      }
      const std::string sub_name = ObjectName(base, ObjectBase(base, sub));
      REXLOG_INFO("hud_reanchor: inventory d{} subdir '{}' -> {:08X}", depth, sub_name, sub);
      DumpDirInventory(base, sub, depth + 1);
    }
  }
}

// Frames between attempts to re-resolve a missing element, so a scene that is still loading
// does not turn the lookup into a per-frame cost. 20 frames is ~0.3 s at 60 Hz.
constexpr int kLookupCooldownFrames = 20;

// The heap sweep is kept ONLY as a diagnostic (`hud_reanchor_scan`): it costs ~2 us per
// 4-byte guest read here (graphics.md §9.2.6), which is why the placement no longer uses it.
struct ScanState {
  uint32_t hud_dir = 0;
  uint32_t begin = 0;
  uint32_t end = 0;
  uint32_t cursor = 0;
  int sweeps = 0;
  // Frames since the last attempt to (re)resolve a missing element.
  int lookup_cooldown = 0;
  uint64_t slice_count = 0;
  uint64_t total_ns = 0;
  uint64_t max_slice_ns = 0;
  uint64_t slots = 0;
  uint64_t candidates = 0;
};
ScanState g_scan;

// Probe-only (defined with the transform table below): lists the records that look like the
// transformable of the complete object a matched element record belongs to.
void ReportTransformableCandidates(uint8_t* base, uint32_t header, const char* name);

// Try one candidate Hmx::Object header whose name field (`header + 32`) holds `name`. A
// record in the scene's string table also has a pointer to the name, so a match is only a
// candidate: the object must have a code vtable at the transformable, a committed local
// translation, a plausible HUD x, and a dir chain that reaches "hud".
void MatchHudHeader(uint8_t* base, uint32_t header, uint32_t name) {
  static int rejected = 0;
  static int rejected_dir = 0;
  // The name comparison needs the terminator inside the same committed page; every target
  // name is shorter than 32 bytes, so a candidate too close to the page end is skipped.
  if (!PageCommitted(name) || (name & 0xFFFu) > 0x1000 - 32) {
    return;
  }
  const char* text = reinterpret_cast<const char*>(base + name);
  if (text[0] < 'A' || text[0] > 'z') {
    return;
  }
  for (auto& target : g_targets) {
    if (!NameIs(text, target.name) || target.instances >= kMaxInstances) {
      continue;
    }
    bool known = false;
    for (int i = 0; i < target.instances; ++i) {
      if (target.headers[i] == header) {
        known = true;
        break;
      }
    }
    if (known) {
      continue;
    }
    // The name alone is not enough: the scene's string table and DTA records also point at
    // these names, and a lookalike object with an unreadable dir passes every cheap shape
    // test. Only an object in the `track/hud.milo` scene may be written to - its dirty call
    // walks the object's child list (0x821F0300), so a false candidate corrupts memory and
    // faults the guest (measured 2026-10-17: 'star_board1' with dir='' -> read of 0xBD).
    if (!InHudDir(base, header)) {
      if (rejected_dir < 8) {
        ++rejected_dir;
        REXLOG_INFO("hud_reanchor: '{}' at {:08X} is not in the hud dir (dir='{}')",
                    target.name, header, DirChain(base, header));
      }
      continue;
    }
    if (!ObjectShape(base, header)) {
      if (rejected < 8) {
        ++rejected;
        REXLOG_INFO("hud_reanchor: '{}' at {:08X} has no code-vtable HUD group shape",
                    target.name, header);
      }
      continue;
    }
    const float x = F32(base, header + kHudGroupTranslation);
    if (!std::isfinite(x) || std::fabs(x) > 4000.0f) {
      continue;
    }
    const int instance = target.instances++;
    target.headers[instance] = header;
    target.transformables[instance] = header;
    target.transform_offsets[instance] = 0;
    target.has_last[instance] = false;
    target.vtables[instance] = U32(base, header);
    target.name_ptrs[instance] = name;
    if (REXCVAR_GET(hud_reanchor_probe)) {
      ReportTransformableCandidates(base, header, target.name);
    }
    REXLOG_INFO("hud_reanchor: found '{}' header={:08X} instance={} vt={:08X} t=({:.3f} "
                "{:.3f} {:.3f}) list_safe={} dir='{}'",
                target.name, header, instance, U32(base, header),
                F32(base, header + kHudGroupTranslation),
                F32(base, header + kHudGroupTranslation + 4),
                F32(base, header + kHudGroupTranslation + 8),
                ChildListSafe(base, header), DirChain(base, header));
    return;
  }
}

// `hud_reanchor_scan` diagnostic: every record in the HUD scene window that looks like a real
// transformable (its child-list sentinel at +156 is the engine's own list) gets one line with
// its name and the two position triples this pass knows how to read. That map is what says
// which object actually carries the visible element, so the write goes to a live field.
void DumpTransformable(uint8_t* base, uint32_t header) {
  static int shown = 0;
  if (shown >= 240 || !Mapped(base, header + kObjectNameOffset)) {
    return;
  }
  const uint32_t name = U32(base, header + kObjectNameOffset);
  if (!PageCommitted(name) || (name & 0xFFFu) > 0x1000 - 32) {
    return;
  }
  const char* text = reinterpret_cast<const char*>(base + name);
  for (size_t i = 0; i < 40; ++i) {
    const char c = text[i];
    if (c == 0) {
      break;
    }
    if (c < 0x20 || c > 0x7E) {
      return;
    }
  }
  if (!ChildListSafe(base, header) || !InHudDir(base, header)) {
    return;
  }
  ++shown;
  REXLOG_INFO("hud_reanchor: xfm {:08X} vt={:08X} '{}' +38=({:.3f} {:.3f} {:.3f}) "
              "+70=({:.3f} {:.3f} {:.3f}) +B0={:.3f} +F0={:.3f}",
              header, U32(base, header), text, F32(base, header + 0x38),
              F32(base, header + 0x3C), F32(base, header + 0x40), F32(base, header + 0x70),
              F32(base, header + 0x74), F32(base, header + 0x78), F32(base, header + 0xB0),
              F32(base, header + 0xF0));
}

// One SLICE of the HUD scene's heap window. Every aligned word is a candidate name field
// (object = word - 32); MatchHudHeader does the name, dir, shape and x checks. The window is
// ±2 MiB around the scene dir — the elements are allocated while the scene loads, but their
// distance from the dir depends on the heap, and a fixed small window missed them in a real
// session (2026-10-17: the owner's run found no element in ±128 KiB, its heap was `0x416...`
// where the harness runs used `0x43EC...`).
constexpr uint32_t kSceneWindowRadius = 0x200000;  // ±2 MiB around the HUD dir
// The slice is budgeted by TIME, not by bytes. A 4-byte guest read on this title's arena
// measures ~2 us here (2,079,744 slots cost 4.08 s in `hud-sweep3.log`), so a byte budget that
// looks small is still tens of milliseconds of frame time. One millisecond per present is the
// ceiling, which keeps the sweep invisible at 60 Hz, and it is zero once the sweep has run.
constexpr double kScanSliceMs = 1.0;
// Returns true when the sweep has reached `end`. Stops as soon as `kScanSliceMs` is spent.
bool ScanHudSlice(uint8_t* base, uint32_t begin, uint32_t end, uint32_t& cursor, bool scan) {
  if (cursor < begin || cursor >= end) {
    cursor = begin;
  }
  const auto start = std::chrono::steady_clock::now();
  while (cursor < end) {
    const uint32_t page = cursor & ~uint32_t(0xFFF);
    if (!PageCommitted(page)) {
      cursor = page + 0x1000;
      continue;
    }
    const uint32_t last = std::min(end, page + 0x1000);
    for (uint32_t slot = std::max(cursor, page); slot + kObjectNameOffset + 4 <= last;
         slot += 4) {
      ++g_scan.slots;
      // The candidate object's own vtable must be a code address in the image. This is one
      // read inside the page already known committed, and it removes ~95% of the words
      // before anything asks the heap API about the name pointer.
      const uint32_t vtable = U32(base, slot);
      if (vtable < 0x82000000 || vtable >= 0x82A40000) {
        continue;
      }
      ++g_scan.candidates;
      const uint32_t name = U32(base, slot + kObjectNameOffset);
      if (name < kHeapBegin || name >= kHeapEnd) {
        continue;
      }
      if (scan) {
        DumpTransformable(base, slot);
      }
      MatchHudHeader(base, slot, name);
    }
    cursor = last;
    const double spent_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count();
    if (spent_ms >= kScanSliceMs) {
      break;
    }
  }
  if (cursor >= end) {
    cursor = end;
    return true;
  }
  return false;
}

// Where each element class keeps its Transform, measured live on 2026-10-17. Only classes in
// this table are written: a shape-only search for the local/world pair is not enough, because
// a record also holds matrix rows and colours that look like a repeated triple, and writing one
// of those faults the guest (`hud-xfm4.log`: a `-268` scale row read 7; `hud-xfm5.log`: a
// `-380` pair reading 300, fault `read of guest 0x31005A2E` six milliseconds after the write).
//
// `8201C784` — the star board and the band meter, the two elements this pass places so far.
// The record `MatchHudHeader` matches is the object's `Hmx::Object` subobject (name at +0x20,
// dir at +0x24); the complete object starts 0x114 bytes earlier, so the local translation
// (transformable + 0x38) is at record - 0xDC and the cached world translation
// (transformable + 0x78) at record - 0x9C. At the HUD's 1.125 px per unit those x values
// reproduce the measured screen positions (star board 270, left block -298.537).
//
// The layout of every class is below, each relative to the `Hmx::Object` view the lookup
// returns. `AppLabel`'s positive offset is confirmed by its constructor, which stores the base
// vtables at +0, +0x44 and +0xC4 (`generated/default/rb_blitz_recomp.83.cpp`), so the label's
// transformable base sits at +0x7C — AFTER the object base, unlike the dirs.
struct KnownTransform {
  uint32_t vtable;
  int32_t transformable;  // offset from the Hmx::Object view to the RndTransformable base
};
constexpr KnownTransform kKnownTransforms[] = {
    // RndDir - the band meter (left cluster) and the star board (right cluster).
    {0x8201C784, -0x114},
    // DynamicLeaderboard - the comparator against the previous run.
    {0x82018EA4, -0x1B4},
    // AppLabel - the score readout.
    {0x8203A95C, +0x7C},
    // RndGroup (`*.grp`) — the `hud.grp`/`remedial_help.grp` family. Measured from the scene
    // inventory: `remedial_help.grp`'s own translation is x -289.338 (the same value its
    // `remedial_help.lbl` child carries as a world x), and it sits 0x108 before the group
    // object, whose transformable vtable reads 0x82024CA8 (`hud-inv3.log`).
    {0x82024C4C, -0x108},
};

int32_t KnownTransformableOffset(uint8_t* base, uint32_t header) {
  if (!Mapped(base, header)) {
    return 0;
  }
  const uint32_t vtable = U32(base, header);
  for (const KnownTransform& known : kKnownTransforms) {
    if (known.vtable == vtable) {
      return known.transformable;
    }
  }
  return 0;
}

// A transformable keeps its child list as a circular list whose SENTINEL is at +0x9C of the
// transformable itself — that is the pointer the dirty routine `0x821F0300` walks. An empty
// list points at itself, which no string table, matrix row or colour can fake, so this is the
// generic way to find the transformable of the complete object a matched record belongs to.
// Reported under `hud_reanchor_probe` for every element whose class is not in the table yet.
void ReportTransformableCandidates(uint8_t* base, uint32_t header, const char* name) {
  for (int32_t off = -0x1F0; off < 0; off += 4) {
    const uint32_t transformable = uint32_t(int32_t(header) + off);
    if (!Mapped(base, transformable + 0xA0)) {
      continue;
    }
    const uint32_t sentinel = transformable + 0x9C;
    const uint32_t first = U32(base, sentinel);
    const bool empty = first == sentinel;
    const bool node = !empty && Mapped(base, first + 8) && Mapped(base, U32(base, first + 8));
    if (!empty && !node) {
      continue;
    }
    REXLOG_INFO("hud_reanchor: '{}' transformable candidate {:+d} ({}) v={:.3f} {:.3f} "
                "{:.3f} world={:.3f} {:.3f} {:.3f}",
                name, off, empty ? "empty list" : "list", F32(base, transformable + 0x38),
                F32(base, transformable + 0x3C), F32(base, transformable + 0x40),
                F32(base, transformable + 0x78), F32(base, transformable + 0x7C),
                F32(base, transformable + 0x80));
  }
}

// One element instance. The direction is the element's measured side, not the sign of the
// group's own x (the star board sits near the origin with its art to the right, the left
// group's x is negative); a centred element (sign 0) is left exactly where it is.
void ApplyInstance(PPCContext& ctx, uint8_t* base, HudTarget& target, int instance,
                   double delta, bool probe) {
  if (target.shift_sign == 0 || !InstanceValid(base, target, instance)) {
    return;
  }
  const uint32_t header = target.headers[instance];
  if (target.transform_offsets[instance] == 0) {
    target.transform_offsets[instance] = KnownTransformableOffset(base, header);
    if (probe || target.transform_offsets[instance] == 0) {
      REXLOG_INFO("hud_reanchor: '{}' instance {} vt {:08X} -> transformable {:+d} "
                  "(0 = class not in the table)",
                  target.name, instance, U32(base, header),
                  target.transform_offsets[instance]);
    }
    if (target.transform_offsets[instance] == 0) {
      return;
    }
  }
  const uint32_t transformable = uint32_t(int32_t(header) + target.transform_offsets[instance]);
  const uint32_t local = transformable + kLocalTranslationOffset;
  const uint32_t world = transformable + kWorldTranslationOffset;
  if (!PageCommitted(local + 8) || !PageCommitted(world + 8)) {
    target.headers[instance] = 0;
    target.has_last[instance] = false;
    return;
  }
  const float x = F32(base, local);
  const float world_x = F32(base, world);
  if (!std::isfinite(world_x) || std::fabs(world_x) > 4000.0f) {
    return;
  }
  // The present pass runs after the frame is built, so the first write is seen on the next
  // frame. If the game did not touch the transform in between, the world x is still what we
  // wrote; do not add the shift a second time. This is also what makes a pause/resume
  // harmless: the shifted value comes back and the record is not shifted again.
  if (target.has_last[instance] &&
      std::fabs(double(world_x) - double(target.last_written[instance])) < 0.001) {
    return;
  }
  const double moved = double(world_x) + double(target.shift_sign) * delta;
  target.last_written[instance] = float(moved);
  target.has_last[instance] = true;
  if (moved == double(world_x)) {
    return;
  }
  if (probe) {
    REXLOG_INFO("hud_reanchor: '{}' instance {} world x {:.3f} -> {:.3f} ({} delta {:.3f} "
                "HUD units; authored local {:.3f}), xfm {:08X}",
                target.name, instance, world_x, float(moved),
                target.shift_sign > 0 ? "right" : "left", delta, x, transformable);
  }
  // The world copy is what the draw reads (measured 2026-10-17, three arms: the local copy
  // alone moved nothing, `hud-xfm2.log`), and the game re-derives it from the local copy now
  // and then (`hud-xfm3.log` logged a second `star_board` application 0.1 s after the first),
  // so the local copy is moved too whenever the pair still agrees — that way a recompute
  // keeps the shift instead of undoing it.
  if (std::fabs(double(x) - double(world_x)) < 0.01) {
    StoreF32(base, local, float(moved));
  }
  StoreF32(base, world, float(moved));
  ++g_apply_writes;
  // A group's CHILDREN keep their own cached world transforms, so moving a parent without
  // invalidating them leaves its art where it was — that is why `star_board`'s x moved while
  // the star cluster did not. `0x821F0300` marks the transformable (dirty byte at +189) and
  // walks its child list at +156, so it must be given the transformable, never the record.
  if (ChildListSafe(base, transformable)) {
    ++g_dirty_calls;
    MarkTransformDirty(ctx, base, transformable);
  }
}

void ApplyTarget(PPCContext& ctx, uint8_t* base, HudTarget& target, double delta, bool probe) {
  if (!REXCVAR_GET(hud_reanchor_apply) || !target.place || target.instances == 0) {
    return;
  }
  const auto start = std::chrono::steady_clock::now();
  for (int instance = 0; instance < target.instances; ++instance) {
    ApplyInstance(ctx, base, target, instance, delta, probe);
  }
  const size_t index = size_t(&target - g_targets.data());
  if (index < 16) {
    g_target_ns[index] += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count());
  }
}

}  // namespace

namespace {

// What the present hook costs, so "it does not drop frames" is a measurement rather than an
// opinion. The sweep this feature used to run measured 200 ms slices; the lookup is one hash
// walk per scene and the per-frame path is a handful of cached guest reads per element.
//
// The cost used to be 16 ms a frame with nothing written, because `Mapped()` is `HostPtr()`
// and that ends in `VirtualQuery` (~1.2 ms here): twelve of those per frame were the lag the
// owner reported (`hud-phase1.log`, `hud-phase2.log`). Every per-frame page test now goes
// through `PageCommitted`, which is an array read after a page's first visit, and the window
// cost is 1-4 us (`hud-verify4.log`). The line below therefore prints only under
// `hud_reanchor_probe` or when the window cost crosses the budget, so a regression is still
// visible in a normal log.
constexpr double kPresentCostWarnUs = 200.0;

struct PresentTimer {
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  ~PresentTimer() {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const uint64_t ns =
        uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    g_present_ns += ns;
    g_present_ns_window += ns;
    g_present_ns_max = std::max(g_present_ns_max, ns);
    if (++g_present_frames % 600 == 0) {
      const bool probe = REXCVAR_GET(hud_reanchor_probe);
      if (probe || double(g_present_ns_window) / 1e3 / 600.0 > kPresentCostWarnUs) {
        REXLOG_INFO("hud_reanchor: {} frames, {:.1f} us/frame in the present hook (worst "
                    "{:.1f} us), {} write(s), {} dirty call(s), {} lookup(s) this scene; "
                    "phases pre {:.1f} apply {:.1f} gate {:.1f} missing {:.1f} us/frame",
                    g_present_frames, double(g_present_ns_window) / 1e3 / 600.0,
                    double(g_present_ns_max) / 1e3, g_apply_writes, g_dirty_calls,
                    g_dir_lookups, double(g_phase_ns[0]) / 1e3 / 600.0,
                    double(g_phase_ns[1]) / 1e3 / 600.0,
                    double(g_phase_ns[2]) / 1e3 / 600.0,
                    double(g_phase_ns[3]) / 1e3 / 600.0);
        REXLOG_INFO("hud_reanchor:   {} heap queries this window ({:.1f}/frame)",
                    g_host_queries, double(g_host_queries) / 600.0);
      }
      g_host_queries = 0;
      g_present_ns_window = 0;
      g_present_ns_max = 0;
      g_apply_writes = 0;
      g_dirty_calls = 0;
      g_phase_ns[0] = g_phase_ns[1] = g_phase_ns[2] = g_phase_ns[3] = 0;
      for (size_t i = 0; i < g_targets.size(); ++i) {
        if (probe && g_target_ns[i] != 0) {
          REXLOG_INFO("hud_reanchor:   target '{}' {:.1f} us/frame", g_targets[i].name,
                      double(g_target_ns[i]) / 1e3 / 600.0);
        }
        g_target_ns[i] = 0;
      }
    }
  }
};

}  // namespace

void rb_blitz::hooks::HudReanchorPresent(PPCContext& ctx, uint8_t* base) {
  const PresentTimer timer;
  uint32_t width = 0;
  uint32_t height = 0;
  double delta = 0.0;
  bool probe = false;
  {
    const PhaseTimer phase(0);
    if (!REXCVAR_GET(hud_reanchor)) {
      return;
    }
    width = rb_blitz::settings::BootRenderWidth();
    height = rb_blitz::settings::BootRenderHeight();
    if (width == 0 || height == 0) {
      return;
    }
    const double units = rb_blitz::settings::HudShiftUnits(width, height);
    if (units == 0.0) {
      return;
    }
    delta = units * double(REXCVAR_GET(hud_reanchor_hud_half_height));
    probe = REXCVAR_GET(hud_reanchor_probe);
  }

  {
    const PhaseTimer phase(1);
    for (auto& target : g_targets) {
      if (target.instances != 0) {
        ApplyTarget(ctx, base, target, delta, probe);
      }
    }
  }
  // The HUD scene only exists in a live song, and a pause keeps it alive; so this section
  // neither clears nor re-shifts on a pause — `has_last` already makes a resumed instance a
  // no-op. Only a different scene dir (a new song) resets the records.
  {
    const PhaseTimer phase(2);
    if (!rb_blitz::input::GameScreenActive()) {
      return;
    }
  }
  // Everything resolved: zero per-frame work until an instance stops validating (a new song).
  bool missing = false;
  {
    const PhaseTimer phase(3);
    missing = TargetsMissing(base);
  }
  if (!missing) {
    return;
  }
  if (g_scan.lookup_cooldown > 0) {
    --g_scan.lookup_cooldown;
    return;
  }
  const uint32_t dir = HudDirNow(base);
  if (dir != 0 && dir != g_scan.hud_dir) {
    // A new scene: its objects are new records, and the old ones are gone.
    if (g_scan.hud_dir != 0) {
      for (auto& target : g_targets) {
        target.instances = 0;
        target.transform_offsets = {};
        target.has_last = {};
      }
    }
    g_scan.hud_dir = dir;
    if (probe) {
      REXLOG_INFO("hud_reanchor: HUD scene dir {:08X} (track/hud.milo)", dir);
      DumpDirInventory(base, dir, 0);
    }
  }
  if (g_scan.hud_dir == 0) {
    g_scan.lookup_cooldown = kLookupCooldownFrames;
    return;
  }
  // Resolve whatever is still missing through the scene's own object hash — a few hundred
  // word reads, not a heap sweep.
  bool resolved = false;
  for (auto& target : g_targets) {
    if (!target.required || TargetPresent(base, target)) {
      continue;
    }
    resolved |= ResolveTarget(base, g_scan.hud_dir, target, probe);
  }
  if (TargetsMissing(base)) {
    g_scan.lookup_cooldown = kLookupCooldownFrames;
    if (resolved) {
      g_scan.lookup_cooldown = 1;
    }
  }
  // Diagnostic only, off by default: the heap sweep that the lookup replaced. It reuses the
  // same target records, so a probe boot can map the scene (`hud_reanchor_scan`).
  if (!REXCVAR_GET(hud_reanchor_scan) || !TargetsMissing(base)) {
    return;
  }
  if (g_scan.begin == 0) {
    const uint32_t radius = kSceneWindowRadius;
    g_scan.begin = g_scan.hud_dir > radius ? g_scan.hud_dir - radius : kHeapBegin;
    g_scan.end = std::min(g_scan.hud_dir + radius, kHeapEnd);
    g_scan.cursor = g_scan.begin;
  }
  const auto slice_start = std::chrono::steady_clock::now();
  const bool swept = ScanHudSlice(base, g_scan.begin, g_scan.end, g_scan.cursor, probe);
  ++g_scan.slice_count;
  g_scan.total_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now() - slice_start)
                                  .count());
  if (swept) {
    g_scan.begin = 0;
    ++g_scan.sweeps;
    REXLOG_INFO("hud_reanchor: diagnostic sweep {} done: {} slice(s), {:.2f} ms total, {} "
                "slot(s), {} candidate(s)",
                g_scan.sweeps, g_scan.slice_count, double(g_scan.total_ns) / 1e6, g_scan.slots,
                g_scan.candidates);
  }
}

extern "C" REX_FUNC(sub_821F44F8) {
  const uint32_t object = uint32_t(ctx.r4.u32);
  const uint32_t message = uint32_t(ctx.r5.u32);
  const std::string symbol = MessageSymbol(base, message);
  // Virtual calls dispatch through the per-module table, not through the direct-call
  // symbol this file overrides, so install the animation hook in that table once.
  static bool dispatch_patched = false;
  if (!dispatch_patched && (REXCVAR_GET(hud_reanchor) || REXCVAR_GET(hud_reanchor_probe))) {
    dispatch_patched = true;
    auto* slot = reinterpret_cast<PPCFunc**>(base + kImageBase + kImageSize +
                                             (kPropAnimSetFrame - kCodeBase) * 2);
    REXLOG_INFO("hud_reanchor: propanim dispatch slot {:p} -> {:p}", (void*)slot,
                (void*)*slot);
    *slot = &sub_82237478;
  }
  // The probe's own liveness line: if a boot shows no `hud_reanchor:` output at all, this says
  // whether the handler was never entered or its message symbol was read differently.
  if (REXCVAR_GET(hud_reanchor_probe)) {
    static int calls = 0;
    if (calls < 200) {
      ++calls;
      const uint32_t nodes = message == 0 ? 0 : U32(base, message);
      const uint32_t raw = nodes == 0 ? 0 : U32(base, nodes + 8);
      REXLOG_INFO("hud_reanchor: call {} obj={:08X} msg={:08X} nodes={:08X} sym={:08X} '{}'",
                  calls, object, message, nodes, raw, symbol);
    }
    if (symbol == "set_local_pos" && Mapped(base, object)) {
      REXLOG_INFO("hud_reanchor: set_local_pos target obj={:08X} name='{}' dir='{}'", object,
                  ObjectName(base, object), DirChain(base, object));
    }
  }
  // The old whole-heap `ScanHudDir` walk used to run here every few hundred messages and
  // stalled the guest for seconds (measured 2026-10-17, the `hud-map1` boot never reached
  // the song). The scene-window diagnostic below is bounded, so it is the only one left.
  __imp__sub_821F44F8(ctx, base);

  if (symbol != "set_local_pos" || !Mapped(base, object)) {
    return;
  }
  ApplyHudRule(ctx, base, object, object - kTransformableBeforeObject, "set_local_pos");
}

// The second seam: PropAnim::SetFrame (0x82237478) would evaluate the target transformable's
// authored position keys into its local translation at +56 every time the animation is
// stepped. The target is the transformable itself (the Hmx::Object header sits 196 bytes
// later, the same split the script handler sees); the target's dir chain keeps world
// animations out. **Unproven for the stock HUD**: the 2560x1080 probe logged no calls at
// all, so either the stock HUD is not a PropAnim or its animation is not stepped. Keep the
// seam faithful and ready for a boot where it is entered.
extern "C" REX_FUNC(sub_82237478) {
  const uint32_t anim = uint32_t(ctx.r3.u32);
  const uint32_t target = Mapped(base, anim + kPropAnimTarget)
                              ? U32(base, anim + kPropAnimTarget)
                              : 0;
  if (REXCVAR_GET(hud_reanchor_probe)) {
    static int calls = 0;
    if (calls < 40) {
      ++calls;
      REXLOG_INFO("hud_reanchor: propanim call {} anim={:08X} target={:08X}", calls, anim,
                  target);
    }
  }
  __imp__sub_82237478(ctx, base);
  if (target == 0 || !Mapped(base, target)) {
    return;
  }
  ApplyHudRule(ctx, base, target + kTransformableBeforeObject, target, "propanim");
}
