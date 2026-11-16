// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
#include <memory>
#include "input/touch_gestures.h"
namespace rex::ui { class ImGuiDialog; class ImGuiDrawer; class Window; }

namespace rb_blitz::input::touch {
Actions SampleGameplay(bool active);
void CancelGameplay();
std::vector<GameplayEvent> DrainGameplay();
bool ConsumeMouse(bool is_touch, Phase phase, bool gameplay);
// Shared with the guest HUD transform seam; drawing and hit testing use one layout.
Layout CurrentLayout();
bool ControlsVisible();
bool TouchInputActive();
bool ButtonsEnabled();
std::unique_ptr<rex::ui::ImGuiDialog> CreateControls(
    rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Window*()> window);
}  // namespace rb_blitz::input::touch
