// SPDX-License-Identifier: GPL-2.0-only
#include "ui/notification_overlay.h"
#include "ui/notifications.h"
#include "ui/help_layout.h"

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

namespace rb_blitz::ui::notifications {
namespace {
class NotificationDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit NotificationDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

  void OnDraw(ImGuiIO& io) override {
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    const auto frame = SharedQueue().Tick();
    if (!frame) return;
    const auto& theme = imgui_drawer()->style().toast;
    const float scale = std::clamp(io.DisplaySize.y / 720.0f, 0.65f, 1.5f);
    const float margin = 16.0f * scale;
    const float width = std::min(380.0f * scale, io.DisplaySize.x - margin * 2);
    if (width < 60.0f) return;
    const float pad = 17.0f * scale;
    const float text_width = width - pad * 2;
    const float title_size = 21.0f * scale;
    const float body_size = 16.0f * scale;
    ImGui::PushFont(theme.heading_font, title_size);
    ImFont* heading = ImGui::GetFont();
    const std::string title = help::FitCaption(frame->notice.title, [&](const std::string& text) {
      const auto bounds = heading->CalcTextSizeA(title_size, 1e9f, text_width, text.c_str());
      return bounds.x <= text_width && bounds.y <= title_size * 2.1f;
    });
    const float title_height = heading->CalcTextSizeA(title_size, 1e9f, text_width,
                                                     title.c_str()).y;
    ImGui::PopFont();
    ImGui::PushFont(theme.font, body_size);
    ImFont* body = ImGui::GetFont();
    const std::string description = help::FitCaption(frame->notice.description, [&](const std::string& text) {
      const auto bounds = body->CalcTextSizeA(body_size, 1e9f, text_width, text.c_str());
      return bounds.x <= text_width && bounds.y <= body_size * 4.1f;
    });
    const float body_height = body->CalcTextSizeA(body_size, 1e9f, text_width,
                                                 description.c_str()).y;
    ImGui::PopFont();
    const float height = pad * 2 + title_height + 5.0f * scale + body_height;
    const float alpha = frame->visibility;
    // Slide from beyond the right edge, with a smooth pop/fade at both ends.
    const ImVec2 pos(io.DisplaySize.x - margin - width + (1 - alpha) * (width + margin), margin);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(alpha);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("##rb_blitz_notification", nullptr, flags)) {
      auto* draw = ImGui::GetWindowDrawList();
      draw->PushClipRect(pos, ImVec2(pos.x + width, pos.y + height));
      const auto color = [alpha](ImVec4 c) {
        c.w *= alpha;
        return ImGui::GetColorU32(c);
      };
      draw->AddText(heading, title_size, ImVec2(pos.x + pad, pos.y + pad),
                    color(theme.title), title.c_str(), nullptr, text_width);
      draw->AddText(body, body_size, ImVec2(pos.x + pad, pos.y + pad + title_height + 5 * scale),
                    color(theme.muted_text), description.c_str(), nullptr, text_width);
      // A small severity marker distinguishes info, warning and error notices.
      const ImVec4 marker = frame->notice.level == Level::kError ? ImVec4(1, .22f, .18f, 1) :
          frame->notice.level == Level::kWarning ? ImVec4(1, .75f, .2f, 1) :
                                                   ImVec4(.5f, .8f, 1, 1);
      draw->AddCircleFilled(ImVec2(pos.x + width - 11 * scale, pos.y + 11 * scale),
                            2.5f * scale, color(marker));
      draw->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
  }
};
}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer) {
  return std::make_unique<NotificationDialog>(drawer);
}
}  // namespace rb_blitz::ui::notifications
