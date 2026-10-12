// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P5b: the merged Help & Options surface, generated. src/ui/settings_screen.h explains what the
// retail surface is, why the merged root needs its own scene record, and how a row shows its
// state; this file is the table of the surface plus the generator that turns it into compiled DTA.

#include "ui/settings_screen.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb_blitz::ui::screen {
namespace {

using dtb::Node;
MenuConfiguration g_configuration;

// Small builders, so the definitions below read as the DTA they are.
Node Sym(const std::string_view name) { return Node::Symbol(name); }
Node Str(const std::string_view text) { return Node::String(text); }
Node Var(const std::string_view name) { return Node::Var(name); }
Node Arr(std::vector<Node> kids) { return Node::Array(std::move(kids)); }
Node Cmd(std::vector<Node> kids) { return Node::Command(std::move(kids)); }

// The list widget every panel of this surface drives, and the **project-owned** scene that holds
// it. Not `options_menu.milo`: that file is retail's `options_panel` scene, and naming it here is
// what made two panels share one list object (see the header). Same directory as the retail scene,
// because the retail scene's own references are relative (`neon_lights.milo`,
// `../resource/lists/list_options_main.milo`). One scene per context - `kSceneName` for the
// main-menu pages, `kPauseSceneName` for the in-song ones, for the same reason.
constexpr char kList[] = "options.lst";

// The two statements that fill a list and place its selection:
// `{options.lst set_data {var rows}}` and `{options.lst set_selected 0}`, emitted as a *pair* and
// never separately. Retail pairs them everywhere it fills a list - `{songs.lst set_selected 0}`
// (song_select), `{cheat.lst set_selected 0}` (cheat), `{recommendations.lst set_selected 0}`
// (recommendations) - and P1's prototype proved the pairing is not optional: a list keeps its
// *selected* symbol across a `set_data`, so a panel that fills its list without saying where the
// selection is leaves the drawing and the selection disagreeing (measured: the first row was
// highlighted and Confirm ran the second row's action).
//
// The rows come from a **variable** rather than a literal array, because which symbols are in the
// list is decided at run time from bridge queries (see PopulateMenu).
std::vector<Node> FillStatements() {
  return {Cmd({Sym(kList), Sym("set_data"), Var("rows")}),
          Cmd({Sym(kList), Sym("set_selected"), Var("rbbz_settings_index")})};
}

// `{write_file "<path>" 0}` - the request channel's write side. The data argument is unused by the
// host (a settings request carries everything in its path), so it is a constant zero.
Node Write(const std::string_view path) {
  return Cmd({Sym("write_file"), Str(path), Node::Int(0)});
}

// `rbbz-settings/<op>/<setting>/<value>` - one text-form request, the only wire form a DTA script
// can spell (it cannot compute the binary frame's CRC-32; see src/hooks/settings_bridge.h).
std::string Path(const std::string_view op, const std::string_view setting,
                 const std::string_view value) {
  std::string out = "rbbz-settings/";
  out += op;
  out += '/';
  out += setting;
  out += '/';
  out += value;
  return out;
}

// `{ui goto_screen {{ui current_screen} get back_screen}}` - how a panel hosted by more than one
// screen leaves: it asks the *current screen object* where back is rather than naming a
// destination. Transcribed from the shipped ui/av_options/gen/av_options.dtb, whose
// `av_options_panel` is hosted by both `av_options_screen` and `av_options_pause_screen` and leaves
// through exactly this statement.
Node Leave() {
  return Cmd({Sym("ui"), Sym("goto_screen"),
              Cmd({Cmd({Sym("ui"), Sym("current_screen")}), Sym("get"), Sym("back_screen")})});
}

// Entering a screen of ours: point it back at whoever sent us, then go. The return target is set
// at navigation time rather than baked into the screen declaration, so one root panel can be
// hosted by the main-menu and the in-song screens and still return to the one it came from.
Node EnterScreen(const std::string_view screen) {
  return Cmd({Sym("do"),
              Cmd({Sym(screen), Sym("set"), Sym("back_screen"),
                   Cmd({Sym("ui"), Sym("current_screen")})}),
              Cmd({Sym("ui"), Sym("goto_screen"), Sym(screen)})});
}

// Entering a retail screen: the same "point it back at us" statement, on the member that screen's
// own Back actually reads - `back_screen` for most (controller_config, tutorials, credits,
// av_options), `cancel_screen` for `cal_welcome_screen`.
Node EnterRetail(const std::string_view screen, const std::string_view member) {
  return Cmd({Sym("do"),
              Cmd({Sym(screen), Sym("set"), Sym(member),
                   Cmd({Sym("ui"), Sym("current_screen")})}),
              Cmd({Sym("ui"), Sym("goto_screen"), Sym(screen)})});
}

// The cases every panel of this screen contributes to its `BUTTON_DOWN_MSG` switch.
//
// **How a press reaches a row, measured rather than transcribed.** An action-reporting table in a
// real boot (armS) showed the pad's A arriving as `kAction_Confirm`, so that is the case a row
// needs. Retail's own options panel forwards its *Start* case to `{handle {this button_down ...
// kAction_Confirm ...}}`, and copying that forwarding for Confirm is what crashed the boot
// (`read of guest 0x0000000F`): the message variables that form would pass - `user`, `raw_button`,
// `pad_num` - are not in scope for this panel. The row table is therefore reached through a method
// of our own (`select_row`), which needs no message variables at all.
//
// They are returned as a *list of case arrays*, not as a whole `{switch action ...}`, because that
// distinction is a real bug: a helper returning a complete switch gets spliced in as a child of the
// panel's own switch, which nests one switch inside another and leaves its cases **never
// evaluated** - measured, that shape made Start and Cancel do nothing at all while the switch above
// them still looked right in a dump.
std::vector<Node> ButtonCases() {
  return {
      Arr({Sym("kAction_Confirm"), Cmd({Var("this"), Sym("select_row")})}),
      Arr({Sym("kAction_Start"), Cmd({Var("this"), Sym("select_row")})}),
      Arr({Sym("kAction_Cancel"), Cmd({Sym("shell_sfx"), Sym("play_back")}),
        Cmd({Sym("if_else"), Var("rbbz_settings_editing"),
          Cmd({Sym("do"), Write(Path("cancel", "-", "-")),
            Cmd({Sym("set"), Var("rbbz_settings_editing"), Node::Int(0)}),
            Cmd({Var("this"), Sym("populate_menu")})}), Leave()})}),
  };
}

// `{select_row {switch {options.lst selected_sym} <cases>}}` - the single row table, reached from
// the button table above. It replaces the retail `SELECT_MSG` route for this surface: a probe in a
// panel's own button table reported every press as a button action and never once as `SELECT_MSG`
// (docs/plans/in-game-settings-bridge-findings.md §12.2), so driving the rows from the message the
// engine demonstrably sends is the honest design. `on_select_msg` is still defined, because the
// *selection change* message is a different one and the retail panels use it for the move sound.

// `{switch action <cases...>}` - the panel's button table, extras first.
// `{switch action <extras...> <shared cases...>}` - the panel's button table. Extra cases come
// first because a switch takes the first matching case, so a panel's own `kAction_Left` /
// `kAction_Right` handling wins over the shared table.
Node ButtonTable(std::vector<Node> extras) {
  std::vector<Node> kids = {Sym("switch"), Var("action")};
  for (Node& item : extras) {
    kids.push_back(std::move(item));
  }
  for (Node& item : ButtonCases()) {
    kids.push_back(std::move(item));
  }
  return Cmd(std::move(kids));
}

// `{new UIPanel <name> {file <milo>} {focus <list>} {enter ...} {exit ...} ...}`
Node Panel(const std::string_view name, const std::string_view scene, std::vector<Node> members) {
  std::vector<Node> kids = {Sym("new"), Sym("UIPanel"), Sym(name),
                            Arr({Sym("file"), Str(scene)}), Arr({Sym("focus"), Sym(kList)})};
  for (Node& member : members) {
    kids.push_back(std::move(member));
  }
  return Cmd(std::move(kids));
}

// `{new UIScreen <name> {panels ...} {focus <panel>} {back_screen <screen>}}`
Node Screen(const std::string_view name, const std::vector<std::string_view>& panels,
            const std::string_view focus, const std::string_view back) {
  std::vector<Node> panel_kids = {Sym("panels")};
  for (const std::string_view panel : panels) {
    panel_kids.push_back(Sym(panel));
  }
  return Cmd({Sym("new"), Sym("UIScreen"), Sym(name), Arr(std::move(panel_kids)),
              Arr({Sym("focus"), Sym(focus)}), Arr({Sym("back_screen"), Sym(back)})});
}

// `{get_focus {{this loaded_dir} get focus_component}}` - the retail pause panel's own accessor, so
// a caller can ask this panel where the focus is (`{this update_focus {get_focus}}`).
Node GetFocusMember() {
  return Arr({Sym("get_focus"),
              Cmd({Cmd({Var("this"), Sym("loaded_dir")}), Sym("get"), Sym("focus_component")})});
}

// `{enter {this populate_menu} {shell_sfx set_interceptor {this}}}` - the panel fills its list,
// then takes the input, exactly as retail's own `options_panel` does.
//
// **Why there is no `{this update_focus {get_focus}}` here.** P1's prototype had one, and it worked
// there - but only because P1's panel loaded *retail's* scene, whose `options.lst` retail's own
// `options_panel` had already focused at boot, so `{get_focus}` answered with the list. On a
// project-owned scene that has just been loaded, `focus_component` is not the list, so calling
// `update_focus` with it moves the focus *away* from the panel's declared `{focus options.lst}` and
// the list stops taking direction keys (measured: the merged root drew its five rows with the
// highlight pinned to row 0 through every press). Retail's own `options_panel` - the panel this one
// is modelled on - declares `{focus options.lst}` and does *not* call `update_focus` on enter.
Node EnterExit() {
  return Arr({Sym("enter"),
              Cmd({Sym("set"), Var("rbbz_settings_index"), Node::Int(0)}),
              Cmd({Sym("set"), Var("rbbz_settings_editing"), Node::Int(0)}),
              Cmd({Var("this"), Sym("populate_menu")}),
              Cmd({Sym("shell_sfx"), Sym("set_interceptor"), Var("this")})});
}

// ------------------------------------------------------------------------------------------------
// The merged surface, as data
// ------------------------------------------------------------------------------------------------
//
// Every row is a handful of facts: which symbol the list is given, how a press on it behaves, which
// bridge setting it addresses, and which screen it enters. A table rather than one function per
// panel is what keeps the whole surface (fifteen screens) reviewable, and what lets a test walk
// every row - symbol, label, request path, destination - without a boot.
//
// A row's *label* is the text its symbol resolves to in the locale. An empty `label` means the
// symbol is a retail row symbol whose text the installed locale already carries (`controls`,
// `calibration`, `audio_video_options`, `tutorial`, `credits`); reusing those is deliberate, because
// the merged root keeps the retail rows and their words.
//
// A row with `provider_only` set is dropped from its list when the gate query answers false, which
// is how a base install shows five root rows and an Ultimate install six (plan §3).

enum class Kind {
  kBool,         // boolean: an `_on`/`_off` symbol pair, Confirm flips it
  kSelect,       // horizontal select: one symbol per choice, Confirm advances, Left/Right pick
  kSubmenu,      // submenu: one symbol, Confirm enters `screen`
  kBoolSubmenu,  // boolean with a submenu: `_on`/`_off` symbols, Confirm enters `screen`
  kChoose,       // one-shot choice: set `setting` to the row's own token, commit, leave
  kAction,       // one-shot action: invoke `setting`
  kInfo,         // a row that is present but deliberately does nothing (the protected entry)
  kRoute,        // a retail screen: set its `member` to the current screen, then enter it
  kRestart,
  kClose,
  kCalibration,
  kRefresh,
  kAchievements,
};

struct Choice {
  std::string token;  // the stable wire token: never an index, never a label
  std::string label;  // English, for the locale entry
};

struct Row {
  std::string symbol;
  Kind kind = Kind::kBool;
  std::string setting;
  std::string screen;
  std::string member;
  std::string label;
  std::string prefix = "t:";  // the wire spelling of a value: `t:` token, `i:` integer
  bool provider_only = false;
  std::vector<Choice> choices;
};

struct PanelSpec {
  std::string name;
  std::string screen;
  std::string back;
  std::vector<std::string> panels;  // the shared retail panels this screen carries
  std::vector<Row> rows;
  std::string scene = kSceneName;  // the dir the panel loads, per context (see `kPauseSceneName`)
  std::string message;
};

Row BoolRow(const char* symbol, const char* setting, const char* label, bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kBool;
  row.setting = setting;
  row.label = label;
  row.provider_only = provider_only;
  return row;
}

Row SelectRow(const char* symbol, const char* setting, const char* label,
              std::vector<Choice> choices, bool provider_only = false, const char* prefix = "t:") {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kSelect;
  row.setting = setting;
  row.label = label;
  row.prefix = prefix;
  row.provider_only = provider_only;
  row.choices = std::move(choices);
  return row;
}

Row SubmenuRow(const char* symbol, const char* screen, const char* label,
               bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kSubmenu;
  row.screen = screen;
  row.label = label;
  row.provider_only = provider_only;
  return row;
}

Row BoolSubmenuRow(const char* symbol, const char* setting, const char* screen, const char* label,
                   bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kBoolSubmenu;
  row.setting = setting;
  row.screen = screen;
  row.label = label;
  row.provider_only = provider_only;
  return row;
}

Row ChooseRow(const char* symbol, const char* setting, const char* token, const char* label,
              bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kChoose;
  row.setting = setting;
  row.label = label;
  row.provider_only = provider_only;
  row.choices = {Choice{token, label}};
  return row;
}

Row ActionRow(const char* symbol, const char* setting, const char* label,
              bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kAction;
  row.setting = setting;
  row.label = label;
  row.provider_only = provider_only;
  return row;
}

Row InfoRow(const char* symbol, const char* label) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kInfo;
  row.label = label;
  return row;
}

