// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/render_settings.h"

#include <atomic>
#include <cstring>
#include <array>
#include <mutex>
#include "settings/resolution_profiles.h"
#include "settings/render_profile_resources.h"
#include "settings/window_policy.h"
#include "settings/boot.h"
#include "input/native_menu.h"
#include <rex/ppc/call.h>

#include "platform/guest_memory.h"

#include <rex/ppc/hook.h>
#include <rex/logging/log_category.h>
#include <rex/memory/endian.h>
#include <rex/memory/mapped_ptr.h>

namespace {
// 0 = the title derives the width from the height, exactly as it ships.
std::atomic<uint32_t> g_render_width{0};
std::atomic<uint32_t> g_render_height{720};
std::atomic<uint32_t> g_portrait_width{0}, g_portrait_height{0};
std::atomic<bool> g_interface_portrait{false}, g_gameplay_portrait{false}, g_active_portrait{false};

// The title's own screen-space -> 16:9 mapping constants (measured 2026-10-12, see the header):
// two adjacent 9/16 floats in the literal pool of the code that lays the UI out.
constexpr uint32_t kLayoutAspectLo = 0x8201E4CC;
constexpr uint32_t kLayoutAspectHi = 0x8201E4D0;
// 9/16 as big-endian float bits: what the two fields above hold on the supported retail image, and
// therefore the shape guard that keeps this write off any other image.
constexpr uint32_t kAspect16By9Inverse = 0x3F100000;
std::mutex g_profiles_mutex;
rb_blitz::settings::DisplayMode g_native;
int g_profile_scale = 0;
bool g_mobile_profiles = false;
bool g_device_profiles = false;
bool g_profile_first_frame = true;
std::map<rb_blitz::settings::SettingId, rb_blitz::settings::Value> g_profiles;

rb_blitz::settings::DisplayMode MobileProfilePixels(
    rb_blitz::settings::DisplayMode native, bool portrait, int value) {
  return rb_blitz::settings::DeviceRenderScalePixels(native, portrait, value);
}
}  // namespace

void rb_blitz::settings::ConfigureResolutionProfiles(DisplayMode native, int draw_scale, bool mobile,
    const std::map<SettingId, Value>& values, bool device) {
  std::lock_guard lock(g_profiles_mutex);
  g_native = native.width > 0 && native.height > 0 ? native : DisplayMode{1280,720};
  g_profile_scale = draw_scale;
  g_mobile_profiles = mobile;
  g_device_profiles = device;
  g_profile_first_frame = true;
  g_profiles = values;
}

bool rb_blitz::settings::QueueResolutionProfile(SettingId id, const Value& value, std::string* error) {
  std::lock_guard lock(g_profiles_mutex);
  const bool scale = id == SettingId::kInterfaceRenderScale || id == SettingId::kGameplayRenderScale;
  const bool portrait = g_device_profiles && (id == SettingId::kGameplayRenderScale
      ? GameplayPortraitEnabled() : InterfacePortraitEnabled());
  const auto pixels = scale ? MobileProfilePixels(g_native, portrait, value.integer)
                            : ProfileResolution(value.text, g_native);
  if (!IsResolutionProfile(id) || !g_profile_scale ||
      (scale ? value.kind != ValueKind::kInt || !IsDeviceRenderScaleValue(value.integer)
             : value.kind != ValueKind::kEnum) ||
      !ProfileGuestFrame(pixels, g_profile_scale).supported) {
    if (error) *error = "the resolution profile cannot be rendered by this device";
    return false;
  }
  g_profiles[id] = value;
  return true;
}

void rb_blitz::settings::SetBootRenderFrame(uint32_t width, uint32_t height) {
  // A zero height would ask the title for a degenerate frame; keep the measured 720 instead.
  const uint32_t safe_height = height == 0 ? 720 : height;
  g_render_height.store(safe_height, std::memory_order_relaxed);
  // A width of 0, and any width that is exactly 16:9 of the height, is what the title derives for
  // itself. Leaving the override off in that case keeps every existing boot on the title's own
  // arithmetic - including its 4:3 branch, which no plan would have produced.
  const bool title_derives_it = width == 0 || uint64_t(width) * 9 == uint64_t(safe_height) * 16;
  g_render_width.store(title_derives_it ? 0 : width, std::memory_order_relaxed);
}

