# Portrait mode screen coverage

Status: planning, 2026-11-23. Static inventory complete; remaining adaptation and visual acceptance are pending.

The main gaps are the **results flow, pause menu, store browsing/details, Messages,
recommendations, leaderboards, and tutorials**. My Songs, powerup selection,
settings, calibration, and credits already have portrait work. Adapting a library
tab or a shared popup does not adapt the screen behind it.

## Scope and status

Map every retail screen registration, generated port surface, modal, and transient
state so uncommon routes stay visible. Group related variants instead of creating
a separate implementation task for each screen.

- **Adapted**: explicit portrait layout or responsive presentation exists in code.
- **Partial**: shared fitting, some components, or an implementation in progress exists; the whole flow is unverified.
- **Missing**: no dedicated portrait adaptation found; existing landscape fitting is the fallback.
- **Conditional**: retained retail/Ultimate/trial route; current reachability needs confirmation.
- **Transition**: routing or shared infrastructure; assess its visible state with its owning flow.

These are implementation statuses, **not visual acceptance**. No game/UI was driven
for this audit. The retail records contain 59 named screen definitions; those
names are all represented below. Panels, first-run states, and generated settings
pages add surfaces beyond that count. A declared screen is not proof it is reachable.

## Screen map

### Startup and shared dialogs

| Screen / surface | Current portrait coverage | Missing or still to check |
| --- | --- | --- |
| Startup logos/movies and Ultimate Mods startup image | **Partial**. Ultimate startup image has portrait scaling; startup skip modes exist. | Audit each retained logo/movie, transitions, and Normal/Fast startup variants. |
| Autosave warning — `autosave_warning_screen` | **Adapted**. Spinner proportions, wrapped warning, and measured centering. | Long localized text and safe areas. |
| Title/Press Start and main menu — `splash_screen` | **Adapted**. Logo, skyline, six menu rows, fonts, and button surfaces. | Every enabled shortcut combination, scrolling, and the music player. |
| Connection, offline prompts, content discovery, graphics preparation — `server_connect_screen` | **Adapted**. Wrapped status, progress, percentage, Back/Skip/Continue actions. | All failure/retry/ignore states; offline prompts enabled as well as skipped. |
| Native confirmations — `confirm_exit_game_screen`, settings confirmations, quit/restart dialogs | **Adapted shared scene**. Width fitting preserves the shade and entry animation. | Long bodies and all action layouts. Does not establish that the surrounding pause/tutorial screen is adapted. |
| Error popup; RhythmVerse search/download/library-management popups | **Adapted shared scene**. Error/search popups use fitted error art; download/library popups also have a dedicated portrait store scene being implemented. | Progress, cancel, success/failure, delete confirmation, Load Guard recovery, and overlapping modal focus. Validate the new store dialog separately. |
| Full error flow — `error_screen` | **Missing**. Separate `error_screen.milo`, unlike the adapted `error_general` popup. | Establish current triggers and fit the complete error screen. |
| Exit — `game_exit_screen`; content refresh, autosave spinner, fades/cache/transition panels | **Transition**. Existing lifecycle/shared panels. | Inspect visible refresh/save/fade states; no separate redesign needed for invisible routing. |

### Library, store, and social tabs

| Screen / surface | Current portrait coverage | Missing or still to check |
| --- | --- | --- |
| My Songs — `song_select_screen` | **Adapted**. Header/tab arrows, Sort/Search tools, grouped list, wrapped titles, song card, and footer. | All sort/group modes, random/empty/large libraries, long metadata, and unavailable songs. |
| My Songs Shortcuts, search results, and text-entry keyboard | **Adapted**. Private shortcut/search list resources; responsive existing keyboard dialog. | Keyboard appearance/dismissal, no results, editing/cancel, and return focus. Shared keyboard support does not adapt store search results. |
| Store/RhythmVerse landing and browser — `store_menu_screen`, `store_browser_screen` | **Partial; implementation in progress**. Workspace changes add native portrait store scenes, list resources, tab arrows, song card, and footer. Build/behavior acceptance is not established by this audit. | Categories, catalogue/search lists, filters, paging, installed states, loading/empty/offline/error views. |
| Store offer details — `store_info_screen`, `store_info_unavailable_screen` | **Partial; implementation in progress**. Workspace changes add portrait offer metadata/actions and a private download/management dialog. | Artwork, metadata, actions, unavailable offers, installed/delete states, and return navigation; verify which variants inherit the new scenes. |
| Store preparation/enumeration — `prep_offer_for_store_screen`, `store_info_enum_screen` | **Transition** within the unfinished store flow. | Check fetching/enumeration/wait/failure visuals and the destination after cancellation. |
| Messages/update feed — `inbox_screen` | **Missing**. Feed/provider work exists independently of portrait. | List, selected message/body/actions, unread state, empty/error/loading states, and long content. |
| Recommended songs — `recommendation_screen`; challenge-request panel | **Missing**. Recommendations and challenge panels have no dedicated portrait layout found. | Cards, actions, empty/offline states, and challenge confirmation. Determine which legacy actions remain reachable. |
| Career leaderboard — `career_leaderboard_screen`; song leaderboard panel | **Missing**. Song leaderboard is also embedded in other flows. | Offline notice, tabs/rows, personal rank, and embedded board placement. Online service restoration is outside scope. |

