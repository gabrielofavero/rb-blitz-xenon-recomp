// SPDX-License-Identifier: GPL-2.0-only
#include "input/native_mesh.h"
#include "input/touch_controls.h"

#include <array>
#include <bit>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <rex/ppc/hook.h>
#include <rex/ppc/call.h>
#include <rex/memory/endian.h>

REX_EXTERN(__imp__sub_823F0118); // RndMesh::OnSync, before CPU vertices are released
REX_EXTERN(sub_821E14A8);       // Multiply(Vector3, Transform)
REX_EXTERN(sub_821FF4A0);       // RndCam::WorldToScreen
REX_EXTERN(sub_82402DE8);       // ObjectDir::FindObject
REX_EXTERN(sub_821F13D0);       // RndTransformable::WorldXfm

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

}

namespace {
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
}

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
std::optional<MenuBounds> ProjectMesh(PPCContext& ctx, uint8_t* base, uint32_t mesh,
                                     uint32_t transform) {
  const auto camera = U32(base, 0x8284A108), owner = U32(base, mesh + 324);
  if (!camera || !owner) return {};
  MeshBox box;
  {
    std::lock_guard lock(mesh_mutex);
    const auto it = mesh_boxes.find(owner);
    if (it == mesh_boxes.end() || it->second.resource != U32(base, owner + 412)) return {};
    box = it->second;
  }
  GuestCall call(ctx, base); auto& c = call.ctx;
  // These sibling meshes may be queried before their own draw updates the world
  // transform. Resolve parent transforms before projecting their bounds.
  if (!transform && base[mesh + 253]) {
    c.r3.u32 = mesh + 64;
    sub_821F13D0(c, base);
  }
  const auto local = call.scratch, world = local + 16, projected = local + 32;
  MenuBounds b{INFINITY, INFINITY, -INFINITY, -INFINITY};
  for (unsigned corner = 0; corner < 8; ++corner) {
    for (unsigned axis = 0; axis < 3; ++axis)
      Float(base, local + axis * 4, corner & (1 << axis) ? box.high[axis] : box.low[axis]);
    c.r3.u32 = local; c.r4.u32 = transform ? transform : mesh + 136; c.r5.u32 = world;
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
  GuestCall call(ctx, base);
  std::memcpy(base + call.scratch, name, std::strlen(name) + 1);
  auto& c = call.ctx;
  c.r3.u32 = dir; c.r4.u32 = call.scratch; c.r5.u32 = 0; c.r6.u32 = 0;
  sub_82402DE8(c, base);
  const auto object = c.r3.u32;
  std::lock_guard lock(mesh_mutex);
  const auto it = mesh_objects.find(object);
  if (it == mesh_objects.end() || ObjectBase(base, it->second) != object ||
      ObjectName(base, object) != name) return 0;
  return it->second;
}


void CaptureLanePowerup(PPCContext& ctx, uint8_t* base, uint32_t dir) {
  if (!GameScreenActive() || !touch::PowerupReady()) return;
  // The caller identifies powerup.grp. Its containing RndDir can be renamed
  // by nested scene loading (powerup_icon1); resolve the mesh in that directory.
  const auto mesh = SceneMesh(ctx, base, dir, "powerup_icon.mesh");
  const auto bounds = mesh ? ProjectMesh(ctx, base, mesh) : std::nullopt;
  if (bounds) {
    const float left = std::max(0.f, bounds->left), top = std::max(0.f, bounds->top);
    const float right = std::min(1.f, bounds->right), bottom = std::min(1.f, bounds->bottom);
    if (right > left && bottom > top) {
      touch::PublishLanePowerup({left, top, right - left, bottom - top});
      return;
    }
  }
  // Track scenes can draw the shared icon at a lane-relative position while
  // the directory mesh projects outside the frame. Keep the native draw as
  // the visibility signal so input can use the lane's viewport-relative slot.
  touch::PublishLanePowerup({});
}

} // namespace rb_blitz::input

REX_HOOK_RAW(sub_823F0118) {
  rb_blitz::input::SyncMesh(ctx, base);
}
