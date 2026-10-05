// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The tab shell (launcher/src/shell.h, A1) and the two input sources.

#include "shell.h"

#include "imgui.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>

namespace rb_blitz::launcher {
namespace {

// The one window's ImGui id. NoImGuiWindowFlags_NoSavedSettings means ImGui never writes an
// ini for it, so the name is only an identity.
constexpr const char* kWindowId = "rb_blitz launcher";

constexpr float kMinValueWidth = 160.0f;
constexpr float kMaxValueWidth = 360.0f;
constexpr float kMinLabelWidth = 120.0f;
constexpr std::size_t kTextBufferSize = 512;

// The keyboard's mapping (A1). ImGui's own navigation is deliberately off, so every key the
// launcher binds is translated here and nowhere else.
class KeyboardNavSource : public NavSource {
 public:
  NavAction Poll() override {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_B, false)) {
      return NavAction::kCancel;
    }
    const bool shift = ImGui::GetIO().KeyShift;
    // A held arrow repeats; a held Tab does not, because Tab is also the focus-move key.
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
      return shift ? NavAction::kPrevious : NavAction::kNext;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
      return NavAction::kNext;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
      return NavAction::kPrevious;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
      return NavAction::kFirst;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
      return NavAction::kLast;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ||
        ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
      return NavAction::kNextTab;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) ||
        ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
      return NavAction::kPreviousTab;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
      return NavAction::kActivate;
    }
    return NavAction::kNone;
  }
};

// The pad is A3's work; the seam exists now so A3 fills it in without touching a widget.
class GamepadNavSource : public NavSource {
 public:
  NavAction Poll() override { return NavAction::kNone; }
};

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

// One widget per schema kind, showing the compiled default (Contract 1's `default_text`).
// A1 draws them read-only: B4 wires the edit path, and nothing here writes anything.
void DrawValueWidget(const settings::Setting& setting, float width) {
  switch (setting.kind) {
    case settings::Kind::kBool: {
      bool value = setting.default_text == "true";
      ImGui::Checkbox("##value", &value);
      return;
    }
    case settings::Kind::kInt: {
      int value = ParseIntOrZero(setting.default_text);
      ImGui::SetNextItemWidth(width);
      ImGui::InputInt("##value", &value);
      return;
    }
    case settings::Kind::kFloat: {
      float value = ParseFloatOrZero(setting.default_text);
      ImGui::SetNextItemWidth(width);
      ImGui::InputFloat("##value", &value);
      return;
    }
    case settings::Kind::kEnum: {
      const std::string current(setting.default_text);
      ImGui::SetNextItemWidth(width);
      if (ImGui::BeginCombo("##value", current.c_str())) {
        ImGui::EndCombo();
      }
      return;
    }
    case settings::Kind::kString: {
      char text[kTextBufferSize] = {};
      CopyToBuffer(setting.default_text, text);
      ImGui::SetNextItemWidth(width);
      ImGui::InputText("##value", text, sizeof(text), ImGuiInputTextFlags_ReadOnly);
      return;
    }
    case settings::Kind::kPathDir:
    case settings::Kind::kPathFile: {
      char text[kTextBufferSize] = {};
      CopyToBuffer(setting.default_text.empty() ? std::string_view("(game default)")
                                               : setting.default_text,
                   text);
      const float browse_width =
          ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
      ImGui::SetNextItemWidth(
          std::max(kMinValueWidth * 0.5f, width - browse_width - ImGui::GetStyle().ItemSpacing.x));
      ImGui::InputText("##value", text, sizeof(text), ImGuiInputTextFlags_ReadOnly);
      ImGui::SameLine();
      ImGui::Button("Browse...", ImVec2(browse_width, 0.0f));
      return;
    }
  }
}

// One setting row: the label is the focus target, the value sits beside it and never takes
// the ring. Clicking the label focuses it, and hovering adopts the ring so the mouse and the
// keyboard agree on one selection (D6).
void DrawSettingRow(const settings::Setting& setting, std::size_t index, FocusModel& ring) {
  ImGui::PushID(setting.key.data());

  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float available = ImGui::GetContentRegionAvail().x;
  const float value_width = std::clamp(available * 0.35f, kMinValueWidth, kMaxValueWidth);
  const float label_width = std::max(kMinLabelWidth, available - value_width - spacing);

  const bool focused = !ring.Empty() && ring.Index() == index;
  const std::string label(setting.label);
  if (ImGui::Selectable(label.c_str(), focused, ImGuiSelectableFlags_None,
                        ImVec2(label_width, 0.0f))) {
    ring.SetIndex(index);
  }
  ring.FocusIf(index, ImGui::IsItemHovered());

  ImGui::SameLine();
  ImGui::BeginDisabled();
  DrawValueWidget(setting, value_width);
  ImGui::EndDisabled();
  ImGui::PopID();
}

}  // namespace

