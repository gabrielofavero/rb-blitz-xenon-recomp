#!/usr/bin/env python3
"""Export the current settings hot-apply audit as JSON from the schema and adapter.

This is a static inventory. A proposed boundary is a hypothesis until the provider's
consumer callback and a same-process observation are recorded in the plan.
"""

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "src/settings/schema.cpp"
MODEL = ROOT / "src/settings/model.cpp"
ADAPTER = ROOT / "src/settings/ultimate_adapter.cpp"

# Source-level native read sites. These are static sites, not a claim that a
# particular running scene observes a later write. The provider sites come from
# the installed-DTB audit when --provider-audit is supplied.
NATIVE_CONSUMERS = {
    "kTarget": ("src/settings/boot.cpp", "initialization"),
    "kStartup": ("src/settings/boot.cpp", "initialization"),
    "kRefreshMusicLibrary": ("src/hooks/menu_filter.cpp", "menu_entry"),
    "kDlcScanThreads": ("src/hooks/dlc.cpp", "initialization"),
    "kLoadGuard": ("src/loading/crash_recovery.cpp", "poll_or_event"),
    "kDlcMenuSongs": ("src/hooks/shell_music.cpp", "initialization"),
    "kPlayCurrentSong": ("src/hooks/settings_payload.cpp", "menu_entry"),
    "kRhythmVerse": ("src/hooks/settings_payload.cpp", "menu_entry"),
    "kRhythmVerseGuitar": ("src/store/store_settings.cpp", "poll_or_event"),
    "kRhythmVerseBass": ("src/store/store_settings.cpp", "poll_or_event"),
    "kRhythmVerseDrums": ("src/store/store_settings.cpp", "poll_or_event"),
    "kRhythmVerseVocals": ("src/store/store_settings.cpp", "poll_or_event"),
    "kRhythmVerseKeys": ("src/store/store_settings.cpp", "poll_or_event"),
    "kHideAchievements": ("src/rb_blitz_app.h", "poll_or_event"),
    "kHideOfflinePrompts": ("src/hooks/menu_filter.cpp", "initialization"),
    "kResolution": ("src/hooks/render_settings.cpp", "initialization"),
    "kVsync": ("src/hooks/hot_apply_runtime.cpp", "poll_or_event"),
    "kRenderer": ("src/settings/boot.cpp", "initialization"),
    "kDisplay": ("src/settings/display_runtime.cpp", "poll_or_event"),
    "kAntiAliasing": ("src/hooks/render_settings.cpp", "initialization"),
    "kFiltering": ("src/hooks/render_settings.cpp", "initialization"),
    "kHideDownloadContent": ("src/hooks/settings_payload.cpp", "menu_entry"),
    "kShowMessages": ("src/settings/menu_payload.cpp", "panel_entry"),
    "kShowRecommendations": ("src/settings/menu_payload.cpp", "panel_entry"),
    "kShowCurrency": ("src/settings/menu_payload.cpp", "panel_entry"),
    "kBackgroundMusicNavigation": ("src/hooks/shell_music.cpp", "poll_or_event"),
    "kMusicPlayer": ("src/ui/menu_music_player.cpp", "poll_or_event"),
    "kHideMainMenu": ("src/hooks/menu_filter.cpp", "menu_entry"),
    "kHiddenMainMenu": ("src/hooks/menu_filter.cpp", "menu_entry"),
    "kSettingsLabels": ("src/hooks/hot_apply_runtime.cpp", "poll_or_event"),
    "kButtonScheme": ("src/hooks/hot_apply_runtime.cpp", "poll_or_event"),
}

