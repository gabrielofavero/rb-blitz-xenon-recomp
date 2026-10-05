// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the General tab (launcher/src/general_tab.h, B1).

#include "general_tab.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "path_validate.h"
#include "row_ui.h"

namespace rb_blitz::launcher {
namespace {

// Not constexpr: ImVec4 has no constexpr constructor.
const ImVec4 kError{0.95f, 0.42f, 0.38f, 1.0f};

constexpr const char* kInstallPopup = "Install Ultimate";

// The profile field a path row owns. B4 generalises this; M1's General tab has exactly two
// path rows and both are named here so nothing else has to guess.
const std::string& ProfilePathValue(const Profile& profile, std::string_view key) {
  return key == "user_data_root" ? profile.user_data_dir : profile.dlc_dir;
}

void SetProfilePath(Profile* profile, std::string_view key, const std::string& value) {
  if (key == "user_data_root") {
    profile->user_data_dir = value;
  } else {
    profile->dlc_dir = value;
  }
}

void SDLCALL FolderChosen(void* userdata, const char* const* filelist, int /*filter*/) {
  static_cast<GeneralTab*>(userdata)->OnFolderChosen(filelist);
}

// The folder the game will actually use for a row whose profile value is empty. The defaults
// belong to the game (src/rb_blitz_app.h for the save folder, src/fs/dlc_layout.h for DLC), so
// they are repeated as arithmetic here rather than left as a blank field - a blank field reads
// as "nowhere", and this is the one place a user can see where their saves are about to go.
std::string EffectivePath(const settings::Setting& setting, const Profile& profile,
                         const GameRoots& roots) {
  if (const std::string& configured = ProfilePathValue(profile, setting.key);
      !configured.empty()) {
    return configured;
  }
  // The same known folder rex::filesystem::GetUserFolder() returns, through SDL rather than the
  // SDK: the launcher does not link the runtime.
  if (setting.key == "user_data_root") {
    const char* documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    if (documents == nullptr) {
      return {};
    }
    return (std::filesystem::path(documents) / "rb_blitz").string();
  }
  if (setting.key == "dlc_root" && !roots.game_root.empty()) {
    return (roots.game_root / "dlc").string();
  }
  return {};
}

}  // namespace

GeneralTab::GeneralTab(GameRoots roots) : roots_(std::move(roots)) {}

GeneralTab::PathRowState& GeneralTab::RowStateFor(std::string_view key) {
  for (PathRowState& state : path_rows_) {
    if (state.key == key) {
      return state;
    }
  }
  path_rows_.push_back(PathRowState{std::string(key), {}, {}});
  return path_rows_.back();
}

void GeneralTab::OnFolderChosen(const char* const* filelist) {
  if (filelist == nullptr || filelist[0] == nullptr) {
    chosen_key_.clear();  // cancelled, or the dialog failed
    return;
  }
  chosen_path_ = filelist[0];
}

void GeneralTab::ApplyChosenPath(ProfileSession& session) {
  const std::string path = chosen_path_;
  chosen_path_.clear();
  const settings::Setting* setting = FindSetting(chosen_key_);
  if (setting == nullptr) {
    return;
  }

  const PathVerdict verdict = ValidatePathValue(setting->validate, path, roots_.game_root);
  if (!verdict.ok) {
    // D4: the refusal is the answer, not a silent acceptance. The row keeps the value the
    // profile actually holds and says why the one that was asked for was not taken.
    message_key_ = std::string(setting->key);
    message_ = verdict.reason;
    RowStateFor(setting->key).source.clear();  // forces the next Draw to reload the field
    return;
  }

  SetProfilePath(&session.profile(), setting->key, path);
  message_key_.clear();
  message_.clear();
  RowStateFor(setting->key).source.clear();
}

const PathVerdict& GeneralTab::VerdictFor(std::string_view key, const std::string& value) {
  for (VerdictCache& cache : verdicts_) {
    if (cache.key == key) {
      if (!cache.valid || cache.value != value) {
        const settings::Setting* setting = FindSetting(key);
        cache.value = value;
        cache.verdict = ValidatePathValue(setting == nullptr ? std::string_view{}
                                                            : setting->validate,
                                         value, roots_.game_root);
        cache.valid = true;
      }
      return cache.verdict;
    }
  }
  verdicts_.push_back(VerdictCache{std::string(key), {}, false, {}});
  return VerdictFor(key, value);
}

void GeneralTab::DrawTargetRows(ProfileSession& session, std::size_t first_row, FocusModel& ring,
                                NavAction action) {
  Profile& profile = session.profile();
  // D5: the target the launcher will actually start, which is the stored choice except while the
  // payload is missing. It is computed for the screen and not written to the profile: the row
  // says what will happen, and installing the payload later restores the choice without the user
  // having to remember what it was (B4).
  const LaunchTarget effective = FallbackTarget(profile.target, state_);

  // D5's "never hard-disable": when the payload is not there, the Ultimate entry is not shown
  // greyed with an explanation - the entry that would install it takes its place, so the row
  // always offers something the user can do.
  if (!UltimateAvailable(state_)) {
    if (DrawFocusableRadio("Rock Band Blitz", first_row, ring, action,
                           effective == LaunchTarget::kCommon)) {
      profile.target = LaunchTarget::kCommon;
    }
    if (DrawFocusableRadio("Rock Band Blitz Demo", first_row + 1, ring, action,
                           effective == LaunchTarget::kDemo)) {
      profile.target = LaunchTarget::kDemo;
    }

    const std::size_t index = first_row + 2;
    // The gap is the point: the two radios above choose the game, and this is the thing to do
    // about it, so it must not read as a third choice in the same list.
    ImGui::Spacing();
    ImGui::Spacing();
    const bool focused = !ring.Empty() && ring.Index() == index;
    if (focused) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const bool clicked = ImGui::Button("Install Ultimate");
    if (focused) {
      ImGui::PopStyleColor();
    }
    ring.FocusIf(index, ImGui::IsItemHovered());
    if (clicked) {
      ring.SetIndex(index);
    }
    if (focused && !ImGui::IsItemVisible()) {
      ImGui::SetScrollHereY(0.5f);
    }
    if (clicked || (focused && action == NavAction::kActivate)) {
      std::string error;
      install_.Start(roots_.game_root, session.executable_dir(), &error);
      // Opened by the modal itself, outside this row's ImGui id scope: a popup is keyed by the
      // id stack it is opened in, and BeginPopupModal is not inside DrawTargetRows.
      install_requested_ = true;
    }
    return;
  }

  if (DrawFocusableRadio("Rock Band Blitz", first_row, ring, action,
                         effective == LaunchTarget::kCommon)) {
    profile.target = LaunchTarget::kCommon;
  }
  if (DrawFocusableRadio("Rock Band Blitz Demo", first_row + 1, ring, action,
                         effective == LaunchTarget::kDemo)) {
    profile.target = LaunchTarget::kDemo;
  }
  if (DrawFocusableRadio("Rock Band Blitz Ultimate", first_row + 2, ring, action,
                         effective == LaunchTarget::kUltimate)) {
    profile.target = LaunchTarget::kUltimate;
  }
}

void GeneralTab::DrawInstallModal(NavAction action) {
  // The helper is watched through its own progress file, so the download and the frame loop
  // run side by side rather than one blocking the other.
  install_.Poll();
  if (install_requested_) {
    install_requested_ = false;
    ImGui::OpenPopup(kInstallPopup);
  }
  if (ImGui::BeginPopupModal(kInstallPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (install_.Busy()) {
      ImGui::TextUnformatted("Downloading and installing the Ultimate mod");
      if (install_.progress_percent() >= 0) {
        ImGui::ProgressBar(install_.progress_percent() / 100.0f, ImVec2(360.0f, 0.0f));
      } else {
        // Indeterminate: the helper has not written its first progress line yet.
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(360.0f, 0.0f));
      }
      if (!install_.progress_detail().empty()) {
        ImGui::TextDisabled("%s", install_.progress_detail().c_str());
      }
      ImGui::Spacing();
      if (ImGui::Button("Cancel") || action == NavAction::kCancel) {
        install_.Cancel();
      }
    } else if (install_.status() == UltimateInstaller::Status::kSucceeded) {
      ImGui::TextWrapped("%s", install_.message().c_str());
      ImGui::Spacing();
      if (ImGui::Button("Close") || action == NavAction::kCancel) {
        install_.Reset();
        ImGui::CloseCurrentPopup();
      }
    } else {
      // Failed, or cancelled: the same honest exit either way - what went wrong, and the two
      // manual steps that do not depend on the helper at all.
      ImGui::PushStyleColor(ImGuiCol_Text, kError);
      ImGui::TextWrapped("%s", install_.message().c_str());
      ImGui::PopStyleColor();
      ImGui::Spacing();
      ImGui::TextUnformatted("You can install it by hand instead:");
      if (!install_.manual_url().empty()) {
        ImGui::BulletText("Download the Rock Band Blitz Ultimate release:");
        ImGui::TextWrapped("%s", install_.manual_url().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open the download page")) {
          OpenInShell(install_.manual_url());
        }
      } else {
        ImGui::BulletText("Download the Rock Band Blitz Ultimate release from its GitHub page.");
      }
      ImGui::BulletText("Unpack the Xbox folder of that archive into:");
      ImGui::TextWrapped("%s", install_.manual_destination().string().c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Open that folder")) {
        OpenFolder(install_.manual_destination());
      }
      ImGui::Spacing();
      if (ImGui::Button("Close") || action == NavAction::kCancel) {
        install_.Reset();
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
  install_modal_open_ = ImGui::IsPopupOpen(kInstallPopup);
}

void GeneralTab::DrawPathValue(const settings::Setting& setting, float value_width,
                               ProfileSession& session, std::size_t index, FocusModel& ring,
                               NavAction action) {
  PathRowState& state = RowStateFor(setting.key);
  // The field shows the path that will be used, not the one that was typed: an empty profile
  // value means the game's own default, and showing nothing there would hide where the saves
  // are about to go. Editing the field is still the way to override it.
  const std::string shown = EffectivePath(setting, session.profile(), roots_);
  if (state.source != shown) {
    state.source = shown;
    const std::size_t count = std::min(shown.size(), sizeof(state.buffer) - 1);
    std::memcpy(state.buffer, shown.data(), count);
    state.buffer[count] = '\0';
  }
  const std::string& current = ProfilePathValue(session.profile(), setting.key);

  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float browse_width =
      ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2.0f;
  const float input_width = std::max(80.0f, value_width - browse_width - spacing);

  ImGui::SetNextItemWidth(input_width);
  const bool entered = ImGui::InputText("##value", state.buffer, sizeof(state.buffer),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  const bool committed = entered || ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  const bool browsed = ImGui::Button("Browse", ImVec2(browse_width, 0.0f));

  // Enter and Space on the focused row open the picker, the same as the button. That is the
  // controller-friendly half of R7 - a pad cannot type a path - and the typed field beside it
  // is for the mouse and the keyboard.
  const bool activated =
      action == NavAction::kActivate && !ring.Empty() && ring.Index() == index;
  if (browsed || activated) {
    chosen_key_ = std::string(setting.key);
    const std::string start = current.empty() ? shown : current;
    SDL_ShowOpenFolderDialog(&FolderChosen, this, nullptr,
                             start.empty() ? nullptr : start.c_str(), false);
  }
  if (committed) {
    // Typing is a pick too, so it is validated the same way. Enter on a field nobody touched
    // describes the same value it already showed, and the profile takes it as an explicit
    // choice rather than as the default - which is what the field said all along.
    chosen_key_ = std::string(setting.key);
    chosen_path_ = state.buffer;
  }
}

void GeneralTab::DrawMessages(const settings::Setting& setting, ProfileSession& session) {
  // A path row's own value is judged every time it changes, not only when it is picked: a
  // hand-edited profile that points a writable root into the game data is refused out loud
  // too (D4), rather than only being caught the first time somebody touches the row.
  const PathVerdict& verdict =
      VerdictFor(setting.key, ProfilePathValue(session.profile(), setting.key));
  if (!verdict.ok) {
    ImGui::PushStyleColor(ImGuiCol_Text, kError);
    ImGui::TextWrapped("%s", verdict.reason.c_str());
    ImGui::PopStyleColor();
  } else if (!verdict.note.empty()) {
    ImGui::TextDisabled("%s", verdict.note.c_str());
  }
  // And the last *refused* pick, which the profile never took, is still worth saying.
  if (message_key_ == setting.key && !message_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kError);
    ImGui::TextWrapped("%s", message_.c_str());
    ImGui::PopStyleColor();
  }
  // And B4's precedence badge, which every tab's rows can carry.
  DrawOverrideBadge(setting, session);
}

void GeneralTab::Draw(const TabLayout& tab, FocusModel& ring, ProfileSession& session,
                      NavAction action) {
  // The file system is the authority, and it is read every frame rather than remembered: four
  // stat calls per frame is nothing, and a state detected once would be a stale promise (D5).
  state_ = DetectUltimateState(roots_.game_root);
  if (!chosen_path_.empty()) {
    ApplyChosenPath(session);
  }

  // While the install notice is up it owns the keyboard, so the rows behind it do not act on a
  // keypress meant for it. B4's block is *not* gated here: it decides for itself whether its own
  // confirmation is up, and it is the one that can still be answering.
  const NavAction rows_action = install_modal_open_ ? NavAction::kNone : action;

  std::size_t row_index = 0;
  for (const LayoutGroup& group : tab.groups) {
    // A category this build has nothing for is hidden outright rather than naming itself.
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
      // The launch target is not a labelled row: it is a stack of choices under the group's
      // own heading, and it occupies kTargetRows ring rows.
      if (setting.key == "launch.target") {
        DrawTargetRows(session, row_index, ring, rows_action);
        row_index += kTargetRows;
        continue;
      }

      // A path is the widest thing on the tab and the one value a user reads character by
      // character, so it takes most of the row; every other kind is a single small widget
      // beside a full-width label.
      const bool is_path =
          setting.kind == settings::Kind::kPathDir || setting.kind == settings::Kind::kPathFile;
      const RowColumns columns = RowColumnWidths(is_path ? 0.62f : 0.35f);
      DrawRowLabel(setting, row_index, ring, columns.label_width);
      ImGui::SameLine();
      if (is_path) {
        DrawPathValue(setting, columns.value_width, session, row_index, ring, rows_action);
      } else {
        ImGui::BeginDisabled();
        DrawReadOnlyValue(setting, columns.value_width,
                          LauncherValueText(session, setting));
        ImGui::EndDisabled();
      }
      DrawMessages(setting, session);
      ++row_index;
    }
  }

  // B4's block, at the end of the tab and in the ring: Save, Reset, Import, Export, Portable.
  panel_.Draw(row_index, session, ring, action);
  // B8's progress/result notice, which owns the keyboard while it is up.
  DrawInstallModal(action);
}

}  // namespace rb_blitz::launcher
