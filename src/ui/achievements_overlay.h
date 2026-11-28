// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>

namespace rex::kernel { class TitleRuntime; }
namespace rex::ui { class IDialog; class IUISystem; }

namespace rb_blitz::ui::achievements {
void RequestOpen();
void Toggle();
bool Active();
std::unique_ptr<rex::ui::IDialog> CreateOverlay(
    rex::ui::IUISystem& ui, rex::kernel::TitleRuntime& title);
}  // namespace rb_blitz::ui::achievements
