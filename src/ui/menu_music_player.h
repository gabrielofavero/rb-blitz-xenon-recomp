// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <memory>

#include "ui/menu_music_player_policy.h"
struct ImGuiIO;
namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
}  // namespace rex::ui
namespace rb_blitz::ui::music {
std::unique_ptr<rex::ui::ImGuiDialog> CreatePlayer(
    rex::ui::ImGuiDrawer* drawer);
// Called in MouseCursor before its raw/pulse/hover routing. Coordinates are
// physical window pixels, converted using the latest drawn logical viewport.
bool RoutePlayerPointer(PointerEvent event, float x, float y, int physical_w,
                        int physical_h, float physical_to_logical,
                        int button = 0);
void CancelPlayerPointer();
bool PlayerPointerOver();
}  // namespace rb_blitz::ui::music
