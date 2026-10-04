// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The probe overlay: the guest-state half of the probe, drawn as an ImGuiDialog.
//
// It is an ordinary SDK dialog, added through ReXApp::OnCreateDialogs - the same
// mechanism the SDK's own F3 debug overlay, console and settings dialogs use -
// so there is exactly one ImGui context and one renderer in the process
// (docs/plans/customization-plan.md P2's "don't"). The dialog itself reads every
// cvar it shows; the one thing it cannot reach is the window, which lives on the
// application, so the application supplies it as a callback.

#pragma once

#include <cstdint>
#include <functional>

#include <rex/ui/imgui_dialog.h>

namespace rb_blitz::diag {

// What the overlay cannot read from a cvar. The window is owned by ReXApp, so
// the application hands these facts over rather than the probe reaching for
// them. DPI scale is derived from the two sizes when the logical size is known.
struct WindowState {
  uint32_t physical_width = 0;
  uint32_t physical_height = 0;
  uint32_t logical_width = 0;
  uint32_t logical_height = 0;
  bool fullscreen = false;
  float dpi_scale = 1.0f;
};

using WindowStateProvider = std::function<WindowState()>;

class ProbeOverlayDialog final : public rex::ui::ImGuiDialog {
 public:
  ProbeOverlayDialog(rex::ui::ImGuiDrawer* imgui_drawer, WindowStateProvider provider);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  WindowStateProvider provider_;
};

}  // namespace rb_blitz::diag
