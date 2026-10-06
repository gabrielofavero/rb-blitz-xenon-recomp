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

#include <optional>
#include <string>
#include <string_view>

#include "imgui.h"

#include "nav_bindings.h"

namespace rb_blitz::launcher::nav_keys {

// The key a binding's name means, or ImGuiKey_None. A name this build does not know resolves to
// nothing rather than to a key that happens to be nearby: an unreadable binding is inert, which
// is what lets a newer launcher's `[nav]` row survive in an older one.
ImGuiKey KeyForName(std::string_view name);

// The name ImGui spells a key as, which is what a captured binding is written as.
std::string NameForKey(ImGuiKey key);

// What the keyboard did this frame, in the binding model's words. Modifier keys are not keys (a
// binding that spelled one would never fire) and neither are ImGui's own pad and mouse keys: the
// pad has its own source (A3, D6), so a binding must not be able to match a pad press behind
// --no-gamepad's back.
nav_bindings::Presses ReadPresses();

// The chord the first key pressed this frame makes, or nothing. This is the editor's capture: the
// modifiers *held* when the key went down are part of the chord, so Ctrl+S can be assigned by
// pressing it, and a modifier press with no key does not assign anything.
std::optional<nav_bindings::Trigger> CapturedTrigger();

}  // namespace rb_blitz::launcher::nav_keys
