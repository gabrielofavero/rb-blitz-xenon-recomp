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
// retail list's last row (docs/settings/architecture.md (part 1, the bridge) §12.3b).
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
  // The "Play Current Song" entry (owner feature, 2026-10-14). `play_current_song` is the setting;
  // `menu_music_songs` is the boot's menu-music pool, using the package metadata's song keys
  // (src/hooks/shell_music.cpp). The row is built only when both say yes: the setting off means the
  // player did not ask for it, and an empty pool means there is no DLC song to start - the two
  // states the settings screen reports as unavailable on its own row.
  bool play_current_song = false;
  std::vector<std::string> menu_music_songs;
  // The restored Download Content row (owner request, 2026-10-20). `download_content_row` is true
  // when this boot builds the row (the visibility setting says visible *and* R7 owns
  // `splash_dlc`, so the retail row's label and action are free); `download_content_row_available`
  // says whether the visibility switch is offered at all - it is meaningful only while R7 owns
  // `splash_dlc`, because with the cache off that retail row is the Download Content entry.
  bool download_content_row = false;
  bool download_content_row_available = false;
  // False when the boot found no DLC at all. The Play Current Song row is then not drawn: a row
  // that exists must work, and with no library there is nothing it could ever start.
  bool has_dlc_library = true;
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

// The main menu's "Play Current Song" entry (owner feature, 2026-10-14): added right after
// `splash_start`, so it is the second of the option array, and dispatched by a case of its own.
inline constexpr char kPlayCurrentRow[] = "splash_play_current";
inline constexpr char kPlayCurrentLabel[] = "Play Current Song";

// The restored main menu "Download Content" entry (owner request, 2026-10-20). R7's refresh took
// the retail row over (`splash_dlc` now performs the library refresh), so this row is a clone of
// the shipped `splash_dlc` case under a symbol of its own: the same action, the same label, and a
// visibility switch (`interface.hide_download_content`) whose default is hidden.
inline constexpr char kDownloadContentRow[] = "splash_dlc_store";
inline constexpr char kDownloadContentLabel[] = "Download Content";
// The probe namespace the row asks the host through (`file_exists`):
// `rbbz-play/current/<pool index>`, parsed by src/hooks/menu_music_pool.h.
inline constexpr char kPlayProbePrefix[] = "rbbz-play/current/";

// The installed Ultimate payload's own settings entry, which the merged surface **removes**: every
// page it opened is reachable from Help & Options now, so a second button for the same thing is one
// too many (owner decision, 2026-10-13).
inline constexpr char kProviderMenuRow[] = "mod_settings";

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
// **Measured status (P5b-r2, ten boots - docs/settings/menu.md (part 1, the merged menu) §3.2).** The
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
// order: `pause_tutorial` (How to Play), `calibration`, `settings_general`, `pause_controls`
// (Controls), `audio_video_options`, and `settings_gameplay` only when the provider is active. The
// two retail spellings are reused so retail's own cases dispatch the same pages, and
// `provider_active` false drops `settings_gameplay` because a base install must never be offered a
// dead row (plan §3).
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

// Removes `row` from a main-menu record entirely: its `[<row> ...]` case and its element in the
// option array. Used for the provider's own settings entry (`kProviderMenuRow`). Refuses, leaving
// `nodes` untouched, when neither is present - an older payload that never had the row is a
// refusal the caller may ignore, while a half-removed row is not a shape to ship.
bool RemoveRow(std::vector<dtb::Node>* nodes, std::string_view row, std::string* reason);

// Adds the "Play Current Song" entry to a splash record: `kPlayCurrentRow` right after
// `splash_start` in the option array (so it is the menu's second row) plus the case that
// dispatches it. The body is built from `configuration().menu_music_songs` and the host answers its
// probes (see the .cpp for the shipped statements it is assembled from). Refuses, leaving `nodes`
// untouched, when the record has no option array bearing `splash_start`, when the row is already
// there (a second pass), or when the pool is empty - the caller decides what an empty pool means.
bool AddPlayCurrentSongRow(std::vector<dtb::Node>* nodes, std::string* reason);

// Adds the restored "Download Content" entry: `kDownloadContentRow` right after `splash_dlc` in
// the option array, plus a case that carries a copy of the shipped `splash_dlc` action. Must run
// before `SetRefreshRowAction` (src/settings/menu_payload.cpp) reroutes that shipped case, because
// the clone is the only place the retail action survives. Refuses when the visibility setting is
// off, when the record has no `splash_dlc` case, or when the row is already there.
bool AddDownloadContentRow(std::vector<dtb::Node>* nodes, std::string* reason);

// The current menu configuration, for the authoring functions that read it (the play row's pool).
const MenuConfiguration& CurrentMenuConfiguration();

// Inserts `{<panel> load}` into the retail `ui/gen/ui.dtb`'s `{init ...}` block, so the merged
// root's panel is created - and its `{file}`/`{focus}` members processed - the way retail creates
// `options_panel` and its siblings, before the first screen is shown. Idempotent, and refuses
// (with a reason, without touching `nodes`) when the init block cannot be found.
bool AddPanelLoad(std::vector<dtb::Node>* nodes, std::string_view panel_name, std::string* reason);

