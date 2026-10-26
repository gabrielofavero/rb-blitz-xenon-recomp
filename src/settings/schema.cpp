// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The P3 schema table (src/settings/schema.h). Every row the parent plan §3 names that P3
// can describe without a capability probe is listed here, in presentation order, with its
// storage location and its submenu group. Rows whose choices come from a capability record
// carry a `choices_source` and leave `Descriptor::choices` empty until BuildDescriptors
// fills it from a snapshot.
//
// Provider rows (Ultimate) are described too, because the migration and the service must
// know they exist; their choices are the provider's own inventory (P6), so the static table
// leaves them empty with `choices_source = "provider"`.

#include "settings/schema.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace rb_blitz::settings::schema {

namespace {

Value BoolValue(bool value) {
  Value out;
  out.kind = ValueKind::kBool;
  out.boolean = value;
  return out;
}

Value IntValue(int32_t value) {
  Value out;
  out.kind = ValueKind::kInt;
  out.integer = value;
  return out;
}

Value EnumValue(std::string token) {
  Value out;
  out.kind = ValueKind::kEnum;
  out.text = std::move(token);
  return out;
}

Value ResolutionValue(int32_t width, int32_t height) {
  Value out;
  out.kind = ValueKind::kResolution;
  out.width = width;
  out.height = height;
  return out;
}

// The "never chosen" value: §7.3's absent hidden-row selection. A list row whose persisted
// value is kNone means the user has never picked, which is what lets the default offline
// trio apply without an explicit empty selection hiding nothing.
Value NoneValue() { return Value{}; }

std::vector<Choice> Choices(std::initializer_list<std::pair<const char*, const char*>> pairs) {
  std::vector<Choice> out;
  out.reserve(pairs.size());
  for (const auto& pair : pairs) {
    out.push_back(Choice{pair.first, pair.second});
  }
  return out;
}

Descriptor Make(SettingId id, Category category, Provider provider, WidgetKind widget,
                ValueKind kind, Value default_value, std::string choices_source = {}) {
  Descriptor descriptor;
  descriptor.id = id;
  descriptor.category = category;
  descriptor.provider = provider;
  descriptor.widget = widget;
  descriptor.value_kind = kind;
  descriptor.compiled_default = std::move(default_value);
  descriptor.choices_source = std::move(choices_source);
  descriptor.label_key = "settings." + std::string(SettingIdToken(id));
  descriptor.help_key = descriptor.label_key + ".help";
  return descriptor;
}

SchemaRow Row(Descriptor descriptor, StorageKey storage, std::string group, std::string label,
              std::string help) {
  SchemaRow row;
  row.descriptor = std::move(descriptor);
  row.storage = storage;
  row.group = std::move(group);
  row.label = std::move(label);
  row.help = std::move(help);
  return row;
}

// A provider (Ultimate) row: available only when a compatible payload is active.
SchemaRow ProviderRow(Descriptor descriptor, std::string group, std::string label,
                      std::string help) {
  return Row(std::move(descriptor), StorageKey{}, std::move(group), std::move(label),
             std::move(help));
}

// Defined below with the §5.5 table it materializes, and applied at the end of BuildTable().
void ApplyStrategies(std::vector<SchemaRow>& rows);

std::vector<SchemaRow> BuildTable() {
  std::vector<SchemaRow> rows;

  // --- General -------------------------------------------------------------
  {
    Descriptor d = Make(SettingId::kTarget, Category::kGeneral, Provider::kNative,
                        WidgetKind::kSubmenu, ValueKind::kEnum, EnumValue("common"));
    d.choices = Choices({{"common", "Rock Band Blitz"},
                         {"ultimate", "Rock Band Blitz Ultimate"},
                         {"demo", "Rock Band Blitz Demo"}});
    rows.push_back(Row(std::move(d), {"general", "target"}, "", "Target",
                       "Which game the launch runs."));
  }
  rows.push_back(Row(Make(SettingId::kCacheDlcLibrary, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "cache_dlc_library"}, "dlc", "Cache DLC Library",
                     "Reuse a cached DLC library scan on the next launch."));
  // The main menu's refresh entry (owner request, 2026-10-15): the row `splash_dlc` carries
  // TapRefreshCache while R7 is on, so it is a feature toggle of its own - a player who does not
  // want the entry can remove it here instead of digging into Interface -> Hidden Items. It is a
  // restart row like the other menu-shaping ones: the row list is baked when the menu's records
  // are read (src/hooks/menu_filter.cpp) and in the payload's own locale. On by default since the
  // owner request of 2026-10-22 (off 2026-10-20 to 2026-10-22): the refresh entry is part of a
  // shipped install again, and the row here is how a player removes it. The visible row and the
  // main-menu entry are both named "Refresh Songs" (renamed with the same owner request).
  rows.push_back(Row(Make(SettingId::kRefreshMusicLibrary, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "refresh_music_library"}, "dlc", "Refresh Songs",
                     "Show the main menu entry that re-scans your DLC folders."));
  {
    Descriptor d = Make(SettingId::kDlcScanThreads, Category::kGeneral, Provider::kNative,
                        WidgetKind::kHorizontalSelect, ValueKind::kInt, IntValue(1),
                        "processor_count");
    rows.push_back(Row(std::move(d), {"general", "dlc_scan_threads"}, "dlc", "DLC Scan Threads",
                       "How many threads scan the DLC library."));
  }
  // `general.dlc_menu_songs` controls R9 - the menu's DLC background music - and is a visible row
  // again (owner request, 2026-10-14): it was retired to "always on" on 2026-10-13, and reviving it
  // as an ordinary boolean whose compiled default is *on* keeps every existing boot's behaviour while
  // giving the feature a control. `src/hooks/shell_music.cpp` is its only consumer, and it reads the
  // store before it mounts anything.
  rows.push_back(Row(Make(SettingId::kDlcMenuSongs, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "dlc_menu_songs"}, "dlc", "DLC Menu Songs",
                     "Play songs from your DLC library as the main menu's background music."));
  {
    Descriptor d = Make(SettingId::kLoadGuard, Category::kGeneral, Provider::kNative,
                        WidgetKind::kHorizontalSelect, ValueKind::kEnum, EnumValue("off"));
    d.choices = Choices({{"off", "Off"}, {"30s", "30s"}, {"60s", "60s"}});
    rows.push_back(Row(std::move(d), {"general", "load_guard"}, "dlc", "Load Guard",
                       "Restart to the main menu if a song takes too long to load. Unsaved progress may be lost."));
  }
  // `general.play_current_song`: the main menu's second entry exists only while this row is on and
  // the menu really has a DLC track playing (owner feature, 2026-10-14). It is consumed at boot, so
  // it is a restart row like the rest of the menu-shaping ones. On by default, like the cache row
  // (owner request, 2026-10-20); a boot that found no DLC at all does not draw the row at all
  // (`src/hooks/settings_payload.cpp`), because there is nothing it could ever start.
  rows.push_back(Row(Make(SettingId::kPlayCurrentSong, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "play_current_song"}, "dlc", "Play Current Song",
                     "Add an entry that starts the song the main menu is playing."));
  // RhythmVerse store (plan P6). The master is a plain boolean in the `dlc` group; the instrument
  // filter is a submenu of its own (no SettingId), so its five leaves carry their own group. The
  // master defaults **on** (owner pass, plan §9/P9-1, 2026-10-22): the store is a shipped feature,
  // and Off is the switch that silences it - the workers still ask `StoreSettings::enabled` before
  // they touch the network, so an Off session makes no request (RhythmVerse plan §5/P8's control
  // leg). A master change is restart-scoped (plan §9/P9-2): see its entry in the strategy table.
  rows.push_back(Row(Make(SettingId::kRhythmVerse, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "rhythmverse"}, "dlc", "RhythmVerse",
                     "Browse the RhythmVerse customs catalogue in the store and download songs."));
  rows.push_back(Row(Make(SettingId::kRhythmVerseGuitar, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "rhythmverse_guitar"}, "rhythmverse", "Guitar",
                     "Only list songs that have a guitar chart."));
  rows.push_back(Row(Make(SettingId::kRhythmVerseBass, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "rhythmverse_bass"}, "rhythmverse", "Bass",
                     "Only list songs that have a bass chart."));
  rows.push_back(Row(Make(SettingId::kRhythmVerseDrums, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "rhythmverse_drums"}, "rhythmverse", "Drums",
                     "Only list songs that have a drum chart."));
  rows.push_back(Row(Make(SettingId::kRhythmVerseVocals, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"general", "rhythmverse_vocals"}, "rhythmverse", "Vocals",
                     "Only list songs that have a vocal chart."));
  rows.push_back(Row(Make(SettingId::kRhythmVerseKeys, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(false)),
                     {"general", "rhythmverse_keys"}, "rhythmverse", "Keys",
                     "Only list songs that have a keys chart."));
  rows.push_back(Row(Make(SettingId::kHideAchievements, Category::kGeneral, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(false)),
                     {"general", "hide_achievements"}, "achievements", "Hide Achievements",
                     "Hide achievement unlock notifications. Achievements are still earned and saved."));
  rows.push_back(ProviderRow(Make(SettingId::kUnlockAll, Category::kGeneral, Provider::kUltimate,
                                  WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                             "achievements", "Unlock All",
                             "Unlock every song and achievement."));
  rows.push_back(ProviderRow(Make(SettingId::kUnlockOnlineAchievements, Category::kGeneral,
                                  Provider::kUltimate, WidgetKind::kAction, ValueKind::kNone,
                                  NoneValue()),
                             "achievements", "Unlock Online Achievements",
                             "Grant the online-only achievements once."));

  // --- Graphics ------------------------------------------------------------
  {
    Descriptor d = Make(SettingId::kResolution, Category::kGraphics, Provider::kNative,
                        WidgetKind::kHorizontalSelect, ValueKind::kResolution,
                        ResolutionValue(0, 0), "resolutions");
    rows.push_back(Row(std::move(d), {"graphics", "resolution"}, "", "Resolution",
                       "The rendered resolution."));
  }
  rows.push_back(Row(Make(SettingId::kVsync, Category::kGraphics, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"graphics", "vsync"}, "", "V-Sync", "Synchronize presentation to the display."));
  {
    // The window mode (owner decision, 2026-10-13): three states over two SDK cvars - `windowed`
    // and `borderless` share `fullscreen = false`, and `borderless` adds the undecorated window.
    Descriptor d = Make(SettingId::kDisplay, Category::kGraphics, Provider::kNative,
                        WidgetKind::kHorizontalSelect, ValueKind::kEnum, EnumValue("fullscreen"));
    d.choices = Choices({{"windowed", "Windowed"},
                         {"borderless", "Borderless"},
                         {"fullscreen", "Full Screen"}});
    rows.push_back(Row(std::move(d), {"graphics", "display"}, "", "Display",
                       "How the game's window is drawn."));
  }
  {
    Descriptor d = Make(SettingId::kRenderer, Category::kGraphics, Provider::kNative,
                        WidgetKind::kHorizontalSelect, ValueKind::kEnum, EnumValue("d3d12"),
                        "renderers");
    rows.push_back(
        Row(std::move(d), {"graphics", "renderer"}, "", "Renderer", "Which graphics backend runs."));
  }
  {
    Descriptor d = Make(SettingId::kAntiAliasing, Category::kGraphics, Provider::kNative,
                        WidgetKind::kBooleanWithSubmenu, ValueKind::kEnum, EnumValue("native"));
    d.choices = Choices({{"none", "None"},
                         {"native", "Native"},
                         {"fxaa", "FXAA"},
                         {"fxaa_extreme", "FXAA Extreme"}});
    rows.push_back(Row(std::move(d), {"graphics", "anti_aliasing"}, "", "Anti-aliasing",
                       "One anti-aliasing choice, never a second flag."));
  }
  {
    Descriptor d = Make(SettingId::kFiltering, Category::kGraphics, Provider::kNative,
                        WidgetKind::kHorizontalSelect, ValueKind::kEnum, EnumValue("native"));
    d.choices = Choices({{"native", "Native"},
                         {"none", "None"},
                         {"1x", "1x"},
                         {"2x", "2x"},
                         {"4x", "4x"},
                         {"8x", "8x"},
                         {"16x", "16x"}});
    rows.push_back(Row(std::move(d), {"graphics", "filtering"}, "", "A. Filtering",
                       "Anisotropic filtering, capped by the device."));
  }

  // --- Interface -----------------------------------------------------------
  rows.push_back(ProviderRow(Make(SettingId::kLetterbox, Category::kInterface,
                                  Provider::kUltimate, WidgetKind::kBoolean, ValueKind::kBool,
                                  BoolValue(false)),
                             "", "4:3 Letterboxing", "Letterbox 4:3 presentation."));
  rows.push_back(ProviderRow(Make(SettingId::kHideHud, Category::kInterface, Provider::kUltimate,
                                  WidgetKind::kBoolean, ValueKind::kBool, BoolValue(false)),
                             "hud", "Hide HUD", "Hide the whole HUD."));
  rows.push_back(ProviderRow(Make(SettingId::kGoldProgress, Category::kInterface,
                                  Provider::kUltimate, WidgetKind::kBoolean, ValueKind::kBool,
                                  BoolValue(false)),
                             "hud", "Gold Star Progress", "Show gold-star progress."));
  rows.push_back(ProviderRow(Make(SettingId::kProgressBar, Category::kInterface,
                                  Provider::kUltimate, WidgetKind::kBoolean, ValueKind::kBool,
                                  BoolValue(false)),
                             "hud", "Song Progress Bar", "Show the song progress bar."));
  rows.push_back(ProviderRow(Make(SettingId::kShowTimer, Category::kInterface, Provider::kUltimate,
                                  WidgetKind::kBoolean, ValueKind::kBool, BoolValue(false)),
                             "hud", "Timer", "Show the song timer."));
  rows.push_back(ProviderRow(Make(SettingId::kShowPb, Category::kInterface, Provider::kUltimate,
                                  WidgetKind::kBoolean, ValueKind::kBool, BoolValue(false)),
                             "hud", "Personal Best", "Show personal-best progress."));
  rows.push_back(ProviderRow(Make(SettingId::kShowSteak, Category::kInterface, Provider::kUltimate,
                                  WidgetKind::kBoolean, ValueKind::kBool, BoolValue(false)),
                             "hud", "Note Streak", "Show the note streak counter."));
  rows.push_back(ProviderRow(Make(SettingId::kInputViewer, Category::kInterface,
                                  Provider::kUltimate, WidgetKind::kBoolean, ValueKind::kBool,
                                  BoolValue(false)),
                             "hud", "Input Viewer", "Show the input viewer."));
  rows.push_back(Row(Make(SettingId::kHideOfflinePrompts, Category::kInterface, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"interface", "hide_offline_prompts"}, "hide_items", "Offline Mode Prompts",
                     "Hide the offline-mode prompts."));
  // `interface.settings_labels` is the caption overlay's own switch (owner request, 2026-10-14):
  // it is the *one* row with a measured live effect (docs/settings/status.md), because the overlay
  // reads the `settings_help_overlay` cvar on every draw, so the `§5.5` strategy table below marks
  // it `kHot` and the runtime applier writes that cvar.
  rows.push_back(Row(Make(SettingId::kSettingsLabels, Category::kInterface, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"interface", "settings_labels"}, "", "Settings Labels",
                     "Show a description of the highlighted row at the bottom of the screen."));
  rows.push_back(Row(Make(SettingId::kBackgroundMusicNavigation, Category::kInterface,
                          Provider::kNative, WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"interface", "background_music_navigation"}, "", "Menu Music Navigation",
                     "Navigate menu music with the shoulders, media keys, or music player. Requires a restart."));
  // P9-4: the shared background panel is authored once at boot.
  rows.push_back(Row(Make(SettingId::kShowMessages, Category::kInterface,
                          Provider::kNative, WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"interface", "show_messages"}, "music_library_store", "Show Messages",
                     "Show the MESSAGES tab in the music library and store. Requires a restart."));
  rows.push_back(Row(Make(SettingId::kShowRecommendations, Category::kInterface,
                          Provider::kNative, WidgetKind::kBoolean, ValueKind::kBool, BoolValue(false)),
                     {"interface", "show_recommendations"}, "music_library_store", "Show Recommendations",
                     "Show the RECOMMENDED tab in the music library and store. Requires a restart."));
  // The main menu's Download Content row (owner request, 2026-10-20). R7's refresh took the row
  // over (`splash_dlc`), so the retail entry lost its label and action; the payload now restores
  // it as a row of its own. This switch is its visibility, and `true` means hidden: a fresh
  // install does not offer a store entry that only ever worked online. The row is only offered
  // while the cache owns `splash_dlc` (src/hooks/settings_payload.cpp), because with the cache off
  // that retail row is the Download Content entry itself.
  rows.push_back(Row(Make(SettingId::kHideDownloadContent, Category::kInterface, Provider::kNative,
                          WidgetKind::kBoolean, ValueKind::kBool, BoolValue(true)),
                     {"interface", "hide_download_content"}, "hide_items", "Download content",
                     "Show the main menu's Download content entry."));
  {
    Descriptor d = Make(SettingId::kHideMainMenu, Category::kInterface, Provider::kNative,
                        WidgetKind::kBooleanWithSubmenu, ValueKind::kBool, BoolValue(false));
    d.submenu_parent = SettingId::kHiddenMainMenu;
    rows.push_back(Row(std::move(d), {"interface", "hide_main_menu"}, "hide_items", "Main Menu",
                       "Hide the chosen main-menu rows."));
  }
  {
    Descriptor d = Make(SettingId::kHiddenMainMenu, Category::kInterface, Provider::kNative,
                        WidgetKind::kSubmenu, ValueKind::kList, NoneValue(), "menu_rows");
    rows.push_back(Row(std::move(d), {"interface", "hidden_main_menu"}, "hide_items",
                       "Hidden Rows", "Which main-menu rows the hide master hides."));
  }

  // --- Gameplay (Ultimate provider) ---------------------------------------
  rows.push_back(ProviderRow(Make(SettingId::kPrecisionMode, Category::kGameplay,
                                  Provider::kUltimate, WidgetKind::kBoolean, ValueKind::kBool,
                                  BoolValue(false)),
                             "", "Precision Mode", "Tighter hit timing."));
  rows.push_back(ProviderRow(Make(SettingId::kDisableRandomWiggles, Category::kGameplay,
                                  Provider::kUltimate, WidgetKind::kBoolean, ValueKind::kBool,
                                  BoolValue(false)),
                             "", "Disable Random Track Wiggles", "Stop the track wiggling."));
  {
    Descriptor d = Make(SettingId::kCameraAngle, Category::kGameplay, Provider::kUltimate,
                        WidgetKind::kHorizontalSelect, ValueKind::kEnum, EnumValue("default"));
    d.choices = Choices({{"default", "Default"}, {"high", "High"}, {"low", "Low"}});
    rows.push_back(ProviderRow(std::move(d), "", "Camera Angle", "Camera height."));
  }
  {
    Descriptor d = Make(SettingId::kGameSpeed, Category::kGameplay, Provider::kUltimate,
                        WidgetKind::kHorizontalSelect, ValueKind::kInt, IntValue(100));
    for (int percent = 100; percent <= 150; percent += 5) {
      d.choices.push_back(Choice{std::to_string(percent), std::to_string(percent) + "%"});
    }
    rows.push_back(ProviderRow(std::move(d), "", "Game Speed", "Song speed percentage."));
  }
  rows.push_back(ProviderRow(Make(SettingId::kDisableDof, Category::kGameplay,
                                  Provider::kUltimate, WidgetKind::kBoolean, ValueKind::kBool,
                                  BoolValue(false)),
                             "", "Disable Depth of Field", "Turn off depth of field."));
  rows.push_back(ProviderRow(Make(SettingId::kDisablePlots, Category::kGameplay,
                                  Provider::kUltimate, WidgetKind::kBoolean, ValueKind::kBool,
                                  BoolValue(false)),
                             "plots", "Disable Plots", "Turn off plot backgrounds."));
  rows.push_back(ProviderRow(Make(SettingId::kForcePlot, Category::kGameplay, Provider::kUltimate,
                                  WidgetKind::kBooleanWithSubmenu, ValueKind::kEnum,
                                  EnumValue("off"), "provider"),
                             "plots", "Force Plot", "Force a background while plots are on."));
  rows.push_back(ProviderRow(Make(SettingId::kForceSkybox, Category::kGameplay,
                                  Provider::kUltimate, WidgetKind::kBooleanWithSubmenu,
                                  ValueKind::kEnum, EnumValue("default"), "provider"),
                             "plots", "Force Skybox", "Force a skybox."));
  rows.push_back(ProviderRow(Make(SettingId::kGems, Category::kGameplay, Provider::kUltimate,
                                  WidgetKind::kHorizontalSelect, ValueKind::kEnum,
                                  EnumValue("default"), "provider"),
                             "", "Gems", "Which note gems the provider draws."));

  // §5.5: every row carries its application strategy, from the one table above.
  ApplyStrategies(rows);
  return rows;
}

// ------------------------------------------------------------------------------------------------
// §5.5's classification table: one entry per SettingId, in one place
// ------------------------------------------------------------------------------------------------
// This table is the source of truth; `Descriptor::strategy` is its materialization, so the UI, the
// acceptance matrix and the docs read the same facts the service commits by. The provider
// adapter's own row table carries a `Strategy` column too (it is the payload's inventory), and
// tests/settings_hot_apply_tests.cpp asserts the two agree row by row, which is what keeps them
// from drifting.
//
// **`kHot` is a measurement, not a preference.** Seven rows carry it, every one because something
// observed the live effect and the evidence column names it:
//
//   * `kVsync` - the GPU plugin's worker re-reads `vsync` every iteration.
//   * `kSettingsLabels` - the caption overlay re-reads `settings_help_overlay` every draw.
//   * The five RhythmVerse filter leaves - the store's workers read the snapshot before every
//     request, and the applier replaces it in-process.
//
// The RhythmVerse **master** is `restart` (owner pass, plan §9/P9-2, 2026-10-22): the switch owns
// what is *drawn*, not only what a worker reads - the main-menu row is compiled into the record the
// boot generates (plan §5/P1) and the store's heading is a locale entry the payload replaces - so a
// value committed mid-boot would leave one of the two lying until the next start. The tab P9-9 adds
// and the hidden flow P9-10 adds will owe the same surface. `settings_hot_apply_tests.cpp` pins
// this as a row that must stay restart, so a future session promotes it only with a measurement.
//
// Everything else is `restart`, and that is a measurement rather than a policy:
//
//   * Every Ultimate row is `restart` because P6 measured *when* the provider's values are read:
//     the payload's own loader fills its DTA variables before the first frame of a boot, so
//     writing its file cannot change this process (docs/settings/architecture.md (part 5, the Ultimate adapter)
//     §4). That includes Hide HUD, which the parent plan §5.5 named as its hot example before that
//     measurement existed.
//   * The render-path rows (resolution, renderer, anti-aliasing, filtering) are structurally
//     restart: they change render-target and backend state at initialization, and re-initializing
//     the renderer from inside a menu is its own spike (parent plan §5.5).
//   * The remaining native rows are read at boot by the code that consumes them (the DLC scan, the
//     main-menu transform, the offline prompts), so they need a new process until one of them is
//     measured live - `kDlcMenuSongs` is the standing candidate (the menu music pool is built when
//     the menu is entered), and the experiment for it is in the P9 handoff.
struct StrategyEntry {
  SettingId id;
  ApplicationStrategy strategy;
  const char* evidence;  // what was observed; empty for a `restart` row
};

constexpr StrategyEntry kStrategies[] = {
    // General
    {SettingId::kTarget, ApplicationStrategy::kRestart, ""},
    {SettingId::kCacheDlcLibrary, ApplicationStrategy::kRestart, ""},
    {SettingId::kRefreshMusicLibrary, ApplicationStrategy::kRestart, ""},
    {SettingId::kDlcScanThreads, ApplicationStrategy::kRestart, ""},
    {SettingId::kLoadGuard, ApplicationStrategy::kHot,
     "Host atomic policy publication; the next attempt snapshots its deadline. See load_policy.h and song_load_monitor_tests.cpp."},
    // Read while the menu's music pool is built (src/hooks/shell_music.cpp, at
    // OnPostLoadXexImage), so a new process is what a change needs.
    {SettingId::kDlcMenuSongs, ApplicationStrategy::kRestart, ""},
    // Consumed by the boot that builds the menu payload, for the same reason.
    {SettingId::kPlayCurrentSong, ApplicationStrategy::kRestart, ""},
    // RhythmVerse store (plan P6; the master made restart-scoped by the owner pass, plan §9/P9-2,
    // 2026-10-22). The **master** owns what is drawn, not only what a worker reads: the main-menu
    // row is baked into the record the boot generates (plan §5/P1) and the store's heading is a
    // locale entry `SetStoreHeading` replaces in that same payload, so a value committed mid-boot
    // would leave one of the two surfaces lying until the next start. A restart is therefore the
    // honest answer, and the prompt asked when Help & Options is left is what carries it. A hot
    // master would also owe P9-9's RhythmVerse tab and P9-10's hidden flow in the same boot, which
    // is the second reason it is not hot today.
    {SettingId::kRhythmVerse, ApplicationStrategy::kRestart, ""},
    // The **filter leaves** are `hot`: nothing in the guest reads them. The store's own workers read
    // the snapshot the applier replaces in-process, so a commit is effective for the next request
    // with no restart. The behaviour is measured by the `store_client` suite (a committed leaf bumps
    // the snapshot generation and the next read sees the new value; an equal one does not).
    {SettingId::kRhythmVerseGuitar, ApplicationStrategy::kHot,
     "the applier replaces the store's in-process snapshot; `store_client` observes the filter "
     "change after a commit"},
    {SettingId::kRhythmVerseBass, ApplicationStrategy::kHot,
     "the applier replaces the store's in-process snapshot; `store_client` observes the filter "
     "change after a commit"},
    {SettingId::kRhythmVerseDrums, ApplicationStrategy::kHot,
     "the applier replaces the store's in-process snapshot; `store_client` observes the filter "
     "change after a commit"},
    {SettingId::kRhythmVerseVocals, ApplicationStrategy::kHot,
     "the applier replaces the store's in-process snapshot; `store_client` observes the filter "
     "change after a commit"},
    {SettingId::kRhythmVerseKeys, ApplicationStrategy::kHot,
     "the applier replaces the store's in-process snapshot; `store_client` observes the filter "
     "change after a commit"},
    {SettingId::kUnlockAll, ApplicationStrategy::kRestart, ""},
    {SettingId::kUnlockOnlineAchievements, ApplicationStrategy::kRestart, ""},
    {SettingId::kHideAchievements, ApplicationStrategy::kRestart, ""},
    // Graphics
    {SettingId::kResolution, ApplicationStrategy::kRestart, ""},
    // The first row promoted to `hot`, and its evidence is a measurement rather than a guess about
    // reloadability: with the guest running, the host flipped this exact cvar and the discovery
    // rate moved from 16.66 ms per item to 5.68 ms (46.7 s -> 15.96 s for 2,807 items), with the
    // log recording `vsync false, was true` 7 ms after the flip (docs/guides/dlc.md, "Frame pacing
    // for the discovery"; the mechanism is the GPU plugin's worker re-reading `vsync` every
    // iteration).
    {SettingId::kVsync, ApplicationStrategy::kHot,
     "flipping the `vsync` cvar with the guest running moved the discovery rate from 16.66 ms per "
     "item to 5.68 ms (46.7 s -> 15.96 s, docs/guides/dlc.md), i.e. a live effect measured in a boot"},
    {SettingId::kRenderer, ApplicationStrategy::kRestart, ""},
    // The window mode is read once, when the window is created: a new process, like the rest of the
    // render path it belongs to.
    {SettingId::kDisplay, ApplicationStrategy::kRestart, ""},
    {SettingId::kAntiAliasing, ApplicationStrategy::kRestart, ""},
    {SettingId::kFiltering, ApplicationStrategy::kRestart, ""},
    // Interface
    {SettingId::kLetterbox, ApplicationStrategy::kRestart, ""},
    {SettingId::kHideDownloadContent, ApplicationStrategy::kRestart, ""},
    {SettingId::kShowMessages, ApplicationStrategy::kRestart, ""},
    {SettingId::kShowRecommendations, ApplicationStrategy::kRestart, ""},
    {SettingId::kHideOfflinePrompts, ApplicationStrategy::kRestart, ""},
    {SettingId::kBackgroundMusicNavigation, ApplicationStrategy::kRestart, ""},
    {SettingId::kHideMainMenu, ApplicationStrategy::kRestart, ""},
    {SettingId::kHiddenMainMenu, ApplicationStrategy::kRestart, ""},
    {SettingId::kHideHud, ApplicationStrategy::kRestart, ""},
    {SettingId::kGoldProgress, ApplicationStrategy::kRestart, ""},
    {SettingId::kProgressBar, ApplicationStrategy::kRestart, ""},
    {SettingId::kShowTimer, ApplicationStrategy::kRestart, ""},
    {SettingId::kShowPb, ApplicationStrategy::kRestart, ""},
    {SettingId::kShowSteak, ApplicationStrategy::kRestart, ""},
    {SettingId::kInputViewer, ApplicationStrategy::kRestart, ""},
    // The second hot row, and the same shape as `kVsync`: the caption overlay re-reads its cvar in
    // `HelpOverlayDialog::OnDraw` (src/ui/help_overlay.cpp) on every frame, so a write to it takes
    // effect on the next draw without a new process. Measured in a boot: the row is flipped in
    // Help & Options and the caption disappears on the next draw, with the log recording
    // `hot_apply: settings_help_overlay = false (was true), applied to the running game`
    // (docs/settings/status.md, the Settings Labels evidence).
    {SettingId::kSettingsLabels, ApplicationStrategy::kHot,
     "the overlay re-reads `settings_help_overlay` every draw, so flipping the row in Help & Options "
     "hid the caption on the next frame (docs/settings/status.md, Settings Labels)"},
    // Gameplay (Ultimate provider)
    {SettingId::kPrecisionMode, ApplicationStrategy::kRestart, ""},
    {SettingId::kDisableRandomWiggles, ApplicationStrategy::kRestart, ""},
    {SettingId::kCameraAngle, ApplicationStrategy::kRestart, ""},
    {SettingId::kGameSpeed, ApplicationStrategy::kRestart, ""},
    {SettingId::kDisableDof, ApplicationStrategy::kRestart, ""},
    {SettingId::kDisablePlots, ApplicationStrategy::kRestart, ""},
    {SettingId::kForcePlot, ApplicationStrategy::kRestart, ""},
    {SettingId::kForceSkybox, ApplicationStrategy::kRestart, ""},
    {SettingId::kGems, ApplicationStrategy::kRestart, ""},
};

// Materializes the table into the rows. A row with no entry keeps the model's default
// (`kRestart`, no evidence), which the test then reports as uncovered rather than allowing it to
// look deliberate.
void ApplyStrategies(std::vector<SchemaRow>& rows) {
  for (SchemaRow& row : rows) {
    for (const StrategyEntry& entry : kStrategies) {
      if (entry.id == row.descriptor.id) {
        row.descriptor.strategy = entry.strategy;
        row.descriptor.strategy_evidence = entry.evidence;
        break;
      }
    }
  }
}

// P10's seam: the effective main-menu row tokens. Empty until a host installs them.
std::vector<Choice>& MutableMenuRows() {
  static std::vector<Choice> rows;
  return rows;
}

void FillChoiceSource(Descriptor& descriptor, const CapabilitySnapshot& caps) {
  const std::string& source = descriptor.choices_source;
  if (source.empty() || source == "provider") {
    return;  // provider choices are P6's; an empty source keeps the static list.
  }
  descriptor.choices.clear();
  if (source == "renderers") {
    for (const RendererCapability& renderer : caps.renderers) {
      if (!renderer.usable && !renderer.active) {
        continue;
      }
      std::string label = renderer.token;
      for (char& character : label) {
        if (character >= 'a' && character <= 'z') {
          character = static_cast<char>(character - 'a' + 'A');
        }
      }
      if (renderer.token == "vulkan") label = "Vulkan";
      descriptor.choices.push_back(Choice{renderer.token, label});
    }
  } else if (source == "resolutions") {
    descriptor.choices.push_back(Choice{"auto", "Auto"});
    for (const DisplayMode& mode : caps.supported_resolutions) {
      if (mode.width <= 0 || mode.height <= 0) {
        continue;
      }
      const std::string token = std::to_string(mode.width) + "x" + std::to_string(mode.height);
      descriptor.choices.push_back(Choice{token, token});
    }
  } else if (source == "processor_count") {
    const int ceiling = static_cast<int>(caps.logical_processors < 1 ? 1 : caps.logical_processors);
    const int highest = ceiling > 32 ? 32 : ceiling;
    for (int count = 1; count <= highest; ++count) {
      descriptor.choices.push_back(Choice{std::to_string(count), std::to_string(count)});
    }
  } else if (source == "menu_rows") {
    descriptor.choices = MutableMenuRows();
  }
}

}  // namespace

const std::vector<SchemaRow>& Rows() {
  static const std::vector<SchemaRow> rows = BuildTable();
  return rows;
}

const SchemaRow* FindRow(SettingId id) {
  if (id == SettingId::kUnknown) {
    return nullptr;
  }
  for (const SchemaRow& row : Rows()) {
    if (row.descriptor.id == id) {
      return &row;
    }
  }
  return nullptr;
}

// P11a: the wire spelling, both forms. This used to live in the runtime applier (src/hooks/
// hot_apply_runtime.cpp) because that was the only consumer; the boot's service-backed sink needs
// the same vocabulary and is SDK-free, so the lookup belongs with the table it searches.
SettingId IdForToken(std::string_view token) {
  if (token.empty()) {
    return SettingId::kUnknown;
  }
  for (const SchemaRow& row : Rows()) {
    const Descriptor& descriptor = row.descriptor;
    if (SettingIdToken(descriptor.id) == token) {
      return descriptor.id;
    }
    if (std::string(CategoryToken(descriptor.category)) + "." +
            std::string(SettingIdToken(descriptor.id)) ==
        token) {
      return descriptor.id;
    }
  }
  return SettingId::kUnknown;
}

// P9: the strategy, without materializing a descriptor list. Same table, same answer - the point of
// having one table is that the boot path and the UI cannot disagree.
ApplicationStrategy StrategyOf(SettingId id) {
  const SchemaRow* row = FindRow(id);
  return row == nullptr ? ApplicationStrategy::kRestart : row->descriptor.strategy;
}std::vector<Descriptor> BuildDescriptors(const CapabilitySnapshot& caps) {
  std::vector<Descriptor> descriptors;
  descriptors.reserve(Rows().size());
  for (const SchemaRow& row : Rows()) {
    Descriptor descriptor = row.descriptor;
    FillChoiceSource(descriptor, caps);
    if (descriptor.provider == Provider::kUltimate) {
      if (!caps.provider.installed || !caps.provider.compatible) {
        descriptor.availability = Availability::kHidden;
      } else if (!caps.provider.active) {
        descriptor.availability = Availability::kVisibleDisabled;
        descriptor.disabled_reason_key = "settings.requires_ultimate";
      } else {
        descriptor.availability = Availability::kVisible;
      }
    }
    descriptors.push_back(std::move(descriptor));
  }
  return descriptors;
}

void SetMenuRows(std::vector<Choice> rows) { MutableMenuRows() = std::move(rows); }

std::vector<Choice> MenuRows() { return MutableMenuRows(); }

bool ParseTokenToValue(const Descriptor& descriptor, std::string_view token, Value* out) {
  switch (descriptor.value_kind) {
    case ValueKind::kNone:
      return false;
    case ValueKind::kBool: {
      if (token == "true" || token == "on") {
        *out = BoolValue(true);
        return true;
      }
      if (token == "false" || token == "off") {
        *out = BoolValue(false);
        return true;
      }
      return false;
    }
    case ValueKind::kInt: {
      if (token.empty()) {
        return false;
      }
      long long parsed = 0;
      for (const char character : token) {
        if (character < '0' || character > '9') {
          return false;
        }
        parsed = parsed * 10 + (character - '0');
        if (parsed > 0x7FFFFFFF) {
          return false;
        }
      }
      *out = IntValue(static_cast<int32_t>(parsed));
      return true;
    }
    case ValueKind::kResolution: {
      if (token == "auto") {
        *out = ResolutionValue(0, 0);
        return true;
      }
      const std::size_t x = token.find('x');
      if (x == std::string_view::npos || x == 0 || x + 1 >= token.size()) {
        return false;
      }
      int width = 0;
      int height = 0;
      for (const char character : token.substr(0, x)) {
        if (character < '0' || character > '9') {
          return false;
        }
        width = width * 10 + (character - '0');
      }
      for (const char character : token.substr(x + 1)) {
        if (character < '0' || character > '9') {
          return false;
        }
        height = height * 10 + (character - '0');
      }
      if (width <= 0 || height <= 0) {
        return false;
      }
      *out = ResolutionValue(width, height);
      return true;
    }
    case ValueKind::kEnum:
    case ValueKind::kString: {
      if (!descriptor.choices.empty()) {
        for (const Choice& choice : descriptor.choices) {
          if (choice.token == token) {
            *out = EnumValue(std::string(token));
            return true;
          }
        }
        return false;
      }
      if (token.empty()) {
        return false;
      }
      *out = EnumValue(std::string(token));
      return true;
    }
    case ValueKind::kList:
      return false;
  }
  return false;
}

std::string LabelForToken(const Descriptor& descriptor, std::string_view token) {
  for (const Choice& choice : descriptor.choices) {
    if (choice.token == token) {
      return choice.label;
    }
  }
  return std::string(token);
}

}  // namespace rb_blitz::settings::schema

namespace rb_blitz::settings {

StorageKey StorageKeyFor(SettingId id) {
  const schema::SchemaRow* row = schema::FindRow(id);
  return row == nullptr ? StorageKey{} : row->storage;
}

}  // namespace rb_blitz::settings
