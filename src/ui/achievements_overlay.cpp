// SPDX-License-Identifier: GPL-2.0-only
#include "ui/achievements_overlay.h"
#include "ui/achievement_navigation.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/hook.h>
#include <rex/system/achievement_manager.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

#include "input/native_menu.h"
#include "input/title_input.h"

namespace rb_blitz::ui::achievements {
namespace {
std::atomic<bool> active{false};

uint32_t HeldCommands() {
  uint32_t held = 0;
  const auto key = [&](ImGuiKey key, uint32_t command) {
    if (ImGui::IsKeyDown(key)) held |= command;
  };
  key(ImGuiKey_LeftArrow, kLeft); key(ImGuiKey_RightArrow, kRight);
  key(ImGuiKey_UpArrow, kUp); key(ImGuiKey_DownArrow, kDown);
  key(ImGuiKey_Home, kFirst); key(ImGuiKey_End, kLast);
  key(ImGuiKey_PageUp, kPageUp); key(ImGuiKey_PageDown, kPageDown);
  key(ImGuiKey_Escape, kBack); key(ImGuiKey_Backspace, kBack);
  using namespace rex::input;
  X_INPUT_STATE state{};
  if (input::SamplePhysicalPad(&state, true)) {
    const auto& pad = state.gamepad;
    if ((pad.buttons & X_INPUT_GAMEPAD_DPAD_LEFT) || pad.thumb_lx < -16000) held |= kLeft;
    if ((pad.buttons & X_INPUT_GAMEPAD_DPAD_RIGHT) || pad.thumb_lx > 16000) held |= kRight;
    if ((pad.buttons & X_INPUT_GAMEPAD_DPAD_UP) || pad.thumb_ly > 16000) held |= kUp;
    if ((pad.buttons & X_INPUT_GAMEPAD_DPAD_DOWN) || pad.thumb_ly < -16000) held |= kDown;
    if (pad.buttons & X_INPUT_GAMEPAD_B) held |= kBack;
  }
  return held;
}

class Browser final : public rex::ui::ImGuiDialog {
 public:
  Browser(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate,
          rex::Runtime* runtime, rex::system::AchievementManager* manager)
      : ImGuiDialog(drawer), manager_(manager), icons_(immediate, runtime) {}
  ~Browser() override { active.store(false); }

