// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <memory>
namespace rex::ui { class ImGuiDialog; class ImGuiDrawer; }
namespace rb_blitz::ui::notifications {
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer);
}