# A restart row keeps its strategy until this specific ownership or observation
# gate is met. These are static release gates, not a claim that a candidate works.
RESTART_GATES = {
    "kTarget": ("Launch target selects a different guest content image.",
                "A separate supported in-process content switch replaces the process boundary."),
    "kStartup": ("Startup behavior is consumed during boot.",
                 "A repeatable same-process startup/menu transition applies both modes."),
    "kRefreshMusicLibrary": ("The menu-return row rebuild lacks full owner acceptance.",
                             "Verify hide/show, selection, input and reverse transitions on menu return."),
    "kDlcScanThreads": ("The boot scan captures its thread count, and no settings handoff updates the refresh worker.",
                        "Synchronize an idle-worker count snapshot before the next scan, with running-scan and rollback tests."),
    "kDlcMenuSongs": ("The menu-music pool and active stream mounts are built at Configure.",
                      "Swap the pool at a safe audio boundary without releasing active mounts."),
    "kPlayCurrentSong": ("The menu-return row rebuild lacks full owner acceptance.",
                         "Verify appearance, selection and reversal after returning to the menu."),
    "kRhythmVerse": ("The store master and menu route are boot-seeded separately.",
                     "Atomically refresh store access, header, menu row and disabled-state gates."),
    "kUnlockAll": ("Provider unlock state is seeded at boot and read by song selection.",
                   "Trace and verify the provider consumer after a same-process change and reversal."),
    "kHideAchievements": ("The SDK constructs its achievement dialog and callback once at module launch.",
                          "Own UI-thread dialog/listener reconfiguration and verify both visibility directions on new events."),
    "kResolution": ("Guest frame and backend GPU caches lack safe live reconstruction.",
                    "Prove screen ownership and transactional renderer reconfiguration per transition."),
    "kRenderer": ("Backend selection initializes the graphics worker and caches.",
                  "A separate backend teardown/recreation project proves a safe transition."),
    "kDisplay": ("Preview works, but the full transition/backend acceptance matrix is incomplete.",
                 "Verify all directed modes, rollback, placement, input and relaunch on both backends."),
    "kAntiAliasing": ("Every choice also writes native MSAA, which is captured by backend render-target caches; FXAA has no settings-owned GPU applier.",
                      "Expose an FXAA-only applier with value-specific strategy, receipt and rollback; verify each choice on both backends."),
    "kFiltering": ("Both backends derive sampler parameters at draws, but settings has no owned apply receipt or rollback.",
                   "Verify a narrow GPU-thread handoff and every filtering step and reversal on both backends."),
    "kHideDownloadContent": ("The menu-return row rebuild lacks full owner acceptance.",
                             "Verify removal, restoration, navigation and reversal on menu return."),
    "kShowMessages": ("Header refresh has a guest sentinel but lacks owner visual acceptance.",
                      "Verify all header combinations, badge, positions and reverse transitions."),
    "kShowRecommendations": ("Header refresh has a guest sentinel but lacks owner visual acceptance.",
                             "Verify all header combinations, navigation and reverse transitions."),
    "kShowCurrency": ("Currency visibility is built into the loaded store header.",
                      "Add an owned header refresh and verify label and placement on reversal."),
    "kHideOfflinePrompts": ("The menu filter patches prompt records at Configure; neither live prompt route has a policy refresh.",
                            "Add a guest-safe policy query for both routes and verify reversal without disturbing an open modal."),
    "kBackgroundMusicNavigation": ("Music navigation spans shell and menu input state.",
                                   "Refresh bindings and verify both input directions while audio plays."),
    "kMusicPlayer": ("Player visibility and shell availability have no acknowledged refresh.",
                     "Verify show/hide and reversal with an active stream at menu return."),
    "kHideMainMenu": ("The menu-return row rebuild lacks full owner acceptance.",
                      "Verify every eligible row, selected-row removal and restoration."),
    "kHiddenMainMenu": ("The menu-return selection rebuild lacks full owner acceptance.",
                        "Verify each selected subset, selection recovery and reversal."),
    "kHideHud": ("The provider constructs HUD objects at song entry.",
                 "Verify the guarded next-song assignment and both visibility directions."),
    "kGoldProgress": ("The provider's score callback trial lacks visual acceptance.",
                      "Verify the next star update and reversal in the same song."),
    "kProgressBar": ("The provider constructs this widget at song entry.",
                     "Verify the guarded next-song assignment and both visibility directions."),
    "kShowTimer": ("One same-process hide/show cycle passed only under the opt-in trial; production strategy and persistence remain unvalidated.",
                   "Verify later provider save, restart persistence, both settings contexts and inactive/mismatched providers before next-song promotion."),
    "kShowPb": ("A guarded assignment and guest receipt now run before the installed song-selection consumers, but visible effect and persistence remain unverified.",
                "Verify hide/show, later provider save, restart persistence and both settings contexts before next-selection promotion."),
    "kShowSteak": ("The provider constructs this widget at song entry.",
                   "Verify the guarded next-song assignment and both visibility directions."),
    "kInputViewer": ("The provider constructs this widget at song entry.",
                     "Verify the guarded next-song assignment and both visibility directions."),
    "kPrecisionMode": ("The scoring configuration reads the provider global at song load.",
                       "Add a guarded load boundary and verify judgment behavior in both directions."),
    "kDisableRandomWiggles": ("Track construction reads the provider global at song load.",
                              "Add a guarded track rebuild and verify both directions."),
    "kCameraAngle": ("The provider binds camera animations at song entry.",
                     "Verify each offered angle and reversal after guarded next-song assignment."),
    "kGameSpeed": ("Provider speed changes can desynchronize chart and audio transport.",
                   "Verify alignment and reversal at every offered speed after a safe boundary."),
    "kDisableDof": ("No non-row provider read was found in the installed 42 DTBs.",
                    "Identify the actual effect consumer before attempting a live handler."),
    "kDisablePlots": ("Plot palette expressions are consumed during song loading.",
                      "Add an owned plot rebuild and verify both directions next song."),
    "kForcePlot": ("Plot selection is consumed during song loading.",
                   "Add an owned plot rebuild and verify every installed choice and reversal."),
    "kForceSkybox": ("Skybox selection is consumed during song loading.",
                     "Add an owned skybox rebuild and verify every installed choice and reversal."),
    "kGems": ("The provider loads gem visuals during song entry.",
               "Verify every installed gem choice and reversal after guarded next-song assignment."),
}


