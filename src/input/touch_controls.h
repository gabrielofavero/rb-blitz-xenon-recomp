// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
#include <memory>
#include "input/touch_gestures.h"
namespace rex::ui { class ImGuiDialog; class ImGuiDrawer; class Window; }

namespace rb_blitz::input::touch {
bool PreviewEnabled();
Actions SampleGameplay(bool active);
void CancelGameplay();
std::vector<GameplayEvent> DrainGameplay();
bool ConsumeMouse(bool is_touch, Phase phase, bool gameplay);
// Shared with the guest HUD transform seam; drawing and hit testing use one layout.
Layout CurrentLayout();
bool ControlsVisible();
bool TouchInputActive();
bool ButtonsEnabled();
// Published by the guest's powerup_can_deploy property setter, not its help timer.
void PublishPowerupReady(bool ready);
bool PowerupReady();
// Normalized guest bounds from the native lane icon's current draw.
void PublishLanePowerup(Rect rect);
std::unique_ptr<rex::ui::ImGuiDialog> CreateControls(
    rex::ui::ImGuiDrawer* drawer, std::function<rex::ui::Window*()> window);
}  // namespace rb_blitz::input::touch
