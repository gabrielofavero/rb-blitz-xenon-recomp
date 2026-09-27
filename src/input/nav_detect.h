// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Reading the guest's own frame, which is the only way to find out which row its
// menus have highlighted: the guest exposes no hit test, it is handed a
// composited image, and no pad event reports where the selection went
// (src/input/ui_nav.h has the rest of the argument).
//
// What this file adds to that is the one observable that survives being read off
// a picture: a menu step *moves* the highlight, so the difference between the
// frame before the press and the frame after it contains the highlight twice -
// once where it was and once where it went. Everything here is about turning
// those two rectangles back into numbers: the old centre, the new centre, and the
// pitch between them, which is what the aligner needs to know how many rows away
// the pointer is.
//
// Why a difference and not a search for the highlight itself. Brightness alone
// cannot find a highlight on a menu: rows of white text are brighter than the
// selection bar and just as wide, and what is a highlight on one screen is a
// heading on another. A difference has no such problem - it only reports what
// moved, and on a resting menu the only thing that moves when a row is stepped
// over is the highlight. The one thing a difference cannot do is say which of its
// two rectangles is the new one; that comes from the direction the press was
// made in, which the caller knows and this file does not need to.
//
// Why the frame is sampled rather than read. Every row is kept, because the bands
// are found per row, but only every other column of each row, which is plenty for
// a bar hundreds of pixels wide and halves the work of walking a 3.7 MB frame.
// The samples keep their colour rather than a brightness: a highlight can change
// hue without changing how bright it is, and one channel differing by 24 levels
// is a difference where the average of three might not be.
//
// Kept free of any SDK dependency, like src/input/ui_nav.h, so
// tests/ui_nav_tests.cpp can pin the geometry without booting the game.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rb_blitz::input {

// One guest frame, as the presenter hands it back: four bytes per pixel, R8 G8 B8
// X8, `stride` bytes per row. The last row need not be padded to the stride.
struct NavFrameView {
  const uint8_t* pixels = nullptr;
  int width = 0;
  int height = 0;
  size_t stride = 0;

  bool IsValid() const {
    return pixels != nullptr && width > 0 && height > 0 &&
           stride >= static_cast<size_t>(width) * 4;
  }
};

// A frame reduced to the samples the detector works on: every row, every other
// column, three bytes per sample. Sample column `sx` is frame column
// `sx * kSampleStep`.
struct NavSampledFrame {
  static constexpr int kSampleStep = 2;

  std::vector<uint8_t> rgb;
  int sample_width = 0;
  int sample_height = 0;
  int frame_width = 0;
  int frame_height = 0;

  bool IsValid() const {
    return sample_width > 0 && sample_height > 0 && frame_width > 0 && frame_height > 0 &&
           rgb.size() >= SampleCount() * 3;
  }

  size_t SampleCount() const {
    return static_cast<size_t>(sample_width) * static_cast<size_t>(sample_height);
  }

  // The same geometry, so that two of these can be compared sample by sample.
  bool Matches(const NavSampledFrame& other) const {
    return sample_width == other.sample_width && sample_height == other.sample_height &&
           frame_width == other.frame_width && frame_height == other.frame_height;
  }

  void Reset() {
    rgb.clear();
    sample_width = 0;
    sample_height = 0;
  }
};

// How many bands one difference may hold. Two is the useful answer - the highlight
// before the press and after it - and anything beyond that is animation or a screen
// change, so the caller sizes its storage from this.
inline constexpr int kMaxDetectBands = 16;

inline void SampleNavFrame(const NavFrameView& frame, NavSampledFrame* out) {
  const int step = NavSampledFrame::kSampleStep;
  const int sample_width = (frame.width + step - 1) / step;
  out->sample_width = sample_width;
  out->sample_height = frame.height;
  out->frame_width = frame.width;
  out->frame_height = frame.height;
  const size_t needed = static_cast<size_t>(sample_width) * static_cast<size_t>(frame.height) * 3;
  if (out->rgb.size() != needed) {
    out->rgb.resize(needed);
  }
  uint8_t* dst = out->rgb.data();
  for (int y = 0; y < frame.height; ++y) {
    const uint8_t* src = frame.pixels + static_cast<size_t>(y) * frame.stride;
    for (int sx = 0; sx < sample_width; ++sx) {
      // The last sample of a row whose width is not a multiple of the step is the
      // last pixel of that row rather than the first one past it.
      const int x = std::min(sx * step, frame.width - 1);
      std::memcpy(dst, src + static_cast<size_t>(x) * 4, 3);
      dst += 3;
    }
  }
}

