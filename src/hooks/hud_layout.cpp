// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/hud_layout.h"

#include "hooks/render_settings.h"

namespace rb_blitz::settings {

double BootHudShiftUnits() { return HudShiftUnits(BootRenderWidth(), BootRenderHeight()); }

double BootHudShiftPixels() { return HudShiftPixels(BootRenderWidth(), BootRenderHeight()); }

}  // namespace rb_blitz::settings
