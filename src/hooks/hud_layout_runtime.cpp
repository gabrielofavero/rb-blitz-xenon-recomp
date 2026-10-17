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
// that boot. So this file currently shifts only script-positioned HUD elements (the
// Ultimate route); a post-load pass over the HUD ObjectDir is the open work, recorded in
// docs/settings/graphics.md §9.4.
//
// The HUD is laid out in a 360-unit-tall screen space (half-height 180 units): the 16:9
// anchors above (star_board +220, dynamic_leaderboard +295) land where the 16:9 captures
// put them at about 3 px per unit on a 1080p frame. That scale is an inference from the
// anchors and the capture, not a measured camera constant, which is why
// `hud_reanchor_hud_half_height` is a cvar: the next boot can correct the one number.

#include <array>
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
                    "Re-anchor the in-song HUD on a non-16:9 frame: every off-centre "
                    "element keeps the pixel distance it has to its nearest horizontal "
                    "edge, centred elements stay centred. A 16:9 boot is untouched.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(hud_reanchor_probe, false, "Diagnostics",
                    "Diagnostics: log every scripted set_local_pos that reaches a HUD "
                    "element (object, dir, x/y/z). The evidence the re-anchor rule needs.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(hud_reanchor_hud_half_height, 180.0, "Graphics",
                      "The HUD scene's half-height in its own units (a 360-unit-tall "
                      "virtual screen). 2560x1080 therefore shifts by 106.7 HUD units; "
                      "tune this only against a capture.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(hud_reanchor_scan, false, "Diagnostics",
                    "Diagnostics: find the loaded 'hud' ObjectDir and log its object list. "
                    "Read-only; the evidence needed for a post-load layout pass.")
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

// A guest object's name: Hmx::Object header +32 in this image (the same field the native
// menu and help observer read). Empty when the pointer is not a mapped, printable name.
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

// True when the object's directory, or one of its parents, is the HUD scene. The HUD's
// ObjectDir is named "hud" (the scene's own string table), and every element the title and
// Ultimate place is registered in it.
bool InHudDir(uint8_t* base, uint32_t object) {
  if (!Mapped(base, object) || !Mapped(base, object + kObjectDirOffset)) {
    return false;
  }
  uint32_t dir = U32(base, object + kObjectDirOffset);
  for (int depth = 0; depth < kDirChainDepth && Mapped(base, dir) &&
                      Mapped(base, dir + kObjectDirOffset);
       ++depth) {
    if (ObjectName(base, dir) == "hud") {
      return true;
    }
    const uint32_t parent = U32(base, dir + kObjectDirOffset);
    if (parent == dir) {
      break;
    }
    dir = parent;
  }
  return false;
}

