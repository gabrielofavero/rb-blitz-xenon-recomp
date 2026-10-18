// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The Controller tab's block (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md §4, P4).
//
// Three binding tables, one capture and two `?` pages:
//
//   * *General Mapping* lists every control a 360 pad has whether or not it is bound, because the
//     list is the answer to "what can I rebind?" - a list that only showed the differences would
//     leave the user guessing. A row is either *stock* (no `[remap]` entry, so the pad reports it
//     exactly as the SDK mapped it) or bound to a list of sources.
//   * *Gameplay Mapping* is two independent tables, each owning one input type
//     (`[gameplay_remap_controller]` takes `pad:`, `[gameplay_remap_keyboard_mouse]` takes
//     `key:`/`mouse:`). Each table hides while its switch is off and keeps its bindings, and a
//     key or mouse binding whose device support is off stays listed but is crossed out.
//
// *Assign* listens for three seconds and adds the first input it sees. For General Mapping any
// device is accepted; a gameplay table refuses the other input type with a visible hint and stays
// armed (plan R11). Adding rather than replacing is what makes "several inputs, one button" work,
// and *Reset* is how a row goes back to its default.
//
// There is no input-source setting to pair with it: the listen watches every connected pad, the
// keyboard and the mouse in the same frame, which is the flexible half of the choice the plan left
// open. The reading is polled rather than event-driven - Controller Settings' event pump already
// belongs to ImGui - so SDL is asked what is down each frame and a capture is a *rising edge* over
// the previous frame. That is also what keeps the click that started the listen from assigning
// itself: the mouse button is already down when the listen begins, so it has no edge left.
//
// The pads it reads are Controller Settings' open ones (A3's PadRegistry): SDL only reports a
// pad's state through a handle somebody opened, so a capture that opened its own would be reading
// a different pad from the one the focus ring is following.
//
// The block is dynamic: each gameplay switch hides its own table, so the shell cannot size the
// tab's ring once. `controller_block.h` is the one visible-layout arithmetic and this tab reports
// a layout change so the shell rebuilds the ring (plan §4's "hidden rows are never focusable").

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include "binding_table.h"
#include "controller_block.h"
#include "profile/gameplay_remap.h"
#include "profile/profile.h"
#include "profile/remap.h"
#include "nav.h"
#include "pad_source.h"
#include "profile_session.h"

namespace rb_blitz::profile {

// The controls the General Mapping table lists: every target the grammar knows (`remap::Targets()`),
// which is every control a 360 pad has except Guide - that one is a platform button, and the
// project never delivers it to the guest. Tied to the vocabulary by the static_assert in
// controller_tab.cpp so the two cannot drift.
inline constexpr std::size_t kControlRows = remap::kTargetCount;

class ControllerTab {
 public:
  // How long a listen lasts. Long enough for a deliberate press and short enough that a
  // mis-click does not lock the panel, and the countdown is on screen the whole time.
  static constexpr double kListenSeconds = 3.0;

  // `pads` is Controller Settings' open pads (A3). A capture reads them through the registry rather
  // than opening anything itself: SDL only reads a pad somebody has opened, so the registry is
  // what makes a listen able to see a pad press at all.
  explicit ControllerTab(const PadRegistry& pads) : pads_(pads) {}

  // Draws the block. `first_row` is the ring index of the block's first entry.
  void Draw(std::size_t first_row, FocusModel& ring, ProfileSession& session, NavAction action);

  // True while a listen is running. The shell uses it to keep Escape from quitting the
  // Controller Settings while the user is waiting for the countdown.
  bool Listening() const { return listening_.has_value(); }

  // True while one of the two `?` pages is up. The shell treats it like a modal: Escape closes
  // the page before it closes the window, and no ring movement reaches the tab behind it.
  bool HelpOpen() const { return help_page_ != HelpPage::kNone; }
  // Draws the open `?` page. Called by the shell inside the window's id stack, after the body.
  void DrawHelpModal(NavAction action);

  // True once after either gameplay switch changed the visible rows. The shell rebuilds the
  // tab's ring and moves the ring to `PendingFocusRow()` (the switch that was just flipped, so a
  // table that vanished does not leave the focus on a row nobody draws).
  bool TakeLayoutChange();
  std::size_t PendingFocusRow() const { return pending_focus_; }

