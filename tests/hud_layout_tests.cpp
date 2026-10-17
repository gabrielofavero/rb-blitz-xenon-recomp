// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Host tests for src/hooks/hud_layout.h - the rule that says where a HUD element goes when
// the frame is not 16:9 (docs/settings/graphics.md (part 3, custom resolutions) §9). Header-only on purpose: the
// policy is arithmetic, so it is tested without the SDK, without a guest and without a
// frame, the same way the fingerprint and DLC-layout tests are.
//
// Two things are worth more here than the individual numbers:
//
//   * every 16:9 size is *exactly* the identity, because the whole point of the rule is that
//     a 16:9 boot is untouched - so the shift has to be 0 and not "0 after rounding";
//   * the distance to the nearest edge is preserved, which is the owner's requirement. That
//     is checked as an invariant over a table of aspects and positions rather than as a list
//     of expected outputs, because the requirement is the invariant and the outputs are just
//     what it produces.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "check.h"
#include "hooks/hud_layout.h"

namespace {

using rb_blitz::settings::HudShiftPixels;
using rb_blitz::settings::HudShiftUnits;
using rb_blitz::settings::kHudReferenceAspect;
using rb_blitz::settings::ShiftHudX;
using rb_blitz::test::BeginCase;
using rb_blitz::test::Fail;
using rb_blitz::test::Finish;
using rb_blitz::test::g_checks;

void CheckClose(const char* file, int line, double actual, double expected, double tolerance,
                const char* expr) {
  ++g_checks;
  if (std::fabs(actual - expected) <= tolerance) {
    return;
  }
  char message[256];
  std::snprintf(message, sizeof(message), "%s:\n         actual   %.12f\n         expected %.12f",
                expr, actual, expected);
  Fail(file, line, message);
}

}  // namespace