### Song preparation, gameplay, pause, and results

| Screen / surface | Current portrait coverage | Missing or still to check |
| --- | --- | --- |
| Powerup selection — `powerup_select_screen` | **Adapted**. Three choices per page, categories, song/difficulty card, Details view, and Back/Play footer. | All categories, no-powerup/locked/unlocked/cost states, paging, and launch/replay paths in retail and Ultimate. |
| Powerup Details modal | **Adapted**. Wrapped localized name/description, Close, and modal input ownership. | Long descriptions and correct selection restoration without equipping. |
| Difficulty selector — `difficulty_select_screen` / `difficulty_select_panel` | **Conditional; missing** portrait layout. | Prove which current song/mode paths use it before adapting or explicitly excluding it. |
| Song loading and ready-to-begin — `loading_screen` | **Adapted**. Tip, powerups, progress/spinner, and Begin prompt occupy separate bands. | Long/localized tips, every powerup combination, failure/recovery, and prompt device changes. |
| Gameplay — `game_screen`, HUD, and touch controls | **Adapted**, with centered landscape-source crop, individual HUD placement, and safe-area adjustments. | Every optional HUD element enabled, all powerup effects, remedial/tutorial messages, song-end transitions, and touch/device rotation. Hidden-by-default HUD elements still need coverage. |
| Pause — `pause_panel` on `game_screen` | **Missing** for the main pause menu. Portrait child settings do not cover pause itself. | Resume/restart/quit, root/options list, backdrop, input hints, and mixed Interface/Gameplay orientations. |
| Quit/restart — `pause_confirm_quit_song_screen`, `pause_confirm_restart_song_screen` | **Adapted shared confirmation**, with surrounding pause/game panels still pending. | Both cancel and confirm; preserve song state and orientation on return. |
| New-game/tutorial handoff — `pause_to_new_game_screen` | **Transition**. Uses the night background and routes to loading/tutorials. | Verify the visible handoff and cancellation with pause/tutorial work. |
| Song summary — `song_results_screen` | **Missing**. | Score, stars, song/artist, actions, and long metadata. |
| Career progression — `career_results_screen` | **Missing**. | Rank/progression, coins, and progression animations. |
| Leaderboard results — `leaderboard_results_screen` | **Missing**. | Board content, offline state, and navigation to adjacent results pages. |
| Detailed breakdown — `results_breakdown_screen` | **Missing**. | Per-instrument/powerup scoring, totals, and overflow. |
| Unlock presentation — `results_unlock_screen` | **Missing**. | Every unlock type, multiple rewards, and continue actions. |
| Submission and apology/failure — `submit_results_screen`, `apology_results_screen` | **Missing**. | Submission/wait/offline/failure states; make these readable even without online services. |
| Shared results shell — `results_shared_screen`, results/shared/cache panels | **Missing** for visible shell elements; caching is **Transition**. | Shared headings, navigation, backdrop, replay/change-powerups/return-to-library routes. Adapt the complete results chain together. |

### Help, settings, and retained routes

| Screen / surface | Current portrait coverage | Missing or still to check |
| --- | --- | --- |
| Help & Options — retail `options_screen` replacement and generated settings tree | **Adapted**. Shared six-row layout, title, footer, and measured caption strip; main-menu and pause-context scene aliases. | Every generated page type: choices, More pages, mapping/bind capture, restore/reset, Keep/Revert, restart prompts, and all caption sizes. |
| Audio/video — `av_options_screen`, `av_options_pause_screen`; render-scale wheels | **Adapted**. Repositioned native controls and portrait scene aliases. | Slider extremes, child pages, and back navigation in both contexts. |
| Controller presets — `controller_config_screen`, `controller_config_pause_screen` | **Adapted**. Larger diagram/callouts, selector arrows, targets, and hover states. | Every preset/controller-art family and input-device changes. |
| Calibration welcome, audio/video, hardware, manual/numeric, quick, completion and failure | **Adapted**. Separate artwork/text bands, dynamic footer, localized touch wording, and centered success. | Hardware-only states, retry/failure, longest locale, and saved-offset results. Exact registrations are listed below. |
| Credits — `credits_screen`, `pause_credits_screen` | **Adapted**. Portrait list widths/fonts and footer. | Full scroll, two-column entries, Ultimate credits, and return from pause. |
| How to Play — `tutorials_screen`, `tutorials_pause_screen`, `tutorials_pause_confirm_exit_screen`; first-run/remedial help | **Missing** for tutorial content. Shared confirmation art only covers the exit prompt. | Every tutorial page/step, in-song lesson overlays, first library visit, and first-use powerup guidance. |
| Player profile — `profile_screen` | **Conditional; missing** portrait layout. | Confirm current local-profile/first-run entry paths and any visible selection or failure states. |
| Rock Band World link — `rbworld_link_screen` | **Conditional; missing** portrait layout. | Confirm entry visibility; preserve a readable unavailable/offline path if retained. |
| Trial/upsell/demo — `upsell_splash_screen`, `upsell_and_exit_screen`, `upsell_panel`, demo panels | **Conditional; missing** portrait layout. | Determine supported trial/retail/Ultimate reachability; explicitly exclude unsupported routes instead of silently forgetting them. |
| Ultimate Mods settings — `mod_settings_screen` and variant-specific panels | **Conditional**. Unified port settings are adapted; original Ultimate surfaces have no separate portrait coverage established here. | Inspect installed payload registration and reachable launch/menu routes; cover remaining native mod pages or record that they are replaced. |

