// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R7's progress bar. See src/hooks/content_progress.cpp for what the engine does with
// it, what this build feeds it, and why the total is the one thing missing.
//
// The faithful behaviour (R7 off, the default) is this file doing nothing at all: the
// hooks call the guest's own functions unchanged.

#pragma once

namespace rb_blitz::content_progress {

// Reads the toggle once, before the guest starts: the hooks run on guest threads and
// must not touch the cvar registry (the same rule menu_filter::Configure() follows).
// Logs what it will do.
void Configure();

// Called once per menu frame from the shell-music poll (src/hooks/shell_music.cpp). Settles
// R7's frame pacing if the discovery is over: the menu frame is the one frame its end is not
// guaranteed to be seen on. One atomic load when nothing is in flight.
void Tick();

}  // namespace rb_blitz::content_progress
