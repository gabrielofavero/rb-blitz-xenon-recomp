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
#include "profile_session.h"
#include "ultimate_state.h"

namespace rb_blitz::launcher {

// One line per fact, in the tab's own row order (the table is the source of it). The session is
// passed whole rather than as a bare Profile so a file that did not parse is reported as such
// instead of its defaults looking like a deliberate choice - and so B4's write path (where the
// file is, whether it is portable, what a save would change, what a reset would remove) is
// reported by the same run.
std::string DescribeGeneral(const ProfileSession& session, const GameRoots& roots);

// The precedence audit (B4, D3): one line for every row of every tab that the game's own
// `rb_blitz.toml` decides, in the words the badge uses. The badge is the one part of B4 that
// only exists on screen, so this is what makes its rule checkable without a screenshot - and it
// is why the report may call the same text helper the panel does.
std::string DescribePrecedence(const ProfileSession& session);

}  // namespace rb_blitz::launcher
