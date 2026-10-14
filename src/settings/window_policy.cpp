// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The arithmetic behind src/settings/window_policy.h - pure integer maths, no SDL, no Win32 and no
// boot, so the rule that decides the game window's size is tested on its own.

#include "settings/window_policy.h"

namespace rb_blitz::settings {
namespace {

bool Positive(const DisplayMode& size) { return size.width > 0 && size.height > 0; }

// Round-half-up division for a positive numerator and denominator, so the fit does not drift
// downwards on repeated conversions.
int64_t RoundDiv(const int64_t numerator, const int64_t denominator) {
  return (numerator * 2 + denominator) / (denominator * 2);
}

// The width of a size of `aspect` whose height is `height`, rounded to nearest.
int64_t WidthForHeight(const int64_t height, const DisplayMode& aspect) {
  return RoundDiv(height * aspect.width, aspect.height);
}

int64_t HeightForWidth(const int64_t width, const DisplayMode& aspect) {
  return RoundDiv(width * aspect.height, aspect.width);
}

}  // namespace

ClientFit FitClientToDesktop(const DisplayMode& requested, const DesktopMetrics& desktop) {
  ClientFit fit;
  fit.client = requested;
  if (!Positive(requested) || !desktop.available || !Positive(desktop.work_area)) {
    return fit;
  }

  // What the client area may be once the decorations are accounted for. The frame metrics are a
  // measurement, so a work area smaller than the frame itself yields a degenerate limit rather than
  // a negative one.
  const int64_t limit_width = desktop.work_area.width - desktop.frame_extent.width;
  const int64_t limit_height = desktop.work_area.height - desktop.frame_extent.height;
  if (limit_width <= 0 || limit_height <= 0) {
    return fit;
  }

  // The common case: the decorated window fits exactly as asked, so nothing about the request is
  // changed - which is what keeps every 16:9 boot at a size the display can hold identical to what
  // it was before this policy existed.
  if (limit_width >= requested.width && limit_height >= requested.height) {
    return fit;
  }

  // Contain the request in the limit: the binding axis decides, the other follows the aspect.
  int64_t width = limit_width;
  int64_t height = HeightForWidth(limit_width, requested);
  if (height > limit_height) {
    height = limit_height;
    width = WidthForHeight(limit_height, requested);
  }
  if (width > requested.width || height > requested.height) {
    // Rounding can only have pushed a fitted size past the request when the request is what binds,
    // which the branch above already handled; keep the request if it happens anyway.
    return fit;
  }

  // Never below the floor. The first clamp that fires decides the other axis, so the shape stays
  // the request's shape even at the floor.
  if (width < kMinClientSize.width) {
    width = kMinClientSize.width;
    height = HeightForWidth(width, requested);
  }
  if (height < kMinClientSize.height) {
    height = kMinClientSize.height;
    width = WidthForHeight(height, requested);
  }

  fit.client.width = int32_t(width);
  fit.client.height = int32_t(height);
  fit.shrunk = fit.client.width != requested.width || fit.client.height != requested.height;
  return fit;
}

DisplayMode PhysicalToLogical(const DisplayMode& physical, const uint32_t scale_percent) {
  if (!Positive(physical)) {
    return DisplayMode{};
  }
  const uint32_t scale = scale_percent == 0 ? 100u : scale_percent;
  // Round to nearest: `physical * 96 / dpi`, spelled with the percent form the caller has.
  const auto to_logical = [scale](const int32_t value) {
    return int32_t((int64_t(value) * 100 + int64_t(scale) / 2) / int64_t(scale));
  };
  DisplayMode logical{to_logical(physical.width), to_logical(physical.height)};
  if (logical.width < 1) {
    logical.width = 1;
  }
  if (logical.height < 1) {
    logical.height = 1;
  }
  return logical;
}

bool SameAspect(const DisplayMode& lhs, const DisplayMode& rhs, const int32_t tolerance_per_mille) {
  if (!Positive(lhs) || !Positive(rhs)) {
    return false;
  }
  const int64_t lhs_per_mille = int64_t(lhs.width) * 1000 / lhs.height;
  const int64_t rhs_per_mille = int64_t(rhs.width) * 1000 / rhs.height;
  const int64_t difference = lhs_per_mille > rhs_per_mille ? lhs_per_mille - rhs_per_mille
                                                          : rhs_per_mille - lhs_per_mille;
  return difference * 1000 <= int64_t(tolerance_per_mille) * rhs_per_mille;
}

}  // namespace rb_blitz::settings
