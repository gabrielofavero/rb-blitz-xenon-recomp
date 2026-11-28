// SPDX-License-Identifier: GPL-2.0-only
#include "ui/achievements_overlay.h"
#include "ui/achievement_navigation.h"
#include "ui/achievement_layout.h"
#include "ui/achievement_scroll.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <rex/filesystem/xdbf.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>
#include <rex/ppc/hook.h>
#include <rex/ui/image_texture.h>
#include <rex/ui/style.h>
#include <rex/ui/ui_system.h>
#include <rex/xam/achievement_manager.h>

#include "input/native_menu.h"
#include "input/title_input.h"
#include "platform/image_decode.h"
#include "settings/runtime_settings.h"

namespace rb_blitz::ui::achievements {
namespace {
std::atomic<bool> active{false};

constexpr size_t kMaxIconBytes = 4 * 1024 * 1024;

std::unique_ptr<rex::ui::ImageTexture> DecodeIcon(const uint8_t* bytes, size_t size) {
  if (!bytes || size == 0 || size > kMaxIconBytes) return nullptr;
  std::vector<uint8_t> rgba;
  uint32_t width = 0, height = 0;
  std::string error;
  if (!platform::DecodeImageRgba(bytes, size, 512, &rgba, &width, &height, &error)) return nullptr;
  auto texture = rex::ui::ImageTexture::Create(width, height, rgba);
  return texture ? std::move(*texture) : nullptr;
}

std::unique_ptr<rex::ui::ImageTexture> LoadIconFile(const std::string& path) {
  std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())),
                     std::ios::binary | std::ios::ate);
  const auto length = file.tellg();
  if (length <= 0 || length > kMaxIconBytes) return nullptr;
  std::vector<uint8_t> bytes(static_cast<size_t>(length));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return nullptr;
  return DecodeIcon(bytes.data(), bytes.size());
}

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
  using namespace rex::hid;
  IInputSystem::PadReading state{};
  if (input::SamplePhysicalPad(&state, true)) {
    const auto& pad = state.state;
    if ((pad.buttons & kPadDPadLeft) || pad.thumb_lx < -16000) held |= kLeft;
    if ((pad.buttons & kPadDPadRight) || pad.thumb_lx > 16000) held |= kRight;
    if ((pad.buttons & kPadDPadUp) || pad.thumb_ly > 16000) held |= kUp;
    if ((pad.buttons & kPadDPadDown) || pad.thumb_ly < -16000) held |= kDown;
    if (pad.buttons & kPadB) held |= kBack;
  }
  return held;
}

class Browser final : public rex::ui::DialogBase {
 public:
  Browser(rex::ui::IUISystem& ui, rex::kernel::TitleRuntime& title) : ui_(ui), title_(title) {
    if (!SDL_AddEventWatch(Watch, this))
      REXLOG_ERROR("achievements: could not register touch events: {}", SDL_GetError());
  }
  bool WantsExclusiveInput() const override { return Active(); }
  ~Browser() override { SDL_RemoveEventWatch(Watch, this); active.store(false); }

