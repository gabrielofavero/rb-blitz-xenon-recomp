// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <functional>
#include <memory>
#include <rex/kernel/title_runtime.h>
#include <rex/ui/ui_system.h>

namespace rb_blitz::input {
// Mouse events and cursor visibility live on the window thread; the native menu
// hook consumes them on the guest thread. The input device only supplies A/B and wheel.
std::unique_ptr<rex::ui::IDialog> CreateMouseMenuCursor(rex::ui::IUISystem& ui);
}
