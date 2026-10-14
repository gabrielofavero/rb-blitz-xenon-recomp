// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cmath>
#include <cstdint>

namespace rb_blitz::settings {
// Where a HUD element goes when the frame is not 16:9.
//
// The owner's rule (2026-10-12, verbatim in docs/settings/graphics.md (part 3, custom resolutions) §9):
//
//   * a truly centred element stays where it is, whatever the frame;
//   * every other element keeps the pixel distance it has to its *nearest* horizontal edge.
//
// That needs no per-element table and no per-element anchor, because both statements are the
// same statement. The title lays its HUD out in the space `render_settings.h` describes: x in
// [-aspect, +aspect], y in [-1, +1], where one unit is half the frame *height* in pixels on
// both axes - which is why a UI disc stays round at any aspect. So an element authored at x
// sits `aspect - |x|` units from its nearest edge, and widening the frame by
// `delta = aspect' - 16/9` moves that edge out by exactly `delta`. Keeping the distance
// therefore means moving the element out by exactly `delta` as well:
//
//     x' = x + sign(x) * delta,        so   |x'| = |x| + delta
//     distance' = aspect' - |x'| = (aspect + delta) - (|x| + delta) = aspect - |x|
//
// i.e. the author's own distance, preserved for edge elements and for interior elements alike.
// Every off-centre element shifts by the *same* amount, so the composition translates outward
// rather than spreading - the owner's "all non edge elements follow the same logic".
//
// At 16:9 delta is exactly 0 and the rule is the identity. That is what makes "no changes at
// 16:9" a property of the arithmetic instead of a special case the hook has to remember.
//
// Three consequences worth stating, because they are the reason this is a policy seam and not
// something the hook derives on the spot:
//
//   1. A centred element is the one x for which the preserving formula is not the right one
//      (`|x'| = 0 + delta` would push it off centre), and it is also the one the owner wants
//      pinned, so `sign(0) = 0` is both the guard and the requirement.
//   2. The shift is signed. A frame *narrower* than 16:9 - 4:3, or a portrait mode - gives a
//      negative delta and the elements move inward by the same amount, which preserves the
//      distance exactly as long as the new frame *has* that distance: at 4:3 an element
//      authored on the 16:9 edge lands on the 4:3 edge, `|x'|` = 1.3333, with its distance of
//      0 intact, and one that was 0.4444 from the edge keeps 0.4444 (`|x'|` = 0.8889).
//   3. That "as long as" is the rule's boundary, and it is where portrait stops being free.
//      An element's authored distance is `d = 16/9 - |x|`, and a frame narrower than `d`
//      cannot honour it - the element would have to cross the centre line and land on the
//      opposite side (0.9 out becoming -0.4556 in a 0.5625-wide portrait frame). ShiftHudX
//      stops it at the centre instead: the formula's own limit, so the map stays continuous
//      and no element ever leaves the frame. That is honest, but it is not a portrait layout,
//      which is why portrait's real answer is the resize lever and per-element moves the owner
//      already named (docs/settings/graphics.md (part 3, custom resolutions) §9.4) - this rule covers the horizontal
//      half of it and says how far it goes.

// 16:9 as the reference, derived the same way the title derives it (`height * 16 / 9`), so the
// comparison is against the same rational rather than a rounded decimal.
inline constexpr double kHudReferenceAspect = 16.0 / 9.0;

// `aspect - 16/9`, in layout units. 0 for a 16:9 frame and for an unset one (width 0 means
// "the guest derives 16:9 from the height" - see render_settings.h).
inline double HudShiftUnits(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) {
    return 0.0;
  }
  return static_cast<double>(width) / static_cast<double>(height) - kHudReferenceAspect;
}

// The same shift in frame pixels: `units * height / 2`. Since a layout unit is half the frame
// height in pixels, this is also the extra width halved, `(width - height * 16 / 9) / 2` -
// 2560x1080 -> 320 px - but it is written from the units and not from that expression, because
// `height * (16/9)` does not round-trip to `width` (720 * 1.7777777777777777 is
// 1279.9999999999998), which would leave a 1e-13 residual shift on a 16:9 frame that is
// supposed to be untouched. tests/hud_layout_tests.cpp asserts the two forms agree.
inline double HudShiftPixels(uint32_t width, uint32_t height) {
  return HudShiftUnits(width, height) * height / 2.0;
}

// An element authored at `x` moved by the rule above.
//
// `centred_epsilon` is the band in which an element counts as centred. An authored centred
// element is 0.0 exactly, so the band only exists so that a rounded constant (0.0005, say) is
// treated as centred rather than moved by the full delta; the default is 0.005 units, about
// 2.7 capture pixels on a 1080-tall frame.
//
// The move is written as `|x| + units` rather than `x + sign(x) * units` for the one case where
// the two differ: a negative `units` larger than `|x|` would carry the element across the
// centre line, so `|x| + units` stops it at 0 instead (consequence 3 above).
inline double ShiftHudX(double x, double units, double centred_epsilon = 0.005) {
  if (units == 0.0 || std::fabs(x) <= centred_epsilon) {
    return x;
  }
  const double moved = std::fabs(x) + units;
  if (moved <= 0.0) {
    return 0.0;
  }
  return x < 0.0 ? -moved : moved;
}

// The boot frame's shift in units, from the size the host handed the guest before it started.
// Declared here, defined in hud_layout.cpp, because that is the file that can see the boot
// frame the hook wrote (src/hooks/render_settings.h) - this header stays pure so
// tests/hud_layout_tests.cpp can include it without the SDK.
double BootHudShiftUnits();

// The boot frame's shift in pixels, for the boot log and for the screenshot check.
double BootHudShiftPixels();
}  // namespace rb_blitz::settings
