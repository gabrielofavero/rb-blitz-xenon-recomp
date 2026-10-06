// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's tab shell (docs/plans/launcher-plan.md A1, B1, B4, A2, A3).
//
// The shell owns the layout (built from the schema table), one focus ring per tab, the two
// NavSources and the session - the profile plus its write path (B4). It draws one frame at a time
// and answers whether the user asked to leave. It is deliberately thin: the rows come from
// schema_view, the ring from nav, the General tab from general_tab, the pads from pad_source
// (A3), and everything else is A1's read-only rendering. The bottom bar (A2) is here, because it
// belongs to the window rather than to any tab.
//
// SDL reaches this header through pad_source.h, which is the one place the launcher's input
// touches it: the pad is a device, and no widget is told that one exists.

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "focus_log.h"
#include "game_launch.h"
#include "general_tab.h"
#include "controller_tab.h"
#include "launcher/profile.h"
#include "nav.h"
#include "nav_bindings.h"
#include "nav_ui.h"
#include "pad_source.h"
#include "profile_session.h"
#include "schema_view.h"
#include "settings_table.h"
#include "ultimate_state.h"

namespace rb_blitz::launcher {

// What the shell needs beyond the profile. Both of these are the caller's rather than the user's,
// which is why neither is a setting in the file the launcher writes: one is a recovery switch and
// the other is the evidence a check reads.
struct ShellEnvironment {
  // D6's --no-gamepad: no pad is read, so a pad holding a direction down - a snapped stick, a
  // stuck trigger - cannot move the ring. The pads are still opened, because the remap block's
  // listen is asked for one press at a time and a launcher whose pads cannot be remapped is a
  // worse trap than one whose ring cannot be moved by a pad.
  bool gamepads = true;
  // --focus-log: where the trace of the ring and the input devices goes, empty for nowhere.
  std::string focus_log_path;
  // A5's --safe-mode: the keys the ring reads are the compiled defaults whatever the profile says.
  // The block that edits them still writes the file - which is how a set of keys that locked a user
  // out is repaired - and it says out loud that the change arrives on the next start rather than
  // this one.
  bool safe_mode = false;
};

class Shell {
 public:
  // `session` is the profile and its file: B1 and B4 edit it, and B4's block is what writes it.
  // `roots` is where the game's data was found. `environment` is what a row's `visible` rule is
  // decided from (a single-display machine has no Monitor row).
  Shell(ProfileSession session, GameRoots roots, RowEnvironment environment,
        ShellEnvironment shell_environment = {});

  // Draws one frame. Returns false when the user asked to leave (Esc, B, or Close).
  bool Frame();

  // What the model is showing: the tab and the focused row inside it. Exposed so the
  // window title and the tests can see the state rather than guess it.
  settings::Tab CurrentTab() const;
  std::size_t FocusedRow() const;

  // The session, so the caller can keep the window geometry (A1) in the same file and see where
  // the settings folder moved it.
  ProfileSession& session() { return session_; }

 private:
  void ApplyAction(NavAction action);
  void RequestTab(int delta);
  void DrawTab(const TabLayout& tab, FocusModel& ring);
  // The bottom bar (A2): what the settings file is doing on the left, the actions that end
  // a session on the right, and under them the focused row's own help and the hints for it.
  // Returns false when Close was chosen.
  bool DrawBottomBar();
  // B7: save what is unsaved, build Contract 3's command line and start the game.
  void LaunchGame();
  // The bar's own two writes, split out of DrawBottomBar so a bound key (A5's Ctrl+S and Ctrl+C)
  // does exactly what the button does rather than a second copy of it: the save, and the command
  // line on the clipboard.
  void SaveProfile();
  void CopyCommandLine();
  // A5's fourth bound action: B4's badge copies the value the game's own file decides, which was
  // mouse-only until now. It acts on the row the ring is on, and says so on the bar when that row
  // is not one the game overrides.
  void CopyEffectiveValue();
  // The setting the ring is on, or nullptr when this tab's ring is past its schema rows.
  const settings::Setting* FocusedSetting() const;
  // B7's "Copy command line" affordance and the failed-start detail, which is a modal because a
  // command line and a log path do not fit on one bar line. Both read the same builder a launch
  // does, so what is copied is exactly what was run.
  LaunchCommand CurrentLaunchCommand() const;
  void DrawLaunchModal(NavAction action);

