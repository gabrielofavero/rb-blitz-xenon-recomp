// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>
#include <functional>

namespace rex::xam { class IAchievementManager; }
namespace rex::ui { class IDialog; class IUISystem; }

namespace rb_blitz::ui::achievements {
void RequestOpen();
void Toggle();
bool Active();
std::unique_ptr<rex::ui::IDialog> CreateOverlay(
    rex::ui::IUISystem& ui,
    std::function<rex::xam::IAchievementManager*()> manager);
}  // namespace rb_blitz::ui::achievements
