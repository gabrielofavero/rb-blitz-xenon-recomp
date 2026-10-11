// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P5b: the **merged Help & Options** surface. This module owns the whole authored definition of
// that surface (every panel and screen), the edits that re-point the retail entry rows at it, the
// labels it draws, and the request paths it sends. It replaces P1's four-row
// `advanced_settings_panel` prototype: the separate Advanced Settings main-menu row is retired and
// that screen no longer exists.
//
// ---------------------------------------------------------------------------------------------
// What the retail surface actually is, measured, and what the merge therefore has to be
// ---------------------------------------------------------------------------------------------
//
// Read out of the shipped records (decoded to out/_swap/p5b/*.tree.txt) rather than assumed:
//
//   * **Main menu**: `ui/splash/gen/splash.dtb`'s `splash_options` case is
//     `{options_screen set back_screen splash_screen}{ui goto_screen options_screen}`.
//     `ui/options_menu/gen/options_menu.dtb` then declares `options_panel`
//     (`{file options_menu.milo}{focus options.lst}`) whose rows are
//     `tutorial, controls, calibration, audio_video_options, credits`, and `options_screen`
//     (`{panels background_night_panel shell_music_panel menu_cache_panel options_panel}`,
//     `{back_screen splash_screen}`).
//   * **In-song pause**: there is **no pause screen** for options. `ui/pause/gen/pause.dtb`'s
//     `pause_panel` keeps a second list in the same scene (`options.lst` inside `pause.milo`) and
//     the `pause_options` case is `{this update_focus options.lst}` - it switches the *focus*
//     between `pause.lst` and `options.lst` on the game screen. The rows are the same five.
//   * Retail already solved "one panel on screen, reached from two contexts", and the shape is the
//     model this module copies: `ui/av_options/gen/av_options.dtb` declares `av_options_screen`
//     (`{panels shell_music_panel menu_cache_panel background_night_panel av_options_panel}`,
//     `{back_screen options_screen}`) **and** `av_options_pause_screen` (`{panels game_panel
//     pause_panel background_night_panel av_options_panel}`, `{back_screen game_screen}`), both
//     focusing the *same* `av_options_panel`. The panel leaves through
//     `{ui goto_screen {{ui current_screen} get back_screen}}`, which is how one panel can be
//     hosted by two screens.
//
// ---------------------------------------------------------------------------------------------
// The defect this package fixes, and why the fix is an owned list resource
// ---------------------------------------------------------------------------------------------
//
// P1's prototype put its panel on retail's `options_menu.milo` and drove `options.lst` - the *same
// list object* retail's own `options_panel` drives, because a panel's `{file X.milo}` goes through
// the engine's per-path dir loader and two panels naming the same file share one loaded dir. The
// two then overwrote each other's rows and selected symbol, and Back landed on `credits`, the
// retail list's last row (docs/plans/in-game-settings-bridge-findings.md §12.3b).
//
// So our panel does **not** use `options_menu.milo`. It uses a **project-owned scene record**,
// `ui/options_menu/gen/settings_menu.milo_xbox`, whose bytes are the installed retail scene copied
// at boot under a name of ours (no retail bytes ship: the payload is generated from the installed
// ark each run - parent plan §8.3). A different path means a different loaded dir means a
// different `options.lst` instance: the merged root's list is ours, so "no row dispatches through
// retail options.lst" is true by construction rather than by convention.
//
// The copy must live **beside** the retail scene, because `options_menu.milo` refers to its
// neighbours by relative path - `../resource/lists/list_options_main.milo` (the row widget),
// `../resource/fonts/eagle_book_label_sm.milo`, `../resource/inline_help/default.milo`,
// `neon_lights.milo` - and those resolve against the scene's own directory.
//
// ---------------------------------------------------------------------------------------------
// The row model, and the one array primitive it leans on
// ---------------------------------------------------------------------------------------------
//
// Retail's list resource is not the mod's value-drawing one, so a row draws a *label* and nothing
// else. A row's state is therefore expressed as **which symbol** the list is given, exactly as in
// P1's prototype: a boolean row contributes an `_on` and an `_off` symbol, and `populate_menu`
// keeps the one the bridge says is current (`file_exists "rbbz-settings/snapshot/<setting>/b:1"`)
// and drops the other with `remove_elem` - the only array primitive this module uses, and one two
// retail panels already use. A hidden-by-default row (the Ultimate-only ones) is dropped the same
// way when its gate query answers false.
//
// SDK-free and pure: this module builds and edits node trees, so tests/settings_screen_tests.cpp
// can pin every invariant - including that the produced files parse again - without a boot.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/dtb_author.h"