Calibration registrations: `cal_welcome_screen`, `cal_audio_screen`,
`cal_visuals_screen`, `cal_hw_audio_screen`, `cal_hw_visuals_screen`,
`cal_manual_video_screen`, `cal_manual_video_postfailure_screen`,
`cal_manual_audio_screen`, `cal_manual_audio_postfailure_screen`,
`cal_complete_screen`, `cal_hw_complete_screen`, `cal_manual_complete_screen`,
`cal_quick_screen`, and `cal_manual_failed_screen`.

## Existing host surfaces to include

| Surface | Current coverage / remaining check |
| --- | --- |
| Achievements browser and unlock toasts | **Adapted**. Single-column portrait browser, safe-area layout, touch scrolling, and bottom-centered portrait achievement notifications. Check long descriptions and input/close behavior. |
| Background music player | **Adapted** compact portrait bar. Check overlap with each guest footer, safe areas, long titles, and menu-to-song transitions. |
| Settings captions, bind-capture/recovery notices | **Partial**. Caption measurement and bounded strips exist. Check Normal/Big/Biggest, longest labels, pause context, and modal ownership. |
| General notifications and diagnostic/probe surfaces | **Partial**. General notifications fit available width; dedicated safe-area/portrait acceptance is unproven. Check collisions and clipping; diagnostics are development-only. |

## Short completion plan

1. **Close the core loop:** results chain and pause root, including replay, restart,
   quit, and return to My Songs. These are the largest ordinary-play omissions.
2. **Finish the library tabs:** RhythmVerse/store browser and details, Messages,
   recommendations, and leaderboards, including their empty/offline/error states.
3. **Cover onboarding and rare routes:** tutorial content, first-use notices,
   full error screen, then resolve profile/difficulty/trial/RB World/Ultimate
   reachability and record explicit exclusions.
4. **Accept the whole route map:** narrow phone and wider tablet, asymmetric safe
   areas, touch/controller/keyboard/mouse, long locales, all four Interface/Gameplay
   orientation combinations, and retail/Ultimate. Include settings opened from
   pause and re-entry after modals, not just initial screen appearance.

D1: Keep invisible transitions grouped with their visible destination. D2: A shared
popup adaptation never closes its parent screen's gap. D3: Every conditional route
must end with either coverage or a documented reachability exclusion. D4: Preserve
guest UI routes and existing host surfaces; this plan does not introduce new overlays.

## Evidence and validation boundary

Static audit, 2026-11-23: decoded the installed retail `ui/**/*.dtb` records with
the existing `rb_blitz_settings_screen_tests dump-record` command through the
Windows test runner. Full output: `out/portrait-screen-inventory/retail-ui-records.txt`.
The workspace gained store portrait implementation changes during the audit:
`AddPortraitStoreLayout`, `PreparePortraitStore`, and `PreparePortraitStoreList`.
They are recorded as partial/in progress, without assuming a successful build or
runtime result. Those implementation files were not edited as part of this document.
Compared registrations against portrait authoring, panel classification, and host layouts:

- [Portrait screen/panel classification](../../../src/ui/portrait_menu.h),
  [asset authoring](../../../src/ui/input_font_scene.cpp), and
  [payload generation](../../../src/settings/menu_payload.cpp).
- [Native menu interaction](../../../src/input/native_menu.cpp),
  [store routes/modals](../../../src/store/store_payload.cpp), and
  [HUD placement](../../../src/hooks/hud_layout_runtime.cpp).
- [Achievements layout](../../../src/ui/achievement_layout.h),
  [music player layout](../../../src/ui/menu_music_player_policy.h), and
  [notification layout](../../../src/ui/notification_overlay.cpp).
- [Current safe-area/layout behavior](../../settings/graphics.md),
  [portrait library](../../engine/song-library-sorting.md#portrait-library), and
  [earlier alignment delivery](../finished/2026-11-19-portrait-ui-alignment.md).

The older graphics section titled “Portrait gameplay experiment” predates later
menu adaptations; its generic “other menus” fallback statement is not a current
screen inventory. This map uses current implementation evidence instead.

Documentation only: this audit changed no runtime code and performed no build or
visual acceptance. Existing automated evidence does not establish complete screen coverage.
Native Linux/macOS/iOS appearance and real-device input remain unverified by this audit.
