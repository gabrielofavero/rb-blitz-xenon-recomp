// SPDX-License-Identifier: GPL-2.0-only
// Headless installed-art check for the circular preboot autosave icon. It never
// starts the title or opens a game window.
#include "check.h"
#include "ui/boot_loading_art.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

using rb_blitz::ui::boot_loading_art::BootLoadingFrames;
using rb_blitz::ui::boot_loading_art::LoadBootLoadingFrames;
using rb_blitz::ui::boot_loading_art::kFrameSize;

int main() {
  namespace fs = std::filesystem;
  const fs::path root = fs::path(RBBLITZ_SOURCE_DIR) / "game";

  rb_blitz::test::BeginCase("missing spinner art fails cleanly");
  BootLoadingFrames frames;
  std::string error;
  CHECK_FALSE(LoadBootLoadingFrames(root / "missing-spinner-test", &frames, &error));
  CHECK_TRUE(!error.empty());

  rb_blitz::test::BeginCase("installed autosave spinner artwork");
  if (!fs::exists(root / "gen" / "main_xbox.hdr")) {
    std::printf("[ SKIP ] installed game ARK is absent\n");
    return rb_blitz::test::Finish();
  }
  error.clear();
  const bool loaded = LoadBootLoadingFrames(root, &frames, &error);
  if (!loaded) std::printf("[ INFO ] %s\n", error.c_str());
  CHECK_TRUE(loaded);
  if (loaded) {
    constexpr size_t kBytes = kFrameSize * kFrameSize * 4;
    for (const auto& frame : frames) CHECK_EQ(frame.size(), kBytes);
    const auto& image = frames[0];
    if (image.size() == kBytes) {
      size_t visible = 0;
      size_t textured = 0;
      for (size_t pixel = 0; pixel < kFrameSize * kFrameSize; ++pixel) {
        const auto* rgba = image.data() + pixel * 4;
        visible += rgba[3] != 0;
        textured += rgba[3] > 128;
      }
      CHECK_TRUE(visible > kFrameSize * kFrameSize * 19 / 100 &&
                 visible < kFrameSize * kFrameSize * 61 / 100);
      CHECK_TRUE(textured > kFrameSize * kFrameSize * 15 / 100);
      CHECK_EQ(image[3], 0);  // transparent corner around the circular motif
      CHECK_EQ(image[(kFrameSize * kFrameSize - 1) * 4 + 3], 0);
      CHECK_EQ(image[((kFrameSize / 2) * kFrameSize + kFrameSize / 2) * 4 + 3], 0);
    }
    CHECK_FALSE(std::equal(frames[0].begin(), frames[0].end(),
                           frames[1].begin(), frames[1].end()));
    // A quarter-cycle must advance to the next plate emblem rather than
    // repeating the entire four-emblem sheet after two seconds.
    CHECK_FALSE(std::equal(frames[0].begin(), frames[0].end(),
                           frames[60].begin(), frames[60].end()));
  }
  return rb_blitz::test::Finish();
}