  void OnDraw(ImGuiIO& io) override {
    if (!Active()) {
      // F7 can dismiss the persistent browser from outside its own draw. Retire
      // the popup as well, otherwise ImGui would keep an invisible modal open.
      if (opened_ && ImGui::BeginPopupModal("##blitz_achievements")) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      opened_ = false;
      return;
    }
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    const bool appearing = !opened_;
    if (appearing) {
      entries_ = manager_->ListAchievements();
      navigation_.Reset();
      ImGui::OpenPopup("##blitz_achievements");
      opened_ = true;
    }
    const auto& theme = imgui_drawer()->style().toast;
    const float scale = std::min(io.DisplaySize.x / 1280.0f, io.DisplaySize.y / 720.0f);
    const float pad = 24 * scale;
    const ImVec2 size(std::min(1140 * scale, io.DisplaySize.x - 2 * pad),
                      io.DisplaySize.y - 2 * pad);
    const ImVec2 pos((io.DisplaySize.x - size.x) / 2, pad);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12 * scale, 12 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 14 * scale);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.025f, .015f, .02f, .97f));
    ImGui::PushStyleColor(ImGuiCol_Text, theme.text);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.025f, .015f, .02f, .97f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, .55f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, theme.border);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(.30f, .30f, .30f, 1));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(.24f, .24f, .24f, 1));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.18f, .18f, .18f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.28f, .28f, .28f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.22f, .22f, .22f, 1));
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    if (ImGui::BeginPopupModal("##blitz_achievements", nullptr, flags)) {
      auto* draw = ImGui::GetWindowDrawList();
      // The same three glowing rails as the achievement unlock notification.
      for (int rail = 0; rail < 3; ++rail) {
        const float inset = (2 + 3 * rail) * scale;
        const ImVec2 lo(pos.x + inset, pos.y + inset);
        const ImVec2 hi(pos.x + size.x - inset, pos.y + size.y - inset);
        for (const auto stroke : {5.0f, 3.0f, 1.8f}) {
          auto color = theme.border;
          color.w *= stroke == 5 ? .12f : stroke == 3 ? .22f : .70f;
          draw->AddRect(lo, hi, ImGui::GetColorU32(color), (8 - 2 * rail) * scale,
                        0, stroke * scale);
        }
        draw->AddRect(lo, hi, ImGui::GetColorU32(theme.border_highlight),
                      (8 - 2 * rail) * scale, 0, .7f * scale);
      }

      ImGui::PushFont(theme.heading_font, 34 * scale);
      ImGui::TextUnformatted("ACHIEVEMENTS");
      ImGui::PopFont();
      ImGui::PushFont(theme.font, 18 * scale);
      int unlocked_count = 0, earned_score = 0, total_score = 0;
      for (const auto& entry : entries_) {
        total_score += entry.gamerscore;
        if (manager_->IsUnlocked(entry.id)) {
          ++unlocked_count;
          earned_score += entry.gamerscore;
        }
      }
      ImGui::Text("%d / %d unlocked    |    %d / %d G", unlocked_count,
                  static_cast<int>(entries_.size()), earned_score, total_score);
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.border);
      ImGui::ProgressBar(entries_.empty() ? 0 : float(unlocked_count) / entries_.size(),
                         ImVec2(-1, 5 * scale), "");
      ImGui::PopStyleColor();

      const uint32_t command = navigation_.Tick(HeldCommands(), input::HostInputFocused(),
                                                 io.DeltaTime);
      bool dismiss = (command & kBack) != 0;
      const float list_height = ImGui::GetContentRegionAvail().y - 50 * scale;
      const bool moved = navigation_.Move(command, static_cast<int>(entries_.size()),
                          std::max(1, int(list_height / (150 * scale))));
      if (ImGui::BeginChild("##achievement_cards", ImVec2(0, list_height),
                            ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar |
                            ImGuiWindowFlags_NoNav)) {
        if (appearing) ImGui::SetScrollY(0);
        if (entries_.empty()) ImGui::TextUnformatted("No achievements are available for this title.");
        const float gap = 12 * scale;
        const float card_width = (ImGui::GetContentRegionAvail().x - gap) / 2;
        const float icon_size = 64 * scale;
        const float inset = 12 * scale;
        const float text_width = card_width - icon_size - 3 * inset;
        ImGui::PushFont(theme.heading_font, 22 * scale);
        ImFont* heading = ImGui::GetFont();
        ImGui::PopFont();
        ImFont* body = ImGui::GetFont();
        const auto description = [&](const rex::system::AchievementInfo& entry) -> const std::string& {
          return !manager_->IsUnlocked(entry.id) && !entry.unachieved_description.empty()
                     ? entry.unachieved_description : entry.description;
        };
        for (int row = 0; row < static_cast<int>(entries_.size()); row += 2) {
          float row_height = icon_size + 2 * inset;
          for (int i = row; i < std::min(row + 2, int(entries_.size())); ++i) {
            const float title_height = heading->CalcTextSizeA(22 * scale, 1e9f,
                                        text_width, entries_[i].label.c_str()).y;
            const float desc_height = body->CalcTextSizeA(18 * scale, 1e9f,
                                        text_width, description(entries_[i]).c_str()).y;
            row_height = std::max(row_height, 2 * inset + title_height + desc_height + 30 * scale);
          }
          for (int i = row; i < std::min(row + 2, int(entries_.size())); ++i) {
            if (i != row) ImGui::SameLine(0, gap);
            const auto& entry = entries_[i];
            const bool unlocked = manager_->IsUnlocked(entry.id);
            const ImVec2 card = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(entry.id));
            ImGui::InvisibleButton("##card", ImVec2(card_width, row_height));
            // A parked pointer does not steal selection back from the controller.
            if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(0) ||
                io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || io.MouseWheel != 0))
              navigation_.selected = i;
            const bool selected = navigation_.selected == i;
            auto* cards = ImGui::GetWindowDrawList();
            const ImVec2 end(card.x + card_width, card.y + row_height);
            cards->AddRectFilled(card, end, ImGui::GetColorU32(selected
                ? ImVec4(.26f, .025f, .035f, .85f) : ImVec4(.08f, .045f, .05f, .95f)), 5 * scale);
            cards->AddRect(card, end, ImGui::GetColorU32(selected
                ? theme.border_highlight : ImVec4(.40f, .16f, .18f, .9f)), 5 * scale,
                0, (selected ? 2 : 1) * scale);
            const ImVec2 image(card.x + inset, card.y + inset);
            if (auto* texture = icons_.GetIcon(entry)) {
              cards->AddImage(reinterpret_cast<ImTextureID>(texture), image,
                  ImVec2(image.x + icon_size, image.y + icon_size), ImVec2(0, 0), ImVec2(1, 1),
                  ImGui::GetColorU32(unlocked ? ImVec4(1, 1, 1, 1) : ImVec4(.65f, .65f, .65f, 1)));
            } else {
              cards->AddRectFilled(image, ImVec2(image.x + icon_size, image.y + icon_size),
                                    IM_COL32(55, 42, 45, 255), 4 * scale);
            }
            const float text_x = image.x + icon_size + inset;
            const float title_height = heading->CalcTextSizeA(22 * scale, 1e9f,
                                        text_width, entry.label.c_str()).y;
            cards->AddText(heading, 22 * scale, ImVec2(text_x, image.y),
                          ImGui::GetColorU32(theme.text), entry.label.c_str(), nullptr, text_width);
            const float status_y = image.y + title_height + 5 * scale;
            const std::string status = std::to_string(entry.gamerscore) + " G  |  " +
                                       (unlocked ? "Unlocked" : "Locked");
            cards->AddText(body, 18 * scale, ImVec2(text_x, status_y),
                          ImGui::GetColorU32(unlocked ? ImVec4(.45f, .45f, .45f, 1) : theme.muted_text),
                          status.c_str());
            cards->AddText(body, 18 * scale, ImVec2(text_x, status_y + 25 * scale),
                          ImGui::GetColorU32(theme.muted_text), description(entry).c_str(),
                          nullptr, text_width);
            // Keep the entire focused row in view, including variable-height descriptions.
            if ((moved || appearing) && navigation_.selected == i) ImGui::SetScrollHereY();
            ImGui::PopID();
          }
        }
      }
      ImGui::EndChild();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("D-pad / left stick / arrows: browse    |    Mouse wheel: scroll");
      ImGui::SameLine();
      const float button_width = 140 * scale;
      ImGui::SetCursorPosX(size.x - pad - button_width);
      if (ImGui::Button("B / Esc: Back", ImVec2(button_width, 30 * scale))) dismiss = true;
      ImGui::PopFont();
      if (dismiss) {
        ImGui::CloseCurrentPopup();
        active.store(false);
        opened_ = false;
      }
      ImGui::EndPopup();
    }
    ImGui::PopStyleColor(10);
    ImGui::PopStyleVar(5);
  }

 private:
  rex::system::AchievementManager* manager_;
  rex::ui::AchievementIconCache icons_;
  std::vector<rex::system::AchievementInfo> entries_;
  Navigation navigation_;
  bool opened_ = false;
};
}  // namespace

void RequestOpen() { active.store(true); }
void Toggle() { active.store(!active.load()); }
bool Active() { return active.load(); }

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(
    rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate,
    rex::Runtime* runtime, rex::system::AchievementManager* manager) {
  return std::make_unique<Browser>(drawer, immediate, runtime, manager);
}
}  // namespace rb_blitz::ui::achievements

// The retail wrapper at 0x827255C0 tail-calls this import (r4 = current title).
// Replace the unavailable Xbox guide with our browser; preserve synchronous success.
extern "C" REX_FUNC(__imp__XamShowAchievementsUI) {
  rb_blitz::ui::achievements::RequestOpen();
  ctx.r3.u64 = 0;
}