Row RouteRow(const char* symbol, const char* screen, const char* member, const char* label) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kRoute;
  row.screen = screen;
  row.member = member;
  row.label = label;
  return row;
}

std::vector<Choice> Choices(std::initializer_list<const char*> tokensAndLabels) {
  std::vector<Choice> out;
  const std::vector<const char*> words(tokensAndLabels);
  for (size_t i = 0; i + 1 < words.size(); i += 2) {
    out.push_back(Choice{words[i], words[i + 1]});
  }
  return out;
}

// The panels a main-menu screen carries besides the settings panel, and the ones an in-song screen
// carries. Both are the retail sets for the matching context (compare `options_screen` with
// `av_options_pause_screen`).
std::vector<std::string> MenuPanels() {
  return {"background_night_panel", "shell_music_panel", "menu_cache_panel"};
}

std::vector<std::string> PausePanels() {
  // Retail's own in-song panel set, read out of the shipped records: `av_options_pause_screen`
  // carries `game_panel pause_panel background_night_panel`, and the two pages retail enters with a
  // plain `{ui goto_screen ...}` (`controller_config_pause_screen`, `pause_credits_screen`) add
  // `game_fade_out_panel`.
  //
  // Which of them a page of ours needs is the question P5b-r2 could not close: a screen of ours that
  // names **any** of them faults the guest with `read of guest 0x00000024` while it is constructed -
  // measured one panel at a time, and with the screen declared in the pause record instead - while
  // retail's own in-song screens name them and open. So the set is retail's shape and is *not*
  // usable by the in-song twins as they stand; see docs/plans/in-game-settings-merged-menu.md §3.2
  // for the matrix and §3.3 for the next arm. (An earlier reading of one arm had
  // `game_fade_out_panel` as the cure for that fault; the later arms show it is not the variable:
  // the fault appears with it and without it.)
  return {"game_panel", "pause_panel", "game_fade_out_panel", "background_night_panel"};
}

