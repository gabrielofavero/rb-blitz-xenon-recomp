// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the measured binding spans (controller-settings/src/binding_text.h).

#include "binding_text.h"

#include <algorithm>

namespace rb_blitz::profile::binding_text {
namespace {

// The longest prefix of `text` whose measured width fits in `width`. Measurement is monotonic in
// the prefix length, so a binary search is exact; it returns 0 when even one character is too
// wide, which the caller turns into "take one character anyway" so a layout can never stall.
std::size_t PrefixThatFits(std::string_view text, const Measure& measure, float width) {
  if (width <= 0.0f || text.empty()) {
    return 0;
  }
  std::size_t low = 0;
  std::size_t high = text.size();
  while (low < high) {
    const std::size_t mid = low + (high - low + 1) / 2;
    if (measure(text.substr(0, mid)) <= width) {
      low = mid;
    } else {
      high = mid - 1;
    }
  }
  return low;
}

}  // namespace

Layout Wrap(const std::vector<Span>& spans, float max_width, const Measure& measure) {
  Layout layout;
  if (!measure) {
    return layout;
  }
  const float limit = std::max(1.0f, max_width);
  Line line;

  const auto flush = [&layout, &line]() {
    if (line.segments.empty()) {
      return;
    }
    layout.widest = std::max(layout.widest, line.width);
    layout.lines.push_back(std::move(line));
    line = Line{};
  };

  for (const Span& span : spans) {
    if (span.text.empty()) {
      continue;
    }
    std::string_view rest = span.text;
    bool first_piece = true;
    while (!rest.empty()) {
      const bool needs_separator = first_piece && !line.segments.empty();
      const float separator_width = needs_separator ? measure(kSeparator) : 0.0f;
      const float whole = measure(rest);
      if (line.width + separator_width + whole <= limit) {
        if (needs_separator) {
          line.segments.push_back(
              Segment{kSeparator, line.width, separator_width, false, true});
          line.width += separator_width;
        }
        line.segments.push_back(Segment{rest, line.width, whole, !span.enabled, false});
        line.width += whole;
        rest = {};
      } else if (!line.segments.empty()) {
        // Not this line: start a new one and try the source again there, without a separator.
        flush();
      } else {
        // Wider than a whole line: take the characters that fit and continue on the next line.
        const std::size_t take = std::max<std::size_t>(1, PrefixThatFits(rest, measure, limit));
        const std::string_view piece = rest.substr(0, take);
        const float width = measure(piece);
        line.segments.push_back(Segment{piece, line.width, width, !span.enabled, false});
        line.width += width;
        rest.remove_prefix(take);
        first_piece = false;
        flush();
      }
    }
  }
  flush();
  return layout;
}

}  // namespace rb_blitz::profile::binding_text