void rb_blitz::settings::SetBootRenderHeight(uint32_t height) {
  // Keep the title's measured 540p allocation and let the Xenos backend scale
  // render targets, viewports and resolves. This is rendering at the requested
  // resolution, not resizing an already rendered frame. The 1080p guest allocation path
  // corrupts title-screen assets before the menu is reached.
  g_render_height.store(height == 0 ? 720 : height, std::memory_order_relaxed);
}

uint32_t rb_blitz::settings::BootRenderWidth() {
  return g_render_width.load(std::memory_order_relaxed);
}

uint32_t rb_blitz::settings::BootRenderHeight() {
  return g_render_height.load(std::memory_order_relaxed);
}

void rb_blitz::settings::SetBootPortraitFrame(uint32_t width, uint32_t height) {
  g_portrait_width = width;
  g_portrait_height = height;
}
void rb_blitz::settings::ConfigureOrientations(bool interface_portrait, bool gameplay_portrait) {
  g_interface_portrait = interface_portrait;
  g_gameplay_portrait = gameplay_portrait;
  g_active_portrait = interface_portrait;
}
bool rb_blitz::settings::InterfacePortraitEnabled() { return g_interface_portrait.load(); }
bool rb_blitz::settings::GameplayPortraitEnabled() { return g_gameplay_portrait.load(); }
uint32_t rb_blitz::settings::BootPortraitWidth() { return g_active_portrait ? g_portrait_width.load() : 0; }
uint32_t rb_blitz::settings::BootPortraitHeight() { return g_active_portrait ? g_portrait_height.load() : 0; }
uint32_t rb_blitz::settings::GameplayPortraitWidth() { return g_gameplay_portrait ? g_portrait_width.load() : 0; }
uint32_t rb_blitz::settings::GameplayPortraitHeight() { return g_gameplay_portrait ? g_portrait_height.load() : 0; }

// 0x823F382C, the title's own `li r11,720`, before it stores its choice at +68 and derives the
// width from it. Setting r11 here is what lets the title lay out at a height it would not have
// chosen (config/functions.toml carries the entry; src/diag/probe_points.cpp forces the
// half-resolution flag when that height is 540, so the title takes its own 540 path).
void RenderHeightHook(PPCRegister& r11) {
  r11.u32 = rb_blitz::settings::BootRenderHeight();
}

// 0x823F3878, the `stw r7,64(r30)` that publishes the frame width, *before* it runs. The title
// reaches here through either aspect branch:
//
//   0x823F385C  divw r7,height*4,3     (4:3)     -> r29 too
//   0x823F3870  divw r7,height*16,9    (16:9)    -> r29 too
//   0x823F3878  stw r7,64(r30)
//
// r7 is the width field; r29 is the same width carried into the two-rect table the function
// builds right after (+848), so both have to agree or the frame and its viewport disagree.
// With no override (width 0) neither register is touched and the title's own arithmetic stands.
void RenderFrameWidthHook(PPCRegister& r7, PPCRegister& r29) {
  const uint32_t width = rb_blitz::settings::BootRenderWidth();
  if (width == 0) {
    return;
  }
  r7.u32 = width;
  r29.u32 = width;
}

namespace {
// Big-endian helpers: the guest is a big-endian machine and these are its own floats.
uint32_t LoadGuestBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void StoreGuestBe32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}
}  // namespace