// The whole merged tree, in the plan's §3 order. Setting tokens are `<category>.<id>` on top of
// docs/plans/in-game-settings-contracts.md §5.1's stable ids.
//
// Two seams are handed over rather than faked, and are marked at the row:
//   * a row whose choices come from the *machine* (`RESOLUTION`, `RENDERER`, `A. FILTERING`,
//     `DLC SCAN THREADS`) carries a placeholder choice set here; P4's CapabilitySnapshot is what
//     fills it (P3's `choices_source` = renderers/resolutions/processor_count already models it),
//     and P10 owns that seam;
//   * a provider row's destination screen is reached through the bridge today and through the
//     installed Ultimate objects after P6.
std::vector<PanelSpec> AllPanels() {
  std::vector<PanelSpec> out;

  // Our own labels are spelled the way the retail rows beside them are spelled ("Controls",
  // "Calibration", "Audio/Video" - the options *page* locale strings), not in the all-caps the
  // main menu uses for its entries. The Main Menu mirror panel is the deliberate exception: it lists
  // the menu's own rows, so it mirrors the menu's own spelling ("PLAY", "EXIT GAME").

  // 0. The merged root: six rows, the retail three at their retail positions.
  out.push_back(PanelSpec{kRootPanel, kRootScreen, "splash_screen", MenuPanels(),
                          {
                              SubmenuRow("settings_general", "settings_general_screen", "General"),
                              RouteRow("controls", "controller_config_screen", "back_screen", ""),
                              RouteRow("calibration", "cal_welcome_screen", "cancel_screen", ""),
                              SubmenuRow("audio_video_options", "settings_av_screen", ""),
                              SubmenuRow("settings_gameplay", "settings_gameplay_screen", "Gameplay",
                                         true),
                              SubmenuRow("settings_interface", "settings_interface_screen",
                                         "Interface"),
                          }});

  // 1. General: the four rows the plan puts here, plus the two retail screens it adopts.
  out.push_back(PanelSpec{"settings_general_panel", "settings_general_screen", kRootScreen,
                          MenuPanels(),
                          {
                              SubmenuRow("settings_target", "settings_target_screen", "Target"),
                              SubmenuRow("settings_dlc", "settings_dlc_screen",
                                         "Downloadable Content"),
                              SubmenuRow("settings_achievements", "settings_achievements_screen",
                                         "Achievements & Unlocks", true),
                              RouteRow("tutorial", "tutorials_screen", "back_screen", ""),
                              RouteRow("credits", "credits_screen", "back_screen", ""),
                          }});

  // 1a. Target: choose one, commit, leave. `ultimate_mode`/`license_mask` are consumed before
  // Ultimate configures, so the row is restart-scoped (plan §5.5) and the restart prompt is P9's.
  out.push_back(PanelSpec{"settings_target_panel", "settings_target_screen", "settings_general_screen",
                          MenuPanels(),
                          {
                              ChooseRow("settings_target_common", "general.target", "common",
                                        "Rock Band Blitz"),
                              ChooseRow("settings_target_ultimate", "general.target", "ultimate",
                                        "Rock Band Blitz Ultimate"),
                              ChooseRow("settings_target_trial", "general.target", "demo",
                                        "Trial"),
                          }});

  // 1b. Downloadable Content.
  out.push_back(PanelSpec{"settings_dlc_panel", "settings_dlc_screen", "settings_general_screen",
                          MenuPanels(),
                          {
                              BoolRow("settings_dlc_cache", "general.cache_dlc_library",
                                      "Cache DLC Library"),
                              SelectRow("settings_dlc_threads", "general.dlc_scan_threads",
                                        "DLC Scan Threads",
                                        Choices({"1", "1", "2", "2", "4", "4", "8", "8"}),
                                        false, "i:"),
                              BoolRow("settings_dlc_songs", "general.dlc_menu_songs",
                                      "DLC Menu Songs"),
                          }});

  // 1c. Achievements & Unlocks (provider only).
  out.push_back(PanelSpec{"settings_achievements_panel", "settings_achievements_screen",
                          "settings_general_screen", MenuPanels(),
                          {
                              BoolRow("settings_unlock_all", "general.unlock_all", "Unlock All",
                                      true),
                              ActionRow("settings_unlock_online",
                                        "general.unlock_online_achievements",
                                        "Unlock Online Only Achievements", true),
                          }});

  // 3. Audio / Video: the Video group is ours, the Audio group is retail's own page (its rows are
  // console-backed through ConsoleSaveData, and re-implementing them is neither P5b's job nor
  // honest - `av_options_screen` is the page that owns them).
  out.push_back(PanelSpec{"settings_av_panel", "settings_av_screen", kRootScreen, MenuPanels(),
                          {
                              SubmenuRow("settings_video", "settings_video_screen", "Video"),
                              RouteRow("settings_audio", "av_options_screen", "back_screen",
                                       "Audio"),
                          }});

  // 3a. Video. Retail's own Overscan row stays on the retail page it is drawn on (plan §3.3's
  // fallback), so it is deliberately not duplicated here.
  out.push_back(PanelSpec{"settings_video_panel", "settings_video_screen", "settings_av_screen",
                          MenuPanels(),
                          {
                              SelectRow("settings_resolution", "graphics.resolution", "Resolution",
                                        Choices({"1280x720", "1280x720", "1920x1080", "1920x1080",
                                                 "2560x1440", "2560x1440", "3840x2160",
                                                 "3840x2160"})),
                              BoolRow("settings_vsync", "graphics.vsync", "V-Sync"),
                              SelectRow("settings_renderer", "graphics.renderer", "Renderer",
                                        Choices({"d3d12", "D3D12", "vulkan", "Vulkan"})),
                              BoolSubmenuRow("settings_aa", "graphics.anti_aliasing",
                                             "settings_aa_screen", "Anti-Aliasing"),
                              SelectRow("settings_filtering", "graphics.filtering",
                                        "Anisotropic Filtering",
                                        Choices({"native", "Native", "none", "None", "1x", "1x",
                                                 "2x", "2x", "4x", "4x", "8x", "8x", "16x",
                                                 "16x"})),
                          }});

  // 3b. Anti-aliasing: the compound row's choice list, "None" included (the Force-Skybox model).
  out.push_back(PanelSpec{"settings_aa_panel", "settings_aa_screen", "settings_video_screen",
                          MenuPanels(),
                          {
                              ChooseRow("settings_aa_none", "graphics.anti_aliasing", "none",
                                        "None"),
                              ChooseRow("settings_aa_native", "graphics.anti_aliasing", "native",
                                        "Native (2x)"),
                              ChooseRow("settings_aa_fxaa", "graphics.anti_aliasing", "fxaa",
                                        "FXAA"),
                              ChooseRow("settings_aa_fxaa_extreme", "graphics.anti_aliasing",
                                        "fxaa_extreme", "FXAA Extreme"),
                          }});

  // 4. Gameplay (provider only; the root row that appears with it is what makes the root six).
  out.push_back(PanelSpec{"settings_gameplay_panel", "settings_gameplay_screen", kRootScreen,
                          MenuPanels(),
                          {
                              BoolRow("settings_precision", "gameplay.precision_mode",
                                      "Precision Mode", true),
                              BoolRow("settings_wiggles", "gameplay.disable_random_wiggles",
                                      "Disable Random Track Wiggles", true),
                              SelectRow("settings_camera", "gameplay.camera_angle", "Camera Angle",
                                        Choices({"default", "Default", "high", "High", "low",
                                                 "Low"}),
                                        true),
                              SelectRow("settings_speed", "gameplay.game_speed", "Game Speed",
                                        Choices({"100", "100%", "105", "105%", "110", "110%",
                                                 "115", "115%", "120", "120%", "125", "125%",
                                                 "130", "130%", "135", "135%", "140", "140%",
                                                 "145", "145%", "150", "150%"}),
                                        true, "i:"),
                              BoolRow("settings_dof", "gameplay.disable_dof",
                                      "Disable Depth of Field", true),
                              SubmenuRow("settings_plots", "settings_plots_screen",
                                         "Plots & Skyboxes", true),
                              SelectRow("settings_gems", "gameplay.gems", "Gems",
                                        Choices({"default", "Default", "freq", "Freq", "amp2003",
                                                 "Amp 2003", "amp2016", "Amp 2016", "monkey",
                                                 "Monkey", "ulti", "Ulti", "x", "X", "bolt",
                                                 "Bolt"}),
                                        true),
                          }});

  // 4a. Plots & Skyboxes. `which_skybox` index 0 is the provider's default/off, and its "no
  // skybox" is a separate index - which is why the row is a select over tokens and never a
  // boolean (plan §3.4).
  out.push_back(PanelSpec{"settings_plots_panel", "settings_plots_screen",
                          "settings_gameplay_screen", MenuPanels(),
                          {
                              BoolRow("settings_disable_plots", "gameplay.disable_plots",
                                      "Disable Plots", true),
                              SelectRow("settings_force_plot", "gameplay.forceplot", "Force Plot",
                                        Choices({"off", "Off", "plot_a", "Plot A", "plot_b",
                                                 "Plot B"}),
                                        true),
                              SelectRow("settings_force_skybox", "gameplay.which_skybox",
                                        "Force Skybox",
                                        Choices({"default", "Default", "none", "No Skybox",
                                                 "custom", "Custom"}),
                                        true),
                          }});

  // 5. Interface.
  out.push_back(PanelSpec{"settings_interface_panel", "settings_interface_screen", kRootScreen,
                          MenuPanels(),
                          {
                              BoolRow("settings_letterbox", "interface.letterbox",
                                      "4:3 Letterboxing", true),
                              SubmenuRow("settings_hud", "settings_hud_screen", "HUD", true),
                              SubmenuRow("settings_hide_items", "settings_hide_items_screen",
                                         "Hide Items"),
                          }});

  // 5a. HUD: the provider's seven fields, original spellings preserved (`show_steak`).
  out.push_back(PanelSpec{"settings_hud_panel", "settings_hud_screen",
                          "settings_interface_screen", MenuPanels(),
                          {
                              BoolRow("settings_hud_hide", "interface.hide_hud", "Hide HUD", true),
                              BoolRow("settings_hud_gold", "interface.goldprogress",
                                      "Gold Star Progress", true),
                              BoolRow("settings_hud_progress", "interface.progressbar",
                                      "Song Progress Bar", true),
                              BoolRow("settings_hud_timer", "interface.show_timer", "Timer", true),
                              BoolRow("settings_hud_pb", "interface.show_pb",
                                      "Personal Best Progress", true),
                              BoolRow("settings_hud_streak", "interface.show_steak", "Note Streak",
                                      true),
                              BoolRow("settings_hud_input", "interface.input_viewer",
                                      "Input Viewer", true),
                          }});

  // 5b. Hide Items.
  out.push_back(PanelSpec{"settings_hide_items_panel", "settings_hide_items_screen",
                          "settings_interface_screen", MenuPanels(),
                          {
                              BoolRow("settings_offline", "interface.hide_offline_prompts",
                                      "Offline Mode Prompts"),
                              BoolSubmenuRow("settings_main_menu", "interface.hide_main_menu",
                                             "settings_main_menu_screen", "Main Menu"),
                          }});

  // 5b-i. Main Menu: the master plus every actual row, by stable token. `splash_options` is the
  // merged entry itself and is present but inert on purpose - hiding it would remove the only way
  // back into settings (plan §3.5).
  out.push_back(PanelSpec{"settings_main_menu_panel", "settings_main_menu_screen",
                          "settings_hide_items_screen", MenuPanels(),
                          {
                              BoolRow("settings_mm_master", "interface.hide_main_menu",
                                      "Hide Selected Rows"),
                              BoolRow("settings_mm_start", "interface.hidden_main_menu.splash_start",
                                      "PLAY"),
                              BoolRow("settings_mm_leaderboard",
                                      "interface.hidden_main_menu.splash_leaderboard",
                                      "LEADERBOARDS"),
                              BoolRow("settings_mm_achievements",
                                      "interface.hidden_main_menu.splash_achievements",
                                      "ACHIEVEMENTS"),
                              InfoRow("settings_mm_options", "HELP & OPTIONS"),
                              BoolRow("settings_mm_upgrade",
                                      "interface.hidden_main_menu.splash_upgrade", "UPGRADE"),
                              BoolRow("settings_mm_dlc", "interface.hidden_main_menu.splash_dlc",
                                      "DOWNLOAD CONTENT"),
                              BoolRow("settings_mm_exit", "interface.hidden_main_menu.splash_exit",
                                      "EXIT GAME"),
                          }});

  // The confirmation scene is copied from the installation under our own name, as with the menu.
  Row restart = InfoRow("settings_restart_now", "Restart now");
  restart.kind = Kind::kRestart;
  Row close = InfoRow("settings_close", "Close");
  close.kind = Kind::kClose;
  out.push_back(PanelSpec{"settings_restart_panel", "settings_restart_screen", kRootScreen,
                         MenuPanels(), {close, restart}, "../pause/settings_confirmation.milo",
                         "settings_restart_message"});
  out.push_back(PanelSpec{"settings_save_error_panel", "settings_save_error_screen", kRootScreen,
                         MenuPanels(), {close}, "../pause/settings_confirmation.milo",
                         "settings_save_error_message"});
  Row achievements = InfoRow("settings_earn_achievements", "Unlock online achievements");
  achievements.kind = Kind::kAchievements;
  out.push_back(PanelSpec{"settings_achievements_confirm_panel", "settings_achievements_confirm_screen", "settings_achievements_screen",
                         MenuPanels(), {close, achievements}, "../pause/settings_confirmation.milo",
                         "settings_achievements_message"});

  for (auto& panel : out) {
    if (panel.name == "settings_dlc_panel") {
      Row refresh = InfoRow("settings_refresh_dlc", "Refresh Downloadable Content");
      refresh.kind = Kind::kRefresh;
      panel.rows.push_back(std::move(refresh));
    }
    if (panel.name == "settings_main_menu_panel" && !g_configuration.menu_rows.empty()) {
      panel.rows.resize(1);
      size_t index = 0;
      for (const auto& [token, label] : g_configuration.menu_rows) {
        const std::string symbol = "settings_mm_live_" + std::to_string(index++);
        panel.rows.push_back(token == "splash_options"
            ? InfoRow(symbol.c_str(), label.c_str())
            : BoolRow(symbol.c_str(), ("interface.hidden_main_menu." + token).c_str(), label.c_str()));
      }
    }
    std::erase_if(panel.rows, [](Row& row) {
      if (row.setting == "general.unlock_online_achievements") {
        row.kind = Kind::kSubmenu;
        row.screen = "settings_achievements_confirm_screen";
      }
      const auto choices = g_configuration.choices.find(row.setting);
      if (choices != g_configuration.choices.end()) {
        if (row.kind == Kind::kSelect) {
          row.choices.clear();
          for (const auto& [token, label] : choices->second) row.choices.push_back({token, label});
          if (row.choices.empty()) { row.kind = Kind::kInfo; row.label += ": Unavailable"; }
        } else if (row.kind == Kind::kChoose) {
          return std::none_of(choices->second.begin(), choices->second.end(),
                             [&](const auto& c) { return c.first == row.choices.front().token; });
        }
      }
      const auto disabled = g_configuration.disabled.find(row.setting);
      if (disabled != g_configuration.disabled.end()) {
        row.kind = Kind::kInfo;
        row.label += ": " + disabled->second;
      }
      return false;
    });
  }
  return out;
}

