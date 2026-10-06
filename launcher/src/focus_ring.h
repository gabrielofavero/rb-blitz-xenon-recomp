// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The focus ring's own geometry (docs/plans/launcher-plan.md A5; §10.2's "1 px outline").
//
// A1 showed the ring by giving the focused row the colour the selected tab uses, which was the one
// drawing primitive it needed. That is a colour alone, and a colour alone is not a signal: a user
// who cannot tell the highlighted row from the rest has no way to find the ring at all. So the
// ring is drawn - an outline around the focused item - and this is the arithmetic behind it.
//
// It lives in a header of its own, with no ImGui in it, because the claim worth testing is the
// *scaling* rule rather than the two draw-list calls that use it: the outline is derived from the
// font size, and the font size is what the display's content scale multiplies, so the ring is the
// same size relative to the text at 100%, 150%, 200% and 300%. A thickness in pixels would be a
// hairline on a 300% display and a border on a 100% one; deriving it is what makes one rule hold at
// every step.
//
// tests/launcher_keys_tests.cpp holds the four steps, and `--ui-scale` (main.cpp) is how the
// harness takes pictures at them on one machine.

#pragma once

#include <algorithm>

namespace rb_blitz::launcher::focus_ring {

// The outline's thickness for a given font size, in the UI's own units - the units ImGui draws in
// once the display's content scale has been applied. An eighth of the face is 2 units at the
// launcher's 16-unit font, which reads as an outline rather than as a border; the floor of one
// unit is what keeps it visible if the face is ever set very small.
inline float Thickness(float font_size) { return std::max(1.0f, font_size / 8.0f); }

// How far outside the item the outline sits, so it does not sit on the item's own frame. Half the
// thickness, which is what makes the outline's inner edge meet the item's edge: an outline drawn
// exactly on the frame is invisible against a focused frame's own border.
inline float Padding(float font_size) { return Thickness(font_size) * 0.5f; }

// The corner rounding: ImGui's own frames are rounded by the style's FrameRounding, and the ring
// follows it. Left as a parameter rather than read from the style so this stays style-free.
inline float Rounding(float frame_rounding) { return frame_rounding; }

}  // namespace rb_blitz::launcher::focus_ring