  bool Draw(ImGuiIO& io) override {
    if (!title_.IsLoaded()) {
      active.store(false);
      entries_.clear();
      icons_.clear();
    }
    if (!Active()) {
      ResetScroll();
      // F7 can dismiss the persistent browser from outside its own draw. Retire
      // the popup as well, otherwise ImGui would keep an invisible modal open.
      if (opened_ && ImGui::BeginPopupModal("##blitz_achievements")) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      opened_ = false;
      return true;
    }
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) { ResetScroll(); return true; }
    auto& manager = title_.GetAchievementManager();
    const bool appearing = !opened_;
    if (appearing) {
      entries_ = manager.ListAchievements();
      LoadIcons();
      navigation_.Reset();
      ImGui::OpenPopup("##blitz_achievements");
      opened_ = true;
    }
    const auto& theme = ui_.GetStyle().toast;
    const auto layout = LayoutBrowser(io.DisplaySize.x, io.DisplaySize.y, CurrentPortraitViewport());
    const float scale = layout.scale, pad = layout.padding;
    const ImVec2 size(layout.width, layout.height);
    const ImVec2 pos(layout.x, layout.y);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12 * scale, 12 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 14 * scale);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.025f, .015f, .02f, .97f));
    ImGui::PushStyleColor(ImGuiCol_Text, rex::ui::ToImVec4(theme.text));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.025f, .015f, .02f, .97f));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, .55f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(.16f, .16f, .16f, 1));
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
        if (manager.IsUnlocked(entry.id)) {
          ++unlocked_count;
          earned_score += entry.gamerscore;
        }
      }
      ImGui::TextWrapped("%d / %d unlocked  |  %d / %d G", unlocked_count,
                  static_cast<int>(entries_.size()), earned_score, total_score);
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.border);
      ImGui::ProgressBar(entries_.empty() ? 0 : float(unlocked_count) / entries_.size(),
                         ImVec2(-1, 5 * scale), "");
      ImGui::PopStyleColor();

      const uint32_t command = navigation_.Tick(HeldCommands(), input::HostInputFocused(),
                                                 io.DeltaTime);
      bool dismiss = (command & kBack) != 0;
      const float button_height = layout.portrait ? std::max(44.f, 48 * scale) : 30 * scale;
      const float list_height = std::max(1.f, ImGui::GetContentRegionAvail().y -
          button_height - ImGui::GetStyle().ItemSpacing.y);
      const bool moved = navigation_.Move(command, static_cast<int>(entries_.size()),
                          std::max(1, int(list_height / (150 * scale))), layout.columns);
      const auto native = ui_.GetNativeWindow();
      auto* window = native ? static_cast<SDL_Window*>(native->sdl) : nullptr;
      int window_width = 0, window_height = 0;
      if (window) SDL_GetWindowSize(window, &window_width, &window_height);
      const ScrollViewport frame{pos.x, pos.y, pos.x + size.x, pos.y + size.y,
          io.DisplaySize.x, io.DisplaySize.y, float(window_width), float(window_height)};
      const bool scroll_enabled = window && window_width > 0 && window_height > 0 &&
          ui_.IsFocused() && input::HostInputFocused() && !dismiss;
      {
        std::lock_guard lock(scroll_mutex_);
        if (appearing || frame != scroll_frame_ || !scroll_enabled || command ||
            io.MouseWheel != 0 || io.MouseWheelH != 0) scroll_.Cancel();
        scroll_frame_ = frame;
        scroll_window_ = window ? SDL_GetWindowID(window) : 0;
        // Apply before BeginChild so card drawing and hit testing use the new
        // viewport in this frame, rather than deferring it to the next frame.
        if (scroll_enabled) if (auto y = scroll_.Advance(double(SDL_GetTicksNS()) * 1e-9))
          ImGui::SetNextWindowScroll(ImVec2(-1, *y));
      }
      if (ImGui::BeginChild("##achievement_cards", ImVec2(0, list_height),
                            ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar |
                            ImGuiWindowFlags_NoNav)) {
        const bool reflowed = layout.columns != previous_columns_;
        previous_columns_ = layout.columns;
        if (appearing) ImGui::SetScrollY(0);
        const auto child_pos = ImGui::GetWindowPos(), child_size = ImGui::GetWindowSize();
        std::optional<ScrollPoint> tap;
        bool touch_owns_view = false;
        {
          std::lock_guard lock(scroll_mutex_);
          auto viewport = frame;
          viewport.left = child_pos.x; viewport.top = child_pos.y;
          viewport.right = child_pos.x + child_size.x - ImGui::GetStyle().ScrollbarSize;
          viewport.bottom = child_pos.y + child_size.y;
          scroll_.Configure(viewport, ImGui::GetScrollY(), ImGui::GetScrollMaxY(), scroll_enabled);
          scroll_ready_ = scroll_enabled;
          tap = scroll_.TakeTap();
          touch_owns_view = scroll_.TouchOwnsView();
        }
        const bool touch_pointer = touch_owns_view || io.MouseSource == ImGuiMouseSource_TouchScreen ||
            settings::title::mouse_as_touch.Get();
        if (entries_.empty()) ImGui::TextUnformatted("No achievements are available for this title.");
        const float gap = 12 * scale;
        const float card_width = (ImGui::GetContentRegionAvail().x - gap * (layout.columns - 1)) /
                                 layout.columns;
        const float icon_size = 64 * scale;
        const float inset = 12 * scale;
        const float text_width = card_width - icon_size - 3 * inset;
        ImGui::PushFont(theme.heading_font, 22 * scale);
        ImFont* heading = ImGui::GetFont();
        ImGui::PopFont();
        ImFont* body = ImGui::GetFont();
        const auto description = [&](const rex::Achievement& entry) -> const std::string& {
          return !manager.IsUnlocked(entry.id) && !entry.unachieved_description.empty()
                     ? entry.unachieved_description : entry.description;
        };
        for (int row = 0; row < static_cast<int>(entries_.size()); row += layout.columns) {
          float row_height = icon_size + 2 * inset;
          for (int i = row; i < std::min(row + layout.columns, int(entries_.size())); ++i) {
            const float title_height = heading->CalcTextSizeA(22 * scale, 1e9f,
                                        text_width, entries_[i].label.c_str()).y;
            const float desc_height = body->CalcTextSizeA(18 * scale, 1e9f,
                                        text_width, description(entries_[i]).c_str()).y;
            row_height = std::max(row_height, 2 * inset + title_height + desc_height + 30 * scale);
          }
          for (int i = row; i < std::min(row + layout.columns, int(entries_.size())); ++i) {
            if (i != row) ImGui::SameLine(0, gap);
            const auto& entry = entries_[i];
            const bool unlocked = manager.IsUnlocked(entry.id);
            const ImVec2 card = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(entry.id));
            ImGui::InvisibleButton("##card", ImVec2(card_width, row_height));
            if (tap && tap->x >= card.x && tap->x < card.x + card_width &&
                tap->y >= card.y && tap->y < card.y + row_height) navigation_.selected = i;
            // A parked pointer does not steal selection back from the controller.
            if (!touch_pointer && ImGui::IsItemHovered() && (ImGui::IsMouseClicked(0) ||
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
            if (const auto icon = icons_.find(entry.id); icon != icons_.end()) {
              cards->AddImage(icon->second->GetTexture(), image,
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
                          ImGui::GetColorU32(rex::ui::ToImVec4(theme.text)), entry.label.c_str(), nullptr, text_width);
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
            if ((moved || appearing || reflowed) && navigation_.selected == i) ImGui::SetScrollHereY();
            ImGui::PopID();
          }
        }
      } else ResetScroll();
      ImGui::EndChild();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(layout.portrait || io.MouseSource == ImGuiMouseSource_TouchScreen ||
          settings::title::mouse_as_touch.Get() ? "Drag or flick to scroll" :
          "D-pad / left stick / arrows: browse    |    Mouse wheel: scroll");
      ImGui::SameLine();
      const float button_width = 140 * scale;
      ImGui::SetCursorPosX(size.x - pad - button_width);
      if (ImGui::Button(layout.portrait ? "Back" : "B / Esc: Back",
                        ImVec2(button_width, button_height))) dismiss = true;
      ImGui::PopFont();
      if (dismiss) {
        ResetScroll();
        ImGui::CloseCurrentPopup();
        active.store(false);
        opened_ = false;
      }
      ImGui::EndPopup();
    } else ResetScroll();
    ImGui::PopStyleColor(11);
    ImGui::PopStyleVar(5);
    return true;
}

 private:
  void ResetScroll() {
    std::lock_guard lock(scroll_mutex_);
    scroll_.Reset(); scroll_ready_ = false;
  }
  static bool SDLCALL Watch(void* data, SDL_Event* event) {
    auto& self = *static_cast<Browser*>(data);
    if (!Active()) return true;
    std::lock_guard lock(self.scroll_mutex_);
    const bool background = event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
                            event->type == SDL_EVENT_DID_ENTER_BACKGROUND;
    const bool window_boundary = event->type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event->type == SDL_EVENT_WINDOW_MINIMIZED;
    if (background || (window_boundary &&
                       event->window.windowID == self.scroll_window_)) {
      self.scroll_.Reset(); self.scroll_ready_ = false;
      return true;
    }
    if (!self.scroll_ready_) return true;
    ScrollPhase phase;
    float x = 0, y = 0;
    uint64_t device = 0, finger = 0;
    switch (event->type) {
      case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION:
      case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED: {
        const auto& touch = event->tfinger;
        if (touch.windowID != self.scroll_window_ || touch.touchID == SDL_MOUSE_TOUCHID ||
            touch.touchID == SDL_PEN_TOUCHID) return true;
        phase = event->type == SDL_EVENT_FINGER_DOWN ? ScrollPhase::kDown :
            event->type == SDL_EVENT_FINGER_MOTION ? ScrollPhase::kMove :
            event->type == SDL_EVENT_FINGER_UP ? ScrollPhase::kUp : ScrollPhase::kCancel;
        x = touch.x; y = touch.y; device = touch.touchID; finger = touch.fingerID;
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
      case SDL_EVENT_MOUSE_MOTION: {
        const bool move = event->type == SDL_EVENT_MOUSE_MOTION;
        const auto window = move ? event->motion.windowID : event->button.windowID;
        const auto which = move ? event->motion.which : event->button.which;
        if (window != self.scroll_window_ || which == SDL_TOUCH_MOUSEID) return true;
        if (!settings::title::mouse_as_touch.Get() ||
            (!move && event->button.button != SDL_BUTTON_LEFT)) {
          self.scroll_.Cancel(); return true;
        }
        phase = move ? ScrollPhase::kMove : event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ?
            ScrollPhase::kDown : ScrollPhase::kUp;
        x = (move ? event->motion.x : event->button.x) / self.scroll_frame_.window_width;
        y = (move ? event->motion.y : event->button.y) / self.scroll_frame_.window_height;
        device = UINT64_MAX;
        break;
      }
      case SDL_EVENT_MOUSE_WHEEL:
        if (event->wheel.windowID == self.scroll_window_) self.scroll_.Cancel();
        return true;
      case SDL_EVENT_KEY_DOWN:
        if (event->key.windowID == self.scroll_window_) self.scroll_.Cancel();
        return true;
      default: return true;
    }
    const auto timestamp = event->common.timestamp ? event->common.timestamp : SDL_GetTicksNS();
    self.scroll_.Observe(device, finger, phase, x, y, double(timestamp) * 1e-9);
    return true;
  }
  void LoadIcons() {
    const auto db = title_.GetTitleXDBF();
    for (const auto& entry : entries_) {
      if (icons_.contains(entry.id)) continue;
      auto texture = entry.icon_path.empty() ? nullptr : LoadIconFile(entry.icon_path);
      if (!texture && entry.image_id != 0 && db->IsValid()) {
        const auto icon = db->GetEntry(rex::filesystem::XDBFSection::kImage, entry.image_id);
        texture = DecodeIcon(icon.buffer, icon.size);
      }
      if (texture) icons_.emplace(entry.id, std::move(texture));
    }
  }
  rex::ui::IUISystem& ui_;
  rex::kernel::TitleRuntime& title_;
  std::map<uint32_t, std::unique_ptr<rex::ui::ImageTexture>> icons_;
  std::vector<rex::Achievement> entries_;
  Navigation navigation_;
  std::mutex scroll_mutex_;
  CardScroll scroll_;
  ScrollViewport scroll_frame_;
  SDL_WindowID scroll_window_ = 0;
  bool scroll_ready_ = false;
  int previous_columns_ = 0;
  bool opened_ = false;
};
}  // namespace

void RequestOpen() { active.store(true); }
void Toggle() { active.store(!active.load()); }
bool Active() { return active.load(); }

std::unique_ptr<rex::ui::IDialog> CreateOverlay(
    rex::ui::IUISystem& ui, rex::kernel::TitleRuntime& title) {
  return std::make_unique<Browser>(ui, title);
}
}  // namespace rb_blitz::ui::achievements

// The retail wrapper at 0x827255C0 tail-calls this import (r4 = current title).
// Replace the unavailable Xbox guide with our browser; preserve synchronous success.
extern "C" REX_FUNC(__imp__XamShowAchievementsUI) {
  rb_blitz::ui::achievements::RequestOpen();
  ctx.r3.u64 = 0;
}