// ------------------------------------------------------------------------------------------------
// The in-song context
// ------------------------------------------------------------------------------------------------
// Retail's own convention, read out of the shipped records rather than invented:
//
//   av_options_screen       {panels shell_music_panel menu_cache_panel background_night_panel av_options_panel}
//   av_options_pause_screen {panels game_panel pause_panel background_night_panel av_options_panel}
//   pause_credits_screen    {panels game_panel pause_panel game_fade_out_panel background_night_panel credits_panel}
//   controller_config_pause_screen  {panels game_panel pause_panel game_fade_out_panel
//                                            background_night_panel controller_config_panel}
//
// So an in-song page keeps the running game, the pause panel and the night background on screen,
// and its back target is `game_screen`. One panel is *shared* between the two contexts when its
// bodies never name a destination (`av_options_panel` leaves through
// `{ui goto_screen {{ui current_screen} get back_screen}}`, which is context-free). Ours name
// destinations, because a submenu has to, so each panel gets an in-song twin whose bodies point at
// the in-song twins below it.
//
// `PauseScreen` / `PausePanel` are public (they are declared in the header) and defined with the
// rest of the public surface below.

// Where a row's destination lives in the in-song context. Ours have twins by construction; a retail
// screen's twin is the one retail already ships, and the four that exist are named here.
// `cal_welcome_screen` is deliberately absent: retail's own in-song calibration route goes through
// `pause_confirm_quit_song_screen` and is driven by retail's own case, which the merge leaves alone.
std::vector<PanelSpec> InSongPanels() {
  std::vector<PanelSpec> out = AllPanels();
  for (PanelSpec& panel : out) {
    panel.name = PausePanel(panel.name);
    panel.screen = PauseScreen(panel.screen);
    panel.back = panel.back == "splash_screen" ? std::string("game_screen") : PauseScreen(panel.back);
    panel.scene = panel.message.empty() ? kPauseSceneName : "../pause/settings_confirmation_pause.milo";
    panel.panels = PausePanels();
    for (Row& row : panel.rows) {
      if (row.screen == "cal_welcome_screen") row.kind = Kind::kCalibration;
      row.screen = InSongDestination(row.screen);
    }
  }
  return out;
}

// The five symbols retail's `pause_panel`'s `{enter}` fills its inline list with, in retail's order.
// The merge refuses a record that no longer holds exactly these, so a changed retail record is
// reported rather than half-rewritten.
std::vector<std::string> RetailPauseRows() {
  return {"pause_tutorial", "pause_controls", "calibration", "audio_video_options", "credits"};
}

// `{do {options.lst set_data (a b c)} {options.lst set_selected 0}}` - the pair `FillStatements`
// emits above, wrapped in one command so it can be an `if_else` branch (each branch is a single
// command). The selection statement is not optional, and P5b-r2 measured that this is what the
// inline pause list needs as much as our own panels do: a list keeps its *selected* symbol across a
// `set_data`, so a fill that does not place the selection leaves the list dispatching a row that is
// no longer in it - the press then matches no case in the panel's switch and the pause menu closes
// instead of opening the row (measured: no case marker fired, no page entered).
Node FillList(const std::vector<std::string>& rows) {
  std::vector<Node> symbols;
  for (const std::string& row : rows) {
    symbols.push_back(Sym(row));
  }
  return Cmd({Sym("do"), Cmd({Sym(kListName), Sym("set_data"), Arr(std::move(symbols))}),
              Cmd({Sym(kListName), Sym("set_selected"), Node::Int(0)})});
}

// ------------------------------------------------------------------------------------------------
// The generator
// ------------------------------------------------------------------------------------------------