  // The block this tab would draw for a profile, and the row naming the bottom bar and the focus
  // trace use. Both take a *flat entry* of the block - what the ring hands them - and resolve it
  // to the row it belongs to. Static so the shell can ask without drawing.
  static ControllerBlock BlockFor(const Profile& profile);
  static std::string HelpText(const Profile& profile, std::size_t entry);
  static std::string TraceName(const Profile& profile, std::size_t entry);

 private:
  // Which table a listen belongs to. One capture session across all three, owned by the table
  // that started it (plan §4).
  enum class TableId : uint8_t { kGeneral, kController, kKeyboardMouse };
  enum class HelpPage : uint8_t { kNone, kGeneral, kGameplay };

  struct ListeningRow {
    TableId table = TableId::kGeneral;
    remap::Target target = remap::Target::kA;
    gameplay_remap::Action action = gameplay_remap::Action::kFirstNote;
  };

  void DrawGeneralMapping(const ControllerBlock& block, std::size_t first_row, FocusModel& ring,
                          ProfileSession& session, NavAction action, double now);
  void DrawGameplayMapping(const ControllerBlock& block, std::size_t first_row, FocusModel& ring,
                           ProfileSession& session, NavAction action, double now);
  // One gameplay table, from `first_row` (its first action's Assign) to `reset_all_row`.
  void DrawGameplayTable(TableId table, std::size_t first_row, std::size_t reset_all_row,
                         FocusModel& ring, ProfileSession& session, NavAction action,
                         bool keyboard_on, bool mouse_on);
  void DrawGameplaySwitch(const ControllerBlock& block, std::size_t row, FocusModel& ring,
                          NavAction action, ProfileSession& session, bool controller, bool on);
  // The `?` beside a section's description: a ring entry that opens a page on click or Activate,
  // and previews one sentence on hover.
  void DrawHelpRow(std::size_t row, FocusModel& ring, NavAction action, HelpPage page,
                   std::string_view preview);
  void OpenHelp(HelpPage page);

  // What a listening row is called in the status line ("A was not changed: nothing was pressed").
  std::string ListeningLabel(const ListeningRow& row) const;
  void BeginListening(ListeningRow row);
  // Adds the captured input to the row's binding, which is how "several inputs, one button" is
  // expressed: Assign never replaces. Returns false when the table refuses the source's kind, in
  // which case the listen stays armed and `status_` says why.
  bool CommitCapture(ProfileSession& session, const remap::Source& source);
  // The first input that went down since the previous frame, if any. Reads every connected pad,
  // then the keyboard, then the mouse: a pad press is the one a listen is usually about, so it
  // wins a tie.
  std::optional<remap::Source> CapturedInput() const;
  // Records what is down now, which is what the next frame's rising edges are measured against.
  void SnapshotInput();

  struct PadSnapshot {
    SDL_JoystickID id = 0;
    std::vector<bool> buttons;  // indexed by SDL_GamepadButton
    bool left_trigger = false;
    bool right_trigger = false;
  };

  // Controller Settings' open pads (A3). Not owned: the shell holds the registry, because the focus
  // ring needs the same pads the capture does.
  const PadRegistry& pads_;

  // The control or action being rebound, and when the listen runs out.
  std::optional<ListeningRow> listening_;
  double listen_until_ = 0.0;

  // The open `?` page, and the one-frame request that opens it: ImGui's OpenPopup has to be
  // called from the id stack BeginPopupModal is drawn in, which is the shell's, not a row's.
  HelpPage help_page_ = HelpPage::kNone;
  bool help_request_ = false;

  // A switch changed the visible rows; the shell has not rebuilt the ring yet.
  bool layout_changed_ = false;
  std::size_t pending_focus_ = kNoBlockRow;

  // The previous frame's input, which is the half of "rising edge" that is not SDL's.
  std::vector<bool> key_down_;
  std::vector<PadSnapshot> pads_snapshot_;
  uint32_t mouse_down_ = 0;

  // The last thing the block did, so a capture, a reset and a listen that found nothing all
  // say so rather than leaving the user to guess.
  std::string status_;
  // The last source a gameplay table refused, drawn right under that table: the status line is at
  // the top of the block, and the user listening at the bottom of a long tab would not see it.
  std::string listen_hint_;
};

}  // namespace rb_blitz::profile
