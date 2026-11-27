// SPDX-License-Identifier: GPL-2.0-only
#include "ui/portrait_dialog.h"
#include "ui/portrait_dialog_state.h"
#include "ui/achievement_layout.h"
#include "input/native_menu.h"
#include "input/title_input.h"
#include "settings/runtime_settings.h"
#include "hooks/render_settings.h"
#include "hooks/render_settings.h"
#include <algorithm>
#include <imgui.h>
#include <rex/ui/style.h>
#include <rex/ui/ui_system.h>

namespace rb_blitz::ui::portrait_dialog {
namespace {
State state;

class Overlay final : public rex::ui::DialogBase {
 public:
  explicit Overlay(const rex::ui::ToastStyle& theme) : theme_(theme) {}
  ~Overlay() override { state.Reset(); }
  bool WantsExclusiveInput() const override { return state.Current().active; }
  bool Draw(ImGuiIO& io) override {
    const auto current = state.Current();
    if (!current.active) {
      if (opened_ && ImGui::BeginPopupModal("##portrait_error")) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      opened_ = false;
      return true;
    }
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return true;
    const auto layout = achievements::LayoutBrowser(io.DisplaySize.x, io.DisplaySize.y,
                                                    CurrentPortraitViewport());
    const float scale = std::max(.1f, layout.scale), pad = 22 * scale;
    const auto& theme = theme_;
    const float width = layout.width, font_size = 20 * scale;
    ImGui::PushFont(theme.font, font_size);
    const float body_height = ImGui::GetFont()->CalcTextSizeA(font_size, 1e9f,
        std::max(1.f, width - 2 * pad), current.body.c_str()).y;
    const float button_height = std::max(44.f, 48 * scale);
    const float height = std::min(layout.height, body_height + button_height + 3 * pad);
    const ImVec2 pos(layout.x, layout.y + (layout.height - height) / 2);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12 * scale, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * scale);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.025f, .015f, .02f, .98f));
    ImGui::PushStyleColor(ImGuiCol_Text, rex::ui::ToImVec4(theme.text));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, .55f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.18f, .12f, .16f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.32f, .18f, .25f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.24f, .14f, .20f, 1));
    const bool appearing = !opened_;
    if (appearing) { ImGui::OpenPopup("##portrait_error"); opened_ = true; }
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    if (ImGui::BeginPopupModal("##portrait_error", nullptr, flags)) {
      auto* draw = ImGui::GetWindowDrawList();
      for (int rail = 0; rail < 3; ++rail) {
        const float inset = (2 + rail * 3) * scale;
        const ImVec2 lo(pos.x + inset, pos.y + inset);
        const ImVec2 hi(pos.x + width - inset, pos.y + height - inset);
        for (float stroke : {5.f, 3.f, 1.8f}) {
          auto color = theme.border;
          color.w *= stroke == 5 ? .12f : stroke == 3 ? .22f : .7f;
          draw->AddRect(lo, hi, ImGui::GetColorU32(color), (8 - rail * 2) * scale,
                        0, stroke * scale);
        }
        draw->AddRect(lo, hi, ImGui::GetColorU32(theme.border_highlight),
                      (8 - rail * 2) * scale, 0, .7f * scale);
      }
      const float available = std::max(1.f, ImGui::GetContentRegionAvail().y - button_height - pad);
      if (ImGui::BeginChild("##message", ImVec2(0, available))) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(current.body.c_str());
        ImGui::PopTextWrapPos();
      }
      ImGui::EndChild();
      rex::hid::IInputSystem::PadReading physical{};
      const bool pad_confirm = input::SamplePhysicalPad(&physical, true) &&
          (physical.state.buttons & rex::hid::kPadA);
      const bool held = ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_Space) || pad_confirm;
      if (appearing || generation_ != current.generation) held_ = held;
      generation_ = current.generation;
      ImGui::PushID(static_cast<int>(current.generation));
      ImGui::BeginDisabled(current.pending);
      const bool clicked = ImGui::Button(current.caption.c_str(), ImVec2(-1, button_height));
      ImGui::EndDisabled();
      ImGui::PopID();
      if (input::HostInputFocused() && (clicked || (held && !held_)))
        state.Confirm(current.generation);
      held_ = held;
      ImGui::EndPopup();
    }
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(5);
    ImGui::PopFont();
    return true;
  }
 private:
  const rex::ui::ToastStyle& theme_;
  bool opened_ = false, held_ = false;
  uint64_t generation_ = 0;
};
}

void Observe(uint32_t screen, uint32_t panel, uint32_t directory, bool eligible) {
  state.Observe(screen, panel, directory, eligible && settings::InterfacePortraitEnabled());
}
void PublishLabel(uint32_t directory, bool body, std::string_view text) {
  state.Label(directory, body, text);
}
bool AnswerQuery(std::string_view path, bool* answer) {
  if (!path.starts_with("rbbz-portrait-dialog/")) return false;
  *answer = false;
  if (!settings::InterfacePortraitEnabled()) { state.Close(); return true; }
  if (path == "rbbz-portrait-dialog/open") state.Open();
  else if (path == "rbbz-portrait-dialog/close") state.Close();
  else if (path == "rbbz-portrait-dialog/confirm") *answer = state.TakeConfirm();
  return true;
}
bool SuppressDirectory(uint32_t directory) {
  const auto current = state.Current();
  return current.active && directory && current.directory == directory;
}
std::unique_ptr<rex::ui::IDialog> CreateOverlay(const rex::ui::ToastStyle& theme) {
  return std::make_unique<Overlay>(theme);
}
}