// A stable, symbol-safe spelling of a choice token, so `graphics.resolution`'s `1920x1080` names a
// row symbol the locale can key on. Only the characters DTA identifiers already use are kept.
std::string SymbolToken(const std::string_view token) {
  std::string out;
  for (const char c : token) {
    out.push_back((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                          c == '_' || c == '-'
                      ? c
                      : '_');
  }
  return out;
}

// The symbols this row can contribute to its list, in the order they appear in the literal array.
// A boolean contributes its pair *adjacent*, so that dropping one leaves the other in the row's
// position.
std::vector<std::string> RowSymbols(const Row& row) {
  switch (row.kind) {
    case Kind::kBool:
    case Kind::kBoolSubmenu:
      return {row.symbol + "_on", row.symbol + "_off"};
    case Kind::kSelect: {
      std::vector<std::string> out;
      for (const Choice& choice : row.choices) {
        out.push_back(row.symbol + "_" + SymbolToken(choice.token));
      }
      return out;
    }
    default:
      return {row.symbol};
  }
}

// The wire spelling of one choice: `t:<token>` for an enum, a resolution or a provider symbol,
// `i:<n>` for a count.
std::string WireValue(const Row& row, const std::string_view token) {
  return std::string(row.prefix) + std::string(token);
}

std::string ChoiceQuery(const Row& row, const Choice& choice) {
  return Path("snapshot", row.setting, WireValue(row, choice.token));
}

std::string ChoiceSet(const Row& row, const Choice& choice) {
  return Path("set", row.setting, WireValue(row, choice.token));
}

// `is this setting currently true?` - the read half of a boolean row.
std::string QueryPath(const Row& row) {
  return Path("snapshot", row.setting, "b:1");
}

// `set this setting to <value>` - the write half.
std::string SetPath(const Row& row, const std::string_view value) {
  return Path("set", row.setting, value);
}

// `{remove_elem rows <symbol>}` - the only array primitive this surface uses (both retail panels
// that build a row list use it).
Node Drop(const std::string_view symbol) {
  return Cmd({Sym("remove_elem"), Var("rows"), Sym(symbol)});
}

Node DropAll(const std::vector<std::string>& symbols) {
  std::vector<Node> kids = {Sym("do")};
  for (const std::string& symbol : symbols) {
    kids.push_back(Drop(symbol));
  }
  return Cmd(std::move(kids));
}

// `populate_menu`: build the row array, drop what does not apply, then fill the list.
//
//   `{var rows {array (<every symbol>)} }`   - the literal array holds both spellings of every
//                                              boolean, adjacent, plus every choice of every select;
//   `{if {! <gate>} {do {remove_elem ...}}}` - a provider-hidden row (and its whole screen) goes;
//   `{if_else <query> <drop off> <drop on>}` - a boolean keeps the spelling that matches;
//   `{if {! <choice query>} <drop choice>}`  - a select drops every choice that is not current,
//   `{if <choice query> <drop first>}`         and the first choice is the default that stays when
//                                              the store holds no value yet.
Node PopulateMenu(const PanelSpec& panel) {
  std::vector<Node> body;

  std::vector<Node> literal;
  for (const Row& row : panel.rows) {
    for (const std::string& symbol : RowSymbols(row)) {
      literal.push_back(Sym(symbol));
    }
  }
  // `{var rows {array (...)}}` - and the tag matters. This compiler encodes a **declaration** as an
  // *array* whose first child is the var (`<array>[var rows]<command>{array ...}`), while a
  // `<command>` whose first child is a var is an *expression* that reads one (that is what
  // `{this populate_menu}` is). Emitting the declaration as a command compiles, parses, recompiles
  // byte for byte, and then crashes the guest on entry with `read of guest 0x00000008` - measured.
  // `TestDoTakesStatementsDirectly` pins the shape.
  body.push_back(Arr({Var("rows"), Cmd({Sym("array"), Arr(std::move(literal))})}));

  std::vector<std::string> hidden;
  for (const Row& row : panel.rows) {
    if (!row.provider_only) {
      continue;
    }
    for (const std::string& symbol : RowSymbols(row)) {
      hidden.push_back(symbol);
    }
  }
  if (!hidden.empty()) {
    body.push_back(Cmd({Sym("if"), Cmd({Sym("!"), Cmd({Sym("file_exists"), Str(kProviderQuery)})}),
                        DropAll(hidden)}));
  }

  for (const Row& row : panel.rows) {
    if (row.kind == Kind::kBool || row.kind == Kind::kBoolSubmenu) {
      const std::string on = row.symbol + "_on";
      const std::string off = row.symbol + "_off";
      body.push_back(Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(QueryPath(row))}),
                          Drop(off), Drop(on)}));
    } else if (row.kind == Kind::kSelect) {
      for (size_t i = 1; i < row.choices.size(); ++i) {
        body.push_back(Cmd({Sym("if"), Cmd({Sym("!"), Cmd({Sym("file_exists"),
                                                           Str(ChoiceQuery(row, row.choices[i]))})}),
                            Drop(row.symbol + "_" + SymbolToken(row.choices[i].token))}));
      }
      for (size_t i = 1; i < row.choices.size(); ++i) {
        body.push_back(Cmd({Sym("if"), Cmd({Sym("file_exists"), Str(ChoiceQuery(row, row.choices[i]))}),
                            Drop(row.symbol + "_" + SymbolToken(row.choices[0].token))}));
      }
    }
  }

  for (Node& statement : FillStatements()) {
    body.push_back(std::move(statement));
  }
  // The statements have to be `do`'s *direct* children - `{do <stmt> <stmt> ...}`. Handing `do` a
  // single array of statements instead compiles and parses, and then silently does nothing, which
  // is exactly how a screen can open with an empty list (measured: the merged root drew its title
  // and no rows at all). `TestEveryDoHasStatements` pins it.
  std::vector<Node> sequence = {Sym("do")};
  for (Node& statement : body) {
    sequence.push_back(std::move(statement));
  }
  return Arr({Sym("populate_menu"), Cmd(std::move(sequence))});
}

// `{<list>.lst set_data {var rows}}` is in FillStatements; this builds the whole `{do ...}`.

// The `select_row` case for one row: what Confirm (or Start) does on it.
Node CommitResult(const bool pause, Node success) {
  const auto destination = [&](const char* name) { return pause ? PauseScreen(name) : std::string(name); };
  return Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(Path("snapshot", "commit.ok", "b:1"))}),
    Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(Path("snapshot", "commit.restart_required", "b:1"))}),
         EnterScreen(destination("settings_restart_screen")), std::move(success)}),
    EnterScreen(destination("settings_save_error_screen"))});
}

bool g_emitting_pause = false;
Node RowCase(const Row& row, const std::string_view symbol) {
  switch (row.kind) {
    case Kind::kBool: {
      const bool is_on = symbol == std::string(row.symbol) + "_on";
      return Arr({Sym(symbol), Write(SetPath(row, is_on ? "b:0" : "b:1")),
                  Write(Path("commit", "-", "-")), CommitResult(g_emitting_pause, Cmd({Var("this"), Sym("populate_menu")}))});
    }
    case Kind::kSelect: {
      return Arr({Sym(symbol), Cmd({Sym("if_else"), Var("rbbz_settings_editing"),
        Cmd({Sym("do"), Write(Path("commit", "-", "-")),
          Cmd({Sym("set"), Var("rbbz_settings_editing"), Node::Int(0)}),
          CommitResult(g_emitting_pause, Cmd({Var("this"), Sym("populate_menu")}))}),
        Cmd({Sym("do"), Cmd({Sym("set"), Var("rbbz_settings_editing"), Node::Int(1)}),
          Cmd({Sym("shell_sfx"), Sym("play_select")})})})});
    }
    case Kind::kSubmenu:
    case Kind::kBoolSubmenu:
      return Arr({Sym(symbol), EnterScreen(row.screen)});
    case Kind::kChoose:
      return Arr({Sym(symbol), Write(ChoiceSet(row, row.choices.front())),
                  Write(Path("commit", "-", "-")), CommitResult(g_emitting_pause, Leave())});
    case Kind::kAction:
      return Arr({Sym(symbol), Write(Path("invoke", row.setting, "-"))});
    case Kind::kInfo:
      return Arr({Sym(symbol), Cmd({Var("this"), Sym("populate_menu")})});
    case Kind::kRoute:
      return Arr({Sym(symbol), EnterRetail(row.screen, row.member)});
    case Kind::kClose:
      return Arr({Sym(symbol), Leave()});
    case Kind::kRestart:
      return Arr({Sym(symbol), Write(Path("restart", "-", "-")),
          Cmd({Sym("if"), Cmd({Sym("!"), Cmd({Sym("file_exists"), Str(Path("snapshot", "restart.ok", "b:1"))})}),
               Cmd({Sym("pause.lbl"), Sym("set_token_fmt"), Sym("settings_restart_error")})})});
    case Kind::kCalibration:
      return Arr({Sym(symbol), Cmd({Sym("pause_confirm_quit_song_screen"), Sym("set"),
          Sym("next_screen"), Sym("cal_welcome_screen")}),
          Cmd({Sym("options_screen"), Sym("set"), Sym("back_screen"), Sym("splash_screen")}),
          Cmd({Sym("ui"), Sym("goto_screen"), Sym("pause_confirm_quit_song_screen")})});
    case Kind::kRefresh:
      return Arr({Sym(symbol), Cmd({Sym("file_exists"), Str("songcache:/rbbz_dlc_refresh")})});
    case Kind::kAchievements:
      // The installed provider's four named grants, executed by its own guest API. No host-built
      // DataNodes or untraced allocator ABI. The session latch makes a repeated confirm harmless.
      return Arr({Sym(symbol), Cmd({Sym("if"), Cmd({Sym("!"), Var("rbbz_achievements_granted")}),
          Cmd({Sym("do"), Cmd({Sym("set"), Var("rbbz_achievements_granted"), Node::Int(1)}),
            Cmd({Sym("foreach"), Var("acc"), Arr({Sym("acc_high_cred"), Sym("acc_higher_cred"),
              Sym("acc_total_coins_spent"), Sym("acc_create_challenge")}),
              Cmd({Sym("acc_mgr"), Sym("earn_accomplishment"), Var("acc")})})})}), Leave()});
  }
  return Arr({Sym(symbol)});
}