def source_line(source, offset):
    return f"{source.relative_to(ROOT).as_posix()}:{source.read_text(encoding='utf-8').count(chr(10), 0, offset) + 1}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider-audit", type=Path,
                        help="JSON emitted by rb_blitz_settings_provider_audit")
    parser.add_argument("--p7-markdown", action="store_true",
                        help="print the current restart gate table for the owning plan")
    args = parser.parse_args()
    provider_audit = None
    provider_details = {}
    if args.provider_audit:
        provider_audit = json.loads(args.provider_audit.read_text(encoding="utf-8-sig"))
        if provider_audit.get("audit") != "static_provider_references" or \
                provider_audit.get("parsed_dtbs") != 42:
            raise ValueError("provider audit is incomplete or has the wrong format")
        provider_details = {item["variable"]: item for item in provider_audit["variables"]}
        if len(provider_details) != 18:
            raise ValueError("provider audit does not cover all 17 bound variables and unlockall")
    schema = SCHEMA.read_text(encoding="utf-8")
    model = MODEL.read_text(encoding="utf-8")
    adapter = ADAPTER.read_text(encoding="utf-8")
    table = schema[schema.index("constexpr StrategyEntry kStrategies[]"):schema.index("// Materializes the table")]
    names = dict(re.findall(r'case SettingId::(k\w+):\s*return "([^"]+)";', model))
    bindings = {}
    section = adapter[adapter.index("const std::vector<RowBinding>& AllBindings()"):adapter.index("// The persisted variable names")]
    for match in re.finditer(r'\{SettingId::(k\w+),\s*"([^"]+)",\s*"([^"]+)"', section):
        bindings[match.group(1)] = {"provider_token": match.group(2),
                                     "provider_variable": match.group(3),
                                     "binding_evidence": source_line(ADAPTER, adapter.index(section) + match.start())}

    menu = {"kRefreshMusicLibrary", "kPlayCurrentSong", "kHideDownloadContent", "kHideMainMenu",
            "kHiddenMainMenu", "kMusicPlayer", "kShowMessages", "kShowRecommendations",
            "kShowCurrency", "kRhythmVerse", "kDlcMenuSongs", "kBackgroundMusicNavigation"}
    song = {"kPrecisionMode", "kGameSpeed", "kDisablePlots", "kForcePlot", "kForceSkybox", "kGems"}
    immediate = {"kLoadGuard", "kVsync", "kSettingsLabels", "kButtonScheme",
                 "kRhythmVerseGuitar", "kRhythmVerseBass", "kRhythmVerseDrums",
                 "kRhythmVerseVocals", "kRhythmVerseKeys"}
    restart = {"kTarget", "kStartup", "kRenderer", "kResolution", "kDisplay",
               "kAntiAliasing", "kFiltering"}
    # Aggregate names from the installed provider archive inspection on 2026-11-06. No DTB
    # contents or snippets are exported. A read site is evidence of a possible boundary, not
    # proof that assigning the global refreshes an object already in a running scene.
    provider_consumers = {
        "kUnlockAll": ("ui/song_select/song_chosen", "next_song_candidate"),
        "kHideHud": ("ulti_game_enter (HUD object deletion)", "next_song_candidate"),
        "kGoldProgress": ("beatmatcher_callbacks (star update)", "consumer_refresh_candidate"),
        "kProgressBar": ("ulti_game_enter (widget visibility)", "next_song_candidate"),
        "kShowTimer": ("ulti_game_enter (widget visibility)", "next_song_candidate"),
        "kShowPb": ("ui/song_select (selection/song chosen)", "next_song_candidate"),
        "kShowSteak": ("ulti_game_enter (widget visibility)", "next_song_candidate"),
        "kInputViewer": ("ulti_game_enter (widget visibility)", "next_song_candidate"),
        "kPrecisionMode": ("config/scoring (judgment expressions)", "next_song_candidate"),
        "kDisableRandomWiggles": ("config/track (track construction)", "next_song_candidate"),
        "kCameraAngle": ("ulti_game_enter (animation refs)", "next_song_candidate"),
        "kGameSpeed": ("beatmatcher_callbacks (music speed)", "next_song_candidate"),
        "kDisableDof": ("provider consumer unresolved", "consumer_untraced"),
        "kDisablePlots": ("config/plot_info (palette expression)", "next_song_candidate"),
        "kForcePlot": ("config/plot_info (palette expression)", "next_song_candidate"),
        "kForceSkybox": ("config/plot_info (palette expression)", "next_song_candidate"),
        "kGems": ("ulti_game_enter; ulti_funcs/load_gems", "next_song_candidate"),
    }
    rows = []
    for match in re.finditer(r'\{SettingId::(k\w+),\s*ApplicationStrategy::(kHot|kMenuReturn|kNextSong|kRestart)', table):
        ident = match.group(1)
        if ident in ("kLetterbox", "kUnlockOnlineAchievements"):
            continue  # legacy/retired row and one-shot action, not player-facing preferences
        position = schema.index(table) + match.start()
        if ident in immediate:
            boundary = "now"
        elif ident in menu:
            boundary = "menu_return_candidate"
        elif ident in song:
            boundary = "next_song_candidate"
        elif ident in restart:
            boundary = "process_restart"
        else:
            boundary = "consumer_untraced"
        binding = bindings.get(ident)
        provider_consumer = provider_consumers.get(ident)
        if provider_consumer:
            boundary = provider_consumer[1]
        consumer = ("src/hooks/hot_apply_runtime.cpp" if ident in immediate else
                    "src/hooks/settings_payload.cpp; src/hooks/menu_payload.cpp" if ident in menu else
                    "src/settings/boot.cpp; src/hooks/render_settings.cpp" if ident in restart else
                    "provider consumer untraced" if binding else
                    "native consumer untraced")
        if provider_consumer:
            consumer = provider_consumer[0]
        native_site = NATIVE_CONSUMERS.get(ident)
        if native_site:
            consumer = native_site[0]
        detail = provider_details.get(binding["provider_variable"] if binding else
                                      "unlockall" if ident == "kUnlockAll" else "")
        sites = ([{"file": native_site[0], "phase": native_site[1], "kind": "native_source_site"}]
                 if native_site else [])
        if detail:
            sites = [{**site, "kind": "compiled_dtb_variable_reference"}
                     for site in detail["references"]]
        gate = RESTART_GATES.get(ident)
        strategy = {"kHot": "hot", "kMenuReturn": "menu_return",
                    "kNextSong": "next_song", "kRestart": "restart"}[match.group(2)]
        if strategy == "restart" and gate is None:
            raise ValueError(f"missing restart gate for {ident}")
        rows.append({
            "id": ident, "token": names.get(ident, "unknown"),
            "visible_contexts": ["main_menu_settings", "in_song_settings"],
            "availability": ("active_compatible_ultimate_only" if binding or ident == "kUnlockAll"
                             else "capability_dependent"),
            "current_strategy": strategy,
            "source": "ultimate_provider" if binding or ident == "kUnlockAll" else "native",
            "persistence": ("ultimate_provider_file" if binding else "settings_file"),
            "consumer": consumer, "proposed_boundary": boundary,
            "restart_reason": gate[0] if strategy == "restart" else "",
            "revisit_condition": gate[1] if strategy == "restart" else "",
            "consumer_sites": sites,
            "original_row_callbacks": detail["callbacks"] if detail else [],
            "original_row_macro": detail["row_macro"] if detail else "",
            "original_macro_callbacks": detail["macro_callbacks"] if detail else [],
            "context_evidence": ["src/ui/settings_screen.cpp:AllPanels",
                                 "src/ui/settings_screen.cpp:InSongPanels"],
            "evidence": [source_line(SCHEMA, position)] + ([binding["binding_evidence"]] if binding else []) +
                        (["installed provider archive, aggregate read-site audit 2026-11-06"]
                         if provider_consumer else []),
            **({"provider_token": binding["provider_token"],
                "provider_variable": binding["provider_variable"]} if binding else {}),
        })
    assert len(rows) == 48, f"expected 48 preferences, found {len(rows)}"
    assert len({row["id"] for row in rows}) == len(rows), "duplicate preference strategy"
    assert len(RESTART_GATES) == 39
    assert {row["id"] for row in rows if row["current_strategy"] == "restart"} <= set(RESTART_GATES)
    assert len(bindings) == 17
    counts = {"immediate": 0, "menu_return": 0, "next_song": 0, "process_restart": 0}
    for row in rows:
        counts[{"hot": "immediate", "menu_return": "menu_return",
                "next_song": "next_song", "restart": "process_restart"}[row["current_strategy"]]] += 1
    if args.p7_markdown:
        print("| Setting | Reason restart remains required | Revisit when |")
        print("| --- | --- | --- |")
        for row in rows:
            if row["current_strategy"] == "restart":
                print(f"| `{row['token']}` | {row['restart_reason']} | {row['revisit_condition']} |")
        return
    print(json.dumps({"audit": "static", "schema": "src/settings/schema.cpp",
                      "adapter": "src/settings/ultimate_adapter.cpp",
                      "provider_audit": ("all_42_installed_dtbs" if provider_audit else "not_supplied"),
                      "current_counts": counts, "rows": rows}, indent=2))


if __name__ == "__main__":
    main()