namespace rb_blitz::ui::screen {

// ------------------------------------------------------------------------------------------------
// The names a saved payload, a request path or a locale key contains. Frozen.
// ------------------------------------------------------------------------------------------------

// The project-owned scene the merged surface draws from, and the retail record it is generated
// from at boot. Same directory: the retail scene's own relative references (`neon_lights.milo`,
// `../resource/lists/list_options_main.milo`) resolve against it.
inline constexpr char kSceneName[] = "settings_menu.milo";
inline constexpr char kSceneRecord[] = "ui/options_menu/gen/settings_menu.milo_xbox";
inline constexpr char kSceneSourceRecord[] = "ui/options_menu/gen/options_menu.milo_xbox";

// The installed retail record our definitions are **appended to**, and the reason: the measured
// delivery site. The same panel declarations open when they ride inside a retail record and crash
// the guest (`write of guest 0x00000200`) when they live in a record of our own reached by
// `[include]` (bridge findings §12.3).
inline constexpr char kDefinitionRecord[] = "ui/options_menu/gen/options_menu.dtb";

// The list our panels drive. The *name* is the retail scene's; the *object* is ours, because the
// scene is ours (see the header comment).
inline constexpr char kListName[] = "options.lst";

// The merged root: one panel, hosted by two screens - the main-menu context and the in-song
// context - exactly as retail hosts `av_options_panel`.
inline constexpr char kRootPanel[] = "settings_panel";
inline constexpr char kRootScreen[] = "settings_screen";
inline constexpr char kRootPauseScreen[] = "settings_pause_screen";

// The retail rows the merge re-points, and the row P1/P5 added and this package retires.
inline constexpr char kMainMenuRow[] = "splash_options";
inline constexpr char kPauseMenuRow[] = "pause_options";
inline constexpr char kRetiredRow[] = "advanced_settings";

// The visibility query for provider-only rows (plan §3: Ultimate rows are absent on a base
// install). A membership query, like every other read on this channel.
inline constexpr char kProviderQuery[] = "rbbz-settings/snapshot/provider.active/b:1";

// ------------------------------------------------------------------------------------------------
// The authored definition
// ------------------------------------------------------------------------------------------------

// Every root of the merged surface: one `{new UIPanel ...}` and one `{new UIScreen ...}` per
// screen, in declaration order. Served as part of `kDefinitionRecord` (appended to the installed
// retail record by the delivery step).
std::vector<dtb::Node> DefinitionRoots();

// The panel names, in declaration order. The root is first.
std::vector<std::string> PanelNames();

// The screen names, in declaration order. The two root contexts are first.
std::vector<std::string> ScreenNames();

// ------------------------------------------------------------------------------------------------
// The edits to shipped records
// ------------------------------------------------------------------------------------------------

// The body a re-pointed main-menu row gets: ask our screen's back_screen for `splash_screen` and
// go there - retail's own two statements with our screen in place of `options_screen`.
std::vector<dtb::Node> MainMenuRouteBody();

// The body a re-pointed pause row gets. **It is retail's own statement**, deliberately: retail's
// in-song Help & Options does not open a screen, it hands the focus to the pause panel's inline
// `options.lst`, and every body that leaves the pause list instead faults the guest
// (`read of guest 0x00000024`, measured - see the definition in settings_screen.cpp). The in-song
// merge belongs in that list's own contents and dispatch, not in this row.
std::vector<dtb::Node> PauseRouteBody();

// Replaces the body of the `[<row> <body...>]` case that dispatches `row`, keeping the row symbol.
// `reason` is set when the case cannot be found. One operation for both entries, because both are
// the same shape: a case array whose first element is the row symbol and whose remaining elements
// are the statements to run.
bool RerouteRow(std::vector<dtb::Node>* nodes, std::string_view row,
                const std::vector<dtb::Node>& body, std::string* reason);

// Inserts `{<panel> load}` into the retail `ui/gen/ui.dtb`'s `{init ...}` block, so the merged
// root's panel is created - and its `{file}`/`{focus}` members processed - the way retail creates
// `options_panel` and its siblings, before the first screen is shown. Idempotent, and refuses
// (with a reason, without touching `nodes`) when the init block cannot be found.
bool AddPanelLoad(std::vector<dtb::Node>* nodes, std::string_view panel_name, std::string* reason);

// ------------------------------------------------------------------------------------------------
// The labels, the requests, and the locale edit
// ------------------------------------------------------------------------------------------------

// Every locale entry the surface needs that retail does not already carry, as (key, English).
// Rows that reuse a retail row symbol (`controls`, `calibration`, `audio_video_options`,
// `tutorial`, `credits`) are *not* here: their labels come from the installed locale unchanged.
std::vector<std::pair<std::string, std::string>> Labels();

// Every request path the definition writes or asks, in a stable order. The test parses each one
// with the bridge's own text grammar, so a grammar change breaks a test rather than a boot.
std::vector<std::string> RequestPaths();

// Appends `<key>` -> `<text>` to a locale definition, the shape every locale file in this title
// uses (an array of exactly two elements). Refuses when the key already exists, so a shadow copy
// built twice cannot drift.
bool AddLocaleEntry(std::vector<dtb::Node>* nodes, std::string_view key, std::string_view text,
                    std::string* reason);

}  // namespace rb_blitz::ui::screen
