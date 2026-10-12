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
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/dtb_author.h"

namespace rb_blitz::ui::screen {

// Frozen before generating the installed menu. Values come from the boot service and provider.
struct MenuConfiguration {
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> choices;
  std::map<std::string, std::string> disabled;
  std::vector<std::pair<std::string, std::string>> menu_rows;
  // Native overrides applied after the installed provider's initialization.
  std::map<std::string, bool> boot_booleans;
};
void ConfigureMenu(MenuConfiguration configuration);

// ------------------------------------------------------------------------------------------------
// The names a saved payload, a request path or a locale key contains. Frozen.
// ------------------------------------------------------------------------------------------------

// The project-owned scene the merged surface draws from, and the retail record it is generated
// from at boot. Same directory: the retail scene's own relative references (`neon_lights.milo`,
// `../resource/lists/list_options_main.milo`) resolve against it.
inline constexpr char kSceneName[] = "settings_menu.milo";
inline constexpr char kSceneRecord[] = "ui/options_menu/gen/settings_menu.milo_xbox";
inline constexpr char kSceneSourceRecord[] = "ui/options_menu/gen/options_menu.milo_xbox";

// The in-song context's **own** scene, and why it is a second copy rather than the one above. A
// panel's `{file}` names the dir the engine loads for it, and the engine caches that dir *by path*
// (bridge findings §12.3b): two distinct panels naming one scene are handed the same loaded dir, and
// with it the same `options.lst` object. Retail never has two panel declarations name one file - its
// in-song twins *share* the one panel object (`av_options_panel` is hosted by both
// `av_options_screen` and `av_options_pause_screen`) - and ours cannot share, because a panel's row
// bodies name their destinations and the two contexts' destinations differ. So each context gets its
// own scene, its own dir, and its own list object.
inline constexpr char kPauseSceneName[] = "settings_menu_pause.milo";
inline constexpr char kPauseSceneRecord[] = "ui/options_menu/gen/settings_menu_pause.milo_xbox";

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
// screen, in declaration order - and, after each main-menu pair, its in-song twin (see
// `PauseScreen`). Served as part of `kDefinitionRecord` (appended to the installed retail record by
// the delivery step).
std::vector<dtb::Node> DefinitionRoots();

// UIScreen resolves its panel symbols when constructed. The options record is included
// before game.dta/pause.dta, so in-song screens must be created in ui.dtb's init after
// init.dta has finished, while their resource-owning panels remain in the options record.
std::vector<dtb::Node> EarlyDefinitionRoots();
bool AddDeferredScreens(std::vector<dtb::Node>* nodes, std::string* reason);

// The panel names, in declaration order. The root is first, and every panel appears twice: once for
// the main-menu context, once for the in-song one.
std::vector<std::string> PanelNames();

// The screen names, in declaration order. The two contexts of each screen are adjacent.
std::vector<std::string> ScreenNames();

// ------------------------------------------------------------------------------------------------
// The in-song context (P5b-r2)
// ------------------------------------------------------------------------------------------------
// A page reached from inside a song is not the same screen as the one reached from the main menu:
// retail pairs every one of them (`av_options_screen` / `av_options_pause_screen`,
// `credits_screen` / `pause_credits_screen`) and the difference is the panel set - the running game
// and the pause panel stay on screen - plus the back target (`game_screen`). Ours follows the same
// convention.
//
// **Measured status (P5b-r2, ten boots - docs/plans/in-game-settings-merged-menu.md §3.2).** The
// dispatch works: retail's own HELP & OPTIONS row, given the merged list and our appended cases,
// reaches our page and stops there with the case markers logged. The **in-song panel set is what a
// screen of ours cannot have**: a screen listing `pause_panel` / `game_fade_out_panel` / `game_panel`
// faults the guest with `read of guest 0x00000024` while it is constructed, whichever of our panels
// it also hosts, whichever record declares it, and with or without any single one of those panels -
// while retail's own in-song screens list the same panels and open. The *menu* panel set is fine
// even in song, and that shape (`lab1`) draws the merged root inside a song, which is the first half
// of the feature; Back is the second, and it fails for the same reason (the game screen's panels are
// gone by then). Until §3.3 narrows the limitation, the in-song twins are an **experiment**: the
// shipped payload leaves `ui/pause/gen/pause.dtb` alone and retail's own in-song page keeps working.
std::string PauseScreen(std::string_view screen);
std::string PausePanel(std::string_view panel);

// Where a row's destination lives in a song. Our own screens get a `_pause_screen` twin; a retail
// screen's twin is the one retail already ships, and the four that exist are named here -
// `controller_config_pause_screen`, `av_options_pause_screen`, `pause_credits_screen`,
// `tutorials_pause_screen`. `cal_welcome_screen` is deliberately unchanged: retail's own in-song
// calibration route goes through `pause_confirm_quit_song_screen` and stays retail's own case.
std::string InSongDestination(std::string_view screen);

// The rows the pause panel's own in-song Help & Options page is filled with, in the merged root's
// order. The **same six symbols** the main-menu root uses (`settings_general`, `controls`,
// `calibration`, `audio_video_options`, `settings_gameplay`, `settings_interface`), so one symbol
// set feeds one label table in two contexts - and `provider_active` false drops `settings_gameplay`,
// because a base install must never be offered a dead row (plan §3).
std::vector<std::string> PauseRootRows(bool provider_active);

// Rewrites the array `pause_panel`'s `{enter}` hands to its inline `options.lst`, so the in-song
// page lists the merged root's rows instead of retail's five. The statement becomes gate-aware:
// the six-row list when the provider gate answers, the five-row one when it does not.
//
// Refuses (leaving `nodes` untouched) unless the record still holds retail's own five-symbol fill,
// so a changed retail record is reported rather than mangled.
bool MergePauseListRows(std::vector<dtb::Node>* nodes, std::string* reason);

// Appends `<row> <body...>` to the `{switch {get_focus selected_sym} ...}` that dispatches the
// pause panel's inline list - the switch retail's own `pause_controls` / `credits` cases live in.
// A switch takes its first matching case, so appending can never shadow a retail row; a row that
// must *replace* retail's behaviour is `RerouteRow`.
bool AddPauseSwitchCase(std::vector<dtb::Node>* nodes, std::string_view row,
                        const std::vector<dtb::Node>& body, std::string* reason);

// The body an in-song row of ours gets: point the destination's `back_screen` at whoever sent us,
// then go - the same statement the main-menu rows use, on the target's in-song twin.
std::vector<dtb::Node> InSongRowBody(std::string_view screen);

// ------------------------------------------------------------------------------------------------
// The edits to shipped records
// ------------------------------------------------------------------------------------------------

// The body a re-pointed main-menu row gets: ask our screen's back_screen for `splash_screen` and
// go there - retail's own two statements with our screen in place of `options_screen`.
std::vector<dtb::Node> MainMenuRouteBody();

// The body a re-pointed pause row gets. **It is retail's own statement**, deliberately: retail's
// in-song Help & Options does not open a screen, it hands the focus to the pause panel's inline
// `options.lst`. Four different bodies that *leave* the pause list instead were measured to fault
// the guest (`read of guest 0x00000024`) - including the focus switch followed by the jump in one
// frame - while retail's own switch, reached from that list, jumps to an in-song screen happily
// (`pause_controls`, `credits`). The in-song merge therefore lives in that list's *contents* and its
// *cases* (`MergePauseListRows` / `AddPauseSwitchCase`), not in this row.
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
