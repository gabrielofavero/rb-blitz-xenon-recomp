// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The tab shell (launcher/src/shell.h, A1) and the two input sources.

#include "shell.h"

#include "imgui.h"

#include <memory>
#include <string>
#include <utility>

#include "row_ui.h"

namespace rb_blitz::launcher {
namespace {

// The one window's ImGui id. ImGuiWindowFlags_NoSavedSettings means ImGui never writes an ini
// for it, so the name is only an identity.
constexpr const char* kWindowId = "rb_blitz launcher";

// The keyboard's mapping (A1). ImGui's own navigation is deliberately off, so every key the
// launcher binds is translated here and nowhere else.
class KeyboardNavSource : public NavSource {
 public:
  NavAction Poll() override {
    // While a text field is being edited the ring must not steal the keys the caret needs -
    // ImGui's navigation is off, so nothing else would stop it - and Escape belongs to the
    // field (ImGui reverts it), not to the launcher. A second Escape, with no field active,
    // leaves.
    if (ImGui::GetIO().WantTextInput) {
      return NavAction::kNone;
    }
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

}  // namespace

Shell::Shell(Profile profile, GameRoots roots)
    : roots_(std::move(roots)),
      profile_(std::move(profile)),
      general_(roots_),
      layout_(BuildLayout()),
      keyboard_(std::make_unique<KeyboardNavSource>()),
      gamepad_(std::make_unique<GamepadNavSource>()) {
  // D5: a profile that has never chosen a target comes up on Ultimate only while the payload
  // is usable. "Ultimate" with nothing to mount is a promise the launcher cannot keep, and
  // saying so here is better than starting a boot that cannot honour it.
  profile_.target = FallbackTarget(profile_.target, DetectUltimateState(roots_.game_root));
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
    default:
      // kActivate belongs to the tab - B1's General rows are the first with anything to
      // activate - so it is left alone here and handed to the tab when it is drawn.
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
  action_ = action;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin(kWindowId, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);

  ImGui::TextUnformatted("rb_blitz launcher");
  ImGui::SameLine();
  ImGui::TextDisabled(
      "Up/Down or Tab moves  |  Left/Right or PageUp/PageDown changes tab  |  Enter opens or "
      "changes  |  Esc or B quits");

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
    if (layout_[tab_].tab == settings::Tab::kGeneral) {
      general_.Draw(layout_[tab_], rings_[tab_], profile_, action_);
    } else {
      DrawTab(layout_[tab_], rings_[tab_]);
    }
  }

  ImGui::End();
  action_ = NavAction::kNone;
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
      const settings::Setting& setting = *row.setting;
      const RowColumns columns = RowColumnWidths();
      RowScope scope(setting.key);
      DrawRowLabel(setting, row_index, ring, columns.label_width);
      ImGui::SameLine();
      ImGui::BeginDisabled();
      DrawReadOnlyValue(setting, columns.value_width);
      ImGui::EndDisabled();
      ++row_index;
    }
  }
}

}  // namespace rb_blitz::launcher
