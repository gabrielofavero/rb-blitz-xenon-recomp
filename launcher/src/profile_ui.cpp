// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the profile block (launcher/src/profile_ui.h, B4).

#include "profile_ui.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <string>
#include <utility>

#include "row_ui.h"
#include "schema_view.h"

namespace rb_blitz::launcher {
namespace {

// Not constexpr: ImVec4 has no constexpr constructor.
const ImVec4 kWarning{0.95f, 0.75f, 0.25f, 1.0f};
const ImVec4 kError{0.95f, 0.42f, 0.38f, 1.0f};

constexpr const char* kResetPopup = "Reset to defaults?";
constexpr const char* kFileFilterName = "Launcher profile";

void SDLCALL ImportChosen(void* userdata, const char* const* filelist, int /*filter*/) {
  static_cast<ProfilePanel*>(userdata)->OnImportChosen(filelist);
}

void SDLCALL ExportChosen(void* userdata, const char* const* filelist, int /*filter*/) {
  static_cast<ProfilePanel*>(userdata)->OnExportChosen(filelist);
}

void SDLCALL SettingsDirChosen(void* userdata, const char* const* filelist, int /*filter*/) {
  static_cast<ProfilePanel*>(userdata)->OnSettingsDirChosen(filelist);
}

// The label a settings key is shown under, when the schema has one. A key no longer in the
// schema - a newer launcher's, or a typo in a hand-edited file - is named by its key, because
// that is exactly and only what a reset would remove.
std::string NameOfSetting(const std::string& key) {
  const settings::Setting* setting = FindSetting(key);
  if (setting == nullptr) {
    return key;
  }
  return std::string(setting->label) + " (" + key + ")";
}

}  // namespace

void ProfilePanel::OnImportChosen(const char* const* filelist) {
  if (filelist == nullptr || filelist[0] == nullptr) {
    return;  // cancelled, or the dialog failed
  }
  import_chosen_ = true;
  chosen_path_ = filelist[0];
}

void ProfilePanel::OnExportChosen(const char* const* filelist) {
  if (filelist == nullptr || filelist[0] == nullptr) {
    return;
  }
  import_chosen_ = false;
  chosen_path_ = filelist[0];
}

void ProfilePanel::OnSettingsDirChosen(const char* const* filelist) {
  if (filelist == nullptr || filelist[0] == nullptr) {
    return;  // cancelled, or the dialog failed
  }
  chosen_settings_dir_ = filelist[0];
}

void ProfilePanel::ApplyPendingDialogs(ProfileSession& session) {
  if (!chosen_settings_dir_.empty()) {
    const std::string dir = std::exchange(chosen_settings_dir_, std::string{});
    const ProfileSession::LocationOutcome outcome = session.SetSettingsDir(dir);
    if (!outcome.ok) {
      status_.clear();
      status_error_ = "Cannot move the settings: " + outcome.error;
    } else if (!outcome.changed) {
      status_error_.clear();
      status_ = "The settings are already in " + dir;
    } else {
      status_error_.clear();
      status_ = "Settings moved to " + session.path().string();
    }
  }
  if (chosen_path_.empty()) {
    return;
  }
  const std::string path = std::exchange(chosen_path_, std::string{});
  const bool importing = std::exchange(import_chosen_, false);

  if (!importing) {
    const SaveOutcome outcome = session.ExportTo(path);
    if (!outcome.ok) {
      status_.clear();
      status_error_ = "Cannot export: " + outcome.error;
    } else {
      status_error_.clear();
      status_ = outcome.wrote ? "Exported to " + path
                              : "Exported to " + path + " (it already held exactly this)";
    }
    return;
  }

  std::string error;
  if (!session.ImportFrom(path, &error)) {
    status_.clear();
    status_error_ = "Cannot import " + path + ": " + error;
    return;
  }
  // An import that only changed what is on screen until the next close would be a puzzle, and
  // the user picked the file on purpose: it is adopted here and now.
  const SaveOutcome outcome = session.Save();
  if (!outcome.ok) {
    status_.clear();
    status_error_ = "Imported " + path + ", but it could not be written: " + outcome.error;
    return;
  }
  status_error_.clear();
  status_ = "Imported " + path;
}

bool ProfilePanel::Item(std::size_t index, const FocusModel& ring, NavAction action,
                        const char* label) {
  const bool focused = action == NavAction::kActivate && !ring.Empty() && ring.Index() == index;
  // The ring is visible on the row labels (A1); a button says the same thing the tab strip does
  // for the selected tab, so focus is never invisible without needing a second drawing primitive.
  const bool in_ring = !ring.Empty() && ring.Index() == index;
  if (in_ring) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  const bool clicked = ImGui::Button(label);
  if (in_ring) {
    ImGui::PopStyleColor();
  }
  // In the ring and out of sight is not usable: bring it back, exactly as a row does (row_ui).
  if (in_ring && !ImGui::IsItemVisible()) {
    ImGui::SetScrollHereY(0.5f);
  }
  return clicked || focused;
}

void ProfilePanel::DrawStatus(const ProfileSession& session) {
  // A block that is fine says nothing: no file path, no "Saved". The path is already the value
  // the *Change settings location* button sits next to, and the two things worth printing are
  // the ones that need the user to do something - a file that could not be read, and changes
  // that are not written yet.
  if (!session.CanSave()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kError);
    ImGui::TextWrapped("This settings file was not understood, so nothing will be written over "
                       "it. Fix it by hand, or import a profile you trust. %s",
                       session.Refusal().c_str());
    ImGui::PopStyleColor();
  } else if (session.Dirty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    ImGui::TextUnformatted("Unsaved changes");
    ImGui::PopStyleColor();
  }