// Every case the row table needs, in row order.
std::vector<Node> RowCases(const PanelSpec& panel) {
  std::vector<Node> cases;
  for (const Row& row : panel.rows) {
    for (const std::string& symbol : RowSymbols(row)) {
      cases.push_back(RowCase(row, symbol));
    }
  }
  return cases;
}

// `{switch {options.lst selected_sym} <cases...>}` inside a `select_row` method.
Node SelectRowCases(const PanelSpec& panel) {
  std::vector<Node> kids = {Sym("switch"), Cmd({Sym(kList), Sym("selected_sym")})};
  for (Node& item : RowCases(panel)) {
    kids.push_back(std::move(item));
  }
  return Arr({Sym("select_row"), Cmd(std::move(kids))});
}

// Left/Right on a select row walk its choices, and nowhere else: a nested switch on the selected
// symbol keeps the arrows from doing anything on a row that has no direction.
std::vector<Node> DirectionCases(const PanelSpec& panel, const bool forward) {
  std::vector<Node> cases;
  for (const Row& row : panel.rows) {
    if (row.kind != Kind::kSelect) {
      continue;
    }
    for (size_t i = 0; i < row.choices.size(); ++i) {
      const size_t target =
          forward ? (i + 1) % row.choices.size()
                  : (i + row.choices.size() - 1) % row.choices.size();
      cases.push_back(Arr({Sym(row.symbol + "_" + SymbolToken(row.choices[i].token)),
                           Write(ChoiceSet(row, row.choices[target])),
                           Cmd({Var("this"), Sym("populate_menu")})}));
    }
  }
  return cases;
}

// ------------------------------------------------------------------------------------------------
// Navigation
// ------------------------------------------------------------------------------------------------
//
// The panel drives its own list position rather than leaving it to the engine's focus chain, and
// that is a measured decision, not a preference. On this surface the direction keys reach the
// *panel* (Confirm provably does - it opens the focused row's screen), the list never becomes the
// focus component, and the highlight stays on row 0 through every press: adding retail's
// `{this update_focus {get_focus}}` to `enter` does not change it (it makes it worse - the freshly
// loaded scene has no focus component to hand the list), and neither does creating the panels the
// way retail creates `options_panel`, with `{<panel> load}` in `ui/gen/ui.dtb`'s init.
//
// So `move_up` / `move_down` walk the list with the one primitive that provably moves the drawn
// selection: `{<list>.lst set_selected <index>}` - the same call `populate_menu` uses to place the
// selection, and the reason the first row is drawn highlighted. Each method is a switch on the
// list's *selected symbol* (so no index state has to be kept anywhere), with one table per provider
// state, because a base install draws five rows where an Ultimate install draws six.

// One cell of the drawn list: a row, every symbol that can occupy it, and its index.
struct Cell {
  const Row* row = nullptr;
  std::vector<std::string> symbols;
  size_t index = 0;
};

std::vector<Cell> Cells(const PanelSpec& panel, const bool provider_active) {
  std::vector<Cell> cells;
  for (const Row& row : panel.rows) {
    if (row.provider_only && !provider_active) {
      continue;
    }
    Cell cell;
    cell.row = &row;
    cell.symbols = RowSymbols(row);
    cell.index = cells.size();
    cells.push_back(std::move(cell));
  }
  return cells;
}

// `{<list>.lst set_selected <index>}`
Node SelectIndex(const size_t index) {
  return Cmd({Sym("do"),
    Cmd({Sym("set"), Var("rbbz_settings_index"), Node::Int(static_cast<int>(index))}),
    Cmd({Sym(kList), Sym("set_selected"), Var("rbbz_settings_index")})});
}

Node DirectionTable(const std::vector<Cell>& cells, const bool forward) {
  std::vector<Node> kids = {Sym("switch"), Cmd({Sym(kList), Sym("selected_sym")})};
  for (size_t i = 0; i < cells.size(); ++i) {
    // Clamp at both ends rather than wrap: a wrap needs the user to notice they have left the
    // bottom, and the first row is where `populate_menu` places the selection.
    if (forward ? i + 1 >= cells.size() : i == 0) {
      continue;
    }
    const size_t target = forward ? i + 1 : i - 1;
    for (const std::string& symbol : cells[i].symbols) {
      kids.push_back(Arr({Sym(symbol), SelectIndex(target)}));
    }
  }
  return Cmd(std::move(kids));
}

// `{move_down {if_else <gate> <six-row table> <five-row table>}}`
Node MoveMember(const PanelSpec& panel, const char* name, const bool forward) {
  const bool has_provider_row = [&panel] {
    for (const Row& row : panel.rows) {
      if (row.provider_only) {
        return true;
      }
    }
    return false;
  }();
  Node body = DirectionTable(Cells(panel, false), forward);
  if (has_provider_row) {
    body = Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(kProviderQuery)}),
               DirectionTable(Cells(panel, true), forward), std::move(body)});
  }
  return Arr({Sym(name), Cmd({Sym("if"), Cmd({Sym("!"), Var("rbbz_settings_editing")}), std::move(body)})});
}

Node PanelDefinition(const PanelSpec& panel) {
  g_emitting_pause = panel.scene == kPauseSceneName ||
                     panel.scene == "../pause/settings_confirmation_pause.milo";
  Node enter = EnterExit();
  if (!panel.message.empty())
    enter.kids.push_back(Cmd({Sym("pause.lbl"), Sym("set_token_fmt"), Sym(panel.message)}));
  std::vector<Node> extras;
  extras.push_back(Arr({Sym("kAction_Up"), Cmd({Var("this"), Sym("move_up")})}));
  extras.push_back(Arr({Sym("kAction_Down"), Cmd({Var("this"), Sym("move_down")})}));
  std::vector<Node> left = DirectionCases(panel, false);
  if (!left.empty()) {
    std::vector<Node> kids = {Sym("switch"), Cmd({Sym(kList), Sym("selected_sym")})};
    for (Node& item : left) {
      kids.push_back(std::move(item));
    }
    extras.push_back(Arr({Sym("kAction_Left"), Cmd({Sym("if"), Var("rbbz_settings_editing"), Cmd(std::move(kids))})}));
    std::vector<Node> right = DirectionCases(panel, true);
    std::vector<Node> rkids = {Sym("switch"), Cmd({Sym(kList), Sym("selected_sym")})};
    for (Node& item : right) {
      rkids.push_back(std::move(item));
    }
    extras.push_back(Arr({Sym("kAction_Right"), Cmd({Sym("if"), Var("rbbz_settings_editing"), Cmd(std::move(rkids))})}));
  }

  return Panel(panel.name, panel.scene,
               {
                   PopulateMenu(panel),
                   // The panel's own navigation, and why it is not the engine's: see above.
                   MoveMember(panel, "move_up", false),
                   MoveMember(panel, "move_down", true),
                   std::move(enter),
                   GetFocusMember(),
                   Arr({Sym("exit"), Cmd({Sym("shell_sfx"), Sym("clear_interceptor")})}),
                   Arr({Sym("on_select_msg"), Cmd({Sym("shell_sfx"), Sym("play_select")})}),
                   SelectRowCases(panel),
                   Arr({Sym("BUTTON_DOWN_MSG"), ButtonTable(std::move(extras))}),
               });
}

Node ScreenDefinition(const PanelSpec& panel) {
  std::vector<std::string_view> panels;
  for (const std::string& name : panel.panels) {
    panels.push_back(name);
  }
  panels.push_back(panel.name);
  return Screen(panel.screen, panels, panel.name, panel.back);
}

// (The P1 prototype's `SubPanel`/`CompoundPanel` are gone: the four row kinds are generated from
// the table above, so a screen no longer needs a function of its own.)

// ------------------------------------------------------------------------------------------------
// The edits
// ------------------------------------------------------------------------------------------------

bool IsSymbol(const Node& node, const std::string_view name) {
  return node.tag == dtb::kSymbol && node.text == name;
}

bool IsBlock(const Node& node) {
  return node.tag == dtb::kArray || node.tag == dtb::kCommand || node.tag == dtb::kProperty;
}

// Depth-first search over block children. Returns the first node satisfying `match`, and the
// parent and index so an edit can splice beside it.
struct Hit {
  Node* parent = nullptr;
  size_t index = 0;
  bool found = false;
};

template <typename Match>
void FindBlock(Node* node, Match match, Hit* hit) {
  if (hit->found || node == nullptr || !IsBlock(*node)) {
    return;
  }
  for (size_t i = 0; i < node->kids.size() && !hit->found; ++i) {
    if (match(node->kids[i])) {
      hit->parent = node;
      hit->index = i;
      hit->found = true;
      return;
    }
    FindBlock(&node->kids[i], match, hit);
  }
}