// What changed in one row: how many samples, and where the outermost of them are
// (sample columns, not pixels; -1 when the row did not change).
struct NavRowDiff {
  int changed = 0;
  int first = -1;
  int last = -1;
};

// How the detector decides what a change is worth believing. The numbers come
// from the menus themselves (docs/history/bringup-log.md): what a step changes is
// either a selection bar - 25-50 px tall and 240-500 px wide on the AV settings
// screen - or, on a screen whose rows are labels rather than bars, the labels
// themselves changing colour, which is 18-20 px of text and as little as 60 px
// wide. So a band is allowed to be small, and the filters that matter are the ones
// that reject what a *screen* does: a change across the whole width, or one taller
// than a row.
struct NavDetectConfig {
  // Every other column of every row, matching NavSampledFrame::kSampleStep.
  int sample_step = NavSampledFrame::kSampleStep;

  // Levels of difference in any one channel that count as a changed sample. Well
  // above the compression and banding noise of a rendered frame - a menu's own
  // background art sits at 10-19 levels between two frames, and that is what this
  // has to stay clear of - and far below the difference a highlight makes, which is
  // 100 levels and up.
  int channel_threshold = 24;

  // Share of a row's samples that must change for the row to count as changed. A
  // band's rows change across their width, but a row of *text* changes only along
  // the glyph strokes, a fifth of it or so, so this cannot be a large share.
  int min_row_fraction_percent = 2;

  // Bands are the shape of a selection marker or they are nothing: 16-140 px tall,
  // and between 4% and 85% of the frame wide. The lower width bound is what rejects
  // animation in a strip and a stray changed pixel, the upper one rejects
  // whole-screen shifts - which is what a menu scroll or a screen transition looks
  // like - and the height bounds drop both a single changed row and the tall
  // graphics of a menu background. The upper height is above a row pitch rather than
  // at the height of a bar, because a bar at least as tall as its row arrives as its
  // own height plus a pitch.
  int min_band_height = 16;
  int max_band_height = 140;
  int min_band_width_percent = 4;
  int max_band_width_percent = 85;
  int max_bands = 8;

  // The two bands of one step are the same marker in two places, so they agree in
  // height and cover the same columns - the marker may be a bar, which is the same
  // width in both, or a label that changed colour, and two labels are different
  // lengths and different widths but are laid out along the same column of the
  // screen. What they cannot be is two unrelated things on opposite sides of it.
  int max_height_difference = 10;
  int min_overlap_percent_of_narrower = 50;

  // How far apart the two positions of one step can be. Below the minimum the bars
  // would be a single band, not two; above the maximum this is not one row of a
  // menu but two screens.
  int min_step_pixels = 16;
  int max_step_pixels = 240;

  // How far a band's centre can be from a row of the grid the rows sit on - the pitch
  // multiplied by a number of rows - and still be a row. A marker's centre is a pixel
  // or two from the row it marks, so this is slack for the measurement rather than for
  // the menu. Used only when the pitch is not known yet: with it, the slack is a share
  // of the pitch instead.
  double row_grid_slack_pixels = 14.0;

  // A row that changed this much is not one row changing but the screen being
  // replaced (or a menu list scrolling under a selection that stayed put, which is
  // the same thing from here).
  int screen_change_row_fraction_percent = 25;

  // A band whose height exceeds the pitch by this much contains a whole step
  // rather than one bar: the bar before the move and the bar after it overlap, so
  // the difference merged them into one rectangle instead of two.
  int merged_band_slack_percent = 15;

  int MinChangedSamples(int sample_width) const {
    // One sample is one pixel's worth of change already; the share is what says how
    // much of a row has to change before the row is worth looking at, and a single
    // sample would let a stray pixel of noise pass for a row.
    const int scaled = sample_width * min_row_fraction_percent / 100;
    return std::max(4, scaled);
  }

  int MinBandWidth(int frame_width) const {
    return frame_width * min_band_width_percent / 100;
  }

