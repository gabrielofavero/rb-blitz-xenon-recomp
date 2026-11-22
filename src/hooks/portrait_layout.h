// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ui/portrait_viewport.h"

namespace rb_blitz::hooks {
// Active only while the owning guest panel and its nested resources draw.
ui::PortraitFit PortraitDrawFit();
}
