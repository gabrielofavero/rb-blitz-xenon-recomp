// SPDX-License-Identifier: GPL-2.0-only
#include "settings/runtime_settings.h"
#include "diag/probe_trace.h"
#include "hooks/ultimate_plan.h"
#include "ui/menu_options.h"

rex::Setting<bool> rb_blitz::settings::title::enhancements_hide_achievements{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_hide_achievements",
false,
    "Hide achievement unlock notifications. Achievements are still earned and saved.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_expand_resolution{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_expand_resolution",
false,
    "R1 custom resolutions: expand the guest's own layout (menu background, HUD, "
                    "3D, song selection, power-up menu) for a non-16:9 video mode instead of "
                    "leaving it to the presenter's letterbox or safe-area path. Faithful: the "
                    "guest's 16:9 layout is drawn as authored. Not implemented.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_ui_scale{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_ui_scale",
false,
    "R2 UI accessibility: scale guest text (and the main menu logo) through the "
                    "game's own font/layout metrics. Faithful: the design's text and logo sizes. "
                    "Not implemented.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_skip_offline_dialog{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_skip_offline_dialog",
true,
    "R3 auto offline mode: take the failed-login and \"Proceed in Offline "
                    "Mode?\" questions out of the panel a game start goes through, so the title "
                    "shows the main menu in offline mode as soon as the start is pressed. "
                    "Faithful: the title asks both questions and the player answers them, which "
                    "is the same pair of transitions.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_icon_schemes{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_icon_schemes",
true,
    "R4 Controller Layout: choose installed controller glyph and preset art "
                    "in Help & Options. Faithful: the art the title ships.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_hide_menu_options{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_hide_menu_options",
true,
    "R5 hiding categories: remove named entries from the main menu's option list, "
                    "in the list the guest navigates and not only in the drawing. Needs "
                    "enhancements_hidden_menu_options to name them. Faithful: every shipped entry "
                    "is present.",
    rex::kRestart};

#if defined(RB_BLITZ_IOS) || defined(__ANDROID__)
rex::Setting<bool> rb_blitz::settings::title::hide_exit_game{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "hide_exit_game",
true,
    "Hide Exit Game on the main menu.",
    rex::kRestart};
#else
rex::Setting<bool> rb_blitz::settings::title::hide_exit_game{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "hide_exit_game",
false,
    "Hide Exit Game on the main menu.",
    rex::kRestart};
#endif

