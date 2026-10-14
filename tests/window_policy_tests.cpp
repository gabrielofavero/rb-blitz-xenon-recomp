// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for src/settings/window_policy.h - how large the game's *window* is (P2).
//
// The policy is arithmetic over measurements the boot takes, so it is tested without a boot, a
// window or a GPU, the same way the HUD re-anchor rule and the fingerprint tests are.
//
// Three properties are worth more than the individual numbers, and each is checked as a property
// rather than as a list of expected outputs, because the property is the requirement:
//
//   * a request the desktop can hold is returned *unchanged* (so every existing 16:9 boot keeps
//     the exact size it asked for, and "nothing changed" is not something to remember);
//   * a shrunk window keeps the requested aspect (black bars come back the moment it does not);
//   * an unmeasured desktop changes nothing at all (a failed monitor query is not evidence).

#include <cstdint>
#include <cstdio>

#include "check.h"
#include "settings/window_policy.h"

namespace {

using rb_blitz::settings::ClientFit;
using rb_blitz::settings::DesktopMetrics;
using rb_blitz::settings::DisplayMode;
using rb_blitz::settings::FitClientToDesktop;
using rb_blitz::settings::kMinClientSize;
using rb_blitz::settings::PhysicalToLogical;
using rb_blitz::settings::SameAspect;
using rb_blitz::test::BeginCase;
using rb_blitz::test::Finish;

// The frame a standard Windows decorated window adds, measured shape: 16 px of width (2 x 8 px
// borders) and 39 px of height (31 px caption + 8 px bottom border) at 100%.
constexpr DisplayMode kFrame{16, 39};

DesktopMetrics Desktop(int32_t width, int32_t height, DisplayMode frame = kFrame,
                       uint32_t scale_percent = 100) {
  DesktopMetrics desktop;
  desktop.available = true;
  desktop.work_area = {width, height};
  desktop.frame_extent = frame;
  desktop.scale_percent = scale_percent;
  return desktop;
}

}  // namespace

