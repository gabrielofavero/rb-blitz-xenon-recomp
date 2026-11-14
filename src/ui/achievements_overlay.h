// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>

namespace rex { class Runtime; }
namespace rex::system { class AchievementManager; }
namespace rex::ui { class ImGuiDialog; class ImGuiDrawer; class ImmediateDrawer; }

namespace rb_blitz::ui::achievements {
// Guest calls only publish a request. All ImGui/texture work stays on the UI thread.
void RequestOpen();
void Toggle();
bool Active();
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(
    rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate,
    rex::Runtime* runtime, rex::system::AchievementManager* manager);
}  // namespace rb_blitz::ui::achievements