  int MaxBandWidth(int frame_width) const {
    return frame_width * max_band_width_percent / 100;
  }
};

// Rows that changed between two frames sampled at the same geometry. Returns
// false when the two do not describe the same frame size, which is what a change
// of resolution looks like from here.
inline bool DiffNavFrames(const NavSampledFrame& before, const NavSampledFrame& after,
                          const NavDetectConfig& config,
                          std::vector<NavRowDiff>* rows_out) {
  if (!before.IsValid() || !after.IsValid() || !before.Matches(after)) {
    return false;
  }
  const int threshold = config.channel_threshold;
  const int sample_width = after.sample_width;
  rows_out->resize(static_cast<size_t>(after.sample_height));
  const uint8_t* a = before.rgb.data();
  const uint8_t* b = after.rgb.data();
  for (int y = 0; y < after.sample_height; ++y) {
    NavRowDiff diff;
    const size_t row = static_cast<size_t>(y) * static_cast<size_t>(sample_width);
    for (int sx = 0; sx < sample_width; ++sx) {
      const size_t index = (row + static_cast<size_t>(sx)) * 3;
      if (std::abs(static_cast<int>(a[index]) - static_cast<int>(b[index])) < threshold &&
          std::abs(static_cast<int>(a[index + 1]) - static_cast<int>(b[index + 1])) < threshold &&
          std::abs(static_cast<int>(a[index + 2]) - static_cast<int>(b[index + 2])) < threshold) {
        continue;
      }
      ++diff.changed;
      if (diff.first < 0) {
        diff.first = sx;
      }
      diff.last = sx;
    }
    (*rows_out)[static_cast<size_t>(y)] = diff;
  }
  return true;
}

// One rectangle of change, in frame pixels, inclusive.
struct NavBand {
  int top = 0;
  int bottom = 0;
  int left = 0;
  int right = 0;

  int Height() const { return bottom - top + 1; }
  int Width() const { return right - left + 1; }
  double CentreY() const { return 0.5 * static_cast<double>(top + bottom); }
};

// Runs of neighbouring rows that changed across at least the configured share of
// their width, kept only when the run is the shape of a highlight. Returns how
// many bands were written, at most config.max_bands.
inline int FindChangedBands(const NavSampledFrame& frame, const std::vector<NavRowDiff>& rows,
                            const NavDetectConfig& config, NavBand* bands_out) {
  if (!frame.IsValid() || rows.empty()) {
    return 0;
  }
  const int min_changed = config.MinChangedSamples(frame.sample_width);
  const int min_width = config.MinBandWidth(frame.frame_width);
  const int max_width = config.MaxBandWidth(frame.frame_width);
  const int row_count = static_cast<int>(rows.size());
  int count = 0;
  int run_first = -1;
  int run_first_x = 0;
  int run_last_x = 0;
  for (int y = 0; y <= row_count; ++y) {
    const NavRowDiff* row = y < row_count ? &rows[static_cast<size_t>(y)] : nullptr;
    if (row != nullptr && row->changed >= min_changed && row->first >= 0) {
      if (run_first < 0) {
        run_first = y;
        run_first_x = row->first;
        run_last_x = row->last;
      } else {
        run_first_x = std::min(run_first_x, row->first);
        run_last_x = std::max(run_last_x, row->last);
      }
      continue;
    }
    if (run_first >= 0) {
      const int left = run_first_x * config.sample_step;
      const int right = std::min((run_last_x + 1) * config.sample_step - 1, frame.frame_width - 1);
      const int height = y - run_first;
      const int width = right - left + 1;
      if (height >= config.min_band_height && height <= config.max_band_height &&
          width >= min_width && width <= max_width && count < config.max_bands) {
        NavBand& band = bands_out[count++];
        band.top = run_first;
        band.bottom = y - 1;
        band.left = left;
        band.right = right;
      }
      run_first = -1;
    }
  }
  return count;
}

