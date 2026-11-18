// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <memory>
namespace rex::ui { class IDialog; class IUISystem; }
namespace rb_blitz::ui::notifications {
std::unique_ptr<rex::ui::IDialog> CreateOverlay(rex::ui::IUISystem& ui);
}