void rb_blitz::settings::WriteGuestLayoutAspect(uint8_t* base) {
  uint32_t width = g_render_width.load(std::memory_order_relaxed);
  uint32_t height = g_render_height.load(std::memory_order_relaxed);
  {
    std::lock_guard lock(g_profiles_mutex);
    if (g_profile_scale) {
      // Scene proportions are selected at boot. Pixel rounding in lower tiers
      // must not alter camera ratios or mutate already-loaded scene layouts.
      width = std::max(g_native.width, g_native.height);
      height = std::min(g_native.width, g_native.height);
    }
  }
  // Returning from a custom frame to 16:9 must restore the original layout constants.
  if (base == nullptr || height == 0) {
    return;
  }
  const float inverse_aspect = width ? float(height) / float(width) : 9.0f / 16.0f;
  const uint32_t bits = [&] {
    uint32_t value = 0;
    std::memcpy(&value, &inverse_aspect, sizeof(value));
    return value;
  }();
  static uint32_t written = 0;
  if (written == bits) {
    return;  // already this boot's value; do not touch the image again
  }
  // The literal pool is inside the image's code region and the page is read-only (the first
  // attempt on the sibling 16/9 constant was refused as a guest access violation). Our own
  // process owns the page, so it is opened for the write and closed again.
  const auto write_guarded = [&](uint32_t address, uint32_t guard, uint32_t value,
                                 const char* what) -> bool {
    uint8_t* field = rex::memory::GuestPtr<uint8_t*>(base, address);
    if (field == nullptr) {
      return false;
    }
    const uint32_t before = LoadGuestBe32(field);
    // Shape guard: these addresses are only meaningful on the image they were measured on. If the
    // field does not hold its measured value, the image is not that image, and writing a float
    // into whatever is there would be worse than leaving the layout alone.
    if (before != guard && before != written) {
      REXLOG_WARN("render: 0x{:08X} holds {:#010X}, not the measured {:#010X}; {} left alone "
                  "(unsupported image?)",
                  address, before, guard, what);
      return false;
    }
    if (!rb_blitz::platform::WriteProtectedGuestBe32(field, value)) {
      REXLOG_WARN("render: 0x{:08X} could not be opened for the {}", address, what);
      return false;
    }
    REXLOG_INFO("render: {} 0x{:08X} {:#010X} -> {:#010X}", what, address, before, value);
    return true;
  };
  size_t done = 0;
  for (const uint32_t address : {kLayoutAspectLo, kLayoutAspectHi}) {
    if (write_guarded(address, kAspect16By9Inverse, bits, "layout aspect")) {
      ++done;
    }
  }
  // Only latch once the writes really landed, so a boot that reached here before the fields were
  // mapped (or on an image whose guard refused) tries again rather than silently giving up.
  if (done == 2) {
    written = bits;
  }
}

REX_EXTERN(sub_823F1318);  // X360 render-target/depth pair allocation
REX_EXTERN(sub_82508428); // D3D texture allocation
REX_EXTERN(sub_8250EC88); // D3DResource::Release, including deferred GPU destruction
REX_EXTERN(sub_82508270); // texture LockRect
REX_EXTERN(sub_82506828); // texture UnlockRect
REX_EXTERN(sub_823E9CE0); // bind the current color/depth pair and invalidate the material cache
REX_EXTERN(sub_823ED1D8); // RndTex wrapper: bind texture and refresh its dimensions/format
REX_EXTERN(sub_825087A8); // D3D device SetTexture, including raw sampler pointer tracking
REX_EXTERN(__imp__sub_82203260); // lazy frame begin, before native tiling captures its rectangles