rex::Setting<std::string> rb_blitz::settings::title::enhancements_hidden_menu_options{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_hidden_menu_options",
rb_blitz::menu_options::kDefaultRows,
    "R5 the main menu rows enhancements_hide_menu_options hides, comma "
                      "separated. The compiled default is the legacy three-entry list: "
                      "splash_leaderboard, splash_achievements, splash_dlc.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_rename_mod_settings{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_rename_mod_settings",
true,
    "R10 rename the mod's settings row: draw the row the Ultimate mod adds as "
                    "\"Ultimate Settings\", which is the name the mod itself uses. Faithful: the "
                    "mod's own label, \"Mod Settings\".",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_refresh_music_library{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_refresh_music_library",
false,
    "R7's refresh entry: show the main menu's Refresh Songs row, which re-scans "
                    "the DLC folders and re-runs the discovery pass. Off by default. Off hides "
                    "the row; the boot's own scan is unaffected.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_dlc_uncap_enumeration{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_dlc_uncap_enumeration",
true,
    "Temporarily disable VSync while content loads, restoring the player's "
                    "value when the usable main menu is current. On by default. Off keeps "
                    "the player's pacing throughout.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_force_controller_scheme{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_force_controller_scheme",
false,
    "R8 force a predefined controller scheme on load. Needs "
                    "enhancements_controller_scheme to name one. Faithful: the player's saved "
                    "layout is used untouched. Not implemented.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::enhancements_controller_scheme{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_controller_scheme",
"",
    "R8 the scheme enhancements_force_controller_scheme applies; empty means "
                      "unnamed, and an unnamed scheme leaves the saved layout alone.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_menu_dlc_songs{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_menu_dlc_songs",
true,
    "R9 main-menu library songs: play bundled songs and installed DLC as background "
                    "music. The in-game Background Music row is the control; this is the cvar it "
                    "is written to, so an explicit flag or environment value still outranks it.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_background_music_nav{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_background_music_nav",
true,
    "Enable pad/key menu-music navigation. Music-player visibility is independent. "
                    "Requires library background music.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::enhancements_music_player{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_music_player",
true,
    "Show the clickable music player at the bottom right of the main menu. "
                    "Independent of pad/key navigation. Requires library background music.",
    rex::kRestart};

rex::Setting<int32_t> rb_blitz::settings::title::enhancements_menu_dlc_song_count{
    &rb_blitz::settings::TitleSettingOwner("blitz.enhancements"), "enhancements_menu_dlc_song_count",
3,
    "R9's retired pool-size row: music uses a rolling session and a bounded "
                     "mount cache, so this legacy value is ignored (a boot says so).",
    rex::Range{1, 64}, rex::kRestart};

#if defined(_WIN32)
rex::Setting<std::string> rb_blitz::settings::title::gpu_backend{
    &rb_blitz::settings::TitleSettingOwner("blitz.gpu"), "gpu_backend",
"any",
    "Graphics backend to render with: d3d12, vulkan, or any (whichever this "
                      "build has)",
    rex::Allowed{"d3d12", "vulkan", "any"}, rex::kUntilLaunch};
#else
rex::Setting<std::string> rb_blitz::settings::title::gpu_backend{
    &rb_blitz::settings::TitleSettingOwner("blitz.gpu"), "gpu_backend",
"vulkan",
    "Graphics backend to render with: vulkan",
    rex::Allowed{"vulkan"}, rex::kUntilLaunch};
#endif

rex::Setting<bool> rb_blitz::settings::title::probe_framerate{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_framerate",
false,
    "Log bounded one-second counts of completed guest main-loop draws and "
                    "guest present routine calls. Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_gameplay{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_gameplay",
false,
    "P0: log the native ButtonDown/ButtonUp -> kBMAction -> gameplay dispatch path. "
                    "Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_gameplay_bypass{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_gameplay_bypass",
false,
    "P0: make the native joypad mapping return no action (candidate native-preset "
                    "bypass). Pair with probe_gameplay to see the suppression. Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_gameplay_inject{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_gameplay_inject",
false,
    "P0: dispatch a fixed Lane0/Lane1/UsePowerUp sequence into the native action "
                    "boundary once a song controller exists. Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_gameplay_inject_track{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_gameplay_inject_track",
false,
    "P0: include TrackUp/TrackDown in the injected sequence. Each track action is "
                    "held long enough for the controller's own repeat, then released. Diagnostics "
                    "only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_gameplay_block_dispatch{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_gameplay_block_dispatch",
false,
    "P0: suppress every native entry into the action boundary (press and release) "
                    "while the song controller exists, covering all four producers. Direct probe "
                    "actions still dispatch. Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_gameplay_inject_spam{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_gameplay_inject_spam",
false,
    "P0: replace the fixed sequence with an alternating Lane0/Lane1 press-released "
                    "loop starting 8 s into the song, to test note judgement against the HUD "
                    "score. Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_gameplay_challenge{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_gameplay_challenge",
false,
    "P0: exercise dormant message and keyboard producers using guest-stack "
                    "fixtures. Pair with block_dispatch for before/after evidence. Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_gameplay_sources{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_gameplay_sources",
false,
    "P0: raw G/H or mouse left/right dispatch notes; N/M dispatch tracks. "
                    "Uses the live song guest thread. Diagnostics only, not saved mappings.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_overlay{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_overlay",
false,
    "Draw the probe overlay: the guest's requested video mode, the window and "
                    "present-path settings, and the newest probe trace events. Live - it is "
                    "re-read every frame"};

rex::Setting<bool> rb_blitz::settings::title::probe_trace{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_trace",
false,
    "Record probe events (guest entry points placed with RBBLITZ_PROBE and "
                    "breadcrumbs) into the ring buffer the overlay shows and DumpToLog writes"};

rex::Setting<int32_t> rb_blitz::settings::title::probe_trace_capacity{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_trace_capacity",
256,
    "How many probe events the ring buffer holds; the oldest are overwritten and "
                     "counted as dropped. The floor is the probe's own policy - fewer than 16 "
                     "events is not a trace - while the buffer itself accepts any non-zero size",
    rex::Range{16, int32_t(rb_blitz::diag::TraceBuffer::kMaxCapacity)}};

rex::Setting<std::string> rb_blitz::settings::title::probe_trace_path{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_trace_path",
"",
    "When set and probe_trace is on, the newest events are rewritten to this "
                      "file about twice a second, oldest first, so a script can read the trace "
                      "after a run without a console. Empty writes nothing"};

rex::Setting<bool> rb_blitz::settings::title::probe_screen_1080p{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_screen_1080p",
false,
    "P2 spike: make the title take its own half-resolution (960x540) screen path "
                    "before it computes its size, and report what it chose. Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_halfres_flag{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_halfres_flag",
false,
    "Diagnostics: force the title's half-resolution flag when the guest height is "
                    "540. Off (the default) forces only the height, which renders 1080p without the "
                    "two corner blocks the flag's own SD path draws; on restores the P11a route.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::probe_aspect{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_aspect",
"",
    "Diagnostics: decimal aspect ratio to write over the guest's 16:9 constant "
                      "(0x82078194) before the title builds its screen, e.g. 2.3703704. Empty "
                      "leaves the title's own constant alone.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_dump_guest_swap{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_dump_guest_swap",
false,
    "P2 spike: dump the guest present object and its Direct3D 9 pointer once, to "
                    "locate the title's real back-buffer size. Diagnostics only.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::probe_screen_fields{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_screen_fields",
false,
    "Diagnostics: find guest fields holding the 1280x720 layout size and overwrite "
                    "the width with the frame width each frame. Diagnostics only.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::probe_screen_fields_filter{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "probe_screen_fields_filter",
"",
    "Diagnostics: comma-separated guest addresses the screen-field poke may "
                      "write; empty means every candidate.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::audio_stream_catchup{
    &rb_blitz::settings::TitleSettingOwner("blitz.audio"), "audio_stream_catchup",
#if defined(__APPLE__)
                    true,
#else
                    false,
#endif
                        "Catch up stream buffering after long render frames",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::dlc_root{
    &rb_blitz::settings::TitleSettingOwner("blitz.runtime"), "dlc_root",
"",
    "DLC directory, laid out as <title_id>/<content_type>/<package> "
                      "(default <game_data_root>/dlc, relative paths resolve against "
                      "game_data_root). Packages are mounted where they lie, read-only.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::dlc_library{
    &rb_blitz::settings::TitleSettingOwner("blitz.runtime"), "dlc_library",
"",
    "Flat DLC library directories, ';'-separated: folders of STFS "
                      "packages nested any way, each mounted under the title id and "
                      "content type its own header names. Relative paths resolve against "
                      "game_data_root. A dlc_root that is not the structured layout is "
                      "scanned as a library too.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::dlc_library_content_type{
    &rb_blitz::settings::TitleSettingOwner("blitz.runtime"), "dlc_library_content_type",
"",
    "Content type to present every library package under, as 8 hex "
                      "digits (e.g. 00000002). Empty (the default) presents only the "
                      "saved-game packages (00000001) as marketplace content (00000002), "
                      "which is what makes a dumped custom-song folder listable and "
                      "playable; the other packages keep their own type.",
    rex::kRestart};

rex::Setting<uint32_t> rb_blitz::settings::title::dlc_scan_threads{
    &rb_blitz::settings::TitleSettingOwner("blitz.runtime"), "dlc_scan_threads",
1,
    "Threads used to read a DLC library's package headers: 1 (the "
                      "default) reads them on the calling thread; 0 uses the machine's "
                      "core count, capped at 8; any other value is that many workers. "
                      "The scan's result is identical at any thread count.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::hud_reanchor{
    &rb_blitz::settings::TitleSettingOwner("blitz.graphics"), "hud_reanchor",
true,
    "Re-anchor the in-song HUD on non-16:9 frames during world-transform "
                    "resolution, independently of HUD element visibility.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::hud_reanchor_apply{
    &rb_blitz::settings::TitleSettingOwner("blitz.graphics"), "hud_reanchor_apply",
true,
    "Place the six measured HUD anchors; their children inherit the shift.",
    rex::kRestart};

rex::Setting<double> rb_blitz::settings::title::hud_reanchor_hud_half_height{
    &rb_blitz::settings::TitleSettingOwner("blitz.graphics"), "hud_reanchor_hud_half_height",
240.0,
    "HUD scene half-height in authored units (240, measured 2026-10-17).",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::hud_reanchor_probe{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "hud_reanchor_probe",
false,
    "Log a bounded sample of HUD world-transform placements.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::hud_reanchor_scan{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "hud_reanchor_scan",
false,
    "Retired HUD heap-scan diagnostic; accepted for compatibility, no work.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::local_save{
    &rb_blitz::settings::TitleSettingOwner("blitz.compatibility"), "local_save",
true,
    "Persist offline stars, scores and controller mapping locally, and "
                    "autosave dirty audio/video settings. Off restores the retail offline "
                    "behaviour, whose player save destination was Rock Central.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::settings_bridge{
    &rb_blitz::settings::TitleSettingOwner("blitz.settings"), "settings_bridge",
true,
    "In-game settings channel: answer the guest's own file_exists/read_file/write_file "
    "builtins when (and only when) the path is in the rbbz-settings/ namespace, "
    "so the native Advanced Settings screen can read and write host settings. "
    "Faithful: no content file is named in that namespace, so every call is "
    "forwarded unchanged.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::settings_bridge_forward_only{
    &rb_blitz::settings::TitleSettingOwner("blitz.settings"), "settings_bridge_forward_only",
false,
    "Diagnostic for the settings channel: forward every call, including paths in "
                    "our own namespace, so an answering hook can be compared against a pure "
                    "no-op forward in one boot. Faithful: the channel answers and the guest gets "
                    "its value.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::settings_bridge_provider_active{
    &rb_blitz::settings::TitleSettingOwner("blitz.settings"), "settings_bridge_provider_active",
false,
    "Diagnostic for P5b's merged Help & Options root: pre-commit "
                    "`provider.active = true` in the in-memory store, so the provider gate query "
                    "answers true and a boot shows the six-row root that an installed Ultimate "
                    "provider produces, instead of the five-row base root. P6 replaces this with "
                    "the adapter's real readiness, which is why it is a diagnostic and not a "
                    "setting. Read at Configure() from `REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE`, "
                    "because the cvar registry is not finalised that early.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::settings_hot_apply_diagnostic{
    &rb_blitz::settings::TitleSettingOwner("blitz.settings"), "settings_hot_apply_diagnostic",
false,
    "Diagnostic for P9's live apply: put a hot row through the real commit path "
                    "once at boot (set + commit on the guest channel), so a boot log proves the "
                    "row was applied to the running process rather than only saved. It writes "
                    "the row's own cvar, so it changes this run - which is why it is a "
                    "diagnostic and off by default. Read at Configure() from "
                    "`REX_SETTINGS_HOT_APPLY_DIAGNOSTIC`.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::settings_ultimate_refresh_trial{
    &rb_blitz::settings::TitleSettingOwner("blitz.settings"), "settings_ultimate_refresh_trial",
false,
    "Trial guarded Ultimate guest assignments after a durable settings commit. "
                    "The setting remains restart-scoped until its consumer is verified.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::background_music_nav_probe{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "background_music_nav_probe",
"",
    "One main-menu command after background_music_nav_probe_delay seconds: next, previous, play_pause."};

rex::Setting<int32_t> rb_blitz::settings::title::background_music_nav_probe_delay{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "background_music_nav_probe_delay",
5,
    "Seconds before the one-shot music navigator probe."};

rex::Setting<bool> rb_blitz::settings::title::background_music_nav_pause_trial{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "background_music_nav_pause_trial",
false,
    "Legacy pause-trial flag: log paused sample progress each second. Native pause no longer requires this flag."};

rex::Setting<int32_t> rb_blitz::settings::title::ultimate_mode{
    &rb_blitz::settings::TitleSettingOwner("blitz.compatibility"), "ultimate_mode",
1,
    "Rock Band Blitz Ultimate: 0=off, 1=auto, 2=force",
    rex::Range{0, 2}, rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::ultimate_payload_root{
    &rb_blitz::settings::TitleSettingOwner("blitz.compatibility"), "ultimate_payload_root",
"",
    "Rock Band Blitz Ultimate payload directory (default "
                      "<game_data_root>/ultimate, relative paths resolve against "
                      "game_data_root)",
    rex::kRestart};

rex::Setting<uint32_t> rb_blitz::settings::title::ultimate_patches{
    &rb_blitz::settings::TitleSettingOwner("blitz.compatibility"), "ultimate_patches",
rb_blitz::ultimate::kPatchAll,
    "Rock Band Blitz Ultimate default.xex edits to reapply: "
                      "1=content device, 2=song blacklist, 4=disk error latch",
    rex::Range{0, rb_blitz::ultimate::kPatchAll}, rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::mouse_ui_nav{
    &rb_blitz::settings::TitleSettingOwner("blitz.input"), "mouse_ui_nav",
true,
    "Legacy profile switch; mouse input is always enabled."};

rex::Setting<bool> rb_blitz::settings::title::mouse_native_trace{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "mouse_native_trace",
false,
    "Trace native menu geometry and selection"};

rex::Setting<bool> rb_blitz::settings::title::keyboard_support{
    &rb_blitz::settings::TitleSettingOwner("blitz.input"), "keyboard_support",
true,
    "Legacy profile switch; keyboard input is always enabled."};

rex::Setting<bool> rb_blitz::settings::title::input_source_trace{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "input_source_trace",
false,
    "Trace the gameplay source sampler: table, action, edge and the binding that "
                    "fired it."};

rex::Setting<std::string> rb_blitz::settings::title::touch_lane_swipe{
    &rb_blitz::settings::TitleSettingOwner("blitz.input"), "touch_lane_swipe",
"inverted",
    "Lane swipe direction: inverted or normal."};

rex::Setting<bool> rb_blitz::settings::title::touch_buttons{
    &rb_blitz::settings::TitleSettingOwner("blitz.input"), "touch_buttons",
true,
    "Show the pause button while touch is the active input."};

rex::Setting<std::string> rb_blitz::settings::title::touch_motion_deploy{
    &rb_blitz::settings::TitleSettingOwner("blitz.input"), "touch_motion_deploy",
"tilt",
    "Power-up motion: tilt, shake, or none."};

rex::Setting<bool> rb_blitz::settings::title::touch_preview{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "touch_preview",
false,
    "Preview the iOS guest controls on desktop, including a ready power-up."};

rex::Setting<std::string> rb_blitz::settings::title::startup_mode{
    &rb_blitz::settings::TitleSettingOwner("blitz.runtime"), "startup_mode",
"normal",
    "Startup: normal shows startup logos and Press A to Start; fast skips both and starts the local user automatically.",
    rex::Allowed{"normal", "fast"}, rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::song_load_crash_recovery{
    &rb_blitz::settings::TitleSettingOwner("blitz.runtime"), "song_load_crash_recovery",
true,
    "Restart into the main menu with a native error dialog after a fatal process exit during song loading.",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::song_load_recovered{
    &rb_blitz::settings::TitleSettingOwner("blitz.internal"), "song_load_recovered",
false,
    "One-shot crash-recovery boot reason. Supplied by the external load monitor.",
    rex::kUntilLaunch};

rex::Setting<bool> rb_blitz::settings::title::song_load_test_crash{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "song_load_test_crash",
false,
    "Exit with a synthetic crash code on the first song load to test the external restart. Removed on recovery.",
    rex::kUntilLaunch};

rex::Setting<std::string> rb_blitz::settings::title::song_load_guard{
    &rb_blitz::settings::TitleSettingOwner("blitz.runtime"), "song_load_guard",
"off",
    "Restart to the main menu when a song load exceeds this deadline; applies to the next load.",
    rex::Allowed{"off", "30s", "60s"}};

rex::Setting<int32_t> rb_blitz::settings::title::song_load_timeout{
    &rb_blitz::settings::TitleSettingOwner("blitz.internal"), "song_load_timeout",
0,
    "One-shot timeout recovery reason, supplied by the load monitor.",
    rex::kUntilLaunch};

rex::Setting<bool> rb_blitz::settings::title::song_load_test_stall{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "song_load_test_stall",
false,
    "Block the first song job before native work, to test timeout restart. Removed on recovery.",
    rex::kUntilLaunch};

rex::Setting<bool> rb_blitz::settings::title::song_load_trace{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "song_load_trace",
false,
    "Read-only song-load lifecycle trace: load setup, job start, native completion, "
    "unload and UI heartbeat. Does not cancel loads or recover guest faults.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::messages_api_url{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "messages_api_url",
"",
    "Host-only feed URL override. Empty uses the project's GitHub Pages feed; "
                      "an HTTP loopback /feed.json URL is accepted for local acceptance tests",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::messages_browser_opener_path{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "messages_browser_opener_path",
"",
    "Host-only absolute path to a browser stand-in executable for Messages "
                      "acceptance runs. It receives the fixed Releases URL as its sole argument.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::launcher_profile{
    &rb_blitz::settings::TitleSettingOwner("blitz.runtime"), "launcher_profile",
"",
    "Path to the settings profile, when Controller Settings started this "
                      "process; empty resolves the file it would write itself"};

rex::Setting<bool> rb_blitz::settings::title::profile_controller_rows_only{
    &rb_blitz::settings::TitleSettingOwner("blitz.compatibility"), "profile_controller_rows_only",
true,
    "Apply only the controller rows (mouse_ui_nav, input_backend, "
                    "keyboard_support) and the host-only host rows (monitor, "
                    "safe areas, dither, overscan cutoff, audio) from the profile; every other "
                    "row is preserved in the file and not applied"};

rex::Setting<bool> rb_blitz::settings::title::settings_safe_graphics{
    &rb_blitz::settings::TitleSettingOwner("blitz.settings"), "settings_safe_graphics",
false,
    "Start with safe graphics without deleting saved preferences.",
    rex::kUntilLaunch};

rex::Setting<bool> rb_blitz::settings::title::store_art_off{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_art_off",
false,
    "Serve no album art: the offer records carry no `preview_art`, so the retail "
                    "panel keeps its own placeholder art. Also stops the art worker from fetching "
                    "cover images. On by default because the art is what P3's exit asks for",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::store_art_dump_dir{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_art_dump_dir",
"",
    "The art worker's scratch directory. Every encoded cover is written there as "
                      "`<key>.png_xbox`, and a file already present for a key is **served instead** "
                      "of being fetched - so the exact bytes the panel receives can be decoded "
                      "offline (`python scripts/assets/hmx_tex.py export <key>.png_xbox --out cover.png`), "
                      "and a shipped `.png_xbox` copied in is a measurement of the serving path "
                      "against a texture retail accepts. Empty does neither",
    rex::kRestart};

rex::Setting<int32_t> rb_blitz::settings::title::store_catalogue_ttl_seconds{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_catalogue_ttl_seconds",
300,
    "How long the fetched RhythmVerse catalogue stays fresh (RhythmVerse plan "
                     "P7's cache, the Recent list's own freshness): a store visit inside this "
                     "window re-uses the list instead of fetching it again. Pages are cached "
                     "separately, see store_page_cache_ttl_seconds",
    rex::kRestart};

rex::Setting<int32_t> rb_blitz::settings::title::store_catalogue_pages{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_catalogue_pages",
4,
    "How many API pages one store action accumulates (25 records each, the site's "
                     "own page size; clamped 1..20). Recent fills the retail browser's single list "
                     "from this many pages - retail has no paging of its own - and the value is "
                     "also the per-action budget the search is given (store_search_pages). The cap "
                     "keeps a boot's traffic bounded; the walk continues past a page the "
                     "instrument filter emptied, because the server's cursor, not the visible "
                     "offer count, decides where the list ends",
    rex::kRestart};

rex::Setting<int32_t> rb_blitz::settings::title::store_page_cache_ttl_seconds{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_page_cache_ttl_seconds",
300,
    "How long one fetched RhythmVerse API page stays in the in-memory page cache "
                     "(RhythmVerse plan P7's ~5 min; 0 turns the cache off, so every visit asks the "
                     "site again). The key is the request body, so the query text, the page number "
                     "and the instrument filter each identity a page; single flight spans Recent "
                     "and Search, so the same page is never requested twice at once",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::store_catalogue_off{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_catalogue_off",
false,
    "Never fetch the RhythmVerse catalogue (the store then answers every menu with "
                    "an empty page); off is the ordinary boot",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::store_api_base{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_api_base",
"",
    "Point the list and search requests at another API origin - an absolute "
                      "http(s)://host[:port] with no path (RhythmVerse plan P9-10's failure "
                      "measurement: an origin that refuses makes the press draw the popup and ask "
                      "for a fresh walk, and a local stub can be flipped to serving inside the "
                      "same boot); empty keeps https://rhythmverse.co. The download and artwork "
                      "resolution stay same-site and do not follow it",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::store_serve_dir{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_serve_dir",
"",
    "Serve the retail store's netcache records from this host directory (a file "
                      "named exactly like the requested path, e.g. "
                      "dlc_store/europe/dlc_top_xbox_eng.dta); empty uses the built-in records",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::store_fetch_deliver{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_fetch_deliver",
true,
    "Deliver a served store record to the panel through the retail notification "
                    "(the file cache's own `file_req_complete` call). On by default because the store "
                    "has nothing to show otherwise: offline the cache's request body never notifies. "
                    "Only a record this module actually serves is affected, and serving itself is "
                    "gated on the RhythmVerse master switch, so a stock boot is untouched",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::store_offer_type_symbol{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_offer_type_symbol",
true,
    "Serve each offer's `type` as a DTA symbol (`(type song)`) instead of a string. "
                    "The symbol form is what makes the retail provider answer the browser's row "
                    "labels (measured 2026-10-21); the null read that used to follow the labels was "
                    "the details panel's `(rank <part>)` path, now unrolled onto `rank_<part>` keys "
                    "by the payload (store_payload.h). Off is the pre-P3 blank-row behaviour.",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::store_front_menu_row{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_front_menu_row",
"no-count",
    "How a served front-menu row spells its count (store_metadata.h's "
                      "FrontMenuRowForm): `no-count` marks the row with retail's own "
                      "`random_song` fourth element, which is the only form whose count label is "
                      "never fed a number; `count` is the pre-P9-5 `(path label TRUE)` that drew "
                      "`1`, `false` and `zero` are the other spellings of the same count",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::store_trace{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_trace",
false,
    "Log the store paths the guest resolves and opens under the game root "
                    "(RhythmVerse plan P2's data-layer probe)"};

rex::Setting<std::string> rb_blitz::settings::title::store_local_server_root{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_local_server_root",
"",
    "Point the retail store's netcache at a local server rooted at this "
                      "guest-relative directory (RhythmVerse plan P2's local-server spike; empty "
                      "leaves config/gen/store.dtb as shipped)",
    rex::kRestart};

rex::Setting<std::string> rb_blitz::settings::title::store_search_seed{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_search_seed",
"",
    "Seed the RhythmVerse search field with this text at boot (RhythmVerse plan "
                      "P4's measurement switch; empty leaves the field empty)",
    rex::kRestart};

rex::Setting<int32_t> rb_blitz::settings::title::store_search_pages{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "store_search_pages",
4,
    "How many API pages one search may read (clamped 1..20; the site's page size "
                     "is 25 records). RhythmVerse plan P7's per-action budget: the walk continues "
                     "past a page the eligibility/instrument filter emptied, because only the "
                     "server's cursor decides where the results end. 1 is the pre-P7 behaviour",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::portrait_render_cutoff{
    &rb_blitz::settings::TitleSettingOwner("blitz.diagnostics"), "portrait_render_cutoff",
true,
    "Skip hidden main-frame pixels during portrait gameplay; disable for comparison",
    rex::kRestart};

rex::Setting<bool> rb_blitz::settings::title::settings_help_overlay{
    &rb_blitz::settings::TitleSettingOwner("blitz.settings"), "settings_help_overlay",
true,
    "Show a short description of the highlighted row at the bottom of the screen "
                    "while a settings menu is open (Help & Options and the retail pages it "
                    "opens). The same switch as Game -> Extra Features -> Settings Labels, applied live. "
                    "Faithful: off draws nothing at all."};

rex::Setting<std::string> rb_blitz::settings::title::settings_help_size{
    &rb_blitz::settings::TitleSettingOwner("blitz.settings"), "settings_help_size",
"normal",
    "Settings description size: normal, big, or biggest. Applied live."};


rex::Setting<bool> rb_blitz::settings::title::window_resizable{
    &rb_blitz::settings::TitleSettingOwner("blitz.window"), "window_resizable", true,
    "Allow the user to resize the game window", rex::kUntilLaunch};
