// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
#include <memory>
namespace rex::ui { class ImGuiDialog; class ImGuiDrawer; class Presenter; }
namespace rb_blitz::ui {
// Updates presentation state on the UI thread; draws no host UI.
std::unique_ptr<rex::ui::ImGuiDialog> CreateGameplayPresentation(
    rex::ui::ImGuiDrawer*, std::function<rex::ui::Presenter*()> presenter,
    std::function<void()> preview_notification = {});
}
