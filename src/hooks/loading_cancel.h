// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R11's cancel prompt. See src/hooks/loading_cancel.cpp for what the title does with a
// song load, what this build adds to it, and why the prompt lives on the title's own
// label.
//
// The faithful behaviour (R11 off) is this file doing nothing at all: the hooks call the
// guest's own functions unchanged.

#pragma once

#include <cstdint>

namespace rb_blitz::loading_cancel {

// Reads the toggle and its timeout once, before the guest starts: the hooks run on guest
// threads and must not touch the cvar registry (the same rule menu_filter::Configure()
// follows). Logs what it will do.
void Configure();

// True while the title's prompt is up early, under this build's cancel text.
bool CancelPromptIsUp();

}  // namespace rb_blitz::loading_cancel