Shell::Shell()
    : layout_(BuildLayout()),
      keyboard_(std::make_unique<KeyboardNavSource>()),
      gamepad_(std::make_unique<GamepadNavSource>()) {
  rings_.resize(layout_.size());
  for (std::size_t index = 0; index < layout_.size(); ++index) {
    rings_[index].Reset(layout_[index].row_count);
  }
}

settings::Tab Shell::CurrentTab() const {
  return tab_ < layout_.size() ? layout_[tab_].tab : settings::Tab::kGeneral;
}

std::size_t Shell::FocusedRow() const {
  return tab_ < rings_.size() ? rings_[tab_].Index() : 0;
}

void Shell::RequestTab(int delta) {
  const int count = static_cast<int>(layout_.size());
  if (count == 0) {
    return;
  }
  int next = (static_cast<int>(tab_) + delta) % count;
  if (next < 0) {
    next += count;
  }
  tab_ = static_cast<std::size_t>(next);
}

void Shell::ApplyAction(NavAction action) {
  switch (action) {
    case NavAction::kNextTab:
      RequestTab(1);
      return;
    case NavAction::kPreviousTab:
      RequestTab(-1);
      return;
    case NavAction::kNone:
    case NavAction::kCancel:  // Frame() answers this before ApplyAction is called
      return;
    default:
      break;
  }
  if (tab_ >= rings_.size()) {
    return;
  }
  FocusModel& ring = rings_[tab_];
  switch (action) {
    case NavAction::kNext:
      ring.MoveNext();
      break;
    case NavAction::kPrevious:
      ring.MovePrevious();
      break;
    case NavAction::kFirst:
      ring.MoveFirst();
      break;
    case NavAction::kLast:
      ring.MoveLast();
      break;
    case NavAction::kActivate:
      // A1's rows are read-only. B4 is what gives Enter and Space something to do, and A2
      // is what names the action in the bottom bar.
      break;
    default:
      break;
  }
}

bool Shell::Frame() {
  // One action per frame from whichever device produced it (D6).
  NavAction action = keyboard_->Poll();
  if (action == NavAction::kNone) {
    action = gamepad_->Poll();
  }
  if (action == NavAction::kCancel) {
    return false;
  }
  ApplyAction(action);

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin(kWindowId, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);

  ImGui::TextUnformatted("rb_blitz launcher");
  ImGui::SameLine();
  ImGui::TextDisabled("Up/Down or Tab moves  |  Left/Right or PageUp/PageDown changes tab  |  Esc or B quits");

  // The tab strip is the model's, not ImGui's tab bar. Two reasons: the selection must change
  // on the frame the key is read, and the ring - which A3's pad will drive - has to be the
  // only thing that moves it. ImGui's own tab bar would also answer Ctrl+Tab behind our back.
  for (std::size_t index = 0; index < layout_.size(); ++index) {
    if (index != 0) {
      ImGui::SameLine();
    }
    const bool selected = index == tab_;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const std::string name(settings::TabName(layout_[index].tab));
    if (ImGui::Button(name.c_str())) {
      tab_ = index;
    }
    if (selected) {
      ImGui::PopStyleColor();
    }
  }
  ImGui::Separator();

  if (tab_ < layout_.size()) {
    DrawTab(layout_[tab_], rings_[tab_]);
  }

  ImGui::End();
  return true;
}

void Shell::DrawTab(const TabLayout& tab, FocusModel& ring) {
  std::size_t row_index = 0;
  for (const LayoutGroup& group : tab.groups) {
    ImGui::SeparatorText(group.group->name.data());
    for (const LayoutRow& row : group.rows) {
      if (row.setting == nullptr) {
        // A group with nothing to draw is its name and one line, never a disabled widget
        // that looks like a setting (D14).
        const std::string note(GroupNoteText(*row.note_group));
        ImGui::TextWrapped("%s", note.c_str());
        ImGui::Spacing();
        continue;
      }
      DrawSettingRow(*row.setting, row_index, ring);
      ++row_index;
    }
  }
}

}  // namespace rb_blitz::launcher
