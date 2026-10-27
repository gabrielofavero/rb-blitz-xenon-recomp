// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The resolution rules behind src/ui/help_topics.h, with the retail text table. SDK-free; every
// string in here is grounded in the shipped records or the English locale, which is what makes
// the texts reviewable without a boot:
//
//   av_overscan / av_bass_boost / av_active_track_volume / av_sound_volume
//     - the four `set_data` symbols of av_options.dtb's `checkbox.lst` (decode with
//     out/_swap/dtb_tree.py); the locale spells them "Overscan", "Bass Boost", "Active Track
//     Boost" and "Sound Effects Volume" (locale_keep.dtb).
//   tutorialsong / tutorialsong3
//     - `tutorials_panel`'s `tutorial_list` property, whose two rows the locale spells
//     "Basic Tutorial" and "Advanced Tutorial".
//
// The in-song pause list reuses the merged root's own symbols, so its text comes from the same
// generated table (`screen::PauseRootRows` + `screen::HelpTextFor`) and is not duplicated here.

#include "ui/help_topics.h"

#include <string>
#include <vector>

#include "ui/settings_screen.h"

namespace rb_blitz::ui::help {
namespace {

// The retail rows this table names, in the order the page draws them. Index -> symbol, exactly
// the arrays the retail records fill their lists with.
std::vector<std::string_view> RetailRows(const std::string_view screen) {
  if (screen == "av_options_screen" || screen == "av_options_pause_screen") {
    return {"av_active_track_volume", "av_sound_volume", "av_bass_boost",
            "settings_dlc_menu_songs", "settings_music_navigation"};
  }
  if (screen == "tutorials_screen" || screen == "tutorials_pause_screen") {
    return {"tutorialsong", "tutorialsong3"};
  }
  return {};
}

// The list name each of those pages focuses, or an empty view when the page is a picker rather
// than a row list. The name is matched against the focused component's own object name, so a
// slider the player focused inside the page (av_options' volume rows open two) resolves through
// the switch below instead of being mistaken for the list.
std::string_view RetailListName(const std::string_view screen) {
  if (screen == "av_options_screen" || screen == "av_options_pause_screen") {
    return "checkbox.lst";
  }
  if (screen == "tutorials_screen" || screen == "tutorials_pause_screen") {
    return "options.lst";
  }
  return {};
}

}  // namespace

std::string TextForSymbol(const std::string_view symbol) {
  if (symbol == "settings_dlc_menu_songs" || symbol == "settings_music_navigation")
    return screen::HelpTextFor(symbol);
  if (symbol.empty()) {
    return {};
  }
  // The retail rows this table owns. Checked first because none of these names can be a generated
  // row's (the generated ones are `settings_*` or the five adopted retail labels).
  if (symbol == "av_overscan") {
    return "Expand the picture so it fills the screen edge to edge.";
  }
  if (symbol == "av_active_track_volume") {
    return "Boost the volume of the track you are playing.";
  }
  if (symbol == "av_sound_volume") {
    return "How loud the game's sound effects play.";
  }
  if (symbol == "av_bass_boost") {
    return "Add extra bass to the mix.";
  }
  if (symbol == "tutorialsong") {
    return "Learn the basics of Rock Band Blitz.";
  }
  if (symbol == "tutorialsong3") {
    return "Advanced techniques for higher scores.";
  }
  return screen::HelpTextFor(symbol);
}

std::string CaptionFor(const Observation& observation) {
  // The in-song pause panel's inline `options.lst`: the merged root's own rows, filled by the
  // payload's `MergePauseListRows`, so the text is the root's own (five or six rows by the gate).
  if (observation.screen == "game_screen") {
    if (observation.focus == "options.lst" && observation.index >= 0) {
      const std::vector<std::string> rows = screen::PauseRootRows(observation.provider_active);
      const size_t index = static_cast<size_t>(observation.index);
      if (index < rows.size()) {
        return screen::HelpTextFor(rows[index]);
      }
    }
    // The ordinary pause list (`pause.lst`) is not a settings surface; nothing to say about it.
    return {};
  }

  // The retail audio/video page: its list of four rows, plus the two sliders the volume rows open.
  if (observation.screen == "av_options_screen" ||
      observation.screen == "av_options_pause_screen") {
    if (observation.focus == "track_sound_slider.sld") {
      return TextForSymbol("av_active_track_volume");
    }
    if (observation.focus == "sound_effects_slider.sld") {
      return TextForSymbol("av_sound_volume");
    }
    if (observation.focus == RetailListName(observation.screen) && observation.index >= 0) {
      const std::vector<std::string_view> rows = RetailRows(observation.screen);
      const size_t index = static_cast<size_t>(observation.index);
      if (index < rows.size()) {
        return TextForSymbol(rows[index]);
      }
    }
    return {};
  }

  // How to Play: the two tutorial rows, in the order the panel's property lists them.
  if (observation.screen == "tutorials_screen" || observation.screen == "tutorials_pause_screen") {
    if (observation.focus == RetailListName(observation.screen) && observation.index >= 0) {
      const std::vector<std::string_view> rows = RetailRows(observation.screen);
      const size_t index = static_cast<size_t>(observation.index);
      if (index < rows.size()) {
        return TextForSymbol(rows[index]);
      }
    }
    return {};
  }

  // Calibration gets **no** caption at all (owner request, 2026-10-14). Its own screens draw the
  // wizard's instructions and its own A/B prompts, and an extra overlay competes with them; the
  // caption the player needs - the root row that opens the wizard - is published by the generated
  // table before entering, and `EnterRetail` hides it on the way in. So `cal_welcome_screen` is
  // deliberately absent from this function *and* from `KnowsScreen`, which is what makes the
  // observer's transition rule clear the caption rather than describe the screen.

  // The controller mapping screen is a horizontal picker, not a list: one caption for the screen.
  if (observation.screen == "controller_config_screen" ||
      observation.screen == "controller_config_pause_screen") {
    return "Choose the controller preset used during play. Left/Right cycles presets.";
  }

  return {};
}

bool KnowsScreen(const std::string_view screen) {
  return screen == "game_screen" || screen == "av_options_screen" ||
         screen == "av_options_pause_screen" || screen == "tutorials_screen" ||
         screen == "tutorials_pause_screen" || screen == "controller_config_screen" ||
         screen == "controller_config_pause_screen";
}

bool IsGuestScreen(const std::string_view screen) {
  if (screen.empty()) {
    return false;
  }
  for (const std::string& name : screen::ScreenNames()) {
    if (name == screen) {
      return true;
    }
  }
  return false;
}

}  // namespace rb_blitz::ui::help