// Whether the frame is not the same screen any more - a transition, or a list
// scrolling under a selection that did not move. A row only counts as changed on
// the same terms as a band, so a frame's worth of noise cannot pass for a new
// screen.
inline bool LooksLikeScreenChange(const NavSampledFrame& frame,
                                  const std::vector<NavRowDiff>& rows,
                                  const NavDetectConfig& config) {
  if (rows.empty() || !frame.IsValid()) {
    return false;
  }
  const int min_changed = config.MinChangedSamples(frame.sample_width);
  int changed_rows = 0;
  for (const NavRowDiff& row : rows) {
    if (row.changed >= min_changed) {
      ++changed_rows;
    }
  }
  const int total = static_cast<int>(rows.size());
  return changed_rows * 100 > total * config.screen_change_row_fraction_percent;
}

// One navigation step read off a difference: the highlight's rectangle before the
// press and the one it moved to, or the single merged rectangle when the two
// overlap. Which of the two is the new position is the caller's to know, since
// only the caller knows which way it pressed; use NewHighlightCentre for that.
struct NavStep {
  bool found = false;
  // The two bars did not overlap and the difference kept them apart.
  bool separated = false;
  // The difference merged them into one rectangle, and the caller's pitch was
  // used to place them inside it.
  bool merged = false;

  double upper_centre = 0.0;
  double lower_centre = 0.0;
  int pitch = 0;

  int upper_top = 0;
  int upper_bottom = 0;
  int lower_top = 0;
  int lower_bottom = 0;
  int left = 0;
  int right = 0;

  // Lower is better: how far the pair is from being the same bar in two places.
  int score = 0;
};

// Where a menu puts its marker: on the rows, which are evenly spaced, so a position
// between two of them is not a selection. `band_centre` is where one of the two bands
// of a difference is; the answer is whether that is where the highlight already was,
// or a whole number of rows from it. Without a pitch the only answer that can be given
// is the first one, with enough slack for a band's centre to be a few pixels from the
// highlight's.
inline bool BandIsOnTheRowGrid(double band_centre, double highlight_centre, int known_pitch,
                               double slack_pixels) {
  const double slack = known_pitch > 0 && slack_pixels <= 0.0
                           ? 0.35 * static_cast<double>(known_pitch)
                           : slack_pixels;
  if (std::fabs(band_centre - highlight_centre) <= slack) {
    return true;
  }
  if (known_pitch <= 0) {
    return false;
  }
  const double pitch = static_cast<double>(known_pitch);
  const double rows = (band_centre - highlight_centre) / pitch;
  return std::fabs(rows - std::round(rows)) * pitch <= slack;
}

