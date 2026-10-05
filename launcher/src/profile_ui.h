// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The profile block on the General tab (docs/plans/launcher-plan.md D2, prompt B4).
//
// It is the visible half of the write path: a *Reset to defaults* that names exactly what it will
// remove before it removes it, *Import*/*Export*, and *Change settings location*, which moves the
// folder the profile lives in. *Save* is not here: it belongs to the bottom bar, next to Close
// and Launch Game, because that is where a user looks for "did that stick?".
//
// It owns the modal and the three folder/file dialogs, so the tab above it only has to give it a
// ring index and a `NavAction`. Everything it *decides* lives in profile_session.h; this file is
// buttons and wording.
//
// The wording is deliberately quiet: a block that is fine says nothing at all. Every line it
// does print is either an action's outcome or something the user has to act on, because a
// status area that always says "Saved" is a line the eye learns to skip. What it prints goes
// through the caller - the bottom bar owns the line - so the two accessors below are how the
// shell reads it.

#pragma once

#include <cstddef>
#include <string>

#include "nav.h"
#include "profile_session.h"

namespace rb_blitz::launcher {

class ProfilePanel {
 public:
  // Focusable items, so the caller can size the tab's ring: Reset to defaults, Import, Export
  // and Change settings location. A5's "complete every tab with the keyboard only" is why they
  // are in the ring at all rather than being mouse-only buttons.
  static constexpr std::size_t kRowCount = 4;

  // Draws the block. `first_row` is the ring index of the first item. `action` is this frame's
  // NavAction, so Enter/Space on a focused item does what clicking it does.
  void Draw(std::size_t first_row, ProfileSession& session, FocusModel& ring, NavAction action);

  // True while the reset confirmation is up. The caller uses it to leave Escape to the modal
  // instead of treating it as "quit" (A1 reads Escape as cancel).
  bool ModalOpen() const { return modal_open_; }

  // The last action's outcome, and the last failure. The failure wins if both are set. Empty
  // when there is nothing worth saying, which is the ordinary case.
  const std::string& status_message() const { return status_; }
  const std::string& status_error() const { return status_error_; }

  // SDL's dialog callbacks, which fire during the event pump. Public only because the C
  // callbacks need them; nothing else calls them.
  void OnImportChosen(const char* const* filelist);
  void OnExportChosen(const char* const* filelist);
  void OnSettingsDirChosen(const char* const* filelist);

 private:
  // `was_open` is the previous frame's ModalOpen(): true when the confirmation was already up,
  // which is what decides whether this frame's Enter or Escape belongs to it.
  void DrawResetModal(ProfileSession& session, bool was_open, NavAction action);
  void ApplyPendingDialogs(ProfileSession& session);
  // One button, ring-reachable: true when it was clicked or activated this frame.
  bool Item(std::size_t index, const FocusModel& ring, NavAction action, const char* label);

  bool modal_open_ = false;
  // The dialogs answer asynchronously, so the choice is recorded and applied by the next Draw.
  bool import_chosen_ = false;
  std::string chosen_path_;
  // The folder the settings-location dialog was answered with, kept apart from the import and
  // export choice because it means something different: a directory, not a profile to copy.
  std::string chosen_settings_dir_;
  // The last action's outcome, and the last failure. The failure wins if both are set.
  std::string status_;
  std::string status_error_;
};

}  // namespace rb_blitz::launcher
