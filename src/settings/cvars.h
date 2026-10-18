// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 — the cvar-facing half of the settings contract (contracts §7 and §8): the runtime
// controller allowlist, the target/license translation, the row-to-cvar table, and the
// config-source reporting a boot or the UI shows for an overridden row.
//
// This is where the coordinator's boot wiring reads its facts from, so the values live in
// one place rather than in a comment. Nothing here touches the SDK: it is data plus pure
// functions, and the game applies the cvars at the config rank (§4.2).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "profile/profile.h"
#include "settings/model.h"

namespace rb_blitz::settings {

// §8, frozen: the only cvars the runtime applies from the legacy Controller Settings profile once
// migration is complete. Everything else in its [settings] table is inert preservation data.
// `guide_button` and `keybind_guide` are excluded by design (contracts §2.3).
//
// The three gameplay-mapping switches joined on 2026-10-18 with P1 of
// docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md (D2/D6): Controller Settings
// writes them, and a boot that was not started by Controller Settings has to read them too.
inline constexpr std::string_view kControllerCvarAllowlist[] = {
    "mouse_ui_nav",                        // controller-owned; Controller Settings writes it
    "input_backend",                       // hand-edit only (no schema row); kept for the XInput path
    "keyboard_support",                    // Controller Settings' Keyboard support row
    "gameplay_mapping_controller",         // Controller Gameplay Mapping switch
    "gameplay_mapping_keyboard_mouse",     // Keyboard + Mouse Gameplay Mapping switch
};
inline constexpr std::size_t kControllerCvarAllowlistCount =
    sizeof(kControllerCvarAllowlist) / sizeof(kControllerCvarAllowlist[0]);

// True when `name` is one of the controller rows above. Inline because the game's own profile apply
// uses it and this module's translation unit is not in the game's source list yet.
inline bool IsControllerCvar(std::string_view name) {
  for (std::string_view candidate : kControllerCvarAllowlist) {
    if (candidate == name) {
      return true;
    }
  }
  return false;
}

// §8's narrowing. The legacy Controller Settings profile's `[settings]` table is a source of game preferences
// only until the game has its own settings file; after that the controller rows are the only part
// of it that is still ours, and everything else in the table is inert preservation data - a record
// of what the old Controller Settings wrote, kept so a person's file is not rewritten, applied to nothing.
//
// One exception, added with the P11b cutover (2026-10-13): a handful of Controller Settings rows are *host*
// rows with no in-game settings row at all - the monitor pick, the two safe areas, dither, the
// overscan cutoff and the two audio rows. Help & Options cannot express them, the store has no
// descriptor for them, and Controller Settings' tabs remain their only editor, so the profile stays their
// carrier for a boot nobody launched through Controller Settings (R4's promise). A row the in-game tree
// owns is *not* in this list, however similar its name looks: `present_letterbox` (the SDK's mouse
// mapping) and the Interface page's Letterbox row (the payload's own variable) are two settings.
inline constexpr std::string_view kHostOnlyCvarAllowlist[] = {
    "monitor",
    "present_safe_area_x",
    "present_safe_area_y",
    "present_allow_overscan_cutoff",
    "present_dither",
    "audio_mute",
    "audio_maxqframes",
};
inline constexpr std::size_t kHostOnlyCvarAllowlistCount =
    sizeof(kHostOnlyCvarAllowlist) / sizeof(kHostOnlyCvarAllowlist[0]);

// True when `name` is one of the host rows above - a row no in-game surface can edit.
inline bool IsHostOnlyCvar(std::string_view name) {
  for (std::string_view candidate : kHostOnlyCvarAllowlist) {
    if (candidate == name) {
      return true;
    }
  }
  return false;
}

// Pure, so the rule is a test rather than a claim: P8 implements it, and the coordinator switches
// it on with the boot wiring that gives the game its own file (a machine that narrowed before that
// would lose the DLC root and the save location, which come from this table today).
inline std::vector<rb_blitz::profile::ProfileSetting> ControllerProfileRows(
    const std::vector<rb_blitz::profile::ProfileSetting>& rows) {
  std::vector<rb_blitz::profile::ProfileSetting> kept;
  for (const rb_blitz::profile::ProfileSetting& row : rows) {
    if (IsControllerCvar(row.key)) {
      kept.push_back(row);
    }
  }
  return kept;
}

// What the game still applies from the legacy profile: the controller rows (still ours) and the
// host-only host rows (never the game's to own). Everything else in the table belongs to the
// game's own settings file now.
inline std::vector<rb_blitz::profile::ProfileSetting> ProfileRowsTheGameApplies(
    const std::vector<rb_blitz::profile::ProfileSetting>& rows) {
  std::vector<rb_blitz::profile::ProfileSetting> kept;
  for (const rb_blitz::profile::ProfileSetting& row : rows) {
    if (IsControllerCvar(row.key) || IsHostOnlyCvar(row.key)) {
      kept.push_back(row);
    }
  }
  return kept;
}

struct CvarAssignment {
  std::string name;
  std::string value;  // the value as the cvar's own text form
};

// The license policy the host passes in: the exact masks are the game's, not guessed here
// (contracts §12 keeps the bit values out of P0).
struct LicensePolicy {
  uint32_t purchased_mask = 0;
  uint32_t trial_mask = 0;
};

// The target translation, applied inside the game before Ultimate is configured (§7 table).
std::vector<CvarAssignment> TargetToCvars(rb_blitz::profile::LaunchTarget target,
                                          const LicensePolicy& policy);

// The AA enum's translation to the backend's two mechanisms (§7.1). One row, one mapping.
std::vector<CvarAssignment> AntiAliasingToCvars(std::string_view token);

// Native = -1, None = 0, 1x = 1, 2x = 2, 4x = 3, 8x = 4, 16x = 5. A "16x" label is never
// written as value 16.
bool FilteringToCvarValue(std::string_view token, int32_t* out);

// The window mode's translation to the two SDK window flags (owner decision, 2026-10-13).
// `windowed` and `borderless` both leave fullscreen mode; `borderless` additionally draws the
// window undecorated, sized to the display. An unknown token yields no assignments and the caller
// reports the row as unapplicable rather than guessing.
std::vector<CvarAssignment> DisplayToCvars(std::string_view token);

// The cvar a native row ultimately drives, or "" for a row the game consumes directly
// (target, resolution) or a provider-owned row. AA and filtering have their own functions
// because each drives more than one value.
std::string_view CvarForSetting(SettingId id);

// The provider (Ultimate) cvar a provider row binds to (§3 tables), or "" when the row is
// not provider-owned. P6 uses this; P3 only names the vocabulary.
std::string_view ProviderCvarFor(SettingId id);

// Precedence rank of a source, higher wins. Compiled defaults sit below the settings file;
// explicit developer config, environment and CLI sit above it (§4.2).
int SourceRank(Source source);

// A one-line explanation for a row that an explicit higher-rank source overrides, so the UI
// can disable the row and say why instead of saving a change that cannot take effect.
std::string OverrideExplanation(Source source, std::string_view setting_token);

}  // namespace rb_blitz::settings