int main() {
  BeginCase("a request the desktop can hold comes back unchanged");
  {
    // The 16:9 sizes this project offers, each on a desktop comfortably larger than it.
    const DisplayMode sizes[] = {{960, 540}, {1280, 720}, {1920, 1080}, {3840, 2160}};
    for (const DisplayMode& size : sizes) {
      const ClientFit fit = FitClientToDesktop(size, Desktop(5120, 2880));
      CHECK_EQ(fit.client.width, size.width);
      CHECK_EQ(fit.client.height, size.height);
      CHECK_FALSE(fit.shrunk);
    }
    // Exactly-fitting is still fitting: the decorated window has to be inside the work area, and
    // the work area equals the display mode in this case.
    const ClientFit exact = FitClientToDesktop({2560, 1080}, Desktop(2560 + kFrame.width, 1080 + kFrame.height));
    CHECK_EQ(exact.client.width, 2560);
    CHECK_EQ(exact.client.height, 1080);
    CHECK_FALSE(exact.shrunk);
  }

  BeginCase("a request the desktop cannot hold is shrunk, and keeps its aspect");
  {
    // The owner's case: a 2560x1080 render size on a 2560x1080 display whose work area is 1040 px
    // tall. The window must still be 21:9 - the picture is scaled, never reshaped.
    const ClientFit fit = FitClientToDesktop({2560, 1080}, Desktop(2560, 1040));
    CHECK_TRUE(fit.shrunk);
    CHECK_EQ(fit.client.height, 1040 - kFrame.height);  // 1001
    CHECK_EQ(fit.client.width, 2373);                   // round(1001 * 2560 / 1080)
    CHECK_TRUE(SameAspect(fit.client, {2560, 1080}));
    CHECK_TRUE(fit.client.width <= 2560 - kFrame.width);
    CHECK_TRUE(fit.client.height <= 1040 - kFrame.height);
  }
  {
    // The other binding axis: a 16:9 request on a desktop that is wide enough but too short.
    const ClientFit fit = FitClientToDesktop({1920, 1080}, Desktop(2560, 1040));
    CHECK_TRUE(fit.shrunk);
    CHECK_EQ(fit.client.height, 1001);
    CHECK_EQ(fit.client.width, 1780);
    CHECK_TRUE(SameAspect(fit.client, {1920, 1080}));
  }
  {
    // A width-bound desktop: tall enough, not wide enough.
    const ClientFit fit = FitClientToDesktop({2560, 1080}, Desktop(2000, 1200));
    CHECK_TRUE(fit.shrunk);
    CHECK_EQ(fit.client.width, 2000 - kFrame.width);  // 1984
    CHECK_EQ(fit.client.height, 837);                 // round(1984 * 1080 / 2560)
    CHECK_TRUE(SameAspect(fit.client, {2560, 1080}));
  }
  {
    // A 4K request on a 1080p desktop is a real case (a stored 3840x2160 request with the monitor
    // now smaller): it shrinks to the desktop, not to 720p.
    const ClientFit fit = FitClientToDesktop({3840, 2160}, Desktop(1920, 1080));
    CHECK_TRUE(fit.shrunk);
    CHECK_EQ(fit.client.height, 1080 - kFrame.height);  // 1041
    CHECK_EQ(fit.client.width, 1851);                   // round(1041 * 3840 / 2160)
    CHECK_TRUE(SameAspect(fit.client, {3840, 2160}));
  }

  BeginCase("the fit never goes below the floor, and keeps the shape there");
  {
    const ClientFit fit = FitClientToDesktop({3840, 2160}, Desktop(700, 300));
    CHECK_TRUE(fit.shrunk);
    CHECK_EQ(fit.client.width, kMinClientSize.width);
    CHECK_EQ(fit.client.height, 360);  // round(640 * 2160 / 3840) == the floor's own height
    CHECK_TRUE(SameAspect(fit.client, {3840, 2160}));
  }

  BeginCase("an unmeasured desktop changes nothing");
  {
    DesktopMetrics nowhere;  // available == false
    const ClientFit fit = FitClientToDesktop({3840, 2160}, nowhere);
    CHECK_EQ(fit.client.width, 3840);
    CHECK_EQ(fit.client.height, 2160);
    CHECK_FALSE(fit.shrunk);
    // A work area smaller than the decorations is the same answer rather than a negative size.
    const ClientFit degenerate = FitClientToDesktop({1920, 1080}, Desktop(20, 10));
    CHECK_EQ(degenerate.client.width, 1920);
    CHECK_EQ(degenerate.client.height, 1080);
    CHECK_FALSE(degenerate.shrunk);
  }

  BeginCase("physical -> logical is the conversion the SDK inverts");
  {
    // 100% is the identity: the common machine is untouched by the unit round trip.
    CHECK_EQ(PhysicalToLogical({2560, 1080}, 100).width, 2560);
    CHECK_EQ(PhysicalToLogical({2560, 1080}, 100).height, 1080);
    // 150%: a 1920-px client is 1280 logical pixels; 1080 px is 720.
    CHECK_EQ(PhysicalToLogical({1920, 1080}, 150).width, 1280);
    CHECK_EQ(PhysicalToLogical({1920, 1080}, 150).height, 720);
    // 125% rounds to nearest rather than truncating.
    CHECK_EQ(PhysicalToLogical({2560, 1080}, 125).width, 2048);
    CHECK_EQ(PhysicalToLogical({2560, 1080}, 125).height, 864);
    // A degenerate scale is treated as 100% rather than dividing by zero.
    CHECK_EQ(PhysicalToLogical({1920, 1080}, 0).width, 1920);
    // Never below one pixel.
    CHECK_EQ(PhysicalToLogical({1, 1}, 300).width, 1);
    // The round trip the SDK performs (`logical * dpi / 96`, rounding up) lands within a pixel of
    // what was asked for at every scale the project supports: the fit is a clamp, and one pixel of
    // rounding must not push the window back out of the work area.
    for (const uint32_t scale : {100u, 125u, 150u, 175u, 200u, 300u}) {
      const DisplayMode physical{2373, 1001};
      const DisplayMode logical = PhysicalToLogical(physical, scale);
      const uint32_t dpi = scale * 96 / 100;
      const auto to_physical = [dpi](const int32_t value) {
        return int32_t((int64_t(value) * dpi + 95) / 96);
      };
      CHECK_TRUE(to_physical(logical.width) <= physical.width + 1);
      CHECK_TRUE(to_physical(logical.height) <= physical.height + 1);
    }
  }

  BeginCase("SameAspect tolerates integer rounding, not a different shape");
  {
    CHECK_TRUE(SameAspect({2560, 1080}, {1280, 540}));
    CHECK_TRUE(SameAspect({2373, 1001}, {2560, 1080}));
    CHECK_TRUE(SameAspect({1780, 1001}, {1920, 1080}));
    // 16:9 against 21:9 is not rounding.
    CHECK_FALSE(SameAspect({1920, 1080}, {2560, 1080}));
    CHECK_FALSE(SameAspect({1280, 720}, {960, 720}));
  }

  return Finish();
}
