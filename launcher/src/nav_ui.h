// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher-keys block on the General tab (docs/plans/launcher-plan.md A5).
//
// It is the visible half of `[nav]`, and it is on the tab that owns the launcher's own file
// because that is where a user whose keys have stopped working will look: the block that resets
// the keys is the block that fixes the file that broke them.
//
// One row per action: what the action is, the keys it answers to, and two buttons. *Assign* takes
// the next key pressed - three seconds of listening, like C5's button capture, so a capture that
// is never answered ends by itself rather than holding the launcher - and *Reset* puts that one
// action back to the keys it ships with. The last row does the same for all of them at once, which
// is the one-button answer to "my ring is on a key I do not have".
//
// What each action *means* and what it is called are nav_bindings'; this file is buttons, wording
// and the capture, exactly as profile_ui.cpp is for the write path.

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "nav.h"
#include "nav_bindings.h"
#include "profile_session.h"

namespace rb_blitz::launcher {

class NavKeysPanel {
 public:
  // Focusable items: Assign and Reset per action, then Reset every key. Sizing the ring from this
  // is why nav_bindings::kActionCount exists at all.
  static constexpr std::size_t kRowCount = nav_bindings::kActionCount * 2 + 1;

  // Draws the block. `first_row` is the ring index of the first item. `action` is this frame's
  // NavAction - Escape cancels a capture and nothing else in here reads a key, because the key a
  // capture is waiting for is read straight from ImGui (nav_keys::CapturedTrigger).
  //
  // `safe_mode` is A5's switch, and the block has to say out loud what it means here: while safe
  // mode is on the keys in force are the defaults whatever the file says, so an assignment written
  // here takes effect the next time the launcher starts rather than now.
  void Draw(std::size_t first_row, ProfileSession& session, FocusModel& ring, NavAction action,
            bool safe_mode);

  // True while a capture is running. The shell leaves the keyboard to it - Escape included - the
  // same way it does for C5's listen: the key being pressed is the input being captured, not a
  // command.
  bool Capturing() const { return capturing_.has_value(); }

  // What the bottom bar says about one of the block's items, in the ring's own order (A2). The
  // schema's rows carry their own tooltip; these are buttons, so their sentence lives here, with
  // the buttons it describes.
  static std::string HelpText(std::size_t row);

  // What --focus-log calls one of those items: "next:assign", "cancel:reset", "reset-all". The
  // trace names rows rather than numbering them for the reason LogFocus does, and for this block
  // the *job* is what a reader (and an assertion) needs: "which of the twenty-five buttons is
  // entry 17?" is not a question the entry index answers. Empty for a row that is not one of ours.
  static std::string RowName(std::size_t row);

 private:
  void BeginCapture(NavAction action);
  // One button, ring-reachable: true when it was clicked or activated this frame. A disabled one is
  // still in the ring (the ring must be able to walk past it) and does nothing.
  bool Item(std::size_t index, FocusModel& ring, NavAction action, const char* label,
            bool enabled);

  // The action being captured for, and when the capture gives up. A capture with no timeout would
  // be a launcher that stops answering keys because it is waiting for one.
  std::optional<NavAction> capturing_;
  double capture_until_ = 0.0;
  // The frame the capture began on, so the key that began it cannot be the key it captures.
  int capture_frame_ = -1;
  // The last thing the block did, which is the bar's line when the profile has nothing to say.
  std::string status_;
};

}  // namespace rb_blitz::launcher