  if (!status_error_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kError);
    ImGui::TextWrapped("%s", status_error_.c_str());
    ImGui::PopStyleColor();
  } else if (!status_.empty()) {
    ImGui::TextDisabled("%s", status_.c_str());
  }
}

void ProfilePanel::DrawResetModal(ProfileSession& session, bool was_open, NavAction action) {
  if (ImGui::BeginPopupModal(kResetPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const ResetPlan plan = session.WhatResetWouldRemove();
    if (plan.empty()) {
      ImGui::TextUnformatted(
          "There is nothing to reset: every value is already at its compiled default.");
    } else {
      // B4's rule: name exactly what will be removed, before it is removed.
      ImGui::TextUnformatted("This sets everything back to the compiled defaults. It removes:");
      for (const std::string& key : plan.settings) {
        ImGui::BulletText("%s", NameOfSetting(key).c_str());
      }
      for (const std::string& item : plan.launch) {
        ImGui::BulletText("%s", item.c_str());
      }
      ImGui::TextDisabled("The window size is not one of these, and is left alone.");
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Enter confirms, Escape cancels.");

    // `was_open` is what keeps the Enter that *opened* this from also confirming it: the popup
    // exists by the time this runs, so the same frame's action would otherwise be read twice.
    const bool confirm = ImGui::Button("Reset to defaults") || (was_open && action == NavAction::kActivate);
    ImGui::SameLine();
    const bool cancel_clicked = ImGui::Button("Cancel") || (was_open && action == NavAction::kCancel);
    if (confirm) {
      session.ResetToDefaults();
      status_error_.clear();
      status_ = "Reset to the compiled defaults (not saved yet)";
      ImGui::CloseCurrentPopup();
    } else if (cancel_clicked) {
      // Escape belongs to the modal while it is up, which is what the shell asks ModalOpen()
      // before it reads Escape as "leave the launcher".
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  modal_open_ = ImGui::IsPopupOpen(kResetPopup);
}

void ProfilePanel::Draw(std::size_t first_row, ProfileSession& session, FocusModel& ring,
                        NavAction action) {
  // While the confirmation is up, the ring behind it is not live: Enter and Escape belong to the
  // modal (and `was_open` keeps the Enter that opened it from confirming it in the same frame).
  const bool modal_was_open = modal_open_;
  const NavAction live = modal_was_open ? NavAction::kNone : action;
  ApplyPendingDialogs(session);

  ImGui::SeparatorText("Settings file");
  DrawStatus(session);

  // One row of buttons with air between them, in the order they are meant to be read: write what
  // is on screen, then the two ways a profile moves between machines, then where it lives.
  const bool usable = session.CanSave();
  if (!usable) {
    ImGui::BeginDisabled();
  }
  if (Item(first_row, ring, live, "Save")) {
    const SaveOutcome outcome = session.Save();
    if (!outcome.ok) {
      status_.clear();
      status_error_ = "Cannot save: " + outcome.error;
    } else {
      status_error_.clear();
      status_ = outcome.wrote ? "Saved" : "Nothing to save: the file already matches";
    }
  }
  ImGui::SameLine();
  if (Item(first_row + 1, ring, live, "Reset to defaults")) {
    ImGui::OpenPopup(kResetPopup);
  }
  if (!usable) {
    ImGui::EndDisabled();
  }
  // Import stays live even when the file on disk did not parse: reading a profile the user
  // trusts is the way out of a damaged one.
  ImGui::SameLine();
  if (Item(first_row + 2, ring, live, "Import")) {
    const SDL_DialogFileFilter filters[] = {{kFileFilterName, "toml"}, {"All files", "*"}};
    const std::string start = session.path().parent_path().string();
    SDL_ShowOpenFileDialog(&ImportChosen, this, nullptr, filters, SDL_arraysize(filters),
                           start.empty() ? nullptr : start.c_str(), false);
  }
  ImGui::SameLine();
  if (!usable) {
    ImGui::BeginDisabled();
  }
  if (Item(first_row + 3, ring, live, "Export")) {
    const SDL_DialogFileFilter filters[] = {{kFileFilterName, "toml"}, {"All files", "*"}};
    const std::string start = session.path().parent_path().string();
    SDL_ShowSaveFileDialog(&ExportChosen, this, nullptr, filters, SDL_arraysize(filters),
                           start.empty() ? nullptr : start.c_str());
  }
  ImGui::SameLine();
  // The folder picker opens at the folder the settings are in, so choosing a location starts
  // from the answer to "where are they now?" rather than from nowhere.
  if (Item(first_row + 4, ring, live, "Change settings location")) {
    const std::string start = session.settings_dir().string();
    SDL_ShowOpenFolderDialog(&SettingsDirChosen, this, nullptr,
                             start.empty() ? nullptr : start.c_str(), false);
  }
  if (!usable) {
    ImGui::EndDisabled();
  }

  DrawResetModal(session, modal_was_open, action);
}

}  // namespace rb_blitz::launcher
