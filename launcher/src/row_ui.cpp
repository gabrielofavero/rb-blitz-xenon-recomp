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

#include "focus_ring.h"

namespace rb_blitz::launcher {
namespace {

constexpr float kMinValueWidth = 160.0f;
constexpr float kMaxValueWidth = 360.0f;
constexpr float kMinLabelWidth = 120.0f;
constexpr std::size_t kTextBufferSize = 512;

// Whether the pointer moved on the desktop this frame. Set once a frame by main.cpp, from SDL's
// global mouse state - row_ui.h says why the pointer's own movement, and not ImGui's delta inside
// the window, is what the rule is written against.
bool pointer_moved = false;

// Not constexpr: ImVec4 has no constexpr constructor.
const ImVec4 kWarning{0.95f, 0.75f, 0.25f, 1.0f};

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

RowColumns RowColumnWidths(float value_fraction) {
  RowColumns columns;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float available = ImGui::GetContentRegionAvail().x;
  // A single widget is capped so the label keeps a readable column; a row that asks for most
  // of the width (an enum's radios) is not capped below what it asked for.
  const float ceiling = value_fraction > 0.5f ? available : kMaxValueWidth;
  columns.value_width = std::clamp(available * value_fraction, kMinValueWidth, ceiling);
  columns.label_width = std::max(kMinLabelWidth, available - columns.value_width - spacing);
  return columns;
}

RowScope::RowScope(std::string_view key) { ImGui::PushID(std::string(key).c_str()); }
RowScope::~RowScope() { ImGui::PopID(); }

bool DrawFocusableRadio(const char* label, std::size_t index, FocusModel& ring, NavAction action,
                        bool selected) {
  const bool focused = !ring.Empty() && ring.Index() == index;
  if (focused) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  const bool clicked = ImGui::RadioButton(label, selected);
  if (focused) {
    ImGui::PopStyleColor(2);
  }
  DrawFocusOutline(focused);
  AdoptRingOnHover(index, ring);
  if (clicked) {
    ring.SetIndex(index);
  }
  if (focused && !ImGui::IsItemVisible()) {
    ImGui::SetScrollHereY(0.5f);
  }
  return clicked || (focused && action == NavAction::kActivate);
}

void DrawFocusOutline(bool focused) {
  if (!focused) {
    return;
  }
  const ImGuiStyle& style = ImGui::GetStyle();
  const float font_size = ImGui::GetFontSize();
  const float padding = focus_ring::Padding(font_size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  // ImGuiCol_NavCursor is the theme's own "this is what the keyboard is on" colour, so the ring
  // follows a theme change rather than hard-coding an accent; the shape is what makes it a signal
  // rather than a shade, which is why it is drawn at all.
  ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - padding, min.y - padding),
                                      ImVec2(max.x + padding, max.y + padding),
                                      ImGui::GetColorU32(ImGuiCol_NavCursor),
                                      focus_ring::Rounding(style.FrameRounding), 0,
                                      focus_ring::Thickness(font_size));
}

void SetPointerMoved(bool moved) { pointer_moved = moved; }

void AdoptRingOnHover(std::size_t index, FocusModel& ring) {
  ring.FocusIf(index, pointer_moved && ImGui::IsItemHovered());
}

ValueStyle StyleForKind(settings::Kind kind) {
  return kind == settings::Kind::kString ? ValueStyle::kBasic : ValueStyle::kBare;
}

bool DrawRowLabel(const settings::Setting& setting, std::size_t index, FocusModel& ring,
                  float label_width, bool focusable) {
  const bool focused = focusable && !ring.Empty() && ring.Index() == index;
  const std::string label(setting.label);
  // Frame height rather than text height: the label and the widget beside it sit on the same line,
  // and a text-height selectable leaves the widget a few units lower - the "offset" between a
  // slider and its title.
  const bool clicked = ImGui::Selectable(label.c_str(), focused, ImGuiSelectableFlags_None,
                                         ImVec2(label_width, ImGui::GetFrameHeight()));
  // A label the column is too narrow for is cut without an ellipsis by ImGui, so the full text is
  // offered on hover - the bottom bar names the row too, but a mouse user should not have to look
  // away from the row they are pointing at.
  if (ImGui::IsItemHovered() && ImGui::CalcTextSize(label.c_str()).x > label_width) {
    ImGui::SetTooltip("%s", label.c_str());
  }
  if (!focusable) {
    return false;
  }
  // A5: the ring is drawn as well as coloured, so the label of the focused row carries the outline
  // even on a display where the highlight is hard to see.
  DrawFocusOutline(focused);
  if (clicked) {
    ring.SetIndex(index);
  }
  AdoptRingOnHover(index, ring);
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
          ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2.0f;
      ImGui::SetNextItemWidth(std::max(kMinValueWidth * 0.5f,
                                       value_width - browse_width - ImGui::GetStyle().ItemSpacing.x));
      ImGui::InputText("##value", text, sizeof(text), ImGuiInputTextFlags_ReadOnly);
      ImGui::SameLine();
      ImGui::Button("Browse", ImVec2(browse_width, 0.0f));
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
