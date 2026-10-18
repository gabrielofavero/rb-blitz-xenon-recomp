// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The value column of a binding table, as measured spans
// (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md §4, P4).
//
// The Controller tab used to flatten a row into one string and hand it to ImGui, which cannot
// style part of a line: a row bound to three inputs, one of them disabled by Keyboard Support,
// has to read as normal text, normal text and *crossed* text. So the sources are laid out here as
// spans - every source its own segment, the ", " between them its own un-crossed segment - and the
// wrapping is measured here rather than left to ImGui's text wrap, because the strike line has to
// be drawn on each wrapped line under the words it crosses.
//
// Dependency-free on purpose - no ImGui, no SDL, not even a font. The caller passes the measuring
// function (ImGui's CalcTextSize in the window, ten units per character in the tests), so the
// arithmetic that decides where a line breaks and how wide a strike line is can be pinned without
// a window (tests/controller_settings_block_tests.cpp).
//
// The layout borrows the spans' text: a Segment's `text` points into the `Span` it came from, so
// the spans must outlive the layout.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::profile::binding_text {

// One source in a row. `enabled` is false when the device that would deliver it is switched off:
// the source is still listed, drawn in the subtitle colour and crossed out, because a binding
// that is present and a device that is available are two different facts.
struct Span {
  std::string text;
  bool enabled = true;
};

// One run of text on one line. `x` and `width` are in the caller's units, measured from the left
// edge of the cell; a separator is drawn in the normal colour and never crossed.
struct Segment {
  std::string_view text;
  float x = 0.0f;
  float width = 0.0f;
  bool struck = false;
  bool separator = false;
};

struct Line {
  std::vector<Segment> segments;
  float width = 0.0f;
};

struct Layout {
  std::vector<Line> lines;
  float widest = 0.0f;
};

// The separator between two sources, spelled the way the profile spells it.
inline constexpr std::string_view kSeparator = ", ";

// How a run of text measures. The caller supplies it so this module stays font-free.
using Measure = std::function<float(std::string_view)>;

// Lays `spans` out within `max_width`: sources are kept whole where they fit, a source that is
// wider than a whole line is split at the last character that fits, and the separator is dropped
// at a line start (so a wrapped row does not begin with a comma).
Layout Wrap(const std::vector<Span>& spans, float max_width, const Measure& measure);

// The strike line's thickness for a font size, derived rather than fixed so it stays a hairline
// at 100% and is still visible at 300% - the same rule the focus outline uses (focus_ring.h).
inline float StrikeThickness(float font_size) {
  return font_size / 12.0f < 1.0f ? 1.0f : font_size / 12.0f;
}

}  // namespace rb_blitz::profile::binding_text