#define CHECK_CLOSE(actual, expected, tolerance) \
  CheckClose(__FILE__, __LINE__, (actual), (expected), (tolerance), #actual)

int main() {
  using rb_blitz::settings::HudShiftPixels;
  using rb_blitz::settings::HudShiftUnits;

  BeginCase("a 16:9 frame is the identity, at every size the title can boot");
  {
    // (width, height) pairs: the two the guest chooses, the two the plan offers as guest
    // bases, and the two 16:9 display sizes the catalogue grows them into.
    const uint32_t sizes[][2] = {{640, 360},  {960, 540},   {1280, 720},
                                 {1920, 1080}, {2560, 1440}, {3840, 2160}};
    for (const uint32_t* size : sizes) {
      // Exact, not "close enough": a 16:9 boot must be byte-for-byte the authored layout.
      CHECK_TRUE(HudShiftUnits(size[0], size[1]) == 0.0);
      CHECK_TRUE(HudShiftPixels(size[0], size[1]) == 0.0);
    }
    // The reference is the title's own rational, not a rounded decimal. 1920/1080 and
    // 16.0/9.0 are the same double, which is why the loop above can be that strict.
    CHECK_TRUE(HudShiftUnits(1920, 1080) == 1920.0 / 1080.0 - kHudReferenceAspect);
    // An unset width means "the guest derives 16:9 from the height" (render_settings.h), so
    // it is a 16:9 frame and not a divide by zero.
    CHECK_TRUE(HudShiftUnits(0, 720) == 0.0);
    CHECK_TRUE(HudShiftUnits(0, 0) == 0.0);
    CHECK_TRUE(HudShiftPixels(0, 720) == 0.0);
    // A delta of 0 is the identity for every position, edge, interior and centred alike.
    const double xs[] = {-1.7, -1.3333, -0.5, -0.004, 0.0, 0.004, 0.5, 1.3333, 1.7};
    for (double x : xs) {
      CHECK_CLOSE(ShiftHudX(x, 0.0), x, 0.0);
    }
  }

  BeginCase("the shift is the extra width, halved");
  {
    // 2560x1080 is the arm that was validated end to end; 1680x720 is the apect the stretch
    // was first measured at (docs/settings/graphics.md (part 3, custom resolutions) §3).
    CHECK_CLOSE(HudShiftUnits(2560, 1080), 0.5925925925925926, 1e-15);
    CHECK_CLOSE(HudShiftPixels(2560, 1080), 320.0, 1e-12);
    CHECK_CLOSE(HudShiftPixels(1680, 720), 200.0, 1e-12);
    // 4:3 and a portrait mode are signed the other way.
    CHECK_CLOSE(HudShiftPixels(640, 480), -106.66666666666667, 1e-12);
    CHECK_CLOSE(HudShiftPixels(1080, 1920), -1166.6666666666667, 1e-12);

    // The two ways of writing the pixel shift have to agree, because the boot log prints one
    // and the capture check measures the other.
    const uint32_t sizes[][2] = {{1680, 720},  {2560, 1080}, {2560, 1440}, {3440, 1440},
                                 {640, 480},   {1080, 1920}, {720, 1280}};
    for (const uint32_t* size : sizes) {
      CHECK_CLOSE(HudShiftPixels(size[0], size[1]),
                  HudShiftUnits(size[0], size[1]) * size[1] / 2.0, 1e-9);
    }
  }

  BeginCase("an element keeps its distance to the nearest edge");
  {
    // The owner's requirement as an invariant, over every frame the plan can ask for: the move
    // keeps `aspect - |x|` - the distance to the nearest horizontal edge. A frame narrower than
    // that distance cannot honour it (the element would have to cross the centre line), and
    // there the rule's limit applies: the element stops at the centre.
    const uint32_t sizes[][2] = {{2560, 1080}, {3440, 1440}, {1920, 1080},
                                 {640, 480},   {1080, 1920}, {1440, 2560}};
    const double xs[] = {-1.7, -1.6, -1.3333, -0.9, -0.5, -0.02, 0.02, 0.5, 0.9, 1.3333, 1.6};
    for (const uint32_t* size : sizes) {
      const double from = kHudReferenceAspect;
      const double to = static_cast<double>(size[0]) / static_cast<double>(size[1]);
      const double units = HudShiftUnits(size[0], size[1]);
      for (double x : xs) {
        const double moved = ShiftHudX(x, units);
        const double before = from - std::fabs(x);
        if (before <= to) {
          CHECK_CLOSE(to - std::fabs(moved), before, 1e-12);
          // Nothing crosses the centre line: it stays on its own side.
          CHECK_TRUE((x < 0.0) == (moved < 0.0));
        } else {
          // The distance is not one this frame has: the centre is where the shift stops.
          CHECK_CLOSE(moved, 0.0, 0.0);
        }
        // Whatever happens, nothing lands outside the frame.
        CHECK_TRUE(std::fabs(moved) <= to + 1e-12);
      }
    }

    // Worked examples at the validated arm, in the direction the owner described: the
    // multiplier goes further left, the score and stars further right, by the same 0.5926
    // units (320 px) each.
    CHECK_CLOSE(ShiftHudX(-1.3333, 0.5925925925925926), -1.9258925925925926, 1e-12);
    CHECK_CLOSE(ShiftHudX(1.3333, 0.5925925925925926), 1.9258925925925926, 1e-12);

    // Narrower frames move elements inward. At 4:3 an element authored on the 16:9 edge lands
    // exactly on the 4:3 edge (distance 0, kept), and one 0.4444 from the edge keeps 0.4444 -
    // which is 0.8889 out, i.e. *inward*, not the 1.3333 a naive `x + sign(x) * delta` would
    // give it.
    const double to_4x3 = HudShiftUnits(640, 480);
    CHECK_CLOSE(ShiftHudX(-kHudReferenceAspect, to_4x3), -4.0 / 3.0, 1e-12);
    CHECK_CLOSE(ShiftHudX(4.0 / 3.0, to_4x3), 0.8888888888888888, 1e-12);
    CHECK_CLOSE(ShiftHudX(-kHudReferenceAspect, HudShiftUnits(1080, 1920)), -0.5625, 1e-12);
    // The boundary, at portrait: an element 0.9 out has an authored distance of 0.8778 and a
    // 0.5625-wide frame has no such distance to give it, so it stops at the centre rather
    // than folding over to the other side.
    CHECK_CLOSE(ShiftHudX(0.9, HudShiftUnits(1080, 1920)), 0.0, 0.0);
  }

  BeginCase("a centred element never moves");
  {
    // The top-middle element: the owner wants it pinned, whatever the frame, and it is the
    // one position the distance-preserving formula does not describe.
    const double units[] = {0.5925925925925926, -0.4444444444444444, -1.2152777777777777};
    for (double u : units) {
      CHECK_CLOSE(ShiftHudX(0.0, u), 0.0, 0.0);
      // Inside the band, a rounded centred constant is treated as centred...
      CHECK_CLOSE(ShiftHudX(0.0005, u), 0.0005, 0.0);
      CHECK_CLOSE(ShiftHudX(-0.004, u), -0.004, 0.0);
      // ...and outside it, it is not a centred element and moves like any other.
      CHECK_TRUE(ShiftHudX(0.02, u) != 0.02);
    }
    // The band is the caller's to widen; 0.0 makes every non-zero x an element to move.
    CHECK_CLOSE(ShiftHudX(0.0005, 0.5925925925925926, 0.0), 0.5930925925925926, 1e-12);
  }

  BeginCase("an element authored off the 16:9 frame stays off a wider frame");
  {
    // The owner's ultrawide capture: the "NEW PERSONAL BEST" banner is authored beyond the
    // 16:9 edge (the developer parked it off-screen at the old aspect), and a wider frame
    // that left it in place revealed it. The rule moves it *further* out by the same delta
    // as every other off-centre element, so it stays hidden - it does not become part of
    // the new composition just because the frame grew.
    const uint32_t wider[][2] = {{1680, 720}, {2560, 1080}, {3440, 1440}};
    const double off_screen[] = {-2.5, -2.0, -1.9, 1.9, 2.0, 2.5};
    for (const uint32_t* size : wider) {
      const double to = static_cast<double>(size[0]) / static_cast<double>(size[1]);
      const double units = HudShiftUnits(size[0], size[1]);
      CHECK_TRUE(to > kHudReferenceAspect);
      for (double x : off_screen) {
        // The premise of the case: the authored position is outside the 16:9 frame.
        CHECK_TRUE(std::fabs(x) > kHudReferenceAspect);
        const double moved = ShiftHudX(x, units);
        // It moves in its own direction, and it stays outside the new frame.
        CHECK_TRUE(std::fabs(moved) > std::fabs(x));
        CHECK_TRUE(std::fabs(moved) >= to);
        CHECK_TRUE((x < 0.0) == (moved < 0.0));
      }
    }
    // The exact number for the validated arm: 2.0 -> 2.5926 at 2560x1080, which is still
    // beyond the 2.3704 edge.
    CHECK_CLOSE(ShiftHudX(2.0, HudShiftUnits(2560, 1080)), 2.5925925925925926, 1e-12);
  }

  return Finish();
}
