// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the shared row pieces (launcher/src/row_ui.h, A1/B1).

#include "row_ui.h"

#include "imgui.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

namespace rb_blitz::launcher {
namespace {

constexpr float kMinValueWidth = 160.0f;
constexpr float kMaxValueWidth = 360.0f;
constexpr float kMinLabelWidth = 120.0f;
constexpr std::size_t kTextBufferSize = 512;

// Not constexpr: ImVec4 has no constexpr constructor.
const ImVec4 kWarning{0.95f, 0.75f, 0.25f, 1.0f};

// A string setting is written back as a quoted TOML string, everything else as a bare token
// (src/launcher/profile.h's ValueStyle).
ValueStyle StyleForKind(settings::Kind kind) {
  return kind == settings::Kind::kString ? ValueStyle::kBasic : ValueStyle::kBare;
}

int ParseIntOrZero(std::string_view text) {
  int value = 0;
  const char* first = text.data();
  const char* last = text.data() + text.size();
  if (std::from_chars(first, last, value).ec != std::errc{}) {
    return 0;
  }
  return value;
}

float ParseFloatOrZero(std::string_view text) {
  const std::string owned(text);
  char* end = nullptr;
  const float value = std::strtof(owned.c_str(), &end);
  return end == owned.c_str() ? 0.0f : value;
}

void CopyToBuffer(std::string_view text, char (&buffer)[kTextBufferSize]) {
  const std::size_t count = std::min(text.size(), kTextBufferSize - 1);
  std::memcpy(buffer, text.data(), count);
  buffer[count] = '\0';
}

}  // namespace

RowColumns RowColumnWidths() {
  RowColumns columns;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float available = ImGui::GetContentRegionAvail().x;
  columns.value_width = std::clamp(available * 0.35f, kMinValueWidth, kMaxValueWidth);
  columns.label_width = std::max(kMinLabelWidth, available - columns.value_width - spacing);
  return columns;
}

RowScope::RowScope(std::string_view key) { ImGui::PushID(std::string(key).c_str()); }
RowScope::~RowScope() { ImGui::PopID(); }

bool DrawRowLabel(const settings::Setting& setting, std::size_t index, FocusModel& ring,
                  float label_width) {
  const bool focused = !ring.Empty() && ring.Index() == index;
  const std::string label(setting.label);
  const bool clicked = ImGui::Selectable(label.c_str(), focused, ImGuiSelectableFlags_None,
                                         ImVec2(label_width, 0.0f));
  if (clicked) {
    ring.SetIndex(index);
  }
  ring.FocusIf(index, ImGui::IsItemHovered());
  // A row the ring has moved to is brought into view. Without this a tab taller than the window
  // - which both the Graphics tab and B4's profile block are - would be reachable by the wheel
  // and not by the keyboard or the pad, and A5's "complete every tab with the keyboard only"
  // would be impossible rather than merely untested.
  if (focused && !ImGui::IsItemVisible()) {
    ImGui::SetScrollHereY(0.5f);
  }
  return clicked;
}

void DrawReadOnlyValue(const settings::Setting& setting, float value_width,
                       std::string_view value_text) {
  switch (setting.kind) {
    case settings::Kind::kBool: {
      bool value = value_text == "true";
      ImGui::Checkbox("##value", &value);
      return;
    }
    case settings::Kind::kInt: {
      int value = ParseIntOrZero(value_text);
      ImGui::SetNextItemWidth(value_width);
      ImGui::InputInt("##value", &value);
      return;
    }
    case settings::Kind::kFloat: {
      float value = ParseFloatOrZero(value_text);
      ImGui::SetNextItemWidth(value_width);
      ImGui::InputFloat("##value", &value);
      return;
    }
    case settings::Kind::kEnum: {
      const std::string current(value_text);
      ImGui::SetNextItemWidth(value_width);
      if (ImGui::BeginCombo("##value", current.c_str())) {
        ImGui::EndCombo();
      }
      return;
    }
    case settings::Kind::kString: {
      char text[kTextBufferSize] = {};
      CopyToBuffer(value_text, text);
      ImGui::SetNextItemWidth(value_width);
      ImGui::InputText("##value", text, sizeof(text), ImGuiInputTextFlags_ReadOnly);
      return;
    }
    case settings::Kind::kPathDir:
    case settings::Kind::kPathFile: {
      char text[kTextBufferSize] = {};
      CopyToBuffer(value_text.empty() ? std::string_view("(game default)") : value_text, text);
      const float browse_width =
          ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
      ImGui::SetNextItemWidth(std::max(kMinValueWidth * 0.5f,
                                       value_width - browse_width - ImGui::GetStyle().ItemSpacing.x));
      ImGui::InputText("##value", text, sizeof(text), ImGuiInputTextFlags_ReadOnly);
      ImGui::SameLine();
      ImGui::Button("Browse...", ImVec2(browse_width, 0.0f));
      return;
    }
  }
}

std::string_view LauncherValueText(const ProfileSession& session,
                                   const settings::Setting& setting) {
  const ProfileSetting* stored = session.profile().FindSetting(setting.key);
  return stored == nullptr ? setting.default_text : std::string_view(stored->value);
}

void DrawOverrideBadge(const settings::Setting& setting, ProfileSession& session) {
  const std::string_view launcher_value = LauncherValueText(session, setting);
  const RowOverride over = session.OverrideFor(setting.key, launcher_value, setting.default_text);
  if (!over.overridden) {
    return;
  }

  ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
  ImGui::TextWrapped("%s", OverrideNoteText(launcher_value, over.game_value).c_str());
  ImGui::PopStyleColor();
  // The value the game will use, adopted into the launcher's own file. A badge that only
  // complained would leave the user to type it, and one that wrote the game's file would fight
  // the F4 overlay (D2).
  if (ImGui::SmallButton("Copy the effective value")) {
    session.SetSetting(setting.key, over.game_value, setting.default_text, StyleForKind(setting.kind));
  }
}

}  // namespace rb_blitz::launcher