// Picks the two rectangles of a single step out of the bands. `known_pitch` is the
// row pitch already measured on this screen, or 0 when there is none yet; when it
// is known, a pair that far apart is preferred over one that merely looks tidy,
// which is what separates a moved highlight from two pieces of animation that
// happen to have moved differently. `highlight_centre` is where the highlight was
// before the press, or a negative value when that is not known either; it is only
inline NavStep FindNavStep(const NavBand* bands, int count, const NavDetectConfig& config,
                           int known_pitch, double highlight_centre) {
  NavStep step;
  if (count <= 0 || config.max_bands <= 0) {
    return step;
  }

  bool have_best = false;
  int best_score = 0;
  int best_pitch = 0;
  NavBand best_upper;
  NavBand best_lower;
  for (int i = 0; i < count; ++i) {
    for (int j = i + 1; j < count; ++j) {
      const NavBand& a = bands[i];
      const NavBand& b = bands[j];
      const NavBand& upper = a.top <= b.top ? a : b;
      const NavBand& lower = a.top <= b.top ? b : a;
      // The marker is the same shape in both positions only roughly - a label with a
      // descender is taller than one without - so the row pitch is the distance
      // between the middles of the two, not between their tops, which would drift by
      // half of every difference in height.
      const int pitch =
          static_cast<int>(std::lround(lower.CentreY() - upper.CentreY()));
      if (pitch < config.min_step_pixels || pitch > config.max_step_pixels) {
        continue;
      }
      if (std::abs(a.Height() - b.Height()) > config.max_height_difference) {
        continue;
      }
      // The marker is in the same column of the screen in both positions: a bar is
      // the same width there, and two labels of different lengths still start or end
      // in the same place. Two bands on opposite sides of the screen are two things,
      // not one thing that moved.
      const int overlap_left = std::max(a.left, b.left);
      const int overlap_right = std::min(a.right, b.right);
      const int narrower = std::min(a.Width(), b.Width());
      const int overlap = overlap_right - overlap_left + 1;
      if (overlap * 100 < narrower * config.min_overlap_percent_of_narrower) {
        continue;
      }
      // The marker was on the highlight measured before the press, and a menu moves it
      // between rows: both places it is seen in have to sit on the row grid, one of
      // them by definition where it was and the other a whole number of rows from it.
      // A pair that is not on the grid is two pieces of the screen's own animation that
      // happened to move - which is what a menu with a moving background puts in a
      // difference taken over more than one frame.
      if (highlight_centre >= 0.0 &&
          (!BandIsOnTheRowGrid(upper.CentreY(), highlight_centre, known_pitch,
                               config.row_grid_slack_pixels) ||
           !BandIsOnTheRowGrid(lower.CentreY(), highlight_centre, known_pitch,
                               config.row_grid_slack_pixels))) {
        continue;
      }
      int score = 2 * std::abs(a.Height() - b.Height()) +
                  std::abs((a.left + a.right) - (b.left + b.right)) / 2 +
                  std::abs(a.Width() - b.Width()) / 4;
      if (known_pitch > 0) {
        // Weighted heavily enough that a pair matching the measured pitch wins
        // over a tidier pair that does not.
        score += 4 * std::abs(pitch - known_pitch);
      }
      if (have_best && score >= best_score) {
        continue;
      }
      have_best = true;
      best_score = score;
      best_pitch = pitch;
      best_upper = upper;
      best_lower = lower;
    }
  }
  if (have_best) {
    step.found = true;
    step.separated = true;
    step.pitch = best_pitch;
    step.upper_centre = best_upper.CentreY();
    step.lower_centre = best_lower.CentreY();
    step.upper_top = best_upper.top;
    step.upper_bottom = best_upper.bottom;
    step.lower_top = best_lower.top;
    step.lower_bottom = best_lower.bottom;
    step.left = std::min(best_upper.left, best_lower.left);
    step.right = std::max(best_upper.right, best_lower.right);
    step.score = best_score;
    return step;
  }

  // Nothing that looks like one bar moving to another place. The one other thing
  // a step can look like is a single rectangle: the bar is at least as tall as the
  // pitch on this screen, so the old and the new position overlap and the
  // difference cannot tell them apart. The pitch, if this screen has one, says
  // where the two centres are inside that rectangle - and the bar has to contain
  // the highlight we last saw, which is what keeps a tall band of animation from
  // passing for a step.
  if (known_pitch <= 0) {
    return step;
  }
  const int slack = known_pitch * config.merged_band_slack_percent / 100;
  const NavBand* merged = nullptr;
  for (int i = 0; i < count; ++i) {
    if (bands[i].Height() <= known_pitch + slack) {
      continue;
    }
    if (highlight_centre >= 0.0 &&
        (bands[i].top > highlight_centre || bands[i].bottom < highlight_centre)) {
      continue;
    }
    if (merged == nullptr || bands[i].Height() > merged->Height()) {
      merged = &bands[i];
    }
  }
  if (merged == nullptr) {
    return step;
  }
  const int bar_height = merged->Height() - known_pitch;
  step.found = true;
  step.merged = true;
  step.pitch = known_pitch;
  step.upper_centre = merged->CentreY() - 0.5 * known_pitch;
  step.lower_centre = merged->CentreY() + 0.5 * known_pitch;
  step.upper_top = merged->top;
  step.upper_bottom = merged->top + bar_height - 1;
  step.lower_top = merged->bottom - bar_height + 1;
  step.lower_bottom = merged->bottom;
  step.left = merged->left;
  step.right = merged->right;
  step.score = 0;
  return step;
}

// The centre of the highlight once the step is over. `screen_direction` is +1 when// pressing moved the selection down the screen, -1 when it moved it up - the one
// thing about a difference that only the caller can know.
inline double NewHighlightCentre(const NavStep& step, int screen_direction) {
  if (step.merged) {
    const double centre = 0.5 * (step.upper_centre + step.lower_centre);
    return centre + 0.5 * static_cast<double>(screen_direction * step.pitch);
  }
  return screen_direction >= 0 ? step.lower_centre : step.upper_centre;
}

}  // namespace rb_blitz::input
