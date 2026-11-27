// SPDX-License-Identifier: GPL-2.0-only
// Production camera hooks with isolated guest memory; no game/UI navigation.
#include "check.h"
#include "hooks/render_settings.h"
#include "settings/portrait_render_target.h"
#include "settings/resolution_profiles.h"
#include "input/native_menu.h"

#include <array>
#include <cmath>
#include <limits>
#include <rex/memory/endian.h>
#include <rex/ppc/hook.h>

REX_EXTERN(sub_821FF728);
REX_EXTERN(sub_821FFBC8);
REX_EXTERN(sub_821FF8E8);
REX_EXTERN(sub_821FFD30);

namespace {
constexpr uint32_t kCamera = 0x1000;
float target_scale = 1.f;
unsigned projection_calls = 0, cache_calls = 0, select_calls = 0;
unsigned destroy_calls = 0;
float authored_x = 1.25f;
bool orthographic = false;
void Close(float actual, float expected) { CHECK_TRUE(std::abs(actual - expected) < .0001f); }
void Put(uint8_t* base, uint32_t address, float value) {
  rex::memory::store_and_swap<float>(base + address, value);
}
float Get(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<float>(base + address);
}
void ResetCamera(uint8_t* base, uint32_t camera) {
  std::memset(base + camera, 0, 896);
  Put(base, camera + 736, 1); Put(base, camera + 740, 1);
}
void Rebuild(PPCContext& ctx, uint8_t* base, uint32_t camera) {
  const auto old_stack = ctx.r1.u64;
  ctx.r3.u32 = camera;
  sub_821FF728(ctx, base);
  CHECK_EQ(ctx.r1.u64, old_stack);
}
void Select(PPCContext& ctx, uint8_t* base, uint32_t camera) {
  const auto old_stack = ctx.r1.u64;
  ctx.r3.u32 = camera;
  sub_821FFBC8(ctx, base);
  CHECK_EQ(ctx.r1.u64, old_stack);
  CHECK_EQ(ctx.r3.u32, camera);
}
}  // namespace

float rb_blitz::settings::PortraitRenderTargetProjectionScale() { return target_scale; }
extern "C" REX_FUNC(sub_821FF8E8) {
  ++cache_calls;
  const auto camera = ctx.r3.u32;
  // Camera +384 is the matrix consumed by native WorldToScreen. The fixture
  // uses an identity view, so native cache rebuilding copies the projection.
  std::memcpy(base + camera + 384, base + camera + 256, 64);
  std::memcpy(base + camera + 448, base + camera + 320, 64);
  std::memcpy(base + camera + 608, base + camera + 512, 96);
}
extern "C" REX_FUNC(__imp__sub_821FF728) {
  ++projection_calls;
  const auto camera = ctx.r3.u32;
  Put(base, camera + 256, authored_x);
  Put(base, camera + 292, -2.5f);
  Put(base, camera + 312, .125f);
  Put(base, camera + 320, 1.f / authored_x);
  Put(base, camera + 344, -.4f);
  const float norm = std::hypot(authored_x, 1.f);
  const std::array<std::array<float, 4>, 6> planes{{
      {0, 1, 0, -.25f}, {0, -1, 0, 100},
      orthographic ? std::array<float,4>{1, 0, 0, 1.f / authored_x}
                   : std::array<float,4>{authored_x / norm, 1.f / norm, 0, 0},
      orthographic ? std::array<float,4>{-1, 0, 0, 1.f / authored_x}
                   : std::array<float,4>{-authored_x / norm, 1.f / norm, 0, 0},
      {0, .4f, -.9165151f, 0}, {0, .4f, .9165151f, 0}}};
  for (size_t i = 0; i < planes.size(); ++i)
    for (size_t j = 0; j < planes[i].size(); ++j)
      Put(base, camera + 512 + uint32_t(i * 16 + j * 4), planes[i][j]);
  sub_821FF8E8(ctx, base);
}
extern "C" REX_FUNC(__imp__sub_821FFBC8) {
  ++select_calls;
}
extern "C" REX_FUNC(__imp__sub_821FFD30) {
  CHECK_EQ(ctx.r3.u32, kCamera + 900);
  ++destroy_calls;
}

