// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
#include <memory>
namespace rex::ui { class IDialog; class IUISystem; }
namespace rex::gpu { class IGraphicsPresentation; }
namespace rb_blitz::ui {
// Updates presentation state on the UI thread; draws no host UI.
std::unique_ptr<rex::ui::IDialog> CreateGameplayPresentation(
    rex::ui::IUISystem& context,
    std::function<rex::gpu::IGraphicsPresentation*()> presentation,
    std::function<void()> preview_notification = {});
}
