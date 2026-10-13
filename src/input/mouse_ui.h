// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <functional>
#include <memory>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>

namespace rb_blitz::input {
// Mouse events and cursor visibility live on the window thread; the native menu
// hook consumes them on the guest thread. The input device only supplies A/B and wheel.
void InstallMouseUiNavigation(rex::RuntimeConfig& config);
std::unique_ptr<rex::ui::ImGuiDialog> CreateMouseMenuCursor(
    rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Window*()> window);
}
