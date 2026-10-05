// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the General tab (launcher/src/general_tab.h, B1).

#include "general_tab.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

#include "path_validate.h"
#include "row_ui.h"

namespace rb_blitz::launcher {
namespace {

// Not constexpr: ImVec4 has no constexpr constructor.
const ImVec4 kWarning{0.95f, 0.75f, 0.25f, 1.0f};
const ImVec4 kError{0.95f, 0.42f, 0.38f, 1.0f};

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

void GeneralTab::ApplyChosenPath(Profile& profile) {
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

  SetProfilePath(&profile, setting->key, path);
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

void GeneralTab::DrawTargetValue(Profile& profile, std::size_t index, FocusModel& ring,
                                 NavAction action) {
  struct Option {
    LaunchTarget target;
    const char* label;
  };
  const Option options[] = {
      {LaunchTarget::kCommon, "Rock Band Blitz (Common)"},
      {LaunchTarget::kDemo, "Rock Band Blitz Demo"},
      {LaunchTarget::kUltimate, "Rock Band Blitz Ultimate"},
  };
  const std::size_t option_count = sizeof(options) / sizeof(options[0]);

  const bool activated =
      action == NavAction::kActivate && !ring.Empty() && ring.Index() == index;

  std::size_t current = 0;
  for (std::size_t i = 0; i < option_count; ++i) {
    if (options[i].target == profile.target) {
      current = i;
    }
  }
  if (activated) {
    current = (current + 1) % option_count;
    profile.target = options[current].target;
  }

  for (std::size_t i = 0; i < option_count; ++i) {
    const Option& option = options[i];
    const bool ultimate = option.target == LaunchTarget::kUltimate;
    // D5: the row says what the files say instead of being greyed out, and "merged" is
    // distinguished from a payload in its own folder.
    std::string label(option.label);
    if (ultimate && state_ == UltimateState::kAlsoPresent) {
      label += " (merged)";
    } else if (ultimate && !UltimateAvailable(state_)) {
      label += " (not installed)";
    }
    if (ultimate && !UltimateSelectable(state_)) {
      ImGui::BeginDisabled();
    }
    bool selected = profile.target == option.target;
    if (ImGui::RadioButton(label.c_str(), selected)) {
      profile.target = option.target;
    }
    if (ultimate && !UltimateSelectable(state_)) {
      ImGui::EndDisabled();
    }
  }
}

void GeneralTab::DrawPathValue(const settings::Setting& setting, float value_width,
                               Profile& profile, std::size_t index, FocusModel& ring,
                               NavAction action) {
  PathRowState& state = RowStateFor(setting.key);
  const std::string& current = ProfilePathValue(profile, setting.key);
  if (state.source != current) {
    state.source = current;
    const std::size_t count = std::min(current.size(), sizeof(state.buffer) - 1);
    std::memcpy(state.buffer, current.data(), count);
    state.buffer[count] = '\0';
  }

  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float browse_width =
      ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
  const float input_width = std::max(80.0f, value_width - browse_width - spacing);

  ImGui::SetNextItemWidth(input_width);
  const bool entered = ImGui::InputText("##value", state.buffer, sizeof(state.buffer),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  const bool committed = entered || ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  const bool browsed = ImGui::Button("Browse...", ImVec2(browse_width, 0.0f));

  // Enter and Space on the focused row open the picker, the same as the button. That is the
  // controller-friendly half of R7 - a pad cannot type a path - and the typed field beside it
  // is for the mouse and the keyboard.
  const bool activated =
      action == NavAction::kActivate && !ring.Empty() && ring.Index() == index;
  if (browsed || activated) {
    chosen_key_ = std::string(setting.key);
    SDL_ShowOpenFolderDialog(&FolderChosen, this, nullptr,
                             current.empty() ? nullptr : current.c_str(), false);
  }
  if (committed) {
    // Typing is a pick too, so it is validated the same way.
    chosen_key_ = std::string(setting.key);
    chosen_path_ = state.buffer;
  }
}

void GeneralTab::DrawMessages(const settings::Setting& setting, const Profile& profile) {
  if (setting.key == "launch.target") {
    const std::string_view state_text = UltimateStateText(state_);
    if (!state_text.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
      ImGui::TextWrapped("%s", std::string(state_text).c_str());
      ImGui::PopStyleColor();
    }
    if (!roots_.game_root_found) {
      const std::string text = "No game folder was found next to the launcher (looked for " +
                               (roots_.game_root / "default.xex").string() +
                               "), so nothing here has been verified against an install.";
      ImGui::TextDisabled("%s", text.c_str());
    }
    return;
  }

  // A path row's own value is judged every time it changes, not only when it is picked: a
  // hand-edited profile that points a writable root into the game data is refused out loud
  // too (D4), rather than only being caught the first time somebody touches the row.
  const PathVerdict& verdict = VerdictFor(setting.key, ProfilePathValue(profile, setting.key));
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
}

void GeneralTab::Draw(const TabLayout& tab, FocusModel& ring, Profile& profile, NavAction action) {
  // The file system is the authority, and it is read every frame rather than remembered: four
  // stat calls per frame is nothing, and a state detected once would be a stale promise (D5).
  state_ = DetectUltimateState(roots_.game_root);
  if (!chosen_path_.empty()) {
    ApplyChosenPath(profile);
  }

  std::size_t row_index = 0;
  for (const LayoutGroup& group : tab.groups) {
    ImGui::SeparatorText(group.group->name.data());
    for (const LayoutRow& row : group.rows) {
      if (row.setting == nullptr) {
        // A group with nothing to draw is its name and one line (D14).
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
      if (setting.key == "launch.target") {
        DrawTargetValue(profile, row_index, ring, action);
      } else if (setting.kind == settings::Kind::kPathDir ||
                 setting.kind == settings::Kind::kPathFile) {
        DrawPathValue(setting, columns.value_width, profile, row_index, ring, action);
      } else {
        ImGui::BeginDisabled();
        DrawReadOnlyValue(setting, columns.value_width);
        ImGui::EndDisabled();
      }
      DrawMessages(setting, profile);
      ++row_index;
    }
  }
}

}  // namespace rb_blitz::launcher
