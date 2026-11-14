// SPDX-License-Identifier: GPL-2.0-only
// Ultimate's existing results button calls Rnd::ScreenDumpUnique (82205C88).
// Replace the guest texture/file route with the presenter's guest-image capture,
// shared by D3D12 and Vulkan. No desktop pixels or host overlays are captured.
#include "hooks/screenshots.h"
#include "fs/screenshots.h"

#include <rex/hook.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/xmemory.h>
#include <rex/ui/presenter.h>

REX_EXTERN(__imp__sub_82205C88);

namespace rb_blitz::screenshots {
namespace {
rex::Runtime* g_runtime = nullptr;
std::filesystem::path g_directory;
}  // namespace

void Configure(rex::Runtime* runtime) {
  g_runtime = runtime;
  g_directory = PicturesDirectory(runtime->user_data_root());
  REXLOG_INFO("screenshots: results captures use 24-bit BMP in {}", g_directory.string());
}

void Capture(PPCContext& ctx, uint8_t* base) {
  if (!g_runtime) { __imp__sub_82205C88(ctx, base); return; }
  // ABI: r3 is Rnd*, r4 is a plain C string (the prefix), not a String object.
  // The original passes r4 through to ScreenDumpUniqueName's format argument.
  std::string prefix;
  if (ctx.r4.u32) {
    const char* text = rex::memory::GuestPtr<const char*>(base, ctx.r4.u32);
    if (text) for (size_t i = 0; i < 1024 && text[i]; ++i) prefix += text[i];
  }
  auto* graphics = g_runtime->graphics_system();
  auto* presenter = graphics ? graphics->presenter() : nullptr;
  rex::ui::RawImage image;
  if (!presenter || !presenter->CaptureGuestOutput(image)) {
    REXLOG_ERROR("screenshots: could not capture the results image");
    return;
  }
  std::filesystem::path saved;
  std::string error;
  if (!SaveBitmap(g_directory, prefix, image.width, image.height, image.stride,
                  image.data, saved, error)) {
    REXLOG_ERROR("screenshots: could not save to {}: {}", g_directory.string(), error);
    return;
  }
  REXLOG_INFO("screenshots: saved {}x{} to {}", image.width, image.height, saved.string());
}
}  // namespace rb_blitz::screenshots

extern "C" REX_FUNC(sub_82205C88) {
  rb_blitz::screenshots::Capture(ctx, base);
}