void rb_blitz::settings::UpdateResolutionProfile(PPCContext& caller, uint8_t* base, uint32_t renderer) {
  DisplayMode pixels;
  int scale;
  bool first_frame;
  SettingId active;
  Value selected;
  const bool game = input::GameplayResolutionActive();
  g_active_portrait = input::GameScreenActive() ? GameplayPortraitEnabled() : InterfacePortraitEnabled();
  {
    std::lock_guard lock(g_profiles_mutex);
    scale = g_profile_scale;
    first_frame = g_profile_first_frame;
    if (!scale || g_profiles.empty()) return;
    active = g_mobile_profiles ? (game ? SettingId::kGameplayRenderScale : SettingId::kInterfaceRenderScale)
                               : (game ? SettingId::kGameplayResolution : SettingId::kInterfaceResolution);
    selected = g_profiles.at(active);
    pixels = g_mobile_profiles
        ? MobileProfilePixels(g_native, g_device_profiles &&
            (game ? GameplayPortraitEnabled() : InterfacePortraitEnabled()),
            g_profiles.at(game ? SettingId::kGameplayRenderScale
                                                             : SettingId::kInterfaceRenderScale).integer)
        : ProfileResolution(g_profiles.at(game ? SettingId::kGameplayResolution
                                              : SettingId::kInterfaceResolution).text, g_native);
  }
  const auto plan = ProfileGuestFrame(pixels, scale);
  const auto acknowledge = [&](bool applied, const std::string& error = {}) {
    if (auto* runtime = boot::SettingsBoot::Current())
      runtime->AcknowledgeResolutionProfile(active, selected, applied, error);
  };
  if (!base || !renderer || !plan.supported) return;
  if (auto* runtime = boot::SettingsBoot::Current();
      runtime && !runtime->BeginResolutionProfileFrame(active, selected)) return;
  const auto read = [&](uint32_t offset) { return LoadGuestBe32(base + renderer + offset); };
  const uint32_t w = plan.guest_size.width, h = plan.guest_size.height;
  const uint32_t flags = read(796);
  const bool tiled = !(flags & 1), vertical = (flags & 2) != 0;
  const uint32_t tile_w = tiled && !vertical ? ProfileTileExtent(w) : w;
  const uint32_t tile_h = tiled && vertical ? ProfileTileExtent(h) : h;
  static DisplayMode failed;
  const bool initial_tiles_match = !tiled || (read(848) == 2 &&
      read(852) == 0 && read(856) == 0 && read(860) == tile_w && read(864) == tile_h &&
      read(868) == (vertical ? 0 : tile_w) && read(872) == (vertical ? tile_h : 0) &&
      read(876) == w && read(880) == h);
  if (read(64) == w && read(68) == h && (!first_frame || initial_tiles_match)) {
    { std::lock_guard lock(g_profiles_mutex); g_profile_first_frame = false; }
    failed = {};
    acknowledge(true);
    return;
  }
  if (failed.width == int(w) && failed.height == int(h)) return;
  if (!read(452) || !read(800) || !read(808)) return;

  // Build a complete replacement before publishing any pointer or size. The
  // guest allocator owns EDRAM layout and resource lifetime. The multiplier
  // stays fixed; obsolete backend caches are pruned after the GPU frame ends.
  PPCContext ctx = caller;
  rex::ppc::OpenCallFrame(ctx, base, 768);
  const uint32_t scratch = ctx.r1.u32 + 128;
  std::memset(base + scratch, 0, 384);
  constexpr std::array<uint32_t, 9> fields{800,808,804,812,816,820,752,756,760};
  std::array<uint32_t, 9> old{}, fresh{};
  for (size_t i = 0; i < fields.size(); ++i) old[i] = read(fields[i]);
  const auto pair = [&](int index, uint32_t samples, uint32_t width, uint32_t height) {
    ctx.r3.u32 = width; ctx.r4.u32 = height; ctx.r5.u32 = samples;
    ctx.r6.u32 = scratch + 64; ctx.r7.u32 = scratch + 68;
    ctx.r8.u32 = scratch + index * 4; ctx.r9.u32 = scratch + (index + 1) * 4;
    sub_823F1318(ctx, base);
  };
  pair(0, flags & 1 ? 1 : 0, w, h);
  pair(2, 1, tile_w, tile_h);
  const auto texture = [&](int index, uint32_t format) {
    ctx.r3.u32 = w; ctx.r4.u32 = h; ctx.r5.u32 = 1; ctx.r6.u32 = 1;
    ctx.r7.u32 = 0; ctx.r8.u32 = format; ctx.r9.u32 = 0; ctx.r10.u32 = 3;
    StoreGuestBe32(base + ctx.r1.u32 + 88, scratch + index * 4);
    sub_82508428(ctx, base);
    StoreGuestBe32(base + scratch + index * 4, ctx.r3.u32);
  };
  texture(4, 0x18280186); texture(5, 0x18280186);
  texture(6, 0x18280106); texture(7, 0x18280106); texture(8, 0x1A220197);
  for (size_t i = 0; i < fresh.size(); ++i) fresh[i] = LoadGuestBe32(base + scratch + i * 4);
  const auto release = [&](const auto& resources) {
    for (auto resource : resources) if (resource) { ctx.r3.u32 = resource; sub_8250EC88(ctx, base); }
  };
  if (std::any_of(fresh.begin(), fresh.end(), [](auto p) { return p == 0; })) {
    release(fresh);
    failed = {int(w), int(h)};
    acknowledge(false, "render target allocation failed; the previous frame is retained");
    REXLOG_WARN("render: {}x{} profile allocation refused; keeping the previous frame", w, h);
    return;
  }
  const auto lock_texture = [&](uint32_t resource) {
    std::memset(base + scratch + 128, 0, 8);
    ctx.r3.u32 = resource; ctx.r4.u32 = 0; ctx.r5.u32 = scratch + 128;
    ctx.r6.u32 = 0; ctx.r7.u32 = 0;
    sub_82508270(ctx, base);
    return ProfileTextureLock{LoadGuestBe32(base + scratch + 128),
                              LoadGuestBe32(base + scratch + 132)};
  };
  const auto clear_texture = [&](uint32_t data, size_t bytes) {
    std::memset(base + data, 0, bytes);
  };
  const auto unlock_texture = [&](uint32_t resource) {
    ctx.r3.u32 = resource; ctx.r4.u32 = 0; sub_82506828(ctx, base);
  };
  if (!InitializeProfileResolveTextures({fresh[6], fresh[7]}, w, h,
                                       lock_texture, clear_texture, unlock_texture)) {
    release(fresh);
    failed = {int(w), int(h)};
    acknowledge(false, "render texture initialization failed; the previous frame is retained");
    REXLOG_WARN("render: {}x{} texture initialization refused; keeping the previous frame", w, h);
    return;
  }
  const auto put = [&](uint32_t offset, uint32_t value) { StoreGuestBe32(base + renderer + offset, value); };
  for (size_t i = 0; i < fields.size(); ++i) put(fields[i], fresh[i]);
  put(64, w); put(68, h); put(468, w); put(472, h); put(572, w); put(576, h);
  put(840, LoadGuestBe32(base + scratch + 64)); put(844, LoadGuestBe32(base + scratch + 68));
  // The full-resolution path tiles two rectangles across the same frame.
  if (tiled) {
    put(848, 2);
    for (uint32_t i = 0; i < 2; ++i) {
      put(852 + i * 16, vertical ? 0 : i * tile_w);
      put(856 + i * 16, vertical ? i * tile_h : 0);
      put(860 + i * 16, vertical || i == 1 ? w : tile_w);
      put(864 + i * 16, !vertical || i == 1 ? h : tile_h);
    }
  }
  SetBootRenderFrame(w, h);
  {
    const auto crop = PortraitCropSize({int(w), int(h)});
    SetBootPortraitFrame(crop.width, crop.height);
  }
  WriteGuestLayoutAspect(base);
  const uint32_t device = read(452);
  RebindProfileTextureUsers({old[4], old[5], old[6], old[7], old[8]},
      {fresh[4], fresh[5], fresh[6], fresh[7], fresh[8]}, {read(824), read(828), read(832)},
      [&](uint32_t stage) { return LoadGuestBe32(base + device + 12920 + stage * 4); },
      [&](uint32_t stage, uint32_t texture) {
        ctx.r3.u32 = device; ctx.r4.u32 = stage; ctx.r5.u32 = texture;
        ctx.r6.u64 = uint32_t{0x80000000} >> stage;
        sub_825087A8(ctx, base);
      },
      [&](uint32_t wrapper, uint32_t texture) {
        ctx.r3.u32 = wrapper; ctx.r4.u32 = texture;
        sub_823ED1D8(ctx, base);
      });
  // Xbox target bindings store raw surface pointers, without AddRef. Replace
  // those bindings and invalidate the material cache before releasing them.
  // Wrappers and sampler slots above must also stop referring to old textures:
  // a later native SetTexture writes through its previous raw resource pointer.
  ctx.r3.u32 = renderer;
  sub_823E9CE0(ctx, base);
  release(old);
  { std::lock_guard lock(g_profiles_mutex); g_profile_first_frame = false; }
  failed = {};
  acknowledge(true);
  caller.fpscr = ctx.fpscr;
  REXLOG_INFO("render: {} profile {}x{} -> guest {}x{} at {}x", game ? "gameplay" : "interface",
              pixels.width, pixels.height, w, h, scale);
}

REX_HOOK_RAW(sub_82203260) {
  // This function can be called repeatedly inside a frame. Only its first call
  // starts tiling (+247 becomes true). BeginDrawing is already too late: an
  // earlier caller may have captured the old tile rectangles for that frame.
  if (base && ctx.r3.u32 && base[ctx.r3.u32 + 247] == 0)
    rb_blitz::settings::UpdateResolutionProfile(ctx, base, ctx.r3.u32);
  __imp__sub_82203260(ctx, base);
}
