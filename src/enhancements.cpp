// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The enhancement toggle contract: the cvars behind the `[enhancements]` table,
// and the one boot line that reports them. See src/enhancements.h for why the
// names carry the table prefix and docs/engine/toggles.md for the table itself.
//
// Every toggle here is off by default except R3 and R10. R10's subject is the
// Ultimate mod's own name: it only does anything when that payload is installed, and
// the launcher hides its row when it is not. R3's subject is a question with one
// answer: with Rock Central gone, a start can only go offline, so the two questions
// it asks first are asked to confirm what the game has already worked out. Every toggle is honest about what it does
// today: a description that ends in "Not implemented" is a toggle that gates
// nothing yet, and R5 and R10 - the implemented pair - say what they edit and
// which keys name it. A user reading --help or a log must not be able to mistake
// a planned feature for a working one. The faithful (off) behaviour is named in
// each description; that is the same record the project requires of a hook,
// applied to a plan.
//
// Each cvar carries kRequiresRestart rather than the hot-reload default: what
// these gate is guest behaviour decided at load, so "changed live" would be a
// claim no implementing prompt has made yet. A prompt that can apply a toggle
// live relaxes its own lifecycle and says so in docs/engine/toggles.md.

#include "enhancements.h"

#include <array>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "ui/menu_options.h"

