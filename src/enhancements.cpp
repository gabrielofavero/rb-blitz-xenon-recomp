// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The enhancement toggle contract: the cvars behind the `[enhancements]` table,
// and the one boot line that reports them. See src/enhancements.h for why the
// names carry the table prefix and docs/engine/toggles.md for the table itself.
//
// Every toggle here is off by default except R3 and R10. R10's subject is the
// Ultimate mod's own name: it only does anything when that payload is installed, and
// Controller Settings hides its row when it is not. R3's subject is a question with one
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

REXCVAR_DEFINE_BOOL(enhancements_hide_achievements, false, "Enhancements",
                    "Hide achievement unlock notifications. Achievements are still earned and saved.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

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
// the label, so the edit has nothing to do; Controller Settings hides the row in that
// case too (controller-settings/config/settings.toml, `visible = "ultimate_installed"`).
REXCVAR_DEFINE_BOOL(enhancements_rename_mod_settings, true, "Enhancements",
                    "R10 rename the mod's settings row: draw the row the Ultimate mod adds as "
                    "\"Ultimate Settings\", which is the name the mod itself uses. Faithful: the "
                    "mod's own label, \"Mod Settings\".")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// R6 uses the existing Controller Settings Input setting as its sole switch.
REXCVAR_DECLARE(bool, mouse_ui_nav);

REXCVAR_DEFINE_BOOL(enhancements_dlc_cache, true, "Enhancements",
                    "Persist the flat DLC package list and reuse it when the roots, content "
                    "type and file fingerprint match. A hit skips header scanning; guest "
                    "enumeration, mounting and song loading still run. On by default. "
                    "--refresh_dlc_cache forces a scan. Off disables the host cache and "
                    "its refresh row; loading progress and pacing are independent.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// The refresh entry's own row (owner request, 2026-10-15): while R7 owns the main menu's
// downloadable-content row, that row is also something a player may not want in the menu, and
// removing it used to mean digging into Interface -> Hidden Items. This cvar is the carrier of
// the in-game "Refresh Songs" row on the Downloadable Content page; with it off, the menu's row
// is hidden and the library is only re-scanned when its fingerprint changes at boot.
// On by default since the owner request of 2026-10-22 (it was off from 2026-10-20): the cache
// remains on and a fresh install carries the refresh entry in its main menu.
REXCVAR_DEFINE_BOOL(enhancements_refresh_music_library, true, "Enhancements",
                    "R7's refresh entry: show the main menu's Refresh Songs row, which re-scans "
                    "the DLC folders and re-runs the discovery pass. On by default. Off hides "
                    "the row; the boot's own scan is unaffected (a changed library is always "
                    "re-read). Needs enhancements_dlc_cache.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_dlc_uncap_enumeration, true, "Enhancements",
                    "Temporarily disable VSync while content loads, restoring the player's "
                    "value when the usable main menu is current. On by default and independent "
                    "of the host cache setting. Off keeps the player's pacing throughout.")
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

// R9, implemented. The title loops ONE of three of its own streamed tracks per menu visit
// (the `shellmusic` block of config/synth.dtb, chosen at random and looped forever); with
// this on, every track the menu plays is one of the loaded DLC songs instead, streamed
// read-only from the package it came from (src/hooks/shell_music.cpp). The pool is drawn
// at random from every song the boot's DLC packages carry - every `.mogg` in a package,
// not just the first, so a merged pack contributes all of its songs - and every boot draws
// a new one, so without a library there is nothing to draw from and the menu is unchanged.
//
// REVIVED (owner request, 2026-10-14): the feature has a visible row again - "DLC Menu Songs" on
// the merged Help & Options surface's Downloadable Content page (`general.dlc_menu_songs`), which
// this cvar is the carrier for (`CvarForSetting`), applied by the boot at the config rank. Its
// compiled default here must equal the row's (`true`): a row nobody has chosen is *not* written, so
// on a fresh install what a boot reads is this cvar's own default.
REXCVAR_DEFINE_BOOL(enhancements_menu_dlc_songs, true, "Enhancements",
                    "R9 main-menu DLC songs: play the loaded DLC as the main menu's background "
                    "music. The in-game \"DLC Menu Songs\" row is the control; this is the cvar it "
                    "is written to, so an explicit flag or environment value still outranks it.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// Input navigation and the visible player are independent preferences sharing one transport.
REXCVAR_DEFINE_BOOL(enhancements_background_music_nav, true, "Enhancements",
                    "Enable pad/key menu-music navigation. Music-player visibility is independent. "
                    "Requires DLC background music.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(enhancements_music_player, true, "Enhancements",
                    "Show the clickable music player at the bottom right of the main menu. "
                    "Independent of pad/key navigation. Requires DLC background music.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(enhancements_menu_dlc_song_count, 3, "Enhancements",
                     "R9's retired pool-size row: music uses a rolling session and a bounded "
                     "mount cache, so this legacy value is ignored (a boot says so).")
    .range(1, 64)
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

// One row per feature in docs/plans/finished/2026-10-03-customization-plan.md R1-R10. This table is
// what the log walks, so a toggle that is not here is a toggle nobody sees. The size is
// explicit, and it has to match the rows below: a short initializer list is not an error,
// it leaves the last row - a null name and a null `enabled` - in the table.
constexpr std::array<ToggleRow, 13> kToggles{{
    {"enhancements_hide_achievements", "hide achievement unlock notifications",
     [] { return REXCVAR_GET(enhancements_hide_achievements); }},
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
    {"mouse_ui_nav", "R6 native mouse",
     [] { return REXCVAR_GET(mouse_ui_nav); }},
    {"enhancements_dlc_cache", "R7 DLC cache",
     [] { return REXCVAR_GET(enhancements_dlc_cache); }},
    {"enhancements_refresh_music_library", "R7 refresh songs row (the in-game row)",
     [] { return REXCVAR_GET(enhancements_refresh_music_library); }},
    {"enhancements_force_controller_scheme", "R8 force controller scheme",
     [] { return REXCVAR_GET(enhancements_force_controller_scheme); }},
    {"enhancements_menu_dlc_songs", "R9 main-menu DLC songs (the in-game DLC Menu Songs row)",
     [] { return REXCVAR_GET(enhancements_menu_dlc_songs); }},
    {"enhancements_background_music_nav", "menu-music navigator (P2; native pause/resume)",
     [] { return REXCVAR_GET(enhancements_background_music_nav); }},
    {"enhancements_rename_mod_settings", "R10 rename the mod's settings row",
     [] { return REXCVAR_GET(enhancements_rename_mod_settings); }},
}};

}  // namespace

void LogToggles() {
  REXLOG_INFO("enhancements: {} toggle(s), off by default except R3, R6, R7's cache, R9, R10 "
              "and the menu-music navigator's own cvar "
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
  REXLOG_INFO("enhancements: {} = {} (retired size; rolling music session ignores this value)",
              "enhancements_menu_dlc_song_count",
              REXCVAR_GET(enhancements_menu_dlc_song_count));
}

}  // namespace rb_blitz::enhancements
