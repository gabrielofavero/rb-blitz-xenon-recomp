// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P2 of docs/plans/finished/2026-10-13-rhythmverse-store-plan.md: the data-layer
// decision gate. This is the *measurement* half — it points the retail store's netcache
// at a local (host-file) server so the paths the store asks for become file opens this
// project can see, instead of a request to a server that no longer exists.
//
// The record is `config/gen/store.dtb`, whose single definition is `netcache_init` (decoded
// verbatim in out/_swap/config_store.tree.txt): a `servers` table of named servers (each with
// `server`/`port`/`root`, and one `local` entry carrying `{local TRUE} {root ...}`) and a
// `default_server` symbol that picks one. The rewrite keeps every other record in the file —
// the store's `groupings`, `dummy_upsell_offer` and the PS3 terms — because those are still
// read by the same parser.
//
// The transform touches **every** `servers`/`default_server` the definition carries, not just
// the Xbox branch: the shipped record keeps its platform branches inside `#ifdef` nodes
// (`HX_XBOX`, `HX_PS3`, `_SHIP`, `_DEBUG`) whose evaluation is the guest's business, so
// rewriting only the branch this build "should" take would make the measurement depend on a
// guess about that evaluation. Rewriting them all means whichever branch the guest takes, its
// default server is the local one.
//
// SDK-free on purpose: no rex headers, so tests/store_payload_tests.cpp covers it without a boot.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ui/dtb_author.h"

