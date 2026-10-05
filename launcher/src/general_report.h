// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The General tab's state as text (prompt B1).
//
// The tab itself is ImGui, but everything it decides is not: which root the game is in, D5's
// Ultimate state, the effective launch target, and each path row's verdict. This turns all of
// that into lines, so B1's four manual runs are assertions on text instead of OCR, and E2/E3
// have something to compare against.
//
// Dependency-free on purpose, like the modules it reports on.

#pragma once

#include <string>

#include "launcher/profile.h"
#include "ultimate_state.h"

namespace rb_blitz::launcher {

// One line per fact, in the tab's own row order (the table is the source of it). `load` is
// passed whole rather than as a bare Profile so a file that did not parse is reported as such
// instead of its defaults looking like a deliberate choice.
std::string DescribeGeneral(const ProfileLoadResult& load, const GameRoots& roots);

}  // namespace rb_blitz::launcher