REXCVAR_DEFINE_BOOL(enhancements_expand_resolution, false, "Enhancements",
                    "R1 custom resolutions: expand the guest's own layout (menu background, HUD, "
                    "3D, song selection, power-up menu) for a non-16:9 video mode instead of "
                    "leaving it to the presenter's letterbox or safe-area path. Faithful: the "
                    "guest's 16:9 layout is drawn as authored. Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_ui_scale, false, "Enhancements",
                    "R2 UI accessibility: scale guest text (and the main menu logo) through the "
                    "game's own font/layout metrics. Faithful: the design's text and logo sizes. "
                    "Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// The second toggle whose default is on, because the question it removes has one
// answer: Rock Central is gone, so a start that asks "cannot connect" and then
// "proceed in offline mode?" is asking the player to confirm what the game has
// already worked out. The edit is the title's own two transitions made to happen on
// their own (src/ui/menu_options.cpp), so the offline mode the second one enters is
// the same mode the player's second press enters.
REXCVAR_DEFINE_BOOL(enhancements_skip_offline_dialog, true, "Enhancements",
                    "R3 auto offline mode: take the failed-login and \"Proceed in Offline "
                    "Mode?\" questions out of the panel a game start goes through, so the title "
                    "shows the main menu in offline mode as soon as the start is pressed. "
                    "Faithful: the title asks both questions and the player answers them, which "
                    "is the same pair of transitions.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_icon_schemes, false, "Enhancements",
                    "R4 different icons: let the button glyphs and controller-layout art be "
                    "replaced per scheme. Faithful: the art the title ships. Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_hide_menu_options, false, "Enhancements",
                    "R5 hiding categories: remove named entries from the main menu's option list, "
                    "in the list the guest navigates and not only in the drawing. Needs "
                    "enhancements_hidden_menu_options to name them. Faithful: every shipped entry "
                    "is present.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(enhancements_hidden_menu_options, rb_blitz::menu_options::kDefaultRows,
                      "Enhancements",
                      "R5 the main menu rows enhancements_hide_menu_options hides, comma "
                      "separated. The compiled default is the three that only ever worked online: "
                      "splash_leaderboard, splash_achievements, splash_dlc.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// The one toggle whose default is on, because its subject is the mod's own name:
// the row only exists when the Ultimate payload is installed, and it only ever
// reads "Mod Settings" there. A run without the payload has neither the file nor
// the label, so the edit has nothing to do; the launcher hides the row in that
// case too (launcher/config/settings.toml, `visible = "ultimate_installed"`).
REXCVAR_DEFINE_BOOL(enhancements_rename_mod_settings, true, "Enhancements",
                    "R10 rename the mod's settings row: draw the row the Ultimate mod adds as "
                    "\"Ultimate Settings\", which is the name the mod itself uses. Faithful: the "
                    "mod's own label, \"Mod Settings\".")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_native_mouse, false, "Enhancements",
                    "R6 different input waves: use the engine's own pointer path for the mouse "
                    "instead of the synthetic pad in src/input/mouse_ui.cpp. Faithful: the pad the "
                    "guest actually reads. Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_dlc_cache, false, "Enhancements",
                    "R7 DLC cache: persist the flat DLC library's enumeration in the save "
                    "folder and read it back when a fingerprint proves the tree unchanged, so "
                    "a boot does not open every package and read its header again; the "
                    "discovery screen's progress bar is given the library's package count, the "
                    "main menu row that refreshes the library is drawn as \"Refresh Song "
                    "Library\", and a boot answered out of the cache says \"Loading Song "
                    "Cache\". Off by default. The refresh is --refresh_dlc_cache. Faithful: "
                    "the library is scanned once per boot, the row keeps its shipped label, "
                    "the discovery screen says what it ships with, and the bar is the "
                    "title's own.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_force_controller_scheme, false, "Enhancements",
                    "R8 force a predefined controller scheme on load. Needs "
                    "enhancements_controller_scheme to name one. Faithful: the player's saved "
                    "layout is used untouched. Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_STRING(enhancements_controller_scheme, "", "Enhancements",
                      "R8 the scheme enhancements_force_controller_scheme applies; empty means "
                      "unnamed, and an unnamed scheme leaves the saved layout alone.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// R9, implemented. The title loops three of its own streamed tracks in the main menu
// (the `shellmusic` block of config/synth.dtb); with this on, every track the menu
// plays is one of the loaded DLC songs instead, streamed read-only from the package it
// came from (src/hooks/shell_music.cpp). The pool is the DLC this boot enumerated, so
// without a library the toggle has nothing to draw from and the menu is unchanged.
REXCVAR_DEFINE_BOOL(enhancements_menu_dlc_songs, false, "Enhancements",
                    "R9 main-menu DLC songs: play the loaded DLC as the main menu's background "
                    "music - one song at a time, picked at random from the library - instead of "
                    "the three tracks the title loops. Needs a DLC library; without one the menu "
                    "is unchanged. Faithful: the title's own three tracks.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(enhancements_menu_dlc_song_count, 3, "Enhancements",
                     "R9 how many DLC songs the main menu's background pool draws from, spread "
                     "over the loaded library (1-64). Read only while "
                     "enhancements_menu_dlc_songs is on.")
    .range(1, 64)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// R11, in progress. The state it is for is real and reproducible: a song whose package is
// missing files leaves the title on its loading screen with no prompt, and the title never
// finishes the load. Everything the feature needs to *say* is in place and verified - the
// timer, the title's own prompt label taking this build's text, and the title's own "PRESS A
// TO BEGIN" replacing it when a load does finish - but the label does not appear without the
// state move the title runs on completion, and calling that move on a load that failed faults
// (docs/engine/loading-cancel.md §3). Until that is answered, this toggle gates a prompt that
// is written and not seen, so it ships off and says so, the way the project's contract
// requires of a toggle whose feature is not there yet.
REXCVAR_DEFINE_BOOL(enhancements_loading_cancel, false, "Enhancements",
                    "R11 cancel a stuck song load: if a song has not reached the title's "
                    "\"PRESS A TO BEGIN\" prompt after enhancements_loading_cancel_seconds, "
                    "offer that prompt early asking for B, which leaves the load and returns to "
                    "the music library. Faithful: the title's own prompt, and no way out of a "
                    "load that never ends. Not implemented: the prompt's label is written and "
                    "not shown (docs/engine/loading-cancel.md).")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_DOUBLE(enhancements_loading_cancel_seconds, 60.0, "Enhancements",
                      "R11 how long a song load may run without reaching the title's own prompt "
                      "before it is shown early under the cancel prompt (1-600 seconds). Read "
                      "only while enhancements_loading_cancel is on.")
    .range(1.0, 600.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rb_blitz::enhancements {

namespace {

const char* SourceName(rex::cvar::Source source) {
  switch (source) {
    case rex::cvar::Source::kDefault:
      return "default";
    case rex::cvar::Source::kConfig:
      return "config";
    case rex::cvar::Source::kEnvironment:
      return "environment";
    case rex::cvar::Source::kCommandLine:
      return "command line";
    case rex::cvar::Source::kRuntime:
      return "runtime";
  }
  return "unknown";
}

struct ToggleRow {
  const char* name;
  const char* feature;
  bool (*enabled)();
};

// One row per feature in docs/plans/customization-plan.md R1-R9. This table is
// what the log walks, so a toggle that is not here is a toggle nobody sees.
constexpr std::array<ToggleRow, 11> kToggles{{
    {"enhancements_expand_resolution", "R1 custom resolutions",
     [] { return REXCVAR_GET(enhancements_expand_resolution); }},
    {"enhancements_ui_scale", "R2 UI accessibility",
     [] { return REXCVAR_GET(enhancements_ui_scale); }},
    {"enhancements_skip_offline_dialog", "R3 skip the offline prompts",
     [] { return REXCVAR_GET(enhancements_skip_offline_dialog); }},
    {"enhancements_icon_schemes", "R4 icon schemes",
     [] { return REXCVAR_GET(enhancements_icon_schemes); }},
    {"enhancements_hide_menu_options", "R5 hide menu options",
     [] { return REXCVAR_GET(enhancements_hide_menu_options); }},
    {"enhancements_native_mouse", "R6 native mouse",
     [] { return REXCVAR_GET(enhancements_native_mouse); }},
    {"enhancements_dlc_cache", "R7 DLC cache",
     [] { return REXCVAR_GET(enhancements_dlc_cache); }},
    {"enhancements_force_controller_scheme", "R8 force controller scheme",
     [] { return REXCVAR_GET(enhancements_force_controller_scheme); }},
    {"enhancements_menu_dlc_songs", "R9 main-menu DLC songs",
     [] { return REXCVAR_GET(enhancements_menu_dlc_songs); }},
    {"enhancements_rename_mod_settings", "R10 rename the mod's settings row",
     [] { return REXCVAR_GET(enhancements_rename_mod_settings); }},
    {"enhancements_loading_cancel", "R11 cancel a stuck song load",
     [] { return REXCVAR_GET(enhancements_loading_cancel); }},
}};

}  // namespace

void LogToggles() {
  REXLOG_INFO("enhancements: {} toggle(s), off by default except R3 and R10 "
              "(docs/engine/toggles.md)",
              kToggles.size());
  for (const ToggleRow& row : kToggles) {
    REXLOG_INFO("enhancements: {} = {} ({}) - {}", row.name, row.enabled() ? "on" : "off",
                SourceName(rex::cvar::GetFlagSource(row.name)), row.feature);
  }
  REXLOG_INFO("enhancements: {} = \"{}\" (R8 scheme; empty means unnamed)",
              "enhancements_controller_scheme", REXCVAR_GET(enhancements_controller_scheme));
  REXLOG_INFO("enhancements: {} = \"{}\" (R5 rows; the three offline-dead entries by default)",
              "enhancements_hidden_menu_options",
              REXCVAR_GET(enhancements_hidden_menu_options));
  REXLOG_INFO("enhancements: {} = {} (R9 pool size; spread over the loaded DLC library)",
              "enhancements_menu_dlc_song_count",
              REXCVAR_GET(enhancements_menu_dlc_song_count));
  REXLOG_INFO("enhancements: {} = {} (R11 timeout; a load that has not prompted by then)",
              "enhancements_loading_cancel_seconds",
              REXCVAR_GET(enhancements_loading_cancel_seconds));
}

}  // namespace rb_blitz::enhancements