// The same search across a whole file: the first match in document order. A retail definition is
// many roots (ui/gen/ui.dtb has twelve, and the `{init ...}` block is the sixth), so a search that
// only looked at root 0 would find nothing and refuse.
template <typename Match>
void FindInRoots(std::vector<Node>* nodes, Match match, Hit* hit) {
  if (nodes == nullptr) {
    return;
  }
  for (Node& root : *nodes) {
    FindBlock(&root, match, hit);
    if (hit->found) {
      return;
    }
  }
}

// A row *case* is an array whose first element is the row symbol and whose remaining elements are
// the statements that run - `[splash_options {options_screen set back_screen splash_screen}
// {ui goto_screen options_screen}]`. The discriminator against a row *list* (an array of symbols,
// such as the main menu's option array, any of which could be first) is that a case's tail is made
// of blocks; and against a nested row table by the switch check below.
bool IsRowCase(const Node& node) {
  if (node.tag != dtb::kArray || node.kids.size() < 2 || node.kids[0].tag != dtb::kSymbol) {
    return false;
  }
  for (size_t i = 1; i < node.kids.size(); ++i) {
    if (!IsBlock(node.kids[i])) {
      return false;
    }
  }
  return true;
}

// `{switch ...}` - the command a row case has to sit directly inside.
bool IsSwitchCommand(const Node& node) {
  return node.tag == dtb::kCommand && !node.kids.empty() && IsSymbol(node.kids[0], "switch");
}

// `{<panel> load}`
bool IsPanelLoad(const Node& node, const std::string_view panel) {
  return node.tag == dtb::kCommand && node.kids.size() == 2 && IsSymbol(node.kids[0], panel) &&
         IsSymbol(node.kids[1], "load");
}

// `{set {var first_screen} ...}` - the statement that ends the retail init block.
bool IsFirstScreenSet(const Node& node) {
  return node.tag == dtb::kCommand && node.kids.size() >= 2 && IsSymbol(node.kids[0], "set") &&
         node.kids[1].tag == dtb::kVar && node.kids[1].text == "first_screen";
}

}  // namespace

void ConfigureMenu(MenuConfiguration configuration) { g_configuration = std::move(configuration); }

// Every request the surface can send or ask, derived from the same table the definition is built
// from, so a row cannot exist without its path being known - and so the test that parses each path
// with the bridge's grammar covers the whole surface rather than the one row someone remembered.
std::vector<std::string> RequestPaths() {
  std::vector<std::string> out;
  auto add = [&out](const std::string& path) {
    if (std::find(out.begin(), out.end(), path) == out.end()) {
      out.push_back(path);
    }
  };
  add(kProviderQuery);
  add(Path("commit", "-", "-"));
  add(Path("restart", "-", "-"));
  // The one-shot edit cancel: B while a horizontal selector is being staged.
  add(Path("cancel", "-", "-"));
  for (const auto* token : {"commit.ok", "commit.restart_required", "restart.ok"})
    add(Path("snapshot", token, "b:1"));
  for (const PanelSpec& panel : AllPanels()) {
    for (const Row& row : panel.rows) {
      if (row.setting.empty()) {
        continue;
      }
      switch (row.kind) {
        case Kind::kBool:
        case Kind::kBoolSubmenu:
          add(QueryPath(row));
          add(SetPath(row, "b:1"));
          add(SetPath(row, "b:0"));
          break;
        case Kind::kSelect:
          for (const Choice& choice : row.choices) {
            add(ChoiceQuery(row, choice));
            add(ChoiceSet(row, choice));
          }
          break;
        case Kind::kChoose:
          add(ChoiceSet(row, row.choices.front()));
          break;
        case Kind::kAction:
          add(Path("invoke", row.setting, "-"));
          break;
        default:
          break;
      }
    }
  }
  return out;
}

std::string PauseScreen(std::string_view screen) {
  constexpr std::string_view kSuffix = "_screen";
  if (screen.size() > kSuffix.size() &&
      screen.substr(screen.size() - kSuffix.size()) == kSuffix) {
    return std::string(screen.substr(0, screen.size() - kSuffix.size())) + "_pause_screen";
  }
  return std::string(screen);
}

std::string PausePanel(std::string_view panel) {
  constexpr std::string_view kSuffix = "_panel";
  if (panel.size() > kSuffix.size() && panel.substr(panel.size() - kSuffix.size()) == kSuffix) {
    return std::string(panel.substr(0, panel.size() - kSuffix.size())) + "_pause_panel";
  }
  return std::string(panel);
}

std::string InSongDestination(std::string_view screen) {
  if (screen.empty()) {
    return {};
  }
  if (screen == "tutorials_screen") {
    return "tutorials_pause_screen";
  }
  if (screen == "credits_screen") {
    return "pause_credits_screen";
  }
  if (screen == "av_options_screen") {
    return "av_options_pause_screen";
  }
  if (screen == "controller_config_screen") {
    return "controller_config_pause_screen";
  }
  if (screen == "cal_welcome_screen") {
    return std::string(screen);
  }
  return PauseScreen(screen);
}

std::vector<dtb::Node> DefinitionRoots() {
  std::vector<dtb::Node> roots;
  // Both contexts, main menu first: a page and then its in-song twin, so the pair sits together in
  // the record a reader dumps.
  for (const std::vector<PanelSpec>& context : {AllPanels(), InSongPanels()}) {
    for (const PanelSpec& panel : context) {
      roots.push_back(PanelDefinition(panel));
      roots.push_back(ScreenDefinition(panel));
    }
  }
  return roots;
}

std::vector<dtb::Node> EarlyDefinitionRoots() {
  std::vector<dtb::Node> roots;
  for (const PanelSpec& panel : AllPanels()) {
    roots.push_back(PanelDefinition(panel));
    roots.push_back(ScreenDefinition(panel));
  }
  for (const PanelSpec& panel : InSongPanels()) {
    roots.push_back(PanelDefinition(panel));
  }
  return roots;
}

bool AddDeferredScreens(std::vector<dtb::Node>* nodes, std::string* reason) {
  Hit anchor;
  FindInRoots(nodes, IsFirstScreenSet, &anchor);
  if (!anchor.found || anchor.parent == nullptr) {
    if (reason) *reason = "no first_screen assignment after UI initialization";
    return false;
  }
  std::vector<Node> screens;
  for (const PanelSpec& panel : InSongPanels()) {
    screens.push_back(ScreenDefinition(panel));
  }
  for (const auto& [variable, value] : g_configuration.boot_booleans)
    screens.push_back(Cmd({Sym("set"), Var(variable), Node::Int(value ? 1 : 0)}));
  anchor.parent->kids.insert(anchor.parent->kids.begin() + anchor.index,
                             screens.begin(), screens.end());
  return true;
}

std::vector<std::string> PanelNames() {
  std::vector<std::string> out;
  for (const std::vector<PanelSpec>& context : {AllPanels(), InSongPanels()}) {
    for (const PanelSpec& panel : context) {
      out.push_back(panel.name);
    }
  }
  return out;
}

std::vector<std::string> ScreenNames() {
  std::vector<std::string> out;
  for (const std::vector<PanelSpec>& context : {AllPanels(), InSongPanels()}) {
    for (const PanelSpec& panel : context) {
      out.push_back(panel.screen);
    }
  }
  return out;
}

std::vector<std::string> PauseRootRows(const bool provider_active) {
  // The merged root's own order (plan §3), in the **in-song spellings**: retail's pause switch
  // dispatches `pause_controls` (its in-song name for the Controls page; the main-menu page spells
  // the same row `controls`), so reusing retail's case is what keeps that row working without a
  // new body. `settings_gameplay` is dropped on a boot whose provider gate answers false - the same
  // rule the root panel applies to itself, applied here to the list retail's pause panel fills.
  std::vector<std::string> rows{"settings_general", "pause_controls", "calibration",
                                "audio_video_options"};
  if (provider_active) {
    rows.push_back("settings_gameplay");
  }
  rows.push_back("settings_interface");
  return rows;
}

std::vector<dtb::Node> MainMenuRouteBody() {
  return {Cmd({Sym(kRootScreen), Sym("set"), Sym("back_screen"), Sym("splash_screen")}),
          Cmd({Sym("ui"), Sym("goto_screen"), Sym(kRootScreen)})};
}

std::vector<dtb::Node> PauseRouteBody() {
  // Retail's own in-song behaviour, and the measured reason it is the one that ships: the pause
  // menu's HELP & OPTIONS row *switches the focus* to the pause panel's inline `options.lst`
  // (`{this update_focus options.lst}`). Four bodies that leave the pause list instead were tried
  // and all four fault the guest with `read of guest 0x00000024` on the pad's Confirm:
  // `{transition_to settings_pause_screen}` (retail's own primitive from that panel),
  // `{ui goto_screen settings_pause_screen}`, the focus switch followed by the jump in the same
  // frame, and the same with a retail panel in the destination screen. A control boot with this
  // record left pristine runs the in-song route clean, so the fault is the *row*, not our panel or
  // our screen.
  //
  // What that measurement does **not** say - and what P5b-r2 measured separately - is that the
  // switch those five rows are dispatched in cannot jump: retail's own `pause_controls` and `credits`
  // cases in that same switch jump to in-song screens and work. So the merge lives where retail's
  // own in-song options page lives: in `options.lst`'s contents (`MergePauseListRows`) and in the
  // cases that dispatch them (`AddPauseSwitchCase`). This row stays retail's statement.
  return {Cmd({Var("this"), Sym("update_focus"), Sym(kList)})};
}

