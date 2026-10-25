// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::platform {
// UI thread only. Request the OS input pane for the game's top-level window.
// False means the platform declined; normal/external text input stays available.
bool RequestVirtualKeyboard(void* native_window);
void DismissVirtualKeyboard(void* native_window);
}  // namespace rb_blitz::platform
