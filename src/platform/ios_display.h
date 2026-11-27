// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <utility>
#include "ui/portrait_viewport.h"

namespace rb_blitz::platform {
// UIKit is available before SDL's video subsystem and display enumeration.
std::pair<int, int> IOSNativeDisplaySize();
ui::PortraitViewport IOSWindowViewport(void* native_window);  // UIWindow, UI thread only.
}
