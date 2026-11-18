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

#include "settings/runtime_values.h"
#include "enhancements.h"

#include <array>

#include "settings/runtime_settings.h"
#include <rex/logging/log_category.h>

#include "ui/menu_options.h"







// The second toggle whose default is on, because the question it removes has one
// answer: Rock Central is gone, so a start that asks "cannot connect" and then
// "proceed in offline mode?" is asking the player to confirm what the game has
// already worked out. The edit is the title's own two transitions made to happen on
// their own (src/ui/menu_options.cpp), so the offline mode the second one enters is
// the same mode the player's second press enters.










// The one toggle whose default is on, because its subject is the mod's own name:
// the row only exists when the Ultimate payload is installed, and it only ever
// reads "Mod Settings" there. A run without the payload has neither the file nor
// the label, so the edit has nothing to do; Controller Settings hides the row in that
// case too (controller-settings/config/settings.toml, `visible = "ultimate_installed"`).


// R6 uses the existing Controller Settings Input setting as its sole switch.


// The refresh entry's own row (owner request, 2026-10-15): while R7 owns the main menu's
// downloadable-content row, that row is also something a player may not want in the menu, and
// removing it used to mean digging into Interface -> Hidden Items. This cvar is the carrier of
// the in-game "Refresh Songs" row under Main Menu / Extra Items; with it off, the menu's row
// is hidden while the boot still scans the library normally.
// Off by default; players can enable the shortcut in Main Menu / Extra Items.








// R9, implemented. The title loops ONE of three of its own streamed tracks per menu visit
// (the `shellmusic` block of config/synth.dtb, chosen at random and looped forever); with
// this on, the menu rotates through regular bundled songs and installed DLC instead.
// Bundled songs stream from the native archive; DLC streams read-only from its packages
// (src/hooks/shell_music.cpp). Merged packages contribute every declared stream, and
// the bundled songs keep this feature available without an installed DLC library.
//
// REVIVED (owner request, 2026-10-14): the feature has a visible row again - "DLC Menu Songs" on
// the merged Help & Options surface's Downloadable Content page (`general.dlc_menu_songs`), which
// this cvar is the carrier for (`CvarForSetting`), applied by the boot at the config rank. Its
// compiled default here must equal the row's (`true`): a row nobody has chosen is *not* written, so
// on a fresh install what a boot reads is this cvar's own default.


// Input navigation and the visible player are independent preferences sharing one transport.





namespace rb_blitz::enhancements {

namespace {

const char* SourceName(rex::SettingLayer source) {
  switch (source) {
    case rex::SettingLayer::kDefault:
      return "default";
    case rex::SettingLayer::kTitle:
      return "title";
    case rex::SettingLayer::kLock:
      return "locked";
    case rex::SettingLayer::kFile:
      return "config";
    case rex::SettingLayer::kEnvironment:
      return "environment";
    case rex::SettingLayer::kCommandLine:
      return "command line";
    case rex::SettingLayer::kRuntime:
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
// what the log walks, so a toggle that is not here is a toggle nobody sees. Derive its size
// from the rows: a larger explicit size silently adds a null entry and crashes at boot.
constexpr auto kToggles = std::to_array<ToggleRow>({
    {"enhancements_hide_achievements", "hide achievement unlock notifications",
     [] { return rb_blitz::settings::title::enhancements_hide_achievements.Get(); }},
    {"enhancements_expand_resolution", "R1 custom resolutions",
     [] { return rb_blitz::settings::title::enhancements_expand_resolution.Get(); }},
    {"enhancements_ui_scale", "R2 UI accessibility",
     [] { return rb_blitz::settings::title::enhancements_ui_scale.Get(); }},
    {"enhancements_skip_offline_dialog", "R3 skip the offline prompts",
     [] { return rb_blitz::settings::title::enhancements_skip_offline_dialog.Get(); }},
    {"enhancements_icon_schemes", "R4 icon schemes",
     [] { return rb_blitz::settings::title::enhancements_icon_schemes.Get(); }},
    {"enhancements_hide_menu_options", "R5 hide menu options",
     [] { return rb_blitz::settings::title::enhancements_hide_menu_options.Get(); }},
    {"mouse_ui_nav", "R6 native mouse",
     [] { return rb_blitz::settings::title::mouse_ui_nav.Get(); }},
    {"enhancements_refresh_music_library", "R7 refresh songs row (the in-game row)",
     [] { return rb_blitz::settings::title::enhancements_refresh_music_library.Get(); }},
    {"enhancements_force_controller_scheme", "R8 force controller scheme",
     [] { return rb_blitz::settings::title::enhancements_force_controller_scheme.Get(); }},
    {"enhancements_menu_dlc_songs", "R9 main-menu library songs (the Background Music row)",
     [] { return rb_blitz::settings::title::enhancements_menu_dlc_songs.Get(); }},
    {"enhancements_background_music_nav", "menu-music navigator (P2; native pause/resume)",
     [] { return rb_blitz::settings::title::enhancements_background_music_nav.Get(); }},
    {"enhancements_rename_mod_settings", "R10 rename the mod's settings row",
     [] { return rb_blitz::settings::title::enhancements_rename_mod_settings.Get(); }},
});

}  // namespace

void LogToggles() {
  REXLOG_INFO("enhancements: {} toggle(s), off by default except R3, R6, R9, R10 "
              "and the menu-music navigator's own cvar "
              "(docs/engine/toggles.md)",
              kToggles.size());
  for (const ToggleRow& row : kToggles) {
    REXLOG_INFO("enhancements: {} = {} ({}) - {}", row.name, row.enabled() ? "on" : "off",
                SourceName(rb_blitz::settings::InspectRuntimeSetting(row.name).staged_source), row.feature);
  }
  REXLOG_INFO("enhancements: {} = \"{}\" (R8 scheme; empty means unnamed)",
              "enhancements_controller_scheme", rb_blitz::settings::title::enhancements_controller_scheme.Get());
  REXLOG_INFO("enhancements: {} = \"{}\" (R5 rows; the three offline-dead entries by default)",
              "enhancements_hidden_menu_options",
              rb_blitz::settings::title::enhancements_hidden_menu_options.Get());
  REXLOG_INFO("enhancements: {} = {} (retired size; rolling music session ignores this value)",
              "enhancements_menu_dlc_song_count",
              rb_blitz::settings::title::enhancements_menu_dlc_song_count.Get());
}

}  // namespace rb_blitz::enhancements
