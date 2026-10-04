// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The enhancement toggle contract: the cvars behind the `[enhancements]` table,
// and the one boot line that reports them. See src/enhancements.h for why the
// names carry the table prefix and docs/engine/toggles.md for the table itself.
//
// Every toggle here is off by default and every one is honest about being
// unimplemented, because a user reading --help or a log must not be able to
// mistake a planned feature for a working one. The faithful (off) behaviour is
// named in each description; that is the same record the project requires of a
// hook, applied to a plan.
//
// Each cvar carries kRequiresRestart rather than the hot-reload default: what
// these gate is guest behaviour decided at load, so "changed live" would be a
// claim no implementing prompt has made yet. A prompt that can apply a toggle
// live relaxes its own lifecycle and says so in docs/engine/toggles.md.

#include "enhancements.h"

#include <array>

#include <rex/cvar.h>
#include <rex/logging.h>

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

REXCVAR_DEFINE_BOOL(enhancements_skip_offline_dialog, false, "Enhancements",
                    "R3 auto offline mode: answer the \"Proceed in Offline Mode?\" prompt for the "
                    "player, enabling offline mode without the interaction. Faithful: the prompt "
                    "is shown and the player chooses. Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_icon_schemes, false, "Enhancements",
                    "R4 different icons: let the button glyphs and controller-layout art be "
                    "replaced per scheme. Faithful: the art the title ships. Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_hide_menu_options, false, "Enhancements",
                    "R5 hiding categories: remove named entries from the main menu's option list, "
                    "in the list the guest navigates and not only in the drawing. Faithful: every "
                    "shipped entry is present. Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_native_mouse, false, "Enhancements",
                    "R6 different input waves: use the engine's own pointer path for the mouse "
                    "instead of the synthetic pad in src/input/mouse_ui.cpp. Faithful: the pad the "
                    "guest actually reads. Not implemented.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(enhancements_dlc_cache, false, "Enhancements",
                    "R7 DLC cache: persist the DLC enumeration host-side so a boot does not "
                    "re-search every package, with an explicit refresh. Faithful: the guest's own "
                    "scan, once per boot. Not implemented.")
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

REXCVAR_DEFINE_BOOL(enhancements_menu_dlc_songs, false, "Enhancements",
                    "R9 change the main menu's songs for loaded DLC ones. Faithful: the songs the "
                    "title puts there. Not implemented.")
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
constexpr std::array<ToggleRow, 9> kToggles{{
    {"enhancements_expand_resolution", "R1 custom resolutions",
     [] { return REXCVAR_GET(enhancements_expand_resolution); }},
    {"enhancements_ui_scale", "R2 UI accessibility",
     [] { return REXCVAR_GET(enhancements_ui_scale); }},
    {"enhancements_skip_offline_dialog", "R3 offline-mode skip",
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
}};

}  // namespace

void LogToggles() {
  REXLOG_INFO("enhancements: {} toggle(s), off by default (docs/engine/toggles.md)",
              kToggles.size());
  for (const ToggleRow& row : kToggles) {
    REXLOG_INFO("enhancements: {} = {} ({}) - {}", row.name, row.enabled() ? "on" : "off",
                SourceName(rex::cvar::GetFlagSource(row.name)), row.feature);
  }
  REXLOG_INFO("enhancements: {} = \"{}\" (R8 scheme; empty means unnamed)",
              "enhancements_controller_scheme", REXCVAR_GET(enhancements_controller_scheme));
}

}  // namespace rb_blitz::enhancements