bool RerouteRow(std::vector<dtb::Node>* nodes, const std::string_view row,
                const std::vector<dtb::Node>& body, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || row.empty() || body.empty()) {
    return refuse("nothing to route");
  }
  Hit hit;
  FindInRoots(
      nodes, [&](const Node& node) { return IsRowCase(node) && IsSymbol(node.kids[0], row); },
      &hit);
  if (!hit.found || hit.parent == nullptr || !IsSwitchCommand(*hit.parent)) {
    return refuse("no {switch ...} case dispatching that row");
  }
  Node& kase = hit.parent->kids[hit.index];
  kase.kids.resize(1);  // keep the row symbol, drop whatever body the shipped case had
  for (const Node& statement : body) {
    kase.kids.push_back(statement);
  }
  if (reason != nullptr) reason->clear();
  return true;
}

// The P1 prototype's two edits (`AddPanelLoad` into ui/gen/ui.dtb, `AddMainMenuRow` into the splash
// option array) are gone with the Advanced Settings row they served. The merge does not add a row:
// it re-points the retail `splash_options` / `pause_options` rows (RerouteRow above), so the locale
// entry the old row needed is gone too - a payload generated from the retail records has no trace
// of `advanced_settings`, which is what retiring it means here.

std::vector<dtb::Node> InSongRowBody(const std::string_view screen) {
  return {EnterScreen(InSongDestination(screen))};
}

bool MergePauseListRows(std::vector<dtb::Node>* nodes, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr) {
    return refuse("nothing to edit");
  }
  Hit hit;
  FindInRoots(
      nodes,
      [](const Node& node) {
        return node.tag == dtb::kCommand && node.kids.size() == 3 &&
               IsSymbol(node.kids[0], kListName) && IsSymbol(node.kids[1], "set_data");
      },
      &hit);
  if (!hit.found || hit.parent == nullptr || !IsBlock(*hit.parent)) {
    return refuse("no {options.lst set_data ...} statement to merge into");
  }
  const Node& statement = hit.parent->kids[hit.index];
  const Node& array = statement.kids[2];
  const std::vector<std::string> retail = RetailPauseRows();
  if (array.tag != dtb::kArray || array.kids.size() != retail.size()) {
    return refuse("the inline list is no longer filled with retail's five rows");
  }
  for (size_t i = 0; i < retail.size(); ++i) {
    if (!IsSymbol(array.kids[i], retail[i])) {
      return refuse("the inline list is no longer filled with retail's five rows");
    }
  }
  // The fill becomes a pair, chosen by the provider gate: the six-row list where an Ultimate payload
  // is active, the five-row one where it is not. `if_else` rather than two statements, because only
  // one of them may run.
  const Node gate =
      Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(kProviderQuery)}), FillList(PauseRootRows(true)),
           FillList(PauseRootRows(false))});
  hit.parent->kids[hit.index] = gate;
  if (reason != nullptr) reason->clear();
  return true;
}

bool AddPauseSwitchCase(std::vector<dtb::Node>* nodes, const std::string_view row,
                        const std::vector<dtb::Node>& body, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || row.empty() || body.empty()) {
    return refuse("nothing to add");
  }
  // The switch that dispatches the inline list: the one carrying retail's own `pause_controls` case.
  // That symbol is a row of `options.lst` only, so it identifies the table without depending on the
  // switch's position in the file.
  Hit hit;
  FindInRoots(
      nodes,
      [](const Node& node) {
        if (node.tag != dtb::kCommand || node.kids.size() < 2 ||
            !IsSymbol(node.kids[0], "switch")) {
          return false;
        }
        for (size_t i = 1; i < node.kids.size(); ++i) {
          if (IsRowCase(node.kids[i]) && IsSymbol(node.kids[i].kids[0], "pause_controls")) {
            return true;
          }
        }
        return false;
      },
      &hit);
  if (!hit.found || hit.parent == nullptr) {
    return refuse("no {switch ...} dispatching the pause panel's inline list");
  }
  Node& table = hit.parent->kids[hit.index];
  // A switch takes its first matching case, so a second case for the same row would be dead code
  // that looks alive in a dump. Refuse instead.
  for (const Node& child : table.kids) {
    if (IsRowCase(child) && IsSymbol(child.kids[0], row)) {
      return refuse("the switch already dispatches that row; use RerouteRow to replace it");
    }
  }
  std::vector<Node> kase = {Sym(row)};
  for (const Node& statement : body) {
    kase.push_back(statement);
  }
  table.kids.push_back(Arr(std::move(kase)));
  if (reason != nullptr) reason->clear();
  return true;
}

bool AddLocaleEntry(std::vector<dtb::Node>* nodes, const std::string_view key,
                    const std::string_view text, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || key.empty() || text.empty()) {
    return refuse("nothing to add");
  }
  for (const Node& root : *nodes) {
    if (root.tag != dtb::kArray || root.kids.empty()) {
      continue;
    }
    if (IsSymbol(root.kids[0], key)) {
      return refuse("the locale already has that key");
    }
  }
  nodes->push_back(Arr({Sym(key), Str(text)}));
  if (reason != nullptr) reason->clear();
  return true;
}

// Every locale entry the surface adds, again derived from the table. A row with an empty label is a
// retail row symbol whose text the installed locale already carries, so nothing is added for it.
std::vector<std::pair<std::string, std::string>> Labels() {
  std::vector<std::pair<std::string, std::string>> out;
  out.emplace_back("settings_restart_message", "Settings saved. Restart the game to apply these changes.");
  out.emplace_back("settings_save_error_message", "Settings could not be saved. Check that the settings folder is writable, then try again.");
  out.emplace_back("settings_restart_error", "The game could not restart. Your settings are saved. Close this message and restart manually.");
  out.emplace_back("settings_achievements_message", "Unlock the four online achievements? This cannot be undone.");
  auto add = [&out](const std::string& key, const std::string& text) {
    for (const auto& item : out) {
      if (item.first == key) {
        return;
      }
    }
    out.emplace_back(key, text);
  };
  for (const PanelSpec& panel : AllPanels()) {
    for (const Row& row : panel.rows) {
      if (row.label.empty()) {
        continue;
      }
      if (row.kind == Kind::kBool || row.kind == Kind::kBoolSubmenu) {
        add(row.symbol + "_on", row.label + ": ON");
        add(row.symbol + "_off", row.label + ": OFF");
      } else if (row.kind == Kind::kSelect) {
        for (const Choice& choice : row.choices) {
          add(row.symbol + "_" + SymbolToken(choice.token), row.label + ": " + choice.label);
        }
      } else {
        add(row.symbol, row.label);
      }
    }
  }
  return out;
}

// Inserts `{<panel> load}` into `ui/gen/ui.dtb`'s `{init ...}` block, immediately before the
// `{set {var first_screen} ...}` statement that ends it. This is how retail creates the panels it
// declares: `ui/gen/ui.dtb` loads `options_panel`, `pause_panel` and ~23 others *before* the first
// screen is shown, which is what gives each of them its `{file}` scene and its `{focus}` component.
// A panel that is only ever named inside a screen's `{panels ...}` list still draws (its rows come
// up), but its focus component is not established and the list never takes the direction keys.
// Idempotent, and refuses rather than guessing when the init block cannot be found.
bool AddPanelLoad(std::vector<dtb::Node>* nodes, std::string_view panel_name, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || panel_name.empty()) {
    return refuse("nothing to edit");
  }
  Hit existing;
  FindInRoots(nodes, [&](const Node& node) { return IsPanelLoad(node, panel_name); }, &existing);
  if (existing.found) {
    if (reason != nullptr) reason->clear();
    return true;
  }
  // The insertion point is the statement that ends the init block, so the panel exists - and its
  // `{file}`/`{focus}` members are processed - before `first_screen` is shown.
  Hit anchor;
  FindInRoots(nodes, IsFirstScreenSet, &anchor);
  // The retail block is an **array** (`<array>[symbol init][include init.dta]<command> ...`), not a
  // command; the check is therefore "a block whose first child is the init symbol" rather than a
  // tag test, so either spelling is accepted and anything else is refused.
  if (!anchor.found || anchor.parent == nullptr || anchor.parent->kids.empty() ||
      !IsSymbol(anchor.parent->kids[0], "init")) {
    return refuse("no {init ...} block ending in {set {var first_screen} ...} to insert into");
  }
  anchor.parent->kids.insert(anchor.parent->kids.begin() + static_cast<ptrdiff_t>(anchor.index),
                             Cmd({Sym(panel_name), Sym("load")}));
  if (reason != nullptr) reason->clear();
  return true;
}

}  // namespace rb_blitz::ui::screen
