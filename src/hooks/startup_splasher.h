// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::hooks::startup_splasher {
void ResetBootState();

// Set after the first native startup logo scene begins loading.
bool NativeLogoLoadStarted();

}  // namespace rb_blitz::hooks::startup_splasher