// The object's dir chain, outermost last, for the probe line. A wrong dir offset shows up as
// an empty or non-scene chain here rather than as a silent no-op.
std::string DirChain(uint8_t* base, uint32_t object) {
  std::string out;
  if (!Mapped(base, object) || !Mapped(base, object + kObjectDirOffset)) {
    return out;
  }
  uint32_t dir = U32(base, object + kObjectDirOffset);
  for (int depth = 0; depth < 4 && Mapped(base, dir) && Mapped(base, dir + kObjectDirOffset);
       ++depth) {
    const std::string name = ObjectName(base, dir);
    if (name.empty()) {
      break;
    }
    if (!out.empty()) {
      out += "/";
    }
    out += name;
    const uint32_t parent = U32(base, dir + kObjectDirOffset);
    if (parent == dir) {
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
// measurement that says where a post-load pass would have to write.
void ScanHudDir(uint8_t* base) {
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
        const std::string dir_name = Mapped(base, dir) ? ObjectName(base, dir) : std::string{};
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
struct HudTarget {
  const char* name;
  uint32_t header = 0;
  uint32_t transformable = 0;
  float last_written = 0.0f;
  bool has_last = false;
  bool applied = false;
};

std::array<HudTarget, 8> g_targets{{
    // The milo loader gives a subdir whose name already exists a numeric suffix; the
    // assets list both spellings, so match the instance names as well as the authored ones.
    {"star_board"},
    {"star_board1"},
    {"band_meter_hud"},
    {"band_meter_hud1"},
    {"dynamic_leaderboard"},
    {"dynamic_leaderboard1"},
    {"score.lbl"},
    {"score.lbl1"},
}};

bool NameIs(const char* text, const char* name) {
  const size_t length = std::strlen(name);
  return std::strncmp(text, name, length) == 0 && text[length] == '\0';
}

// A real scene object, as opposed to the scene's string table or a DTA script object that
// also mentions the name: the transformable has a code vtable in the image, and the
// Hmx::Object has a mapped mDir. The HUD scene's own ObjectDir name is not guaranteed to be
// literally "hud" on every screen, so the literal dir chain is evidence (logged) rather than
// the admission test.
bool ObjectShape(uint8_t* base, uint32_t header, uint32_t transformable) {
  if (!Mapped(base, transformable) || !Mapped(base, transformable + kLocalTranslation + 8) ||
      !Mapped(base, header + kObjectDirOffset)) {
    return false;
  }
  const uint32_t vtable = U32(base, transformable);
  if (vtable < 0x82000000 || vtable >= 0x82A40000) {
    return false;
  }
  const uint32_t dir = U32(base, header + kObjectDirOffset);
  return dir != 0 && Mapped(base, dir);
}

bool TargetValid(uint8_t* base, const HudTarget& target) {
  if (target.header == 0 || !Mapped(base, target.header + kObjectNameOffset)) {
    return false;
  }
  const uint32_t name = U32(base, target.header + kObjectNameOffset);
  if (!Mapped(base, name)) {
    return false;
  }
  const char* text = reinterpret_cast<const char*>(HostPtr(name));
  return text != nullptr && NameIs(text, target.name) &&
         ObjectShape(base, target.header, target.transformable);
}

// One heap walk, matching every still-missing group by its exact object name. A record in
// the scene's string table also has a pointer to the name, so a match is only a candidate:
// the object must have a code vtable at the transformable, a committed local translation,
// a plausible HUD x, and a dir chain that reaches "hud".
bool FindHudTargets(uint8_t* base) {
  int missing = 0;
  for (const auto& target : g_targets) {
    if (target.header == 0) {
      ++missing;
    }
  }
  if (missing == 0) {
    return true;
  }
  static int rejected_dir = 0;
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
        if (text == nullptr || text[0] < 'A' || text[0] > 'z') {
          continue;
        }
        for (auto& target : g_targets) {
          if (target.header != 0 || !NameIs(text, target.name)) {
            continue;
          }
          // Two layouts are possible in this image: a virtual-base Hmx::Object 196 bytes
          // after the transformable (the RndDir groups), or the object at +0 (the labels).
          // Try the virtual-base form first, then the plain form; the x gate keeps a
          // neighbouring record's field from being mistaken for a HUD anchor.
          uint32_t transformable = 0;
          float x = 0.0f;
          for (uint32_t before : {kTransformableBeforeObject, 0u}) {
            const uint32_t candidate = header - before;
            if (!ObjectShape(base, header, candidate)) {
              continue;
            }
            const float candidate_x = F32(base, candidate + kLocalTranslation);
            // The groups are authored in HUD units (the payload's +220 / -200.54 / +295
            // anchors); a centred element is already the identity and needs no target.
            if (!(candidate_x == candidate_x) || candidate_x < -1000.0f ||
                candidate_x > 1000.0f || std::fabs(double(candidate_x)) < 10.0) {
              continue;
            }
            transformable = candidate;
            x = candidate_x;
            break;
          }
          if (transformable == 0) {
            if (rejected_dir < 8) {
              ++rejected_dir;
              REXLOG_INFO("hud_reanchor: '{}' at {:08X} has no plausible transformable "
                          "(dir='{}')",
                          target.name, header, DirChain(base, header));
            }
            continue;
          }
          target.header = header;
          target.transformable = transformable;
          target.has_last = false;
          target.applied = false;
          REXLOG_INFO("hud_reanchor: found '{}' header={:08X} xfm={:08X} x={:.3f} "
                      "in_hud={} dir='{}'",
                      target.name, header, transformable, x, InHudDir(base, header),
                      DirChain(base, header));
          --missing;
          break;
        }
        if (missing == 0) {
          return true;
        }
      }
    }
  }
  return missing == 0;
}

void ApplyTarget(PPCContext& ctx, uint8_t* base, HudTarget& target, double delta, bool probe) {
  if (!TargetValid(base, target)) {
    target.header = 0;
    target.has_last = false;
    return;
  }
  if (!Mapped(base, target.transformable + kLocalTranslation + 8)) {
    target.header = 0;
    target.has_last = false;
    return;
  }
  const float x = F32(base, target.transformable + kLocalTranslation);
  // The present pass runs after the frame is built, so the first write is seen on the next
  // frame. If the game did not touch the transform in between, `x` is still what we wrote;
  // do not add the shift a second time.
  if (target.has_last && std::fabs(double(x) - double(target.last_written)) < 0.001) {
    return;
  }
  const double moved = rb_blitz::settings::ShiftHudX(double(x), delta, 0.5);
  target.last_written = float(moved);
  target.has_last = true;
  if (moved == double(x)) {
    return;
  }
  StoreF32(base, target.transformable + kLocalTranslation, float(moved));
  MarkTransformDirty(ctx, base, target.transformable);
  if (!target.applied || probe) {
    REXLOG_INFO("hud_reanchor: '{}' x {:.3f} -> {:.3f} (delta {:.3f} HUD units)",
                target.name, x, float(moved), delta);
    target.applied = true;
  }
}

}  // namespace

void rb_blitz::hooks::HudReanchorPresent(PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(hud_reanchor)) {
    return;
  }
  const uint32_t width = rb_blitz::settings::BootRenderWidth();
  const uint32_t height = rb_blitz::settings::BootRenderHeight();
  if (width == 0 || height == 0) {
    return;
  }
  const double units = rb_blitz::settings::HudShiftUnits(width, height);
  if (units == 0.0) {
    return;
  }
  const double delta = units * double(REXCVAR_GET(hud_reanchor_hud_half_height));
  const bool probe = REXCVAR_GET(hud_reanchor_probe);

  static int frame = 0;
  ++frame;
  bool missing = false;
  for (auto& target : g_targets) {
    if (target.header == 0) {
      missing = true;
    } else {
      ApplyTarget(ctx, base, target, delta, probe);
    }
  }
  if (!missing || rb_blitz::input::MenuAvailable()) {
    return;
  }
  // The scene loads while the song starts; scan at most once every two seconds, and stop
  // after a minute of gameplay. A full heap walk is not a per-frame cost.
  static int attempts = 0;
  if (attempts >= 30 || (frame % 120) != 0) {
    return;
  }
  ++attempts;
  FindHudTargets(base);
  if (attempts == 30) {
    REXLOG_WARN("hud_reanchor: HUD groups still not found after {} heap scans; the scene "
                "may use a different object name or dir. Run with hud_reanchor_scan for "
                "the ObjectDir evidence.",
                attempts);
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
  if (REXCVAR_GET(hud_reanchor_scan) && rb_blitz::settings::BootRenderWidth() != 0) {
    static int scan_calls = 0;
    ++scan_calls;
    if (scan_calls == 100 || scan_calls == 300 || scan_calls == 600) {
      ScanHudDir(base);
    }
  }
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
