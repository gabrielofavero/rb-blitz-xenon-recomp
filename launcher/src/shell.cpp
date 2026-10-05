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
#include "settings_edit.h"

namespace rb_blitz::launcher {
namespace {

// The one window's ImGui id. ImGuiWindowFlags_NoSavedSettings means ImGui never writes an ini
// for it, so the name is only an identity.
constexpr const char* kWindowId = "Rock Band Blitz Launcher";

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

Shell::Shell(ProfileSession session, GameRoots roots)
    : roots_(std::move(roots)),
      session_(std::move(session)),
      general_(roots_),
      layout_(BuildLayout()),
      keyboard_(std::make_unique<KeyboardNavSource>()),
      gamepad_(std::make_unique<GamepadNavSource>()) {
  // D5's target fallback lives in the General tab, which applies it to what it shows rather than
  // to the profile (B4): with a write path, mutating the stored target here would have made a
  // window resize record a choice the user never made.
  rings_.resize(layout_.size());
  for (std::size_t index = 0; index < layout_.size(); ++index) {
    // A row takes one ring entry, except an enum, which takes one per choice because each
    // choice is drawn as its own radio (schema_view's FocusEntriesFor). B4's profile block is
    // focusable too and lives at the end of the General tab, so that tab adds its rows.
    std::size_t entries = 0;
    for (const LayoutGroup& group : layout_[index].groups) {
      for (const LayoutRow& row : group.rows) {
        if (row.setting != nullptr) {
          entries += FocusEntriesFor(*row.setting);
        }
      }
    }
    const std::size_t extra =
        layout_[index].tab == settings::Tab::kGeneral ? GeneralTab::kExtraRows : 0;
    rings_[index].Reset(entries + extra);
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
    // Escape belongs to the modal that is up - B4's reset confirmation, or B8's install
    // progress - and only means "leave the launcher" when nothing modal is on screen (A1).
    if (!general_.ModalOpen()) {
      return false;
    }
  }
  // A modal owns the launcher while it is up, so the tab cannot be changed behind it: its own
  // tab has to keep being drawn for its state machine to keep running, and walking away from it
  // would leave the helper installing with nothing watching.
  if (general_.ModalOpen() &&
      (action == NavAction::kNextTab || action == NavAction::kPreviousTab)) {
    action = NavAction::kNone;
  }
  ApplyAction(action);
  action_ = action;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin(kWindowId, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);

  ImGui::TextUnformatted("Rock Band Blitz Launcher");
  ImGui::SameLine();
  ImGui::TextDisabled(
      "Tab / Up / Down move  |  Left / Right switches tab  |  Enter activates  |  Esc quits");

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
  // B4: a change the profile has not been saved with. It sits beside the tabs rather than only in
  // the General tab's block, because a badge's action or (B2/B3) a row's edit can happen on
  // another tab, and "did that stick?" must not depend on looking at a different tab.
  if (session_.Dirty()) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.25f, 1.0f));
    ImGui::TextUnformatted("unsaved changes");
    ImGui::PopStyleColor();
  }
  ImGui::Separator();

  if (tab_ < layout_.size()) {
    if (layout_[tab_].tab == settings::Tab::kGeneral) {
      general_.Draw(layout_[tab_], rings_[tab_], session_, action_);
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
    // A category this build has nothing for is hidden outright rather than naming itself
    // (D14 allows either; the plan's "reduce the useless text" is the newer instruction).
    if (group.unavailable) {
      continue;
    }
    ImGui::SeparatorText(group.group->name.data());
    for (const LayoutRow& row : group.rows) {
      if (row.setting == nullptr) {
        continue;
      }
      const settings::Setting& setting = *row.setting;
      RowScope scope(setting.key);
      // A row whose widget is a line of radios (an enum) needs the width for them, so it takes
      // most of the row and keeps its label in what is left; everything else is a single
      // widget in a value column beside a full-width label.
      const RowColumns columns =
          RowColumnWidths(setting.kind == settings::Kind::kEnum ? 0.72f : 0.35f);
      DrawRowLabel(setting, row_index, ring, columns.label_width);
      ImGui::SameLine();
      // B2: the row is the widget its kind calls for, and what it changes lands in the
      // profile - which B4's Save is what writes to disk.
      DrawEditableSetting(setting, columns.value_width, row_index, ring, action_, session_);
      // D12: the row's own truth. A restart row says so rather than letting a user believe the
      // change took effect while the game is not even running.
      if (setting.applies == settings::Applies::kRestart) {
        ImGui::SameLine();
        ImGui::TextDisabled("(needs restart)");
      }
      // B4's precedence badge: a row the game's own file decides says so here too, so the
      // warning does not depend on which tab the user is looking at.
      DrawOverrideBadge(setting, session_);
      row_index += FocusEntriesFor(setting);
    }
  }
}

}  // namespace rb_blitz::launcher
