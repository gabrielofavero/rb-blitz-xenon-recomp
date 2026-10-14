// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The caption overlay's drawing and its two publishers. See src/ui/help_overlay.h for the design
// and the threading rules.

#include "ui/help_overlay.h"

#include <algorithm>
#include <mutex>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "hooks/settings_bridge.h"
#include "ui/help_topics.h"

REXCVAR_DEFINE_BOOL(settings_help_overlay, true, "Settings",
                    "Show a short description of the highlighted row at the bottom of the screen "
                    "while a settings menu is open (Help & Options and the retail pages it "
                    "opens). White text in a translucent black box, drawn as a host overlay, so "
                    "every selection is described the moment it is highlighted. Faithful: off "
                    "draws nothing at all.");

namespace rb_blitz::ui::help {
namespace {

// The current caption. Written by whichever source just changed it (guest thread), read and
// copied by the dialog's draw (UI thread). Equal text is a no-op, which is what makes the
// observer's once-per-frame publication free.
std::mutex g_text_mutex;
std::string g_text;

class CaptionSink final : public settings::bridge::HelpSink {
 public:
  void Show(const std::string_view symbol) override { PublishSymbol(symbol); }
};
CaptionSink g_sink;

class HelpOverlayDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit HelpOverlayDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}
  ~HelpOverlayDialog() override {
    if (settings::bridge::GetHelpSink() == &g_sink) {
      settings::bridge::SetHelpSink(nullptr);
    }
  }

  void OnDraw(ImGuiIO& io) override {
    std::string text;
    {
      std::lock_guard lock(g_text_mutex);
      text = g_text;
    }
    // Off is invisible, not "drawn and empty", and the cvar is re-read here so the toggle is
    // live - the probe overlay's rule.
    if (text.empty() || !REXCVAR_GET(settings_help_overlay)) {
      return;
    }

    // A subtitle's proportions: centred, a fixed margin off the bottom, wrapped well short of the
    // window's edges, on a barely-rounded black box. Everything is derived from DisplaySize, so
    // it follows windowed, borderless and full-screen alike.
    constexpr float kPad = 14.0f;
    const float max_width = std::max(320.0f, io.DisplaySize.x * 0.72f);
    const float bottom = std::max(24.0f, io.DisplaySize.y * 0.06f);

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - bottom),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(max_width + kPad * 2.0f, 1e5f));
    ImGui::SetNextWindowBgAlpha(0.72f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Begin("##rb_blitz_help", nullptr, flags)) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + max_width);
      ImGui::TextUnformatted(text.c_str());
      ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
  }
};

}  // namespace

void PublishText(std::string text) {
  {
    std::lock_guard lock(g_text_mutex);
    if (g_text == text) {
      return;
    }
    g_text = text;
  }
  REXLOG_DEBUG("help_overlay: caption -> '{}'", text.empty() ? "(hidden)" : text);
}

void PublishSymbol(const std::string_view symbol) {
  PublishText(TextForSymbol(symbol));
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer) {
  settings::bridge::SetHelpSink(&g_sink);
  return std::make_unique<HelpOverlayDialog>(drawer);
}

}  // namespace rb_blitz::ui::help