namespace rb_blitz::store {

// The `netcache_init` definition's own name in `config/gen/store.dtb`.
inline constexpr char kNetcacheInitName[] = "netcache_init";

// Rewrites `roots` (the parsed `config/gen/store.dtb` record) in place so that:
//
//   * every `servers` table carries a `local` entry rooted at `server_root`
//     (an existing `local` entry is rewritten rather than duplicated), and
//   * every `default_server` picks `local`.
//
// `server_root` is the guest-relative directory the netcache resolves against, spelled the way
// the shipped `local` entry spells its own (`ui/store/remote_metadata/`, trailing slash
// included). An empty root is refused: a local server with no root would measure nothing.
//
// Returns false and fills `why` when the record does not carry `netcache_init`, when it carries
// no `default_server` at all, or when `server_root` is empty — never a partial rewrite that
// looks successful.
bool PointNetcacheAtLocalServer(std::vector<ui::dtb::Node>* roots, std::string_view server_root,
                                std::string* why);

// The `netcache_init` definition inside a parsed `config/gen/store.dtb`, or null. Used to hand the
// same definition to the manager through `ui/gen/ui.dtb` (below), so the manager's argument has the
// shipped shape by construction.
const ui::dtb::Node* FindNetcacheInit(const std::vector<ui::dtb::Node>& roots);

// `ui/gen/ui.dtb` creates the netcache manager once, at boot, with a definition it looks up in the
// engine's syscfg:
//
//   {net_cache_mgr init {find syscfg store netcache_init}}
//
// The lookup is unmeasured (measured 2026-10-20: the store's `request` is issued and the manager
// then performs no file, content or socket call at all — so whether `syscfg.store` is populated in
// this build is not known), so this rewrite replaces the argument with `definition` — normally the
// record's own rewritten `netcache_init`, never a hand-built one, because a literal that differs
// from the shipped shape is a guess about what the native manager reads.
//
// Returns false and fills `why` when the record carries no `net_cache_mgr init` command or when
// `definition` is not a `netcache_init` array — again before any edit, so a refusal leaves the tree
// as it was.
bool PointUiNetcacheAtLocalServer(std::vector<ui::dtb::Node>* ui_roots,
                                  const ui::dtb::Node& definition, std::string* why);

// P3 step 0's second half. The panel starts its first request only from its own DTA callback
// `on_init_commerce_success`, which fires when a native commerce session is created — a session
// an offline boot never gets (measured 2026-10-20: no `store-probe: request` line is ever printed,
// and the screen sits on its loading visual state). The callback's own body is harmless to run
// (`{this request_index_if_needed}` then a `has_menus` check that sets the visual state), so this
// rewrite appends `{store_panel on_init_commerce_success}` to `store_menu_screen`'s `enter`
// method and lets the retail DTA do the rest.
//
// Returns false and fills `why` when the record carries no `store_menu_screen` with an `enter`
// method — before any edit.
bool AddStoreEntryBypass(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// P4's entry from the store, re-pointed by P9-11 at the title's own keyboard. The front menu's
// rows are provider names, and `store_menu_panel.SELECT_MSG` answers a press with
//
//   {do {var $provider {this get_menu_provider}} {var $ix {component selected_pos}}
//        {this mark_current_highlight_idx} {this set_focus ""}
//        {store_panel set_source … TRUE} {store_panel request <name> TRUE}
//        {store_panel update_loading_status}}
//
// (out/_swap/store.tree.txt, verbatim). This rewrite wraps the `request` command in a two-level
// gate:
//
//   {if_else {== $ix <search_row>}
//      {if_else {file_exists "rbbz-store/search/show"} <request>
//               {{ui current_screen} <keyboard_method>}}
//      <request>}
//
// so the Search row opens the keyboard while the search has no answer to present, and asks for the
// search page - which the serving policy answers with that answer - once it has one
// (`store_search.h`'s `kSearchShowQuery`). Every other row keeps retail's own body verbatim.
//
// The row is identified by its **index**, the value `{component selected_pos}` already holds: the
// menu is written by this project (`FrontMenuRowIndex`), so the position is a fact rather than a
// guess about how the provider spells a name, and `{== $ix 1}` is an integer comparison the DTA can
// evaluate without depending on symbol-vs-string equality.
//
// Idempotent (a second pass is refused by name); refused, before any edit, when the record carries
// no `store_menu_panel.SELECT_MSG` with a `request` command, or when `search_row` is negative.
//
// The method is the screen's own (see `AddSearchKeyboard`); the name is a constant because the
// gate and the definition are written by two transforms and the suite pins both.
inline constexpr char kSearchKeyboardMethod[] = "open_store_search";
// The message the title's own keyboard sends when the player finishes with it. `sub_8237A878`
// interns exactly this spelling; `macros.dtb`'s `VIRTUAL_KEYBOARD_RESULT_MSG` is the macro that
// expands to it, so the record's method has to carry the expanded (lowercase) name.
inline constexpr char kSearchKeyboardResultMethod[] = "virtual_keyboard_result_msg";
// The keyboard's own title and description tokens (P9-11). The Ultimate payload's song search
// passes `search_for_song` / `search_for_song_desc`, but retail's locale carries neither, so both
// are added by `menu_payload.cpp` while the store is on.
inline constexpr char kSearchKeyboardTitleToken[] = "search_for_song";
inline constexpr char kSearchKeyboardTitleText[] = "SEARCH";
inline constexpr char kSearchKeyboardDescToken[] = "search_for_song_desc";
inline constexpr char kSearchKeyboardDescText[] =
    "Search by artist or song title, one at a time.";
// The store page the Search row asks for, as the front menu names it: `MenuProviderPath(locale,
// kSearchMenuId)` = `/eng/search/`, which the panel joins with the region (`store_metadata.h`).
// The front menu is written with the default locale, so the row's own `{$provider get_string $ix}`
// is exactly this string; the keyboard's result handler asks for it after the submit.
inline constexpr char kSearchPageRequest[] = "/eng/search/";
inline constexpr char kSearchLoadingPanel[] = "rbbz_search_loading_panel";
inline constexpr char kSearchLoadingToken[] = "rbbz_search_loading";
inline constexpr char kSearchLoadingText[] = "Searching RhythmVerse...";
inline constexpr char kSearchCancelToken[] = "rbbz_search_cancel";
inline constexpr char kSearchCancelText[] = "Press <alt1>B</alt1> to Cancel";
inline constexpr char kSearchEmptyToken[] = "rbbz_search_empty";
inline constexpr char kSearchEmptyText[] = "No matches. Try another artist or song title.";
inline constexpr char kSearchFailedToken[] = "rbbz_search_failed";
inline constexpr char kSearchFailedText[] = "The search could not finish. Please try again shortly.";
inline constexpr char kSearchShortToken[] = "rbbz_search_short";
inline constexpr char kSearchShortText[] = "Enter at least 3 characters to search.";
bool RouteSearchRowToKeyboard(std::vector<ui::dtb::Node>* store_ui_roots, int search_row,
                              std::string_view keyboard_method, std::string* why);

// P9-11: the title's own keyboard, as the store's search field. Measured in the retail image
// (2026-10-23): `.?AVVirtualKeyboard@@` is a native class whose vtable slot 5 (`sub_8237AD10`)
// dispatches `show_keyboard`, the boot init `sub_823631C0` names one instance `virtual_keyboard`
// in the object directory, and `sub_8237A878` answers the callback receiver with the message
// `virtual_keyboard_result_msg` carrying two positional values, `ok` and `text`. The Ultimate
// payload's own song search drives it from DTA with
//
//   {virtual_keyboard show_keyboard $user 100 (localize <title>) (localize <desc>) "" $this}
//
// so the seam is a script method pair on the screen the row is pressed on:
//
//   (open_store_search
//      {do {write_file "rbbz-store/search/open" 0}
//          {if_else {file_exists "rbbz-store/search/seed"}
//                   {do {write_file "rbbz-store/search/done" 0} <show loading modal>}
//                   {do {$user {user_mgr get_participating_user}}
//                       {if_else $user
//                                {virtual_keyboard show_keyboard $user 100
//                                                 (localize search_for_song)
//                                                 (localize search_for_song_desc) "" $this}
//                                {do {$user {user_mgr get_user_from_pad_num 0}}
//                                    {if_else $user <the same call>
//                                             {write_file "rbbz-store/search/leave" 0}}}}}}})
//   (virtual_keyboard_result_msg ($ok $text)
//      {if_else $ok
//               {do {write_file {sprintf "rbbz-store/search/text/%s" $text} 0}
//                   {write_file "rbbz-store/search/done" 0}
//                   <show loading modal>}
//               {write_file "rbbz-store/search/leave" 0}})
//
// The focused loading panel uses the retail error popup's resource. Its `poll`
// waits for `search/busy` to clear, hides the popup, and requests the results with
// the shipped row's set_source/request/update_loading_status triple. No guest
// thread waits for HTTP. Empty/invalid/failed searches show a terminal notice.
// B/Back cancels the request and restores the store's list focus; all other
// buttons are consumed. Leaving with the popup active cancels the search too.
//
// The keyboard's first argument is a **User object**: `sub_8237B008` asks it for the player's name
// through its first virtual slot before it calls the platform keyboard, so the bare symbol is a
// null read (measured 2026-10-23: `read of guest 0x00000000` right after the press). The store is
// reachable with the participating user (retail's front-menu gate) or, offline, with pad 0's, so
// the body binds the first that answers and dismisses the surface when neither does.
//
// `$this` is the screen object, so the keyboard's answer lands on the same screen that opened it;
// retail spells this handler with a parameter list and a body (`store_info.dtb`'s
// `(preview_download_complete_msg ($bSuccess) …)`), which is how the two values arrive.
// `$ok` false is the keyboard's own Cancel, which dismisses the surface (`leave`) and leaves the
// field and any earlier answer untouched. The `seed` branch is the measurement switch
// (`store_search.h`'s `kSearchSeedQuery`): a boot whose field was seeded submits it instead of
// drawing a keyboard a script cannot type on.
//
// The same call also appends `{write_file "rbbz-store/search/leave" 0}` to `store_menu_panel`'s
// `exit`, so leaving the store stops the caption strip (the surface the sentences belong to is
// gone) while the field and the answer stay for the next visit.
//
// Idempotent (a second pass is refused by name); refused, before any edit, when the record carries
// no `store_menu_screen` definition or no `store_menu_panel.exit`.
bool AddSearchKeyboard(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// P3's free-download seam (plan §2.12). Retail draws its price chrome in two places, both of them
// text the scripts show and no data we could supply hides:
//
//   * the store's footer, "ALL PRICES ARE IN MICROSOFT(R) POINTS" (`microsoft.lbl`, shown on the
//     Xbox branch of `BandStorePanel.enter`);
//   * the details page's `cost.lbl`/`cost_ps3.lbl`, which `store_info_panel.refresh_panel` re-shows
//     whenever `purchase_allowed` is true.
//
// RhythmVerse customs are free, so this appends `{microsoft.lbl set_showing FALSE}` to the panel's
// `enter` (which every store screen runs, because the panel is a member of all three) and returns
// false when that method is missing - before any edit.
bool HideStorePriceChrome(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// The same seam on the details record (`ui/store/gen/store_info.dtb`): every command that shows a
// cost label is removed from `store_info_panel.refresh_panel`, and both labels are hidden once more
// at the end of the method. The purchase button needs no edit: `purchase_allowed` needs
// `{offer is_available}`, which a free custom never sets - see `Offer::downloaded`.
bool HideStoreInfoPriceChrome(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// ---------------------------------------------------------------------------------------------
// P9-13 - the details page's box carries the custom's own credit block
// ---------------------------------------------------------------------------------------------
//
// The box that holds the Download / Cancel rows (`store_info_panel`, `ui/store/gen/store_info.dtb`)
// drew the song's name alone (`song.lbl set_offer_name`; the P3 details capture OCRs
// `Building a Mystery` and nothing else above the buttons). The owner's correction of 2026-10-22 is
// the point of the block: a title can have several customs with different authors and release
// dates, so the page has to name the custom the player is looking at - its charter, its release
// date, its audio type and its download count.
//
// The scene has no spare label (`store_info.milo_xbox` declares `song.lbl`, `drm.lbl`,
// `rating.lbl`, `rating_description.lbl`, `cost.lbl`/`cost_ps3.lbl` as its only `UILabel`s), and
// retail's `refresh_panel` already *hides* the ones it does not use for a data-only offer:
// `rating.grp` is hidden when the offer carries no `rating`, and the cost labels are hidden by
// `HideStoreInfoPriceChrome`. This transform shows them again, **after** those hides, and
// replaces their retail positioning with a bounded, top-left-aligned stack. The lines are:
//
//   drm.lbl                 Song Artist
//   rating.lbl              Audio type (`AudioTypeLabel`'s text; hidden when the catalogue has none)
//   rating_description.lbl  Charted by <charter> (hidden when the record has none)
//   cost.lbl                Released at <date> (hidden when absent)
//   cost_ps3.lbl            <N> Downloads (hidden when absent)
//   song.lbl                Song Title - retail's own `set_offer_name`
//
// Each fact gets its own row. Every string is the record's own key written by
// `store_metadata.cpp` (`audio_type`, `charter`, `rbbz_release_date`, `downloads`), so the payload
// composes text and never invents a value; a missing or unknown value draws no line rather than a
// placeholder, and the raw `audio_type` code is never drawn.
//
// The statements are appended to the **method**, as one `{do ...}` block after its own body,
// because they must run after `refresh_panel`'s own hides and after the two cost hides above -
// which is also why this transform must run after `HideStoreInfoPriceChrome`. The offer is bound
// inside that block with the compiled record's own binding statement
// (`(Var offer (Property cur_offer))`), the shape `refresh_buttons` uses and the one a `{var}`
// command is not (P5 measured that distinction twice).
//
// Reset rating.grp's offset so its labels share the box's coordinates. The padded title/artist
// block uses sizes 24/18 and a 480-unit width. Metadata uses size 14, width 440 and a 22-unit
// pitch, advancing only for visible facts. Long values use ellipsis. The actions keep their
// retail lower-right position. A boot provides visual confirmation; tests do not render the scene.
//
// Idempotent (the marker is a `set_display_text_string` on `drm.lbl`); refused, before any edit,
// when the record has no `store_info_panel.refresh_panel`, when that method does not drive one of
// the labels this edit writes (the anchor that keeps it from writing into a scene it does not
// know), or when the string writer's name it depends on is spelled differently.
bool ShowOfferCredits(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// A free custom is never "completely unavailable": that state only exists in the retail purchase
// model. `store_info_enum_panel.on_enum_finished` opens `store_info_screen` only when
// `{offer is_completely_unavailable}` is false, and measured 2026-10-21 the A press on a served
// offer landed on `store_info_unavailable_screen` instead. This replaces that guard's test with
// `FALSE` (the else branch - the details screen - always runs) in **every** panel that carries it
// (`store_info_enum_panel` and `store_offer_fetch_panel` both do; the first cut edited only the
// first match, which in document order is the fetch panel, so the browser's own guard survived).
// Refused, before any edit, when no such guard exists in the record at all.
bool AlwaysShowOfferDetails(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// The browser's A press hands the offer to `store_info_enum_panel.enum_and_show_offer`, which
// switches to `store_info_enum_screen` and lets the native `set_offer` enumerate it. For a served
// offer that enumeration reports an error and the press ends on `store_info_unavailable_screen`
// (measured 2026-10-21: the popup survives `AlwaysShowOfferDetails`). This rewrite replaces the
// method's body with the body the retail panel itself runs when its enumeration succeeds - the
// `on_enum_finished` else branch, verbatim - so the details screen is populated directly:
//
//   {do {store_info_panel clear_data}
//       {store_info_panel set cur_offer offer}
//       {store_details_panel refresh offer}
//       {ui goto_screen store_info_screen}}
//
// `set_return_target` still runs before it (the browser's own case), so Back keeps its target.
// Idempotent; refused, before any edit, when the panel or the method is missing.
bool ShowOfferDetailsDirectly(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// The details page's Downloaded row. `store_info_panel.refresh_buttons` (printed out of the shipped
// record 2026-10-22) inserts `store_download_again` under `{if_else {offer is_purchased}
// {insert_elem buttons 0 store_download_again} {if {this purchase_allowed offer}
// {insert_elem buttons 0 store_buy_now}}}` - so the row does **not** depend on `purchase_allowed`,
// which is what this build keeps false. The gate itself is the problem: `is_purchased` is answered
// **natively** by the offer object's own message handler (`sub_82453928`, whose literals include
// `get_data`, `has_data`, `offer_type`, `year_released`, `genre`, `is_purchased`), from a commerce
// state a data-only custom never has, so the record's `(is_purchased TRUE)` was ignored and a
// marked offer's page drew Exit alone (measured 2026-10-22, `out/_swap/store-probe/20261022-091435`;
// the same record counted as marked in the trace). This replaces that gate with the **data** read
// retail itself uses for `preview_audio` - `{offer get_data (is_purchased)}` - which the offer walk
// resolves like any other one-component key. An offer without the key reads a null, so its page is
// unchanged (Exit only). Idempotent; refused, before any edit, when `refresh_buttons` is missing or
// carries no such gate.
bool UseDataForDownloadedRow(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// P5's button (plan §5/P5). The details page's button list is built by `refresh_buttons`, and for a
// song it carries **two** rows in the not-downloaded case: `exit`, and - only when
// `{this purchase_allowed offer}` - `store_buy_now`. A free custom is never purchasable, so the page
// drawn for a served offer has to get its Download row from this build.
//
// The insert goes into the **else** branch of the `{if_else {offer is_purchased} <again> <else>}`
// gate (the not-downloaded side), wrapped as
//
//   {do {insert_elem buttons 0 store_download_now} <retail's own else branch>}
//
// so the page reads `DOWNLOAD / EXIT` and retail's buy path is untouched. `insert_elem … 0` puts the
// new row first - the same call retail uses for `store_download_again`, which is why the row's
// position matches how every other price/detail row is placed.
//
// Run **after** `UseDataForDownloadedRow`: that transform replaces the gate's test, this one only
// adds to a branch, so the order does not strictly matter - but doing it in this order means the
// else branch this pass finds is already the rewritten shape the tests pin.
//
// Idempotent (a second pass finds its own `store_download_now` insert and refuses by name); refused,
// before any edit, when `refresh_buttons` is missing or carries no `store_download_again` insert.
bool AddDownloadButton(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// The button's other half: the press. `store_info_panel.SELECT_MSG` dispatches
// `{switch {component selected_sym} ...}` over the button symbols, and retail's
// `store_download_again` case calls the native `{store_panel re_download …}`, which needs the
// commerce object this build never creates. This rewrites that case and appends a
// `store_download_now` one so both rows reach the worker through the request channel instead:
//
//   {do ($offer {property cur_offer})
//        {if_else {offer has_data job}
//           {do ($job {offer get_data (job)})
//               ($path {sprintf "rbbz-store/download/<start|again>/%s" $job})
//               {rbbz_download_panel show_popup $job}
//                {write_file $path 0}}
//           {notify "This offer carries no download job"}}}
//
// Every shape is one the shipped `refresh_buttons` uses: the property-to-local **binding** before a
// data read (an array in the compiled record - a `{var}` *command* inside a `do` is not the same
// thing and crashed the first boot that pressed the row), the `has_data` guard that keeps a missing
// key from being walked, `sprintf` with a nested command as its argument, and the request channel's
// own `write_file`. The path is the grammar `store_download.h` owns (`ParseJobCommand`), and the
// token is the offer's `job` key. The popup opens first (`AddDownloadPopup`), so it is up before
// the transfer starts and it is the only place the job's state is drawn.
//
// Measured 2026-10-22 on the first boot that pressed the row (`out/_swap/store-probe/
// 20261022-115913`, log `rb_blitz_212.log`): the case dispatched and reached the host (a literal
// probe write answered `refusing a malformed write`), and the binding written as a command died on
// `read of guest 0x00000000` the same frame - which is what the array form above fixes.
//
// Idempotent (the appended case is the marker); refused, before any edit, when the record carries no
// `SELECT_MSG` switch or no `store_download_again` case. Requires `AddDownloadButton` to have run
// only for the row itself - the two transforms do not depend on each other's output.
bool RouteDownloadButtons(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// ---------------------------------------------------------------------------------------------
// P9-6 - the download press opens one modal popup
// ---------------------------------------------------------------------------------------------
//
// P5 shipped the transfer's feedback as a host-side line in the caption strip, drawn while the job
// ran and for 30 s after it ended. The owner's 2026-10-23 pass replaced that with one **modal**
// popup, in the shape P9-11's search loading panel already proved: a focus-owning panel over the
// details screen, the retail error popup's own resource, B cancelling, and the panel itself moving
// to its outcome state when the job reaches a terminal one - no second popup and no host drawing.
//
// `AddDownloadPopup` defines that panel (`(new UIPanel rbbz_download_panel …)`, the same
// `../error/error_general.milo` the search modal uses) and appends its symbol to
// `store_info_screen`'s `panels` list, so the record that carries the press is the record that
// carries the popup. Its methods:
//
//   show_popup ($job)  stores the job token, sets the loading sentence and the Cancel prompt,
//                      shows `all.grp` and takes the screen's focus;
//   poll               waits here for the job's terminal state
//                      (`state/<job>/installed|failed|cancelled`, the bridge grammar
//                      `store_download.h` owns): `installed` switches the labels to
//                      `Downloaded '<name>'.` and the Close prompt, `failed`/`cancelled` to the
//                      generic failure sentence; a `terminal` property makes the switch once;
//   hide_popup         hides, releases `active`, and gives the focus back to
//                      `store_info_panel`'s `buttons.lst`;
//   BUTTON_DOWN_MSG    B writes `rbbz-store/download/cancel/<job>` while the job is not terminal
//                      and closes - so a cancelled transfer never outlives the press - and A (see
//                      below) runs the in-session refresh in the `installed` state; every other
//                      action is consumed;
//   exit               leaving the screen with the popup up is a cancel as well (`active` and not
//                      `terminal`), which is the search modal's rule.
//
// The press itself is `RouteDownloadButtons`' body: it shows the popup with the offer's `job` key
// **before** it writes the start/again request, so the modal is up when the transfer begins.
//
// The outcome states are the panel's, not the strip's: the panel switches its own labels to the
// installed sentence and the installed prompt (or the generic failure sentence) exactly once.
//
// **P9-14 - the installed toast's A Refresh.** Once the job is `installed`, **A**
// (`kAction_Confirm`) runs the main menu's own refresh so the song the transfer just installed is
// listed in the same session:
//
//   (kAction_Confirm
//     {if [installed] {rbbz_download_panel hide_popup}}
//     {if [installed] {ui goto_screen server_connect_screen}})
//
// The native discovery route is the menu row's own. Close and navigate are
// siblings: a `do` around them loses everything after
// the navigation, because the screen change takes the evaluation with it (measured 2026-10-15). The
// guard is therefore repeated per statement: each one is skipped on its own, so a cancelled or
// failed job has no action behind the caption it does not draw, and no partial sequence can run.
// The connect panel starts the host rescan and waits before enumerating.
//
// `installed` is the second terminal property (TRUE in `finish_installed`, FALSE in `show_popup`
// and `finish_failed`), so a cancelled or failed job draws no **A**. The prompt line switches with
// it: `rbbz_download_refresh` reads **A Refresh   B Close**.
//
// Idempotent (the panel definition is the marker); refused, before any edit, when the record carries
// no `store_info_screen` definition or no `panels` list on it.
inline constexpr char kDownloadPanelName[] = "rbbz_download_panel";
inline constexpr char kDownloadLoadingToken[] = "rbbz_download_loading";
inline constexpr char kDownloadLoadingText[] = "Downloading from RhythmVerse...";
inline constexpr char kDownloadCancelToken[] = "rbbz_download_cancel";
inline constexpr char kDownloadCancelText[] = "Press <alt1>B</alt1> to Cancel";
inline constexpr char kDownloadCloseToken[] = "rbbz_download_close";
inline constexpr char kDownloadCloseText[] = "Press <alt1>B</alt1> to Close";
inline constexpr char kDownloadRefreshToken[] = "rbbz_download_refresh";
inline constexpr char kDownloadRefreshText[] = "<alt1>A</alt1> Refresh   <alt1>B</alt1> Close";
inline constexpr char kDownloadFailedToken[] = "rbbz_download_failed";
inline constexpr char kDownloadFailedText[] = "The download could not finish.";
bool AddDownloadPopup(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// Live ownership queries, package deletion and the leave-without-refresh confirmation.
// Uses the title's own error dialog scene and widgets.
bool AddLibraryManagement(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// The details page's Preview Audio row is built from `refresh_buttons`' own preview block, and its
// gate is `{&& is_song {offer has_data preview_audio} {is_allowed_to_preview_offer offer}}`. The
// record must carry `preview_audio` (its previous statement reads it unconditionally, and an absent
// key is a null read - see `Offer::preview_audio`), but an empty string satisfies `has_data`, so the
// row appeared on a served offer with nothing behind it (measured 2026-10-21). This removes the
// `store_preview_audio` insertion from `store_info_panel.refresh_buttons`; the rest of the method
// (which hands the preview manager an empty file name, a no-op) is left as retail wrote it.
// Idempotent; refused, before any edit, when the method is missing.
bool HidePreviewButton(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);

// Album art, the last link in the chain. `store_details_panel.refresh` sets
// `album.mat`'s texture to `cover_art_none.tex`, calls `{store_panel load_art <path> this}` and
// expects the completion to message `art_loaded`, whose handler sets the material to
// `{store_panel album_tex}`. Measured 2026-10-21 with a served texture: the file is inserted, the
// panel decodes it and the texture object is filled (256x256 DXT5, `has_bitmap=1`), but the
// completion skips the message - its guard reads the request object's state word and finds 2, not
// the 1 an asynchronous download would have. Our delivery is synchronous (the insert happens inside
// the request), so that guard can never be satisfied: this append runs the same assignment the
// callback would have, right after `load_art` returns (boot `rb_blitz_176.log`).
//
// Cold covers keep their preview_art path in the page. A selection requests that
// cover through the host bridge; poll_store_art waits for art/ready/<path> before
// loading and linking it. The placeholder stays until then. Only cur_preview_art
// can be linked, so a late cover for an earlier selection cannot replace it.
// Idempotent; refused, before any edit, when `store_details_panel.refresh` has no `load_art` call.
bool LinkAlbumArt(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// The details panel's difficulty read is `{offer get_data (rank <part>)}` -
// `store.dtb`'s `store_details_panel.refresh`, `{foreach part (guitar drum vocals bass keys)}`,
// which then converts each rank with `{song_mgr rank_tier rank part}` and hands the result to
// `{this set_inst_difficulty diff_<part> <tier> TRUE}`. That read is a **two-component** path, and
// the offer's `get_data` walk cannot resolve it against any five-part rank map: measured 2026-10-21
// by decoding the walk (`sub_82453928` -> `sub_824538A8` -> `sub_824534A8`), each component is
// interned and `sub_823FAE00` matches it against the FIRST element of a pair, so the second
// component asks the `rank` pair for a child whose first element is the part. A map can answer that
// for one part; for the rest the lookup returns nothing and the walk dereferences the null (boot
// `rb_blitz_165.log`: `read of guest 0x00000000` at the walk, stack `sub_824534A8` <-
// `sub_82453928` <- `sub_8275C6C0` <- the DTA evaluator).
//
// This rewrite replaces the loop with five explicit blocks that read the offer's own
// `(diff_<part>)` - the **tier** the catalogue publishes - and pass it to `set_inst_difficulty`
// unchanged, dropping the `rank_tier` conversion. Two measurements say that is the right seam:
// the call takes a tier (its argument is retail's own `$tier`), and the meter asset is built for
// the catalogue's 0..6 scale - five pips plus a devil icon
// (`ui/difficulty_meter/gen/diff_meter_all_inst.milo`: `diff_1.mesh` ... `diff_5.mesh`,
// `icon_devil.tex`), with 0 drawing the locale's `no_part`. The rank path would need
// `rank_tier`'s per-instrument thresholds, which this build does not carry; the catalogue already
// publishes the tier, so the display shows the catalogue's difficulty instead of a guess.
//
// Cost of that choice, stated plainly: an offer whose `diff_*` the API does not publish reads
// `NO PART` for that part, and `NO PART` is also the correct state for a part the song has no chart
// for (`diff_* = 0`). Idempotent; refused, before any edit, when `store_details_panel.refresh` has
// no `(rank <part>)` loop and no unrolled block.
bool UnrollOfferDifficulty(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// The store heading is retail's own locale entry `store` ("MUSIC STORE"); the owner's attribution
// requirement (§1.8) is met by a store that names its source. Replaces the entry's value whatever
// the locale currently says, because a translated heading is still retail's, not ours. Returns
// false and fills `why` when the locale does not carry the key.
inline constexpr char kStoreHeadingText[] = "RHYTHMVERSE";
bool SetStoreHeading(std::vector<ui::dtb::Node>* locale_roots, std::string_view heading,
                     std::string* why);

// ---------------------------------------------------------------------------------------------
// P9-7/P9-8 - the store's own footer: the Search Filters button, and no View Item caption
// ---------------------------------------------------------------------------------------------
//
// The retail store's footer is the scene's `InlineHelp` panel, and each store scene bakes a caption
// per pad action into itself (`store_menu.milo`: Confirm/Back/Start/Option; `store_browser.milo`:
// the same set for `store.ihp`; `store_info.milo` for the details page). Two of those captions are
// wrong for this build:
//
//   * **Start** reads `inline_help_options` ("HELP & OPTIONS") and its case jumps to retail's
//     `options_screen`. P9-7 re-points the button at this build's **Search Song Filter** page, so
//     the caption follows the destination;
//   * **Y** (`kAction_Option`) reads `store_offer` ("VIEW ITEM"). On the front menu the case opens a
//     trial upsell or the banner's marquee offer (we serve no marquee path), on the browser it opens
//     the provider's shortcut list (our provider supplies none) and on the details page it opens a
//     recommendation (`recommended.grp` is never shown). The caption therefore names an action that
//     can do nothing; P9-8 removes it from our surfaces.
//
// The commands are the retail idiom (`{<ihp> set_action_token <action> <token>}` /
// `{<ihp> clear_action_token <action>}`, as `song_select.dtb` and `store_info.dtb` spell them), and
// the object names are the scenes' own - measured 2026-10-23 with `python scripts/assets/hmx_milo.py list`:
// `store_menu.milo` declares `InlineHelp.ihp`, `store_browser.milo` declares `store.ihp`, and
// `store_info.milo` declares `store_info.ihp` and `view_offer.ihp`.

// The locale key and text the Start caption reads on both store screens (P9-7). The entry is added
// to every installed locale by `menu_payload.cpp`; `FILTERS` matches the retail entries' own
// upper-case spelling (`inline_help_options` = "HELP & OPTIONS"). It read `SEARCH FILTERS` until
// 2026-10-23; the owner shortened it, because the button edits the filter list - searching is the
// Search row's job.
inline constexpr char kFooterSearchFiltersToken[] = "inline_help_search_filters";
inline constexpr char kFooterSearchFiltersText[] = "FILTERS";

// P9-7's destination. Both store screens carry the shipped Start case -
//
//   {options_screen set back_screen store_menu_screen} {ui goto_screen options_screen}
//
// (the front menu's `BUTTON_DOWN_MSG` and the browser's, out/_swap/store.tree.txt:450-461 and
// :1061-1075) - and both are re-pointed at `filter_screen`, with the back target left as the store
// front menu retail already sets. The back target is runtime state, not a second page: a settings
// screen's Back reads `{{ui current_screen} get back_screen}` (src/ui/settings_screen.cpp's
// `Leave`), so this edit points the filter page at the store for *this* flow while Help & Options'
// own `EnterScreen` re-points it at the RhythmVerse page on its way in.
//
// Idempotent (a case that already goes to `filter_screen` is left alone); refused, before any edit,
// when either panel or its Start case is missing, or when the case carries neither the shipped
// `options_screen` pair nor this edit's own destination.
bool RouteStoreStartToFilter(std::vector<ui::dtb::Node>* store_ui_roots,
                             std::string_view filter_screen, std::string* why);

// P9-7's caption: `store_menu_panel.enter` and `store_browser_panel.refresh_list` are told to draw
// `start_token` for Start, on `InlineHelp.ihp` and `store.ihp` respectively. Idempotent; refused,
// before any edit, when either method is missing.
//
// **The front menu's command goes in a panel method, not the screen's `enter`.** Measured on the
// owner's boot of 2026-10-23: the same commands appended to `store_menu_screen.enter` changed
// nothing on screen, while the browser's (in `refresh_list`, a panel method) drew `SEARCH
// FILTERS` — the screen's `enter` runs before the panels' scenes are the objects a command
// resolves against, which is the rule `settings_screen.cpp` already records for retail scenes
// ("a statement in our own panel runs before `{ui goto_screen …}`, when the retail scene is not
// loaded yet"). `store_panel.enter`'s own `{microsoft.lbl set_showing FALSE}` is the measured
// precedent that a panel `enter` does reach its scene's objects. The command is inserted **first**
// in the method too, because the shipped `store_menu_panel.enter` ends in a `{cond …}` whose
// default arm is `{fail …}`.
bool SetStoreFooterStart(std::vector<ui::dtb::Node>* store_ui_roots, std::string_view start_token,
                         std::string* why);

// P9-8 on the two screens: `{InlineHelp.ihp clear_action_token kAction_Option}` in
// `store_menu_panel.enter` (first statement, for the reason above) and
// `{store.ihp clear_action_token kAction_Option}` at the end of `store_browser_panel.refresh_list`
// (after its own shortcut-header gate, so the caption cannot come back on a refresh). Idempotent;
// refused, before any edit, when either method is missing.
bool ClearStoreFooterOption(std::vector<ui::dtb::Node>* store_ui_roots, std::string* why);

// P9-8 on the details record (`ui/store/gen/store_info.dtb`): `store_info_panel.enter` clears
// `kAction_Option` on both inline-help objects its scene declares, because the page's Y case only
// opens a recommendation and this build never shows one. Idempotent; refused, before any edit, when
// the panel or its `enter` method is missing.
bool ClearDetailsFooterOption(std::vector<ui::dtb::Node>* store_info_roots, std::string* why);
}  // namespace rb_blitz::store