// Appends `statements` to the **end** of a retail panel's `{enter ...}` member. This is the only
// way to touch a retail scene's objects: the panel's scene is loaded (and its own enter has run)
// before the appended statements execute, whereas a statement emitted *before* `{ui goto_screen
// ...}` runs against the old scene and silently does nothing. Used for the retail Audio page,
// whose header and list need a post-entry correction; refuses (with a reason, leaving `nodes`
// untouched) when the panel or its `enter` member cannot be found.
bool AppendPanelEnterStatements(std::vector<dtb::Node>* nodes, std::string_view panel_name,
                                const std::vector<dtb::Node>& statements, std::string* reason);

// The statements appended to retail `av_options_panel`'s `{enter}`: apply the list position the
// Audio route row captured (`rbbz_audio_x/y/z`), then rename the header. The route row runs in
// *our* scene, before the retail scene is loaded, so it can only capture; the application belongs
// here. The translation messages are `get_local_pos`/`set_local_pos` (guest 0x821F0A88 /
// 0x821F1678, three float arguments); the earlier `local_xfm` spelling did not move the list, and
// the owner's capture from it showed Overscan off on entry (measured 2026-10-19, out/audio-fix/probe3).
std::vector<dtb::Node> AudioPageEnterStatements();

// ------------------------------------------------------------------------------------------------
// The labels, the requests, and the locale edit
// ------------------------------------------------------------------------------------------------

// Every locale entry the surface needs that retail does not already carry, as (key, English).
// Rows that reuse a retail row symbol (`controls`, `calibration`, `audio_video_options`,
// `tutorial`, `credits`) are *not* here: their labels come from the installed locale unchanged.
std::vector<std::pair<std::string, std::string>> Labels();

// Retail locale entries the merged surface **rewrites**, as (key, from, to). The retail AV page
// draws its two checkboxes as `Overscan | ENABLED` / `Bass Boost | DISABLED` - a `|` separator and
// all-caps state - while every boolean of the merged surface reads `<label>: On`. These four
// entries are what that page draws, so rewriting them makes the retail rows match the rows beside
// them. Applied to the payload's locale record (which has no byte budget), not through the
// byte-neutral read-hook path, because all four sit in the file's first read block.
struct LocaleOverride {
  std::string key;
  std::string from;
  std::string to;
};
std::vector<LocaleOverride> LocaleOverrides();

// Rewrites the label of the locale entry filed under `key` from `from` to `to`. A locale that
// does not carry the entry, or carries a different label (a translated locale), is left alone -
// that is a skip, not a refusal, because the payload edits every installed locale. Returns false
// only for unusable input.
bool ReplaceLocaleEntry(std::vector<dtb::Node>* nodes, std::string_view key, std::string_view from,
                        std::string_view to, std::string* reason);

// Every request path the definition writes or asks, in a stable order. The test parses each one
// with the bridge's own text grammar, so a grammar change breaks a test rather than a boot.
std::vector<std::string> RequestPaths();

// ------------------------------------------------------------------------------------------------
// The help captions (owner request, 2026-10-13)
// ------------------------------------------------------------------------------------------------
// Every row of the surface carries a one- or two-line description. While a row is highlighted, the
// generated DTA publishes its **canonical** symbol through the settings channel
// (`rbbz-settings/help/<symbol>/-`; `help/-/-` when the surface is left - see `HelpWrite` in the
// .cpp), and the host's help overlay draws the description below the game as a subtitle. These are
// the host-side half: the lookup the overlay performs, and the enumeration the tests walk.

// The longest text the table may carry. The overlay wraps at its own width; the bound is the
// "short, 1-2 lines" contract from the request, enforced by tests/settings_screen_tests.cpp.
inline constexpr size_t kMaxHelpTextChars = 128;

// The description registered for a row's canonical symbol, or empty when there is none (the
// overlay treats empty as "nothing to show", which is also what an unknown symbol gets).
std::string HelpTextFor(std::string_view symbol);

// Every canonical row symbol of the surface, de-duplicated, in declaration order. A row cannot be
// reachable without being in here, and the test proves each has text by looking it up.
std::vector<std::string> HelpSymbols();

// ------------------------------------------------------------------------------------------------
// The binding rows a page lists (P1 of the interface/gameplay mapping restructure plan)
// ------------------------------------------------------------------------------------------------
//
// A page lists its rows in `PanelSpec::rows` order and every binding row contributes exactly one
// symbol to the list (`_on`/`_off` are adjacent and one is dropped), so a drawn row's logical index
// is its page-row index. This returns the token each index addresses, in draw order, with an empty
// string for a row that is not a binding (`Reset All`, a submenu, an Assign/Reset page). Empty for
// any screen this surface does not define.
//
// Both contexts resolve: the in-song twin's screen name answers the same rows. The mapping is the
// row's *identity*, not its value - the overlay asks the effective binding for the value every
// frame, so a capture shows in the row without re-entering the page.
std::vector<std::string> ScreenBindingRows(std::string_view screen);

// Appends `<key>` -> `<text>` to a locale definition, the shape every locale file in this title
// uses (an array of exactly two elements). Refuses when the key already exists, so a shadow copy
// built twice cannot drift.
bool AddLocaleEntry(std::vector<dtb::Node>* nodes, std::string_view key, std::string_view text,
                    std::string* reason);

}  // namespace rb_blitz::ui::screen