  // The one line the bar shows, and whether it is news (red), a warning (yellow) or just the
  // last thing that happened (dim).
  struct StatusLine {
    std::string text;
    bool warning = false;
    bool error = false;
  };
  StatusLine CurrentStatus() const;

  // What the bar says about one entry of the current tab's ring (A2, D7): the row's own help -
  // the schema's tooltip, or the sentence the block that owns the row is the only thing that
  // knows - and the verb Enter performs on it. An entry with no verb is one there is nothing to
  // do with, which is a tab with no rows in it at all.
  struct HelpEntry {
    std::string text;
    std::string_view verb;
  };
  HelpEntry HelpForEntry(std::size_t entry) const;

  // Which device put the ring where it is (D6). It is the bar's business and nothing else's: the
  // hints under the tooltip are key names on a keyboard and the pad's own button names on a pad,
  // and someone who has just picked up a controller should not be told to press Enter.
  enum class InputDevice { kKeyboard, kGamepad };
  // The hint line for a row: `<input> <verb>` for the row itself, then the two things that are
  // always true. Composed rather than drawn so the bar can measure it before it places the
  // tooltip beside it, and so a line too wide for the window can lose its last hint rather than
  // run under the tooltip text.
  std::string HintLine(const HelpEntry& help) const;

  // The focus trace (--focus-log). Each one writes only when what it reports has changed.
  void LogFocus();
  void LogDevice();
  void LogPads();

  // A5: the keys the ring reads, rebuilt from the profile before the frame's keys are read, so a
  // binding assigned in the panel is the one the next press uses. Under --safe-mode it is never
  // rebuilt: the defaults are what the switch means, and the panel says so.
  void RefreshKeys();

  // A5's own table member, and why it is a member at all: a binding the panel assigns this frame
  // is what the next frame's press has to be read with. `safe_mode_` is the switch that stops it
  // being taken from the file.
  bool safe_mode_ = false;

  PadRegistry pads_;
  FocusLog log_;
  GameRoots roots_;
  ProfileSession session_;
  // Declared before the keyboard source below, which holds a reference to it.
  nav_bindings::Table keys_;
  // A5's block: the shell's rather than the General tab's, because the shell has to ask whether a
  // capture owns the keyboard before it reads one.
  NavKeysPanel nav_keys_;
  GeneralTab general_;
  ControllerTab controller_;
  std::vector<TabLayout> layout_;
  std::vector<FocusModel> rings_;
  std::size_t tab_ = 0;
  NavAction action_ = NavAction::kNone;
  std::unique_ptr<NavSource> keyboard_;
  // Null under --no-gamepad. Held as its concrete type because the bar's hints need the pad's own
  // button names (A2); the seam the ring sees is still NavSource.
  std::unique_ptr<GamepadNavSource> gamepad_;
  InputDevice device_ = InputDevice::kKeyboard;
  // B7: the game the bottom bar started, if it is still running.
  GameProcess game_;
  // B7: the bottom bar's own messages. The profile panel keeps the ones its actions produce;
  // these are the ones the bar is responsible for. `launch_error_` is the whole failed-start
  // sentence (command line and log path included) and is what the modal shows; `launch_command_`
  // is the exact command it names, kept so the modal's own Copy button has the same bytes the
  // bar's Copy button produces. `launch_modal_open_` is one frame behind the popup on purpose:
  // the shell reads Escape before it draws, so it must know the modal was up last frame.
  std::string save_error_;
  std::string save_note_;
  std::string launch_error_;
  LaunchCommand launch_command_;
  bool launch_popup_requested_ = false;
  bool launch_modal_open_ = false;
  // What the trace last reported, so it writes a line when something changes and not sixty times
  // a second.
  bool logged_focus_ = false;
  settings::Tab logged_tab_ = settings::Tab::kGeneral;
  std::size_t logged_entry_ = 0;
  std::optional<InputDevice> logged_device_;
  bool logged_pads_ = false;
  std::size_t logged_pad_count_ = 0;
  std::string logged_pad_name_;
};

}  // namespace rb_blitz::launcher