int main() {
  using namespace rb_blitz::test;
  using namespace rb_blitz::settings;
  BeginCase("active visual orientation selects compact gameplay, pause and interface targets");
  struct Context { bool song, game_quality, ui_portrait, song_portrait, authored, compact; };
  for (const auto context : {
      Context{true, true, false, true, false, true},   // Portrait song, landscape menus.
      Context{true, true, true, false, false, false},  // Landscape song, portrait menus.
      Context{false, true, true, false, false, true},  // Pause/quit uses Interface orientation.
      Context{false, true, false, true, false, false},
      Context{false, false, true, false, true, true},  // Adapted settings/library.
      Context{false, false, false, true, true, false},
      Context{false, false, true, true, false, false}, // Unadapted interface remains fitted.
      Context{true, false, false, true, false, true}}) // Cached quality may lag a scene boundary.
    CHECK_EQ(CompactPortraitContext(context.song, context.game_quality, context.ui_portrait,
                                    context.song_portrait, context.authored), context.compact);
  BeginCase("compact gameplay targets allocate exactly the existing centered crop");
  for (const auto native : {DisplayMode{2622, 1206}, {2556, 1179}, {2400, 1080},
                            {1920, 1080}, {2732, 2048}}) {
    const auto full = DeviceRenderScalePixels(native, true, 10);
    const int scale = SelectedResolutionProfileDrawScale(full, full);
    for (const auto& choice : DeviceRenderScaleChoices(native, true)) {
      const auto source = ProfileGuestFrame(DeviceRenderScalePixels(native, true,
          std::stoi(choice.token)), scale);
      const auto target = CompactPortraitRenderTarget(source, true);
      CHECK_TRUE(source.supported && target.supported);
      CHECK_EQ(target.draw_scale, source.draw_scale);
      CHECK_EQ(target.guest_size.height, source.guest_size.height);
      CHECK_TRUE(target.guest_size.width < target.guest_size.height);
      CHECK_EQ(target.guest_size.width, uint32_t(std::lround(
          double(source.guest_size.height) * source.guest_size.height / source.guest_size.width)));
      const auto unchanged = CompactPortraitRenderTarget(source, false);
      CHECK_EQ(unchanged.guest_size.width, source.guest_size.width);
      CHECK_EQ(unchanged.guest_size.height, source.guest_size.height);
      const auto factor = PortraitTargetProjectionScale(source.guest_size.width, target.guest_size.width);
      // Independent oracle: transforming source pixels into the old centered
      // crop equals projecting directly into the compact target, at both edges.
      for (float clip_x : {-1.f, -.5f, -.1f, 0.f, .1f, .5f, 1.f}) {
        const auto source_x = (clip_x * .5f + .5f) * source.guest_size.width;
        const auto cropped_x = source_x - (source.guest_size.width - target.guest_size.width) * .5f;
        const auto target_x = (clip_x * factor * .5f + .5f) * target.guest_size.width;
        CHECK_TRUE(std::abs(cropped_x - target_x) < .001f);
      }
    }
  }
  CHECK_FALSE(CompactPortraitRenderTarget({}, true).supported);
  Close(PortraitTargetProjectionScale(0, 100), 1);
  Close(PortraitTargetProjectionScale(100, 0), 1);

  BeginCase("lane pointer coordinates follow the actual compact frontbuffer aspect");
  const auto portrait_source = PresentationPointerSource({332, 720}, {1565, 720}, true);
  CHECK_EQ(portrait_source.width, 332);
  CHECK_EQ(portrait_source.height, 720);
  for (const auto point : {rb_blitz::input::MenuPoint{.2f, .15f},
                           rb_blitz::input::MenuPoint{.5f, .5f},
                           rb_blitz::input::MenuPoint{.8f, .85f}}) {
    const auto window = rb_blitz::input::MenuPointToWindow(point, 332, 720,
        portrait_source.width, portrait_source.height, true, false, 100, 100);
    Close(window.x, point.x * 332);
    Close(window.y, point.y * 720);
    const auto back = rb_blitz::input::MenuPointer(window.x, window.y, 332, 720,
        portrait_source.width, portrait_source.height, true, false, 100, 100);
    Close(back.x, point.x); Close(back.y, point.y);
  }
  const auto native_video = PresentationPointerSource({332, 720}, {1565, 720}, false);
  CHECK_EQ(native_video.width, 1565);
  const auto derived = PresentationPointerSource({0, 540}, {1565, 720}, true);
  CHECK_EQ(derived.width, 960);
  const auto invalid_frame = PresentationPointerSource({0, 0}, {1565, 720}, true);
  CHECK_EQ(invalid_frame.width, 1565);

  std::array<uint8_t, 0x10000> memory{};
  auto* base = memory.data();
  PPCContext ctx{}; ctx.r1.u32 = 0xF000;
  ResetCamera(base, kCamera);
  BeginCase("compact projection changes horizontal coverage and native pointer matrices together");
  target_scale = 4;
  Rebuild(ctx, base, kCamera);
  Close(Get(base, kCamera + 256), authored_x * 4);
  Close(Get(base, kCamera + 320), 1.f / (authored_x * 4));
  Close(Get(base, kCamera + 384), authored_x * 4);
  Close(Get(base, kCamera + 448), 1.f / (authored_x * 4));
  Close(Get(base, kCamera + 292), -2.5f);
  Close(Get(base, kCamera + 312), .125f);
  const auto first_calls = projection_calls;
  Select(ctx, base, kCamera);
  Select(ctx, base, kCamera);
  CHECK_EQ(projection_calls, first_calls);
  Close(Get(base, kCamera + 256), authored_x * 4);

  BeginCase("existing cameras refresh once per target change without accumulating scale");
  target_scale = 3;
  Select(ctx, base, kCamera);
  CHECK_EQ(projection_calls, first_calls + 1);
  Close(Get(base, kCamera + 256), authored_x * 3);
  Close(Get(base, kCamera + 384), authored_x * 3);
  authored_x = 2.f;
  Rebuild(ctx, base, kCamera);
  Close(Get(base, kCamera + 256), 6.f);
  target_scale = 1;
  Select(ctx, base, kCamera);
  Close(Get(base, kCamera + 256), 2.f);
  Close(Get(base, kCamera + 320), .5f);
  Close(Get(base, kCamera + 384), 2.f);
  const auto restored_calls = projection_calls;
  Select(ctx, base, kCamera);
  CHECK_EQ(projection_calls, restored_calls);

  BeginCase("perspective culling rejects invisible side bands while preserving sphere-radius margins");
  target_scale = 4;
  orthographic = false;
  Rebuild(ctx, base, kCamera);
  for (const uint32_t offset : {544u, 560u}) {
    const float nx = Get(base, kCamera + offset), ny = Get(base, kCamera + offset + 4);
    Close(std::hypot(nx, ny), 1);
    Close(std::abs(nx / ny), authored_x * target_scale);
    Close(Get(base, kCamera + offset + 12), 0);
    Close(Get(base, kCamera + offset + 96), nx);
    Close(Get(base, kCamera + offset + 100), ny);
    const auto signed_distance = [&](float x, float depth) { return nx * x + ny * depth; };
    const float boundary = 10.f / (authored_x * target_scale);
    const float outside_x = std::copysign(boundary + .5f, -nx);
    CHECK_TRUE(signed_distance(outside_x, 10) < -.25f);
    const float near_edge_x = std::copysign(boundary + .01f, -nx);
    CHECK_TRUE(signed_distance(near_edge_x, 10) >= -.25f);
    CHECK_TRUE(signed_distance(0, 10) > 0);
  }
  Close(Get(base, kCamera + 512 + 12), -.25f);
  Close(Get(base, kCamera + 528 + 12), 100);
  Close(Get(base, kCamera + 576 + 4), .4f);
  Close(Get(base, kCamera + 576 + 8), -.9165151f);
  Close(Get(base, kCamera + 592 + 8), .9165151f);

  BeginCase("orthographic culling shrinks only horizontal distances and restores on mode changes");
  orthographic = true;
  Rebuild(ctx, base, kCamera);
  Close(Get(base, kCamera + 544), 1);
  Close(Get(base, kCamera + 560), -1);
  Close(Get(base, kCamera + 556), 1.f / (authored_x * target_scale));
  Close(Get(base, kCamera + 572), 1.f / (authored_x * target_scale));
  Close(Get(base, kCamera + 576 + 4), .4f);
  target_scale = 1;
  Select(ctx, base, kCamera);
  Close(Get(base, kCamera + 556), 1.f / authored_x);
  Close(Get(base, kCamera + 256), authored_x);
  orthographic = false;
  CHECK_EQ(PortraitTargetHorizontalPlane({1, 0, 0, 1},
      std::numeric_limits<float>::infinity())[3], 1.f);
  CHECK_EQ(PortraitTargetHorizontalPlane({0, 0, 0, 1}, 4)[3], 1.f);

  BeginCase("offscreen and partial-viewport cameras retain their own projections");
  target_scale = 4;
  rex::memory::store_and_swap<uint32_t>(base + kCamera + 756, 0x4000);
  Rebuild(ctx, base, kCamera);
  Close(Get(base, kCamera + 256), authored_x);
  rex::memory::store_and_swap<uint32_t>(base + kCamera + 756, 0);
  Put(base, kCamera + 736, .5f);
  Rebuild(ctx, base, kCamera);
  Close(Get(base, kCamera + 256), authored_x);
  Put(base, kCamera + 736, 1.f);
  Put(base, kCamera + 728, .1f);
  Rebuild(ctx, base, kCamera);
  Close(Get(base, kCamera + 256), authored_x);
  Put(base, kCamera + 728, 0.f);
  Select(ctx, base, kCamera);
  Close(Get(base, kCamera + 256), authored_x * 4);
  BeginCase("destroyed cameras do not leave projection state at recycled guest addresses");
  ctx.r3.u32 = kCamera + 900;
  sub_821FFD30(ctx, base);
  CHECK_EQ(destroy_calls, 1u);
  ResetCamera(base, kCamera);
  const auto calls_before_reuse = projection_calls;
  Select(ctx, base, kCamera);
  CHECK_EQ(projection_calls, calls_before_reuse + 1);
  Close(Get(base, kCamera + 256), authored_x * 4);
  BeginCase("publishing a target refreshes a camera selected before lazy frame begin");
  target_scale = 1;
  Select(ctx, base, kCamera);
  Close(Get(base, kCamera + 384), authored_x);
  const auto calls_before_publication = projection_calls;
  target_scale = 4;
  ctx.r3.u32 = 0x12345678;
  const auto prior_stack = ctx.r1.u64;
  RefreshPortraitRenderTargetCamera(ctx, base, kCamera);
  CHECK_EQ(projection_calls, calls_before_publication + 1);
  CHECK_EQ(ctx.r3.u32, 0x12345678u);
  CHECK_EQ(ctx.r1.u64, prior_stack);
  Close(Get(base, kCamera + 384), authored_x * 4);
  RefreshPortraitRenderTargetCamera(ctx, base, kCamera);
  CHECK_EQ(projection_calls, calls_before_publication + 1);
  RefreshPortraitRenderTargetCamera(ctx, base, 0);
  CHECK_EQ(projection_calls, calls_before_publication + 1);
  CHECK_TRUE(cache_calls > projection_calls);
  CHECK_TRUE(select_calls > 0);
  return Finish();
}
