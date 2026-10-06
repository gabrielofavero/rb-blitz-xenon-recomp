// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The ImGui half of the launcher's own keys (launcher/src/nav_bindings.h, A5).
//
// nav_bindings decides what a key *means*; this is the only place that knows a name is an ImGui
// key and that "is it down?" is a question ImGui answers. Split for the same reason pad_nav.cpp
// is split from pad_source.cpp: the rules stay testable without a window, and the one file that
// needs the framework has nothing in it that could be wrong quietly.

#pragma once

#include <string_view>

#include "imgui.h"

#include "nav_bindings.h"

namespace rb_blitz::launcher::nav_keys {

// What the keyboard did this frame, in the binding model's words. Modifier keys are not keys (a
// binding that spelled one would never fire) and neither are ImGui's own pad and mouse keys: the
// pad has its own source (A3, D6), so a binding must not be able to match a pad press behind
// --no-gamepad's back.
nav_bindings::Presses ReadPresses();

}  // namespace rb_blitz::launcher::nav_keys
