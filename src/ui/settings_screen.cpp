// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P5b: the merged Help & Options surface, generated. src/ui/settings_screen.h explains what the
// retail surface is, why the merged root needs its own scene record, and how a row shows its
// state; this file is the table of the surface plus the generator that turns it into compiled DTA.

#include "ui/settings_screen.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "profile/gameplay_remap.h"
#include "profile/input_rows.h"
#include "profile/remap.h"
#include "store/store_search.h"

namespace rb_blitz::ui::screen {
namespace {

using dtb::Node;
MenuConfiguration g_configuration;

// The one select row whose list belongs to the machine and whose *disappearance* is a decision
// rather than a failure - see the `Kind::kSelect` branch in `BuildPanels`.
constexpr char kRendererRowSetting[] = "graphics.renderer";

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

// `{write_file "rbbz-settings/help/<symbol>/-" 0}` - the caption event (owner request,
// 2026-10-13). It rides the settings channel's write side exactly like a set/commit, but carries no
// value and expects no answer: the host routes it to the help overlay, which shows the description
// registered for `symbol`. `-` means "no row" and hides the caption - what every leave emits.
// The symbol is the row's *canonical* name (`settings_vsync`), never one of its `_on`/`_off`/
// choice spellings, because the caption describes the row and not its current value.
Node HelpWrite(const std::string_view symbol) {
  return Write(Path("help", symbol.empty() ? "-" : symbol, "-"));
}

// ------------------------------------------------------------------------------------------------
// The RhythmVerse search keys (plan P4)
// ------------------------------------------------------------------------------------------------
//
// A key press is a `rbbz-store/search/key/<token>` write: the guest never carries the typed string
// (a DTA script cannot build one - src/store/store_search.h records the measurement), so the host
// composes the query from these tokens and the page's own caption reports it back. `open` is
// written by every key page's `{enter}` so the host knows its field is the current surface.
std::string StorePath(const std::string_view action) {
  return std::string(store::kStoreNamespace) + "search/" + std::string(action);
}

Node StoreKeyWrite(const std::string_view token) {
  return Write(StorePath(std::string("key/") + std::string(token)));
}

// `{ui goto_screen {{ui current_screen} get back_screen}}` - how a panel hosted by more than one
// screen leaves: it asks the *current screen object* where back is rather than naming a
// destination. Transcribed from the shipped ui/av_options/gen/av_options.dtb, whose
// `av_options_panel` is hosted by both `av_options_screen` and `av_options_pause_screen` and leaves
// through exactly this statement.
Node Leave() {
  return Cmd({Sym("do"),
              // Leaving the page retires its caption first; whatever the destination draws next
              // (its own entry event, or the retail observer's table) then takes over.
              HelpWrite(""),
              Cmd({Sym("ui"), Sym("goto_screen"),
                   Cmd({Cmd({Sym("ui"), Sym("current_screen")}), Sym("get"),
                        Sym("back_screen")})})});
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
              // Our row's caption ends here; a retail page either has a table of its own (the
              // screen observer publishes it when the screen becomes current) or shows nothing.
              HelpWrite(""),
              Cmd({Sym(screen), Sym("set"), Sym(member),
                   Cmd({Sym("ui"), Sym("current_screen")})}),
              Cmd({Sym("ui"), Sym("goto_screen"), Sym(screen)})});
}

// Entering the restart confirmation: it inherits **the leave target the root was about to use**, not
// the root itself.
//
// This is a reproduced defect, not a style choice (2026-10-14). `EnterScreen` points a destination
// back at `{ui current_screen}`, so the confirmation's Close returned to the *merged root* - and the
// confirmation is only ever reached from the root's Cancel, so every later Cancel asked the question
// again and Help & Options could not be left at all. Measured on the installed-build route:
// `07-leave-root.png` shows the confirmation, `08-after-close.png` shows the merged root again,
// `09-close-then-back.png` shows the confirmation again. The prompt belongs to the *act of leaving*,
// so dismissing it (A on Close or B) has to finish that leave - `splash_screen` from the main menu,
// the pause screen from a song. `Kind::kRestart` ("Restart now") closes the process instead.
Node RestartPrompt(const std::string_view screen) {
  return Cmd({Sym("do"),
              Cmd({Sym(screen), Sym("set"), Sym("back_screen"),
                   Cmd({Cmd({Sym("ui"), Sym("current_screen")}), Sym("get"),
                        Sym("back_screen")})}),
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
// **Cancel is where the restart prompt lives** (owner decision, 2026-10-13). Every row of this
// surface commits durably the moment it is pressed, and a commit that needs a restart is *not*
// interrupted: only leaving the merged root asks `snapshot/restart.pending` and opens the restart
// confirmation when it answers true. Deeper pages just go back - the prompt belongs to the act of
// leaving Help & Options, not to each value that happens to need one.
//
// They are returned as a *list of case arrays*, not as a whole `{switch action ...}`, because that
// distinction is a real bug: a helper returning a complete switch gets spliced in as a child of the
// panel's own switch, which nests one switch inside another and leaves its cases **never
// evaluated** - measured, that shape made Start and Cancel do nothing at all while the switch above
// them still looked right in a dump.
std::vector<Node> ButtonCases(const bool root, const bool pause) {
  Node cancel = Leave();
  if (root) {
    const std::string restart = pause ? PauseScreen("settings_restart_screen")
                                      : std::string("settings_restart_screen");
    cancel = Cmd({Sym("if_else"),
                  Cmd({Sym("file_exists"), Str(Path("snapshot", "restart.pending", "b:1"))}),
                  RestartPrompt(restart), Leave()});
  }
  return {
      Arr({Sym("kAction_Confirm"), Cmd({Var("this"), Sym("select_row")})}),
      Arr({Sym("kAction_Start"), Cmd({Var("this"), Sym("select_row")})}),
      Arr({Sym("kAction_Cancel"), Cmd({Sym("shell_sfx"), Sym("play_back")}), std::move(cancel)}),
  };
}

// `{select_row {switch {options.lst selected_sym} <cases>}}` - the single row table, reached from
// the button table above. It replaces the retail `SELECT_MSG` route for this surface: a probe in a
// panel's own button table reported every press as a button action and never once as `SELECT_MSG`
// (docs/settings/architecture.md (part 1, the bridge) §12.2), so driving the rows from the message the
// engine demonstrably sends is the honest design. `on_select_msg` is still defined, because the
// *selection change* message is a different one and the retail panels use it for the move sound;
// `SCROLL_MSG` (see `ScrollHelpCases`) is where that change publishes the caption.

// `{switch action <cases...>}` - the panel's button table, extras first.
// `{switch action <extras...> <shared cases...>}` - the panel's button table. Extra cases come
// first because a switch takes the first matching case, so a panel's own `kAction_Left` /
// `kAction_Right` handling wins over the shared table.
Node ButtonTable(std::vector<Node> extras, const bool root, const bool pause) {
  std::vector<Node> kids = {Sym("switch"), Var("action")};
  for (Node& item : extras) {
    kids.push_back(std::move(item));
  }
  for (Node& item : ButtonCases(root, pause)) {
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
  // P7's read-only state rows. Each draws a `_on`/`_off` pair selected by a query, like a
  // boolean, but a press never writes the setting:
  //
  //   kStateSubmenu  the pair is "Custom"/"Default" and Confirm enters `screen` (a binding row);
  //   kStateAction   the `_on` spelling writes `path` and commits (a per-row Reset); an `_off`
  //                  press is inert, which is the disabled row the plan asks for.
  kStateSubmenu,
  kStateAction,
  // D9's plain action (P3/P4): one symbol, a `path` write on press, a commit. No state query, so
  // the row always reads its label - it is how every `Reset All` stopped drawing a `: Nothing`
  // suffix, and why a table's reset works whether or not the table is custom.
  kPathAction,
  // One-shot Controls rows: `path` is a `rbbz-settings/` write. `kCapture` arms the listening
  // session and enters the dialog. The dialog is left by the capture itself (a valid press, or
  // the countdown's zero), so no row writes the bridge's `capture-cancel` op.
  kCapture,
  // RhythmVerse plan P4: one typed key. `path` is a `rbbz-store/search/...` write and nothing is
  // committed - the store is not a settings transaction. `kStoreKeyLeave` writes and then leaves
  // the surface (Cancel). Done deliberately does **not** leave: these panels receive one press as
  // two (see the duplicate-press note in src/store/store_search.h), so a leaving Done would leave
  // twice - the second delivery running the destination's own selection.
  kStoreKey,
  kStoreKeyLeave,
  kChoose,       // one-shot choice: set `setting` to the row's own token, commit, leave
  kAction,       // one-shot action: invoke `setting`
  kInfo,         // a row that is present but deliberately does nothing (the protected entry)
  kRoute,        // a retail screen: set its `member` to the current screen, then enter it
  kRestart,
  kClose,
  kCalibration,
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
  // The `_on` / `_off` spelling's text when it says more than the label (`Hidden`, `Visible`,
  // `Reset: Default`). Empty means the ordinary `label: On` / `label: Off`.
  std::string on_label;
  std::string off_label;
  // The `rbbz-settings/` write a Controls row performs (capture, capture-cancel, reset, reset-all).
  std::string path;
  // The one to two line caption the help overlay shows while this row is highlighted (owner
  // request, 2026-10-13). Every row carries one; the factories below take it as a required
  // argument so a row cannot be added without it, and tests/settings_screen_tests.cpp fails a row
  // whose text is empty. 1-2 lines is a layout contract: the overlay wraps at its own width, so
  // the table's texts stay under `kMaxHelpTextChars` (settings_screen.h) characters.
  std::string description;
  std::string prefix = "t:";  // the wire spelling of a value: `t:` token, `i:` integer
  bool provider_only = false;
  // A boolean whose *setting* is the negative of what its label says: `Depth of Field: On` is the
  // provider's `disable_dof == false`. The symbol pair keeps its usual meaning (`_on` = the label
  // reads On), so the query and the write flip - see PopulateMenu and RowCase.
  bool inverted = false;
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
  // A message the panel swaps in for a *reason* the host reports, queried on entry as
  // `snapshot/commit.failed/t:<reason>`. P3's rule is that a failed save must say what failed:
  // "check that the settings folder is writable" is only true of the I/O reason, and a journal or
  // a refused value that wears that text sends the player looking in the wrong place (measured on
  // an installed copy, 2026-10-14).
  std::vector<std::pair<std::string, std::string>> message_by_reason;
  // The title drawn by the scene's `header.lbl` while this page is open, and the locale key that
  // carries it. Empty leaves the scene's own title alone (the confirmation scene has no header
  // label of ours). The key is assigned from the panel name once, before the in-song copy is made,
  // so both contexts share one token and no page's own name (e.g. an action called Pause) can be
  // mistaken for the `_pause` twin suffix.
  std::string title;
  std::string title_key;
  // A RhythmVerse search page (plan P4): `{enter}` opens the host's search field, so the caption
  // shows the guidance (or the query being typed) as soon as the page is reached.
  bool opens_search = false;
};

Row BoolRow(const char* symbol, const char* setting, const char* label, const char* description,
            bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kBool;
  row.setting = setting;
  row.label = label;
  row.description = description;
  row.provider_only = provider_only;
  return row;
}

// A row whose two states name a visibility rather than On/Off. `_on` is the setting true state
// (the main-menu row is hidden), so it draws "Hidden"; `_off` draws "Visible".
Row VisibilityRow(const char* symbol, const char* setting, const char* label,
                  const char* description, bool provider_only = false) {
  Row row = BoolRow(symbol, setting, label, description, provider_only);
  row.on_label = std::string(label) + ": Hidden";
  row.off_label = std::string(label) + ": Visible";
  return row;
}

// The same pair for a provider variable whose true state means *shown* (the HUD element rows),
// so `_on` draws "Visible" and `_off` draws "Hidden".
Row ElementVisibilityRow(const char* symbol, const char* setting, const char* label,
                         const char* description, bool provider_only = false) {
  Row row = BoolRow(symbol, setting, label, description, provider_only);
  row.on_label = std::string(label) + ": Visible";
  row.off_label = std::string(label) + ": Hidden";
  return row;
}

// The same row, drawn with the opposite sense: `label` says what "On" means, and the setting it
// writes is the negative of that (the provider's `disable_dof`).
Row InvertedBoolRow(const char* symbol, const char* setting, const char* label,
                    const char* description, bool provider_only = false) {
  Row row = BoolRow(symbol, setting, label, description, provider_only);
  row.inverted = true;
  return row;
}

Row SelectRow(const char* symbol, const char* setting, const char* label, const char* description,
              std::vector<Choice> choices, bool provider_only = false, const char* prefix = "t:") {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kSelect;
  row.setting = setting;
  row.label = label;
  row.description = description;
  row.prefix = prefix;
  row.provider_only = provider_only;
  row.choices = std::move(choices);
  return row;
}

Row SubmenuRow(const char* symbol, const char* screen, const char* label, const char* description,
               bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kSubmenu;
  row.screen = screen;
  row.label = label;
  row.description = description;
  row.provider_only = provider_only;
  return row;
}

Row BoolSubmenuRow(const char* symbol, const char* setting, const char* screen, const char* label,
                   const char* description, bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kBoolSubmenu;
  row.setting = setting;
  row.screen = screen;
  row.label = label;
  row.description = description;
  row.provider_only = provider_only;
  return row;
}

Row ChooseRow(const char* symbol, const char* setting, const char* token, const char* label,
              const char* description, bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kChoose;
  row.setting = setting;
  row.label = label;
  row.description = description;
  row.provider_only = provider_only;
  row.choices = {Choice{token, label}};
  return row;
}

Row ActionRow(const char* symbol, const char* setting, const char* label, const char* description,
              bool provider_only = false) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kAction;
  row.setting = setting;
  row.label = label;
  row.description = description;
  row.provider_only = provider_only;
  return row;
}

Row InfoRow(const char* symbol, const char* label, const char* description) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kInfo;
  row.label = label;
  row.description = description;
  return row;
}

Row RouteRow(const char* symbol, const char* screen, const char* member, const char* label,
             const char* description) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kRoute;
  row.screen = screen;
  row.member = member;
  row.label = label;
  row.description = description;
  return row;
}

// P7: a binding row. Its state reads Custom when the profile has a stored row for it and Default
// when the compiled default is what plays; Confirm enters the row's own Assign/Reset page.
// `label` is the control or action's own name ("A", "First Note"); the state suffix keeps the
// drawn line inside the 28-character budget the in-game-settings skill records.
Row StateSubmenuRow(const char* symbol, const char* setting, const char* screen, const char* label,
                    const char* description) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kStateSubmenu;
  row.setting = setting;
  row.screen = screen;
  row.label = label;
  row.off_label = std::string(label) + ": Default";
  row.description = description;
  return row;
}

// A read-only state pair whose `_on` press writes `path` and commits: Reset on a binding row,
// Reset All on a table. The `_off` press is deliberately inert, which is what the plan's
// "disabled when no custom row exists" means on this surface.
Row StateActionRow(const char* symbol, const char* setting, const char* path, const char* label,
                   const char* off_label, const char* description) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kStateAction;
  row.setting = setting;
  row.path = path;
  row.label = label;
  row.off_label = off_label != nullptr ? off_label : label;
  row.description = description;
  return row;
}

// D9's plain action: one symbol, a `path` write on press, a commit - no query and no state
// spelling, so the row reads its label whatever the table holds. Used by `Reset All`; the
// per-row `Reset` keeps the read-only pair (`Reset: Default`), which is P3's deliberate split.
Row PathActionRow(const char* symbol, const char* path, const char* label,
                  const char* description) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kPathAction;
  row.path = path;
  row.label = label;
  row.description = description;
  return row;
}

// RhythmVerse plan P4: one key of the authored search keyboard. The press writes the key token to
// the store's own channel (`src/store/store_search.h`), which is why there is no commit: a search
// field is not a settings transaction, and the store's worker performs the request it enables.
Row StoreKeyRow(const char* symbol, const std::string& token, const char* label,
                const char* description) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kStoreKey;
  row.path = StorePath("key/" + token);
  row.label = label;
  row.description = description;
  return row;
}

// The key that is also navigation: Cancel writes the store's cancel op (which invalidates in-flight
// work and clears the field) and then leaves, exactly as the plan's cancel path asks.
Row StoreKeyLeaveRow(const char* symbol, const std::string& action, const char* label,
                     const char* description) {
  Row row = StoreKeyRow(symbol, "", label, description);
  row.kind = Kind::kStoreKeyLeave;
  row.path = StorePath(action);
  return row;
}

// A store action that is not a character: Done submits the field, Clear empties it. Its path is
// `rbbz-store/search/<action>`, so it must not go through the `key/<token>` spelling above.
Row StoreActionRow(const char* symbol, const std::string& action, const char* label,
                   const char* description) {
  Row row = StoreKeyRow(symbol, "", label, description);
  row.path = StorePath(action);
  return row;
}

// A gated retail route used to live here (`Default Presets` while Controller Custom was off).
// P2 of the interface/gameplay mapping restructure plan removes the Custom switches, so the row
// is a plain `RouteRow` now: the preset page is always available.

// The Assign row: arm the capture for `path` (a `capture/<token>/-` write) and enter the shared
// listening dialog. The dialog sets its own back target when entered, so every binding row can
// share one screen.
Row CaptureRow(const char* symbol, const char* path, const char* screen, const char* label,
               const char* description) {
  Row row;
  row.symbol = symbol;
  row.kind = Kind::kCapture;
  row.path = path;
  row.screen = screen;
  row.label = label;
  row.description = description;
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

// Defined with the generator below; declared here because the title keys are built before it.
std::string SymbolToken(std::string_view token);

std::string UpperAscii(std::string text) {
  for (char& c : text) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return text;
}

// The title each page draws in the scene's `header.lbl`. Retail's own pages (How to Play,
// Calibration, Credits, Audio) keep their own titles; every page of ours names itself here, so a
// player who enters a submenu sees where they are instead of the root's title.
void ApplyDefaultTitle(PanelSpec* panel) {
  static const std::map<std::string, std::string> kTitles{
      {"settings_panel", "HELP & OPTIONS"},
      {"settings_controls_panel", "CONTROLS"},
      {"settings_input_types_panel", "INPUT TYPES"},
      {"settings_interface_mapping_panel", "INTERFACE MAPPING"},
      {"settings_interface_navigation_panel", "NAVIGATION"},
      {"settings_interface_music_library_panel", "MUSIC LIBRARY OPTIONS"},
      {"settings_gameplay_mapping_panel", "GAMEPLAY MAPPING"},
      {"settings_gameplay_controller_panel", "CONTROLLER"},
      {"settings_gameplay_keyboard_mouse_panel", "MOUSE & KEYBOARD"},
      {"settings_controller_actions_panel", "CUSTOM MAPPING"},
      // P4's three action groups, shared by both tables (D7).
      {"settings_controller_notes_panel", "NOTES"},
      {"settings_controller_move_lane_panel", "MOVE LANE"},
      {"settings_controller_pause_powerup_panel", "PAUSE & POWER-UP"},
      {"settings_keyboard_mouse_notes_panel", "NOTES"},
      {"settings_keyboard_mouse_move_lane_panel", "MOVE LANE"},
      {"settings_keyboard_mouse_pause_powerup_panel", "PAUSE & POWER-UP"},
      {"settings_listen_panel", "ASSIGN INPUT"},
      {"settings_general_panel", "GENERAL"},
      {"settings_target_panel", "TARGET"},
      {"settings_dlc_panel", "DOWNLOADABLE CONTENT"},
      {"settings_dlc_menu_panel", "DLC MENU MUSIC"},
      {"settings_rhythmverse_filter_panel", "RHYTHMVERSE SONG FILTER"},
      {"settings_rhythmverse_search_panel", "RHYTHMVERSE SEARCH"},
      {"settings_rhythmverse_search_letters_panel", "LETTERS"},
      {"settings_rhythmverse_search_af_panel", "LETTERS A-F"},
      {"settings_rhythmverse_search_gl_panel", "LETTERS G-L"},
      {"settings_rhythmverse_search_mr_panel", "LETTERS M-R"},
      {"settings_rhythmverse_search_sx_panel", "LETTERS S-X"},
      {"settings_rhythmverse_search_yz_panel", "LETTERS Y-Z"},
      {"settings_rhythmverse_search_digits_low_panel", "NUMBERS 0-5"},
      {"settings_rhythmverse_search_digits_high_panel", "NUMBERS 6-9"},
      {"settings_rhythmverse_search_more_panel", "MORE KEYS"},
      {"settings_achievements_panel", "ACHIEVEMENTS & UNLOCKS"},
      {"settings_interface_panel", "INTERFACE"},
      {"settings_main_menu_panel", "MAIN MENU"},
      {"settings_hud_panel", "HUD"},
      {"settings_hud_elements_panel", "HUD ELEMENTS"},
      {"settings_av_panel", "AUDIO/VIDEO"},
      {"settings_video_panel", "VIDEO"},
      {"settings_display_panel", "DISPLAY"},
      {"settings_graphics_panel", "GRAPHICS"},
      {"settings_aa_panel", "ANTI-ALIASING"},
      {"settings_gameplay_panel", "GAMEPLAY"},
      {"settings_tracks_panel", "TRACKS & GEMS"},
      {"settings_plots_panel", "PLOTS & SKYBOXES"},
  };
  if (panel == nullptr || !panel->title.empty()) {
    return;
  }
  const auto found = kTitles.find(panel->name);
  if (found != kTitles.end()) {
    panel->title = found->second;
  }
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
  // usable by the in-song twins as they stand; see docs/settings/menu.md (part 1, the merged menu) §3.2
  // for the matrix and §3.3 for the next arm. (An earlier reading of one arm had
  // `game_fade_out_panel` as the cure for that fault; the later arms show it is not the variable:
  // the fault appears with it and without it.)
  return {"game_panel", "pause_panel", "game_fade_out_panel", "background_night_panel"};
}

// P3's Interface Mapping rows. `profile::InterfaceTargetLabel` is the one spelling the list row,
// the overlay's replacement line and the help caption all read, and the token behind the row is
// still the `[remap]` target it always was (`input.general.a`).
Row InterfaceBindingRow(profile::remap::Target target, const char* description) {
  const std::string token = profile::GeneralBindingToken(target);
  const std::string symbol = profile::InputRowSymbol(token);
  const std::string screen = symbol + "_screen";
  return StateSubmenuRow(symbol.c_str(), token.c_str(), screen.c_str(),
                         std::string(profile::InterfaceTargetLabel(target)).c_str(), description);
}

// One visible Interface row's Assign/Reset page. `back` is the page that *lists* the row, so a
// child of Navigation returns to Navigation rather than to the top of the tree.
PanelSpec InterfaceActionPage(profile::remap::Target target, const std::string& back) {
  const std::string token = profile::GeneralBindingToken(target);
  const std::string symbol = profile::InputRowSymbol(token);
  const std::string assign = symbol + "_assign";
  const std::string reset = symbol + "_reset";
  PanelSpec page{symbol + "_panel", symbol + "_screen", back, MenuPanels(),
                 {
                     CaptureRow(assign.c_str(), Path("capture", token, "-").c_str(),
                                "settings_listen_screen", "Assign",
                                "Press the input you want to add to this control."),
                     StateActionRow(reset.c_str(), token.c_str(),
                                    Path("bind-reset", token, "-").c_str(), "Reset",
                                    "Reset: Default",
                                    "Remove this control's custom inputs and restore its default."),
                 }};
  page.title = profile::InterfaceTargetLabel(target);
  return page;
}

// The whole merged tree, in the plan's §3 order. Setting tokens are `<category>.<id>` on top of
// docs/settings/contracts.md §5.1's stable ids.
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

  // 0. The merged root: six rows (five on a base install), in the owner's order. How to Play and
  // Calibration are retail's own pages; General, Controls, Audio/Video and Gameplay are ours.
  out.push_back(PanelSpec{kRootPanel, kRootScreen, "splash_screen", MenuPanels(),
                          {
                              RouteRow("tutorial", "tutorials_screen", "back_screen", "",
                                       "Watch the game's how-to-play tutorials."),
                              RouteRow("calibration", "cal_welcome_screen", "cancel_screen", "",
                                       "Line up the audio and video so notes hit in time with the music."),
                              SubmenuRow("settings_general", "settings_general_screen", "General",
                                         "Target, interface, downloadable content, achievements, and credits."),
                              SubmenuRow("controls", "settings_controls_screen", "",
                                         "Choose the devices and the controller mappings that play the game."),
                              SubmenuRow("audio_video_options", "settings_av_screen", "",
                                         "Audio, video, and graphics settings."),
                              SubmenuRow("settings_gameplay", "settings_gameplay_screen", "Gameplay",
                                         "Camera, HUD, tracks, and scenery options from the Ultimate mod.", true),
                          }});

  // ----------------------------------------------------------------------------------------------
  // P7's Controls tree (plan §14.1), rebalanced by P2/P3/P4 of the interface/gameplay restructure
  // plan. The root's `controls` row opens this page instead of adopting retail's controller
  // screen in place; Gameplay Mapping's Controller page is where retail's preset page is reached.
  //
  // Every page stays within the in-game-settings skill's six-visible-row budget: Interface Mapping
  // draws five rows and its two submenus four and three (P3 replaced the 16-row list), Gameplay
  // Mapping draws two, its Controller page two, each table's groups page four and each group two
  // (P4), and every binding page owns two.
  // ----------------------------------------------------------------------------------------------
  out.push_back(PanelSpec{"settings_controls_panel", "settings_controls_screen", kRootScreen,
                          MenuPanels(),
                          {
                              SubmenuRow("settings_controls_input_types",
                                         "settings_input_types_screen", "Input Types",
                                         "Choose which devices drive the game."),
                              SubmenuRow("settings_controls_interface",
                                         "settings_interface_mapping_screen", "Interface Mapping",
                                         "The inputs that drive the interface only. Gameplay in a song uses Gameplay Mapping; a pause returns to these."),
                              SubmenuRow("settings_controls_gameplay",
                                         "settings_gameplay_mapping_screen", "Gameplay Mapping",
                                         "The inputs that play notes, change lanes, use power-ups, and pause."),
                          }});

  // 6a. Input Types. Controller is fixed on with the explanation the plan asks for; Keyboard and
  // Mouse are the two device gates (`keyboard_support` and `mouse_ui_nav`).
  out.push_back(PanelSpec{"settings_input_types_panel", "settings_input_types_screen",
                          "settings_controls_screen", MenuPanels(),
                          {
                              InfoRow("settings_input_controller", "Controller: Always on",
                                      "Rock Band Blitz was designed for a controller, so controller input is always supported."),
                              BoolRow("settings_input_keyboard", "input.keyboard_support", "Keyboard",
                                      "Let the keyboard play and navigate. Off blocks every key outside binding capture."),
                              BoolRow("settings_input_mouse", "input.mouse_support", "Mouse",
                                      "Let the mouse point, click, and (when bound) play. Off blocks every mouse button."),
                          }});

  // 6b. Interface Mapping (P3). The page owns the *interface*: the four directions, confirm, back,
  // and the song list's three buttons - nine rows in three groups, each with its own Assign/Reset
  // page. The token behind a row is still the `[remap]` target it always was (`input.general.a`),
  // so a profile written before the rebuild binds the same control; only the name the page gives
  // it changed (D1). The seven remaining targets (X, the shoulders, the triggers, the two stick
  // clicks) keep their compiled defaults and are deliberately not shown (D2); Reset All still
  // resets all sixteen.
  {
    out.push_back(PanelSpec{"settings_interface_mapping_panel", "settings_interface_mapping_screen",
                            "settings_controls_screen", MenuPanels(),
                            {
                                SubmenuRow("settings_interface_navigation",
                                           "settings_interface_navigation_screen", "Navigation",
                                           "The four directions that move the interface's selection."),
                                InterfaceBindingRow(profile::remap::Target::kA,
                                                    "The button that accepts menus, rows, and dialogs."),
                                InterfaceBindingRow(profile::remap::Target::kB,
                                                    "The button that leaves a page or cancels a dialog."),
                                SubmenuRow("settings_interface_music_library",
                                           "settings_interface_music_library_screen",
                                           "Music Library Options",
                                           "The song list's search, shortcuts, and sort buttons."),
                                PathActionRow("settings_reset_general",
                                              Path("bind-reset-all", "input.reset.general", "-").c_str(),
                                              "Reset All",
                                              "Restore all 16 interface controls to their compiled defaults."),
                            }});
    out.push_back(PanelSpec{"settings_interface_navigation_panel",
                            "settings_interface_navigation_screen",
                            "settings_interface_mapping_screen", MenuPanels(),
                            {
                                InterfaceBindingRow(profile::remap::Target::kDpadUp,
                                                    "Move the interface's selection up."),
                                InterfaceBindingRow(profile::remap::Target::kDpadDown,
                                                    "Move the interface's selection down."),
                                InterfaceBindingRow(profile::remap::Target::kDpadLeft,
                                                    "Move the interface's selection left."),
                                InterfaceBindingRow(profile::remap::Target::kDpadRight,
                                                    "Move the interface's selection right."),
                            }});
    out.push_back(PanelSpec{"settings_interface_music_library_panel",
                            "settings_interface_music_library_screen",
                            "settings_interface_mapping_screen", MenuPanels(),
                            {
                                InterfaceBindingRow(profile::remap::Target::kStart,
                                                    "Open the song list's own search box."),
                                InterfaceBindingRow(profile::remap::Target::kY,
                                                    "Open the song list's shortcuts."),
                                InterfaceBindingRow(profile::remap::Target::kBack,
                                                    "Change how the song list is sorted."),
                            }});
    // One Assign/Reset page per visible row, in the page's own reading order. The back target is
    // the page that lists the row, so a child of Navigation returns to Navigation.
    for (const profile::remap::Target target : {profile::remap::Target::kDpadUp,
                                                profile::remap::Target::kDpadDown,
                                                profile::remap::Target::kDpadLeft,
                                                profile::remap::Target::kDpadRight}) {
      out.push_back(InterfaceActionPage(target, "settings_interface_navigation_screen"));
    }
    for (const profile::remap::Target target :
         {profile::remap::Target::kA, profile::remap::Target::kB}) {
      out.push_back(InterfaceActionPage(target, "settings_interface_mapping_screen"));
    }
    for (const profile::remap::Target target : {profile::remap::Target::kStart,
                                                profile::remap::Target::kY,
                                                profile::remap::Target::kBack}) {
      out.push_back(InterfaceActionPage(target, "settings_interface_music_library_screen"));
    }
  }

  // 6c. Gameplay Mapping (P4). Two input types, and both tables play at all times (D3): the
  // Controller page keeps the game's own preset page - always available now (D5) - beside the
  // custom rows, and the Mouse & Keyboard page enters its rows directly (D6, no Custom switch).
  //
  // D7's three groups keep every page inside the six-visible-row budget: Notes, Move Lane, and
  // Pause & Power-up, two actions each, plus a plain `Reset All` (D9). The Assign/Reset child of
  // an action returns to the group that lists it, not to the top of the table.
  out.push_back(PanelSpec{"settings_gameplay_mapping_panel", "settings_gameplay_mapping_screen",
                          "settings_controls_screen", MenuPanels(),
                          {
                              SubmenuRow("settings_gameplay_controller",
                                         "settings_gameplay_controller_screen", "Controller",
                                         "The game's own controller presets, and the pad's custom action bindings."),
                              SubmenuRow("settings_gameplay_keyboard_mouse",
                                         "settings_gameplay_keyboard_mouse_screen", "Mouse & Keyboard",
                                         "The keyboard's and mouse's action bindings. Always active in a song."),
                          }});
  out.push_back(PanelSpec{"settings_gameplay_controller_panel",
                          "settings_gameplay_controller_screen", "settings_gameplay_mapping_screen",
                          MenuPanels(),
                          {
                              // D5: retail's own page, no longer gated by a Custom switch.
                              RouteRow("settings_controller_presets", "controller_config_screen",
                                       "back_screen", "Controller Presets",
                                       "The game's four presets, always available. A custom action "
                                       "plays where the preset would."),
                              SubmenuRow("settings_controller_mapping",
                                         "settings_controller_actions_screen", "Custom Mapping",
                                         "Notes, lane changes, pause and power-up: each action takes its own pad controls."),
                          }});
  {
    // One group of a gameplay table: the page's own name and the actions it lists, in the order
    // the page draws them. Pause comes before Power-up because the group's name does.
    struct ActionGroup {
      const char* suffix;
      const char* label;
      const char* description;
      std::vector<profile::gameplay_remap::Action> actions;
    };
    const std::vector<ActionGroup> groups{
        {"notes", "Notes", "The two notes the game asks you to strike.",
         {profile::gameplay_remap::Action::kFirstNote,
          profile::gameplay_remap::Action::kSecondNote}},
        {"move_lane", "Move Lane", "Change lanes to the right and to the left.",
         {profile::gameplay_remap::Action::kMoveLaneRight,
          profile::gameplay_remap::Action::kMoveLaneLeft}},
        {"pause_powerup", "Pause & Power-up", "Pause the song, and deploy a power-up.",
         {profile::gameplay_remap::Action::kPause, profile::gameplay_remap::Action::kPowerup}},
    };
    for (const profile::gameplay_remap::TableKind kind : profile::gameplay_remap::TableKinds()) {
      const bool controller = kind == profile::gameplay_remap::TableKind::kController;
      const std::string prefix = controller ? "settings_controller_" : "settings_keyboard_mouse_";
      // The page that lists the groups: the Controller's Custom Mapping page, or the Mouse &
      // Keyboard page itself, which Gameplay Mapping enters directly (D6).
      const std::string groups_panel = controller ? "settings_controller_actions_panel"
                                                  : "settings_gameplay_keyboard_mouse_panel";
      const std::string groups_screen = controller ? "settings_controller_actions_screen"
                                                   : "settings_gameplay_keyboard_mouse_screen";
      const std::string groups_back = controller ? "settings_gameplay_controller_screen"
                                                 : "settings_gameplay_mapping_screen";
      std::vector<Row> rows;
      for (const ActionGroup& group : groups) {
        const std::string symbol = prefix + group.suffix;
        rows.push_back(SubmenuRow(symbol.c_str(), (symbol + "_screen").c_str(), group.label,
                                  group.description));
      }
      const std::string reset_token =
          controller ? "input.reset.controller" : "input.reset.keyboard_mouse";
      const std::string reset_symbol =
          controller ? "settings_reset_controller" : "settings_reset_keyboard_mouse";
      // D9: one symbol and a direct write, so the row always reads `Reset All`.
      rows.push_back(PathActionRow(
          reset_symbol.c_str(), Path("bind-reset-all", reset_token, "-").c_str(), "Reset All",
          controller
              ? "Remove every custom Controller action binding, back to the preset's own inputs."
              : "Remove every custom Mouse & Keyboard action binding, back to the defaults."));
      out.push_back(
          PanelSpec{groups_panel, groups_screen, groups_back, MenuPanels(), std::move(rows)});
      for (const ActionGroup& group : groups) {
        const std::string group_symbol = prefix + group.suffix;
        std::vector<Row> action_rows;
        for (const profile::gameplay_remap::Action action : group.actions) {
          const std::string token = profile::GameplayBindingToken(kind, action);
          const std::string symbol = profile::InputRowSymbol(token);
          action_rows.push_back(StateSubmenuRow(
              symbol.c_str(), token.c_str(), (symbol + "_screen").c_str(),
              std::string(profile::gameplay_remap::ActionLabel(action)).c_str(),
              "Press to assign or reset the inputs for this action."));
        }
        out.push_back(PanelSpec{group_symbol + "_panel", group_symbol + "_screen", groups_screen,
                                MenuPanels(), std::move(action_rows)});
        for (const profile::gameplay_remap::Action action : group.actions) {
          const std::string token = profile::GameplayBindingToken(kind, action);
          const std::string symbol = profile::InputRowSymbol(token);
          const std::string assign = symbol + "_assign";
          const std::string reset = symbol + "_reset";
          PanelSpec page{symbol + "_panel", symbol + "_screen", group_symbol + "_screen",
                         MenuPanels(),
                         {
                             CaptureRow(assign.c_str(), Path("capture", token, "-").c_str(),
                                        "settings_listen_screen", "Assign",
                                        "Press the input you want to add to this action."),
                             StateActionRow(reset.c_str(), token.c_str(),
                                            Path("bind-reset", token, "-").c_str(), "Reset",
                                            "Reset: Default",
                                            "Remove this action's custom inputs and restore its default."),
                         }};
          page.title = std::string(profile::gameplay_remap::ActionLabel(action));
          out.push_back(std::move(page));
        }
      }
    }
  }

  // 6d. The listening dialog. One screen serves every binding row; the Assign row that opened it
  // is its back target (set at navigation time).
  //
  // The page carries no pressable Cancel row on purpose: while a capture is armed the guest is
  // handed a neutral pad, so a press could not reach a row here - and a press that did reach one
  // would be the very input the player is binding. There is no cancel gesture either: the live
  // caption counts 3, 2, 1 (restructure plan D11) and the page closes itself, which is what keeps
  // Back and Escape assignable - a tap of either binds it.
  out.push_back(PanelSpec{"settings_listen_panel", "settings_listen_screen",
                          "settings_controls_screen", MenuPanels(),
                          {
                              InfoRow("settings_listen_status", "Listening for input",
                                      "Press the input you want to bind. The page closes itself "
                                      "after a short countdown."),
                          }});

  // 1. General: the edition, the interface page, downloadable content, achievements, and credits.
  out.push_back(PanelSpec{"settings_general_panel", "settings_general_screen", kRootScreen,
                          MenuPanels(),
                          {
                              SubmenuRow("settings_target", "settings_target_screen", "Target",
                                         "Choose which edition starts: Rock Band Blitz, Ultimate, or the Demo."),
                              SubmenuRow("settings_interface", "settings_interface_screen", "Interface",
                                         "Main-menu visibility and the settings caption."),
                              SubmenuRow("settings_dlc", "settings_dlc_screen",
                                         "Downloadable Content",
                                         "Scanning and caching options for your downloadable-content library."),
                              SubmenuRow("settings_achievements", "settings_achievements_screen",
                                         "Achievements & Unlocks",
                                         "Unlock the game's content and its online-only achievements.", true),
                              RouteRow("credits", "credits_screen", "back_screen", "",
                                       "View the game's credits."),
                          }});

  // 1a. Target: choose one, commit, leave. `ultimate_mode`/`license_mask` are consumed before
  // Ultimate configures, so the row is restart-scoped (plan §5.5) and the restart prompt is P9's.
  // The demo (trial) choice is offered by name; the plan's own note - that the trial boot used to
  // fault before the menu - is what the row's presence now records, because the owner asked for the
  // choice to be pickable rather than omitted.
  out.push_back(PanelSpec{"settings_target_panel", "settings_target_screen", "settings_general_screen",
                          MenuPanels(),
                          {
                              ChooseRow("settings_target_common", "general.target", "common",
                                        "Rock Band Blitz", "Start the base Rock Band Blitz."),
                              ChooseRow("settings_target_ultimate", "general.target", "ultimate",
                                        "Rock Band Blitz Ultimate",
                                        "Start Rock Band Blitz with the Ultimate mod's features."),
                              ChooseRow("settings_target_demo", "general.target", "demo",
                                        "Rock Band Blitz Demo", "Start the Rock Band Blitz demo."),
                          }});

  // 1b. Downloadable Content.
  //
  // Six rows is the page budget (the in-game-settings skill §2), and the RhythmVerse pair below is
  // what brought the page to seven. The two menu-music rows therefore moved into their own submenu
  // rather than letting the page scroll invisibly: they are one subject (the main menu's music and
  // the entry that starts it), and the player still reaches both in one press.
  out.push_back(PanelSpec{"settings_dlc_panel", "settings_dlc_screen", "settings_general_screen",
                          MenuPanels(),
                          {
                              BoolRow("settings_dlc_cache", "general.cache_dlc_library",
                                      "Cache DLC Library",
                                      "Reuse an unchanged package list on the next launch. Songs still need to load."),
                              // The refresh entry's own control (owner request, 2026-10-15). It sits
                              // beside the cache row because that is what it needs: the main menu's
                              // row only exists while R7 owns it, and this is how a player removes
                              // that entry without going through Interface -> Hidden Items.
                              BoolRow("settings_dlc_refresh", "general.refresh_music_library",
                                      "Refresh Music Library",
                                      "Show the main menu entry that re-scans your DLC folders and re-runs the discovery pass. Needs Cache DLC Library."),
                              SelectRow("settings_dlc_threads", "general.dlc_scan_threads",
                                        "DLC Scan Threads",
                                        "How many threads scan your packages. More threads scan faster.",
                                        Choices({"1", "1", "2", "2", "4", "4", "8", "8"}),
                                        false, "i:"),
                              // The RhythmVerse store (plan P6). Off by default: the store makes no
                              // network request until the player asks for it. The filter page is a
                              // plain submenu - it holds no value of its own - and it stays
                              // reachable while the master is off, because its five rows are the
                              // next thing a player wants after switching the store on.
                              BoolRow("settings_rhythmverse", "general.rhythmverse", "RhythmVerse",
                                      "Browse the RhythmVerse customs catalogue in the store and download songs."),
                              SubmenuRow("settings_rhythmverse_filter",
                                         "settings_rhythmverse_filter_screen",
                                         "RhythmVerse Song Filter",
                                         "Choose which instruments a custom must have to be listed."),
                              SubmenuRow("settings_dlc_menu", "settings_dlc_menu_screen",
                                         "DLC Menu Music",
                                         "The main menu's DLC background music and its Play Current Song entry."),
                          }});

  // 1b-i. DLC Menu Music: R9's own row (owner request, 2026-10-14) and the Play Current Song entry.
  // The two are one subject, and their order here is the order they had on the Downloadable Content
  // page before the RhythmVerse pair joined it: menu music first, the entry that starts it second.
  out.push_back(PanelSpec{"settings_dlc_menu_panel", "settings_dlc_menu_screen", "settings_dlc_screen",
                          MenuPanels(),
                          {
                              // R9's own row - and the row Play Current Song is wired to: with this
                              // off there is no background DLC song to start, so the row below reads
                              // unavailable and the main-menu entry is not built at all (see
                              // src/hooks/settings_payload.cpp).
                              BoolRow("settings_dlc_menu_songs", "general.dlc_menu_songs",
                                      "DLC Menu Songs",
                                      "Play songs from your DLC library as the main menu's background music."),
                              BoolRow("settings_play_current", "general.play_current_song",
                                      "Play Current Song",
                                      "Add a main menu entry that starts the song playing in the background."),
                          }});

  // 1b-ii. RhythmVerse Song Filter (plan P6 §1.6): one boolean per instrument, On = that chart is
  // required, Off = no condition, so all five off means no instrument constraint at all. The labels
  // are the instrument names a player already reads on the retail surfaces.
  out.push_back(PanelSpec{"settings_rhythmverse_filter_panel",
                          "settings_rhythmverse_filter_screen", "settings_dlc_screen", MenuPanels(),
                          {
                              BoolRow("settings_rv_guitar", "general.rhythmverse_guitar", "Guitar",
                                      "Only list customs that have a guitar chart."),
                              BoolRow("settings_rv_bass", "general.rhythmverse_bass", "Bass",
                                      "Only list customs that have a bass chart."),
                              BoolRow("settings_rv_drums", "general.rhythmverse_drums", "Drums",
                                      "Only list customs that have a drum chart."),
                              BoolRow("settings_rv_vocals", "general.rhythmverse_vocals", "Vocals",
                                      "Only list customs that have a vocal chart."),
                              BoolRow("settings_rv_keys", "general.rhythmverse_keys", "Keys",
                                      "Only list customs that have a keys chart."),
                          }});

  // 1b-iii. The RhythmVerse search keys (plan P4). There is no retail keyboard to borrow - all 131
  // compiled DTA records of the main ark were scanned on 2026-10-20 and the only `show_keyboard`
  // belongs to `rbworld_link_panel`, the Rock Central e-mail field - so the entry is authored here.
  //
  // The shape is the six-row budget applied twice: a root page (letters, two number pages, space,
  // backspace, "Done & Clear") and one page per group of six characters. Every key writes a
  // `rbbz-store/search/key/<token>` request; the host owns the query and reports it in the caption
  // (src/store/store_search.h). None of these pages is reached from a settings row - the store's
  // Search entry opens the root (plan P3), and `store_search_diagnostic` opens it for a
  // measurement boot.
  {
    PanelSpec search{"settings_rhythmverse_search_panel", "settings_rhythmverse_search_screen",
                     kSearchRetailBack, MenuPanels(),
                     {
                         // Done first, because the root is where every Back from a letter page
                         // lands: a search is finished with two presses (Back, Done) whatever was
                         // typed, and the selection resets to row 0 on entry. Done submits and
                         // stays - the strip reports the request, its answer (or the reason it was
                         // refused), and Back returns to the store to list the answer.
                         StoreActionRow("settings_rv_key_done", store::kKeyDone, "Done",
                                        "Search the catalogue for what you typed."),
                         SubmenuRow("settings_rv_search_letters",
                                    "settings_rhythmverse_search_letters_screen", "Letters",
                                    "Choose a letter to add to your search."),
                         SubmenuRow("settings_rv_search_digits_low",
                                    "settings_rhythmverse_search_digits_low_screen", "Numbers 0-5",
                                    "Choose a number to add to your search."),
                         SubmenuRow("settings_rv_search_digits_high",
                                    "settings_rhythmverse_search_digits_high_screen", "Numbers 6-9",
                                    "Choose a number to add to your search."),
                         StoreKeyRow("settings_rv_key_space", store::kKeySpace, "Space",
                                     "Add a space to your search."),
                         SubmenuRow("settings_rv_search_more",
                                    "settings_rhythmverse_search_more_screen", "More Keys",
                                    "Backspace, clear the field, or leave the search."),
                     }};
    search.opens_search = true;
    out.push_back(std::move(search));
  }
  {
    PanelSpec letters{"settings_rhythmverse_search_letters_panel",
                      "settings_rhythmverse_search_letters_screen",
                      "settings_rhythmverse_search_screen", MenuPanels(),
                      {
                          SubmenuRow("settings_rv_search_af",
                                     "settings_rhythmverse_search_af_screen", "Letters A-F",
                                     "Letters A to F. Choose a letter to add to your search."),
                          SubmenuRow("settings_rv_search_gl",
                                     "settings_rhythmverse_search_gl_screen", "Letters G-L",
                                     "Letters G to L. Choose a letter to add to your search."),
                          SubmenuRow("settings_rv_search_mr",
                                     "settings_rhythmverse_search_mr_screen", "Letters M-R",
                                     "Letters M to R. Choose a letter to add to your search."),
                          SubmenuRow("settings_rv_search_sx",
                                     "settings_rhythmverse_search_sx_screen", "Letters S-X",
                                     "Letters S to X. Choose a letter to add to your search."),
                          SubmenuRow("settings_rv_search_yz",
                                     "settings_rhythmverse_search_yz_screen",
                                     "Letters Y-Z and Symbols",
                                     "Letters Y and Z, plus apostrophe, dash and full stop."),
                      }};
    letters.opens_search = true;
    out.push_back(std::move(letters));
  }
  // One page per character group. Each row's symbol is the key token (`settings_rv_key_a`), so a
  // caption or a test can name one key without depending on which page it sits on. The caption
  // names the key under the cursor - the overlay's only way to say which letter a press will add,
  // since the row itself draws just the character.
  const auto key_page = [&out](const char* panel, const char* screen, const char* back,
                               std::initializer_list<std::pair<const char*, const char*>> keys) {
    PanelSpec page{panel, screen, back, MenuPanels(), {}};
    for (const auto& [token, label] : keys) {
      const std::string symbol = std::string("settings_rv_key_") + token;
      std::string description = "Add '";
      description += label;
      description +=
          "' to the search. Search by artist or song title, one at a time.";
      page.rows.push_back(StoreKeyRow(symbol.c_str(), token, label, description.c_str()));
    }
    page.opens_search = true;
    out.push_back(std::move(page));
  };
  // Every character page returns to the *root*, not to the letters index: Back is the first half of
  // "finish the search", and the root's first row is Done.
  key_page("settings_rhythmverse_search_af_panel", "settings_rhythmverse_search_af_screen",
           "settings_rhythmverse_search_screen",
           {{"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}, {"e", "E"}, {"f", "F"}});
  key_page("settings_rhythmverse_search_gl_panel", "settings_rhythmverse_search_gl_screen",
           "settings_rhythmverse_search_screen",
           {{"g", "G"}, {"h", "H"}, {"i", "I"}, {"j", "J"}, {"k", "K"}, {"l", "L"}});
  key_page("settings_rhythmverse_search_mr_panel", "settings_rhythmverse_search_mr_screen",
           "settings_rhythmverse_search_screen",
           {{"m", "M"}, {"n", "N"}, {"o", "O"}, {"p", "P"}, {"q", "Q"}, {"r", "R"}});
  key_page("settings_rhythmverse_search_sx_panel", "settings_rhythmverse_search_sx_screen",
           "settings_rhythmverse_search_screen",
           {{"s", "S"}, {"t", "T"}, {"u", "U"}, {"v", "V"}, {"w", "W"}, {"x", "X"}});
  key_page("settings_rhythmverse_search_yz_panel", "settings_rhythmverse_search_yz_screen",
           "settings_rhythmverse_search_screen",
           {{"y", "Y"},
            {"z", "Z"},
            {store::kKeyApostrophe, "'"},
            {store::kKeyDash, "-"},
            {store::kKeyPeriod, "."}});
  key_page("settings_rhythmverse_search_digits_low_panel",
           "settings_rhythmverse_search_digits_low_screen", "settings_rhythmverse_search_screen",
           {{"0", "0"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"}});
  key_page("settings_rhythmverse_search_digits_high_panel",
           "settings_rhythmverse_search_digits_high_screen", "settings_rhythmverse_search_screen",
           {{"6", "6"}, {"7", "7"}, {"8", "8"}, {"9", "9"}});
  {
    PanelSpec more{"settings_rhythmverse_search_more_panel",
                   "settings_rhythmverse_search_more_screen",
                   "settings_rhythmverse_search_screen", MenuPanels(),
                   {
                       StoreKeyRow("settings_rv_key_backspace", store::kKeyBackspace,
                                   "Backspace", "Remove the last character of your search."),
                       StoreKeyRow("settings_rv_key_clear", store::kKeyClear, "Clear",
                                   "Empty the search field."),
                       StoreKeyLeaveRow("settings_rv_key_cancel", "cancel",
                                        "Cancel", "Leave the search and start over next time."),
                   }};
    more.opens_search = true;
    out.push_back(std::move(more));
  }

  // 1c. Achievements & Unlocks (provider only).
  out.push_back(PanelSpec{"settings_achievements_panel", "settings_achievements_screen",
                          "settings_general_screen", MenuPanels(),
                          {
                              BoolRow("settings_unlock_all", "general.unlock_all", "Unlock All",
                                      "Unlock everything, including songs, plots, and power-ups.",
                                      true),
                              ActionRow("settings_unlock_online",
                                        "general.unlock_online_achievements",
                                        "Unlock Online Achievements",
                                        "Grant the achievements that normally need online play.", true),
                          }});

  // 3. Audio / Video: Audio first, then Video. The Audio page is retail's own page (its rows are
  // console-backed through ConsoleSaveData, and re-implementing them is neither P5b's job nor
  // honest - `av_options_screen` is the page that owns them).
  out.push_back(PanelSpec{"settings_av_panel", "settings_av_screen", kRootScreen, MenuPanels(),
                          {
                              RouteRow("settings_audio", "av_options_screen", "back_screen",
                                       "Audio", "Volume options plus overscan and bass boost."),
                              SubmenuRow("settings_video", "settings_video_screen", "Video",
                                         "Display, resolution, graphics, and renderer options."),
                          }});

  // 3a. Video: three entries - the two groups and the backend. Retail's own Overscan row stays on
  // the retail page it is drawn on (plan §3.3's fallback), so it is deliberately not duplicated
  // here; the display name is the row's own group, which is why it appears at both levels.
  out.push_back(PanelSpec{"settings_video_panel", "settings_video_screen", "settings_av_screen",
                          MenuPanels(),
                          {
                              SubmenuRow("settings_display_menu", "settings_display_screen",
                                         "Display", "Window mode, render size, and V-Sync."),
                              SubmenuRow("settings_graphics_menu", "settings_graphics_screen",
                                         "Graphics",
                                         "Rendering quality: anti-aliasing, filtering, and depth of field."),
                              SelectRow("settings_renderer", "graphics.renderer", "Renderer",
                                        "Which graphics backend renders the game. Restart to apply.",
                                        Choices({"d3d12", "D3D12", "vulkan", "Vulkan"})),
                          }});

  // 3a-i. Display: the window mode, the render size and V-Sync.
  out.push_back(PanelSpec{"settings_display_panel", "settings_display_screen",
                          "settings_video_screen", MenuPanels(),
                          {
                              SelectRow("settings_display", "graphics.display", "Display",
                                        "Run the game windowed, borderless, or full screen.",
                                        Choices({"windowed", "Windowed", "borderless",
                                                 "Borderless", "fullscreen", "Full Screen"})),
                              SelectRow("settings_resolution", "graphics.resolution", "Resolution",
                                        "The size the game renders and presents at.",
                                        Choices({"1280x720", "1280x720", "1920x1080", "1920x1080",
                                                 "2560x1440", "2560x1440", "3840x2160",
                                                 "3840x2160"})),
                              BoolRow("settings_vsync", "graphics.vsync", "V-Sync",
                                      "Match the frame rate to your display to remove screen tearing."),
                          }});

  // 3a-ii. Graphics: the render-path rows, with Depth of Field moved here from Gameplay and drawn
  // in the positive sense (`Depth of Field: On`), because that is what the provider variable
  // means when it is off.
  out.push_back(PanelSpec{"settings_graphics_panel", "settings_graphics_screen",
                          "settings_video_screen", MenuPanels(),
                          {
                              BoolSubmenuRow("settings_aa", "graphics.anti_aliasing",
                                             "settings_aa_screen", "Anti-Aliasing",
                                             "Smooth jagged edges at a quality or performance cost."),
                              SelectRow("settings_filtering", "graphics.filtering",
                                        "Anisotropic Filtering",
                                        "Sharpen textures seen at an angle. Higher values cost more performance.",
                                        Choices({"native", "Native", "none", "None", "1x", "1x",
                                                 "2x", "2x", "4x", "4x", "8x", "8x", "16x",
                                                 "16x"})),
                              InvertedBoolRow("settings_dof", "gameplay.disable_dof",
                                              "Depth of Field",
                                              "Blur distant scenery for a cinematic look. Turn off for a cleaner image.",
                                              true),
                          }});

  // 3b. Anti-aliasing: the compound row's choice list, "None" included (the Force-Skybox model).
  out.push_back(PanelSpec{"settings_aa_panel", "settings_aa_screen", "settings_graphics_screen",
                          MenuPanels(),
                          {
                              ChooseRow("settings_aa_none", "graphics.anti_aliasing", "none",
                                        "None", "No anti-aliasing: fastest, with visible jagged edges."),
                              ChooseRow("settings_aa_native", "graphics.anti_aliasing", "native",
                                        "Native (2x)",
                                        "The game's built-in 2x multisampling: a good quality-speed balance."),
                              ChooseRow("settings_aa_fxaa", "graphics.anti_aliasing", "fxaa",
                                        "FXAA", "Fast post-process smoothing that can soften fine details."),
                              ChooseRow("settings_aa_fxaa_extreme", "graphics.anti_aliasing",
                                        "fxaa_extreme", "FXAA Extreme",
                                        "A stronger FXAA pass: less jagged, a little softer."),
                          }});

  // 4. Gameplay (provider only; the root row that appears with it is what makes the root six):
  // five entries, with the HUD page moved here from Interface and the track/gem rows kept in a
  // submenu so the page stays a page rather than a list of everything the provider can do.
  out.push_back(PanelSpec{"settings_gameplay_panel", "settings_gameplay_screen", kRootScreen,
                          MenuPanels(),
                          {
                              SelectRow("settings_camera", "gameplay.camera_angle", "Camera Angle",
                                        "How high the camera sits while you play.",
                                        Choices({"default", "Default", "high", "High", "low",
                                                 "Low"}),
                                        true),
                              SelectRow("settings_speed", "gameplay.game_speed", "Game Speed",
                                        "Play the song faster than its normal speed.",
                                        Choices({"100", "100%", "105", "105%", "110", "110%",
                                                 "115", "115%", "120", "120%", "125", "125%",
                                                 "130", "130%", "135", "135%", "140", "140%",
                                                 "145", "145%", "150", "150%"}),
                                        true, "i:"),
                              SubmenuRow("settings_hud", "settings_hud_screen", "HUD",
                                         "Choose which heads-up displays appear during play.", true),
                              SubmenuRow("settings_tracks_menu", "settings_tracks_screen",
                                         "Tracks & Gems",
                                         "Note, gem, and track appearance options.", true),
                              SubmenuRow("settings_plots", "settings_plots_screen",
                                         "Plots & Skyboxes",
                                         "Choose what scenery plays behind the track.", true),
                          }});

  // 4a. Tracks & Gems: the note-shape rows the provider owns.
  out.push_back(PanelSpec{"settings_tracks_panel", "settings_tracks_screen",
                          "settings_gameplay_screen", MenuPanels(),
                          {
                              BoolRow("settings_precision", "gameplay.precision_mode",
                                      "Precision Mode",
                                      "An alternate note style that shows hit timing more precisely.",
                                      true),
                              BoolRow("settings_wiggles", "gameplay.disable_random_wiggles",
                                      "Disable Random Track Wiggles",
                                      "Turn off the random wobble that jitters the track and notes.",
                                      true),
                              SelectRow("settings_gems", "gameplay.gems", "Gems",
                                        "Which gem and note style is drawn.",
                                        Choices({"default", "Default", "freq", "Freq", "amp2003",
                                                 "Amp 2003", "amp2016", "Amp 2016", "monkey",
                                                 "Monkey", "ulti", "Ulti", "x", "X", "bolt",
                                                 "Bolt"}),
                                        true),
                          }});

  // 4b. Plots & Skyboxes. Force Plot and Force Skybox are the payload's *boolean-with-submenu*
  // pair: its own row toggles, a panel behind it chooses, and the provider's file keeps the chosen
  // **position** (0 = off/default). This build draws the whole choice set as one select over the
  // payload's own tokens (plan §3.4), so the tokens below are placeholders that the boot replaces
  // with the inventory of the installed payload (`settings_payload.cpp`), while the token ⇄ position
  // translation lives with the adapter (`ultimate_adapter.h`'s `RowBinding::position_value`).
  // Never draw these two as booleans: a boolean cannot name a plot, and the commit could not spell
  // it - which is the installed-copy dead end the plan's P7 records.
  out.push_back(PanelSpec{"settings_plots_panel", "settings_plots_screen",
                          "settings_gameplay_screen", MenuPanels(),
                          {
                              BoolRow("settings_disable_plots", "gameplay.disable_plots",
                                      "Disable Plots",
                                      "Play the tracks without any scenery behind them.", true),
                              SelectRow("settings_force_plot", "gameplay.forceplot", "Force Plot",
                                        "Force a scenery plot instead of letting the song choose.",
                                        Choices({"off", "Off", "plot_a", "Plot A", "plot_b",
                                                 "Plot B"}),
                                        true),
                              SelectRow("settings_force_skybox", "gameplay.which_skybox",
                                        "Force Skybox",
                                        "Force a sky, or remove it entirely.",
                                        Choices({"default", "Default", "none", "No Skybox",
                                                 "custom", "Custom"}),
                                        true),
                          }});

  // 5. Interface: the main menu's own visibility page and the caption overlay's switch. The HUD
  // rows live under Gameplay now; the old 4:3 letterboxing row is gone because it fights the HUD
  // repositioning that non-16:9 frames rely on.
  out.push_back(PanelSpec{"settings_interface_panel", "settings_interface_screen",
                          "settings_general_screen", MenuPanels(),
                          {
                              SubmenuRow("settings_main_menu", "settings_main_menu_screen",
                                         "Main Menu",
                                         "Choose which main-menu entries are visible."),
                              VisibilityRow("settings_offline", "interface.hide_offline_prompts",
                                            "Offline Prompts",
                                            "Show or hide the pop-ups shown while playing offline."),
                              // The caption overlay's own switch (owner request, 2026-10-14). It is
                              // the surface's second `hot` row - flipping it hides or shows the
                              // caption on the next frame, with no restart prompt.
                              BoolRow("settings_labels", "interface.settings_labels",
                                      "Help & Options Labels",
                                      "Show a description of the highlighted row at the bottom of the screen."),
                          }});

  // 5a. HUD: the element visibility page, then the two provider switches that read On/Off. The
  // HUD page itself is three rows, so it stays inside the six-row budget, and the element page is
  // five; the original spellings are preserved (`show_steak`).
  out.push_back(PanelSpec{"settings_hud_panel", "settings_hud_screen",
                          "settings_gameplay_screen", MenuPanels(),
                          {
                              SubmenuRow("settings_hud_elements", "settings_hud_elements_screen",
                                         "HUD Elements",
                                         "Choose which heads-up displays appear during play.", true),
                              BoolRow("settings_hud_streak", "interface.show_steak", "Note Streak",
                                      "Show the current note streak counter.", true),
                              BoolRow("settings_hud_input", "interface.input_viewer",
                                      "Input Viewer", "Show a live view of your controller input.",
                                      true),
                          }});
  out.push_back(PanelSpec{"settings_hud_elements_panel", "settings_hud_elements_screen",
                          "settings_hud_screen", MenuPanels(),
                          {
                              VisibilityRow("settings_hud_hide", "interface.hide_hud",
                                            "All HUD Elements",
                                            "Hide every on-screen display during play.", true),
                              ElementVisibilityRow("settings_hud_gold", "interface.goldprogress",
                                                   "Gold Star Progress",
                                                   "Show progress toward the gold-star goal.", true),
                              ElementVisibilityRow("settings_hud_progress", "interface.progressbar",
                                                   "Song Progress Bar",
                                                   "Show the song progress bar.", true),
                              ElementVisibilityRow("settings_hud_timer", "interface.show_timer",
                                                   "Timer", "Show the elapsed song time.", true),
                              ElementVisibilityRow("settings_hud_pb", "interface.show_pb",
                                                   "Personal Best",
                                                   "Show progress toward your personal best score.", true),
                          }});

  // 5b. Main Menu: one visibility row per hideable entry. There is no master switch: each row is
  // its own Visible/Hidden state. PLAY, HELP & OPTIONS and EXIT GAME are absent because the menu
  // keeps them visible by design; `mod_settings` is absent because its pages are all inside this
  // surface now; and Play Current Song follows its Downloadable Content setting instead of a row.
  out.push_back(PanelSpec{"settings_main_menu_panel", "settings_main_menu_screen",
                          "settings_interface_screen", MenuPanels(),
                          {
                              VisibilityRow("settings_mm_leaderboard",
                                            "interface.hidden_main_menu.splash_leaderboard",
                                            "LEADERBOARDS",
                                            "Show or hide the main menu's LEADERBOARDS entry."),
                              VisibilityRow("settings_mm_achievements",
                                            "interface.hidden_main_menu.splash_achievements",
                                            "ACHIEVEMENTS",
                                            "Show or hide the main menu's ACHIEVEMENTS entry."),
                              VisibilityRow("settings_mm_upgrade",
                                            "interface.hidden_main_menu.splash_upgrade", "UPGRADE",
                                            "Show or hide the main menu's UPGRADE entry."),
                              VisibilityRow("settings_mm_dlc",
                                            "interface.hidden_main_menu.splash_dlc",
                                            "DOWNLOAD CONTENT",
                                            "Show or hide the main menu's DOWNLOAD CONTENT entry."),
                              // The retail Download Content entry R7's refresh took over, restored
                              // as a row of its own (owner request, 2026-10-20). Hidden by default;
                              // the static fallback is shown only when the menu inventory is empty,
                              // so the live list below normally provides this row instead.
                              VisibilityRow("settings_mm_download_content",
                                            "interface.hide_download_content",
                                            "DOWNLOAD CONTENT",
                                            "Show or hide the main menu's DOWNLOAD CONTENT entry."),
                          }});

  // The confirmation scene is copied from the installation under our own name, as with the menu.
  Row restart = InfoRow("settings_restart_now", "Restart now",
                        "Restart the game now to apply your saved changes.");
  restart.kind = Kind::kRestart;
  Row close = InfoRow("settings_close", "Close", "Close this message.");
  close.kind = Kind::kClose;
  out.push_back(PanelSpec{"settings_restart_panel", "settings_restart_screen", kRootScreen,
                         MenuPanels(), {close, restart}, "../pause/settings_confirmation.milo",
                         "settings_restart_message"});
  out.push_back(PanelSpec{"settings_save_error_panel", "settings_save_error_screen", kRootScreen,
                         MenuPanels(), {close}, "../pause/settings_confirmation.milo",
                         "settings_save_error_message",
                         {// The default text is the I/O one, which is what the not-writable token says.
                          {"provider", "settings_save_error_provider"},
                          {"conflict", "settings_save_error_conflict"},
                          {"other", "settings_save_error_other"}}});
  if (g_configuration.play_current_song && !g_configuration.menu_music_songs.empty()) {
    out.push_back(PanelSpec{"settings_play_unavailable_panel", "settings_play_unavailable_screen",
                           "splash_screen", MenuPanels(), {close},
                           "../pause/settings_confirmation.milo", "settings_play_unavailable"});
  }
  Row achievements = InfoRow("settings_earn_achievements", "Unlock Online Achievements",
                             "Grant the online-only achievements. This cannot be undone.");
  achievements.kind = Kind::kAchievements;
  out.push_back(PanelSpec{"settings_achievements_confirm_panel", "settings_achievements_confirm_screen", "settings_achievements_screen",
                         MenuPanels(), {close, achievements}, "../pause/settings_confirmation.milo",
                         "settings_achievements_message"});

  for (auto& panel : out) {
    if (panel.name == "settings_main_menu_panel") {
      if (!g_configuration.download_content_row_available) {
        // The restored row exists only while R7 owns `splash_dlc`; with the cache off that retail
        // row is the Download Content entry itself and the live list already offers it.
        std::erase_if(panel.rows, [](const Row& row) {
          return row.setting == "interface.hide_download_content";
        });
      } else {
        // The static fallback lists the retail row under its shipped name; while R7 owns it the
        // row is the library refresh, which is the identity the live list gives it too.
        for (Row& row : panel.rows) {
          if (row.setting == "interface.hidden_main_menu.splash_dlc") {
            row.label = "REFRESH MUSIC LIBRARY";
            row.on_label = "REFRESH MUSIC LIBRARY: Hidden";
            row.off_label = "REFRESH MUSIC LIBRARY: Visible";
          }
        }
      }
      if (!g_configuration.menu_rows.empty()) {
        std::vector<Row> live;
        size_t index = 0;
        for (const auto& [token, label] : g_configuration.menu_rows) {
          // The menu keeps these visible, so they are not offered. `mod_settings` is gone from the
          // menu itself (all of its pages are inside this surface), and the Play Current Song row
          // follows the Downloadable Content setting rather than a visibility toggle.
          if (token == std::string(kMainMenuRow) || token == "splash_start" ||
              token == "splash_exit" || token == kProviderMenuRow || token == kPlayCurrentRow) {
            continue;
          }
          const std::string symbol = "settings_mm_live_" + std::to_string(index++);
          // The live mirror follows the installed menu, so the caption is derived from the row's
          // own label rather than a token table: a menu this record does not know still gets text.
          const std::string description =
              "Show or hide the main menu's " + label + " entry.";
          live.push_back(VisibilityRow(symbol.c_str(),
                                       ("interface.hidden_main_menu." + token).c_str(),
                                       label.c_str(), description.c_str()));
          // The restored Download Content row sits where the retail menu drew it: right after the
          // row R7 took over (`splash_dlc`), whose label above is the refresh name while the cache
          // owns it. Its own switch is the feature's, so it is not part of the hide list.
          if (token == "splash_dlc" && g_configuration.download_content_row_available) {
            live.push_back(VisibilityRow("settings_mm_download_content",
                                         "interface.hide_download_content",
                                         "DOWNLOAD CONTENT",
                                         "Show or hide the main menu's DOWNLOAD CONTENT entry."));
          }
        }
        // An effective menu with nothing hideable keeps the static fallback rows rather than an
        // empty, unfocusable page.
        if (!live.empty()) {
          panel.rows = std::move(live);
        }
      }
    }
    std::erase_if(panel.rows, [](Row& row) {
      // The Play Current Song row is hidden, not disabled, on a boot that found no DLC at all
      // (owner request, 2026-10-20): a row that exists must work, and there is nothing for it to
      // start. With a library present it stays, drawing the DLC Menu Songs reason when needed.
      if (row.setting == "general.play_current_song" && !g_configuration.has_dlc_library) {
        return true;
      }
      if (row.setting == "general.unlock_online_achievements") {
        row.kind = Kind::kSubmenu;
        row.screen = "settings_achievements_confirm_screen";
      }
      const auto choices = g_configuration.choices.find(row.setting);
      if (choices != g_configuration.choices.end()) {
        if (row.kind == Kind::kSelect) {
          row.choices.clear();
          for (const auto& [token, label] : choices->second) row.choices.push_back({token, label});
          if (row.choices.empty()) {
            row.kind = Kind::kInfo;
            row.label += ": Unavailable";
          } else if (row.setting == kRendererRowSetting && row.choices.size() < 2) {
            // Owner decision, 2026-10-14: the Renderer row is drawn only when there is a backend to
            // choose between. One entry means a plugin built with a single backend or a machine
            // without a Vulkan loader - there is nothing to pick, and "Renderer: Unavailable" would
            // read as a failure rather than as "this is the only way it can render". The boot still
            // says what was offered (`settings_menu: Renderer offers N: ...`,
            // src/hooks/settings_payload.cpp), so a packaging fault that ships a one-backend plugin
            // is still visible in the log even though the row is gone.
            return true;
          }
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
  for (PanelSpec& panel : out) {
    ApplyDefaultTitle(&panel);
    if (!panel.title.empty()) {
      panel.title_key = "settings_title_" + SymbolToken(panel.name);
    }
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
    // "splash_screen" is the main-menu context's way out; every other back target of ours is a
    // page of ours and has an in-song twin. A *retail* screen keeps its name: the store screen is
    // only ever entered from the main menu, so a `store_menu_pause_screen` would be a name no
    // record declares (measured by `TestEveryDestinationExists` when the rule did suffix it).
    if (panel.back == "splash_screen") {
      panel.back = "game_screen";
    } else if (panel.back.rfind("settings_", 0) == 0) {
      panel.back = PauseScreen(panel.back);
    }
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
    case Kind::kStateSubmenu:
    case Kind::kStateAction:
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
    if (row.kind == Kind::kBool || row.kind == Kind::kBoolSubmenu ||
        row.kind == Kind::kStateSubmenu || row.kind == Kind::kStateAction) {
      const std::string on = row.symbol + "_on";
      const std::string off = row.symbol + "_off";
      // The pair keeps its usual meaning (`_on` = the row reads On); an inverted row's setting is
      // false when it reads On, so the query it is matched against is the `b:0` one.
      const std::string query =
          row.inverted ? Path("snapshot", row.setting, "b:0") : QueryPath(row);
      body.push_back(Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(query)}),
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
//
// A commit that needs a restart does **not** open the restart screen here (owner decision,
// 2026-10-13): the change is already durable, and the prompt is asked once, when the player leaves
// the merged root (see `ButtonCases`). A commit that *failed* still reports itself, because a
// setting that did not save must not look saved.
Node CommitResult(const bool pause, Node success) {
  const auto destination = [&](const char* name) { return pause ? PauseScreen(name) : std::string(name); };
  return Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(Path("snapshot", "commit.ok", "b:1"))}),
              std::move(success), EnterScreen(destination("settings_save_error_screen"))});
}

// The index of the choice a select row's symbol stands for. A select contributes one symbol per
// choice (`<row>_<token>`), so the symbol is the whole identity a press carries back to us.
size_t ChoiceIndexForSymbol(const Row& row, const std::string_view symbol) {
  for (size_t i = 0; i < row.choices.size(); ++i) {
    if (symbol == row.symbol + "_" + SymbolToken(row.choices[i].token)) {
      return i;
    }
  }
  return 0;
}

bool g_emitting_pause = false;
Node RowCase(const Row& row, const std::string_view symbol) {
  switch (row.kind) {
    case Kind::kBool: {
      const bool is_on = symbol == std::string(row.symbol) + "_on";
      // A normal row writes the opposite of what it showed; an inverted one (`Depth of Field`)
      // shows `On` while its setting is *false*, so it writes the opposite of that.
      const bool write_on_value = is_on != row.inverted;
      return Arr({Sym(symbol), Write(SetPath(row, write_on_value ? "b:0" : "b:1")),
                  Write(Path("commit", "-", "-")), CommitResult(g_emitting_pause, Cmd({Var("this"), Sym("populate_menu")}))});
    }
    case Kind::kSelect: {
      // Confirm advances to the **next** choice and wraps at the last one: a horizontal selector
      // steps with A, which is the interaction the owner asked for after using the staged version.
      const size_t index = ChoiceIndexForSymbol(row, symbol);
      const size_t next = (index + 1) % row.choices.size();
      return Arr({Sym(symbol), Write(ChoiceSet(row, row.choices[next])),
                  Write(Path("commit", "-", "-")),
                  CommitResult(g_emitting_pause, Cmd({Var("this"), Sym("populate_menu")}))});
    }
    case Kind::kSubmenu:
    case Kind::kBoolSubmenu:
      return Arr({Sym(symbol), EnterScreen(row.screen)});
    case Kind::kStateSubmenu:
      // Both spellings enter: the state says whether the row is customised, and the page behind
      // it is the one place to change or reset that.
      return Arr({Sym(symbol), EnterScreen(row.screen)});
    case Kind::kStateAction: {
      if (symbol != std::string(row.symbol) + "_on") {
        return Arr({Sym(symbol), Cmd({Var("this"), Sym("populate_menu")})});
      }
      return Arr({Sym(symbol), Write(row.path), Write(Path("commit", "-", "-")),
                  CommitResult(g_emitting_pause, Cmd({Var("this"), Sym("populate_menu")}))});
    }
    case Kind::kPathAction:
      // D9: one symbol, always the label - the press writes and commits, then the list redraws so
      // the rows that reset restore their own states in place.
      return Arr({Sym(symbol), Write(row.path), Write(Path("commit", "-", "-")),
                  CommitResult(g_emitting_pause, Cmd({Var("this"), Sym("populate_menu")}))});
    case Kind::kStoreKey:
      // P4: one key of the search keyboard. Nothing is committed and nothing repopulates - the
      // host's session holds the field, and its status is the overlay's caption.
      return Arr({Sym(symbol), Write(row.path)});
    case Kind::kStoreKeyLeave:
      return Arr({Sym(symbol), Write(row.path), Leave()});
    case Kind::kCapture:
      return Arr({Sym(symbol), Write(row.path), EnterScreen(row.screen)});
    case Kind::kChoose:
      return Arr({Sym(symbol), Write(ChoiceSet(row, row.choices.front())),
                  Write(Path("commit", "-", "-")), CommitResult(g_emitting_pause, Leave())});
    case Kind::kAction:
      return Arr({Sym(symbol), Write(Path("invoke", row.setting, "-"))});
    case Kind::kInfo:
      return Arr({Sym(symbol), Cmd({Var("this"), Sym("populate_menu")})});
    case Kind::kRoute: {
      Node body = EnterRetail(row.screen, row.member);
      // The Audio page is retail's own scene, drawn with its own list at a lower offset. Capture
      // the merged list's position while *our* scene is still current; the retail panel's
      // `{enter}` (edited at payload-build time) applies it and renames its header to AUDIO. The
      // application cannot live here: before `{ui goto_screen ...}` runs, the retail scene is not
      // loaded yet, so `{av_options_panel find ...}` finds nothing (measured 2026-10-19).
      if (row.screen == "av_options_screen" || row.screen == "av_options_pause_screen") {
        // The translation messages are `get_local_pos`/`set_local_pos` (guest 0x821F0A88 /
        // 0x821F1678, three float arguments each). The earlier `local_xfm` property spelling did
        // not move the list; copying this position puts the Audio page's first row where every
        // other page's first row is (measured: the retail list is authored one row pitch lower,
        // 63 px at 993 client height, out/audio-fix/probe1 vs probe2).
        body.kids.insert(body.kids.begin() + 1,
                         Cmd({Sym(kList), Sym("get_local_pos"), Var("rbbz_audio_x"),
                              Var("rbbz_audio_y"), Var("rbbz_audio_z")}));
      }
      return Arr({Sym(symbol), std::move(body)});
    }
    case Kind::kClose:
      return Arr({Sym(symbol), Leave()});
    case Kind::kRestart:
      return Arr({Sym(symbol), Write(Path("restart", "-", "-")),
          Cmd({Sym("if"), Cmd({Sym("!"), Cmd({Sym("file_exists"), Str(Path("snapshot", "restart.ok", "b:1"))})}),
               Cmd({Sym("pause.lbl"), Sym("set_token_fmt"), Sym("settings_restart_error")})})});
    case Kind::kCalibration:
      // The in-song calibration route hands the game to retail's own confirm/quit flow, which
      // hosts none of our panels; the caption ends with the row that opened it.
      return Arr({Sym(symbol), HelpWrite(""),
          Cmd({Sym("pause_confirm_quit_song_screen"), Sym("set"),
          Sym("next_screen"), Sym("cal_welcome_screen")}),
          Cmd({Sym("options_screen"), Sym("set"), Sym("back_screen"), Sym("splash_screen")}),
          Cmd({Sym("ui"), Sym("goto_screen"), Sym("pause_confirm_quit_song_screen")})});
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

// `{SCROLL_MSG {switch {options.lst selected_sym} <row> <help write> ...}}` - the caption of the
// row a selection scroll has just reached.
//
// **Why this member exists (measured 2026-10-19).** The native mouse hover changes the selected
// row by calling `UIList::SetSelected` and then `ScrollStart`/`ScrollComplete` directly
// (src/input/native_menu.cpp), so the panel's `move_up` / `move_down` methods - the only place a
// caption used to be published - never run, and the overlay kept showing the row the pointer had
// left. `SCROLL_MSG` is the engine's own notification for that scroll: retail's
// `powerup_select_panel` updates its description from exactly this message and `selected_sym`
// (decoded in out/_swap/powerup_select.tree.txt), and `ScrollStart` is what the host's hover
// already calls. The caption therefore has one source for controller, keyboard and mouse selection
// alike, and a mouse move can never lag it.
//
// A page that suppresses captions (the confirmation/error panels, see `SuppressesHelp`) gets no
// member at all, so a scroll there cannot put a description over its own message.
std::vector<Node> ScrollHelpCases(const PanelSpec& panel) {
  std::vector<Node> kids = {Sym("switch"), Cmd({Sym(kList), Sym("selected_sym")})};
  for (const Row& row : panel.rows) {
    for (const std::string& symbol : RowSymbols(row)) {
      kids.push_back(Arr({Sym(symbol), HelpWrite(row.symbol)}));
    }
  }
  return kids;
}

// Left/Right on a select row walk its choices, and nowhere else: a nested switch on the selected
// symbol keeps the arrows from doing anything on a row that has no direction. Like Confirm, each
// press commits: the arrow is the other way to make the same one-step change, so it must be as
// durable as A is.
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
                           Write(Path("commit", "-", "-")),
                           CommitResult(g_emitting_pause,
                                        Cmd({Var("this"), Sym("populate_menu")}))}));
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

Node DirectionTable(const std::vector<Cell>& cells, const bool forward, const bool suppress_help) {
  std::vector<Node> kids = {Sym("switch"), Cmd({Sym(kList), Sym("selected_sym")})};
  for (size_t i = 0; i < cells.size(); ++i) {
    // Clamp at both ends rather than wrap: a wrap needs the user to notice they have left the
    // bottom, and the first row is where `populate_menu` places the selection.
    if (forward ? i + 1 >= cells.size() : i == 0) {
      continue;
    }
    const size_t target = forward ? i + 1 : i - 1;
    // The move also publishes the caption of the row being moved *to*. The target row's canonical
    // symbol is a compile-time fact of this table - no guest query is needed - and the write rides
    // behind the selection so the caption can never name a row the highlight is not on.
    //
    // A suppressed page (a confirmation: see `SuppressesHelp`) publishes the *hide* instead, so the
    // move announces nothing while still placing the selection.
    const std::string target_symbol = suppress_help ? std::string() : cells[target].row->symbol;
    for (const std::string& symbol : cells[i].symbols) {
      kids.push_back(Arr({Sym(symbol), Cmd({Sym("do"), SelectIndex(target),
                                            HelpWrite(target_symbol)})}));
    }
  }
  return Cmd(std::move(kids));
}

// True when a page carries no explanatory caption at all.
//
// The rule is the panel's own message token, and it is exactly the informational/confirmation
// surfaces (owner request, 2026-10-14): a panel with a `pause.lbl set_token_fmt <token>` body is
// the restart confirmation, the save-error report, or the achievements confirmation - each of which
// draws a *real* message and a handful of buttons, and each of which would otherwise show a stale
// row description over its own text. The three panels that carry a message are the whole set; no
// ordinary page does, so no ordinary page loses its caption.
bool SuppressesHelp(const PanelSpec& panel) { return !panel.message.empty(); }

// The caption an entry publishes: the row the highlight is about to sit on, which is the first
// *visible* row of the page - and which rows are visible depends on the provider gate, because a
// page may begin with provider-only rows (Interface does). The two gate worlds each get their
// write; where they agree (most pages) a single statement is emitted instead. A suppressed page
// publishes the hide in both worlds.
Node EnterHelpWrite(const PanelSpec& panel) {
  if (SuppressesHelp(panel)) {
    return HelpWrite("");
  }
  const std::vector<Cell> base = Cells(panel, false);
  const std::vector<Cell> all = Cells(panel, true);
  const std::string symbol = base.empty() ? "" : base.front().row->symbol;
  const std::string provider_symbol = all.empty() ? "" : all.front().row->symbol;
  if (symbol == provider_symbol) {
    return HelpWrite(symbol);
  }
  return Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(kProviderQuery)}),
              HelpWrite(provider_symbol), HelpWrite(symbol)});
}

// `{move_down {if_else <gate> <six-row table> <five-row table>}}`
Node MoveMember(const PanelSpec& panel, const char* name, const bool forward) {
  const bool suppress_help = SuppressesHelp(panel);
  const bool has_provider_row = [&panel] {
    for (const Row& row : panel.rows) {
      if (row.provider_only) {
        return true;
      }
    }
    return false;
  }();
  Node body = DirectionTable(Cells(panel, false), forward, suppress_help);
  if (has_provider_row) {
    body = Cmd({Sym("if_else"), Cmd({Sym("file_exists"), Str(kProviderQuery)}),
               DirectionTable(Cells(panel, true), forward, suppress_help), std::move(body)});
  }
  return Arr({Sym(name), Cmd({Sym("if"), Cmd({Sym("!"), Var("rbbz_settings_editing")}), std::move(body)})});
}

Node PanelDefinition(const PanelSpec& panel) {
  g_emitting_pause = panel.scene == kPauseSceneName ||
                     panel.scene == "../pause/settings_confirmation_pause.milo";
  Node enter = EnterExit();
  if (!panel.message.empty())
    enter.kids.push_back(Cmd({Sym("pause.lbl"), Sym("set_token_fmt"), Sym(panel.message)}));
  // RhythmVerse plan P4: a search key page tells the host its field is the current surface. The
  // write is what makes the overlay show the guidance (or the query being typed) instead of the
  // row caption, and it is repeated per page because every page of the keyboard is reachable from
  // every other one.
  if (panel.opens_search) {
    enter.kids.push_back(Write(StorePath("open")));
  }
  // A reason-specific message is asked for once per entry: the queries are one-bit reads on the
  // same channel the rows already use, and the panel's own default stays in place when none of
  // them matches (an older host, or a failure the panel was not told about).
  for (const auto& [reason, token] : panel.message_by_reason) {
    enter.kids.push_back(Cmd(
        {Sym("if"), Cmd({Sym("file_exists"), Str(Path("snapshot", "commit.failed",
                                                       "t:" + reason))}),
         Cmd({Sym("pause.lbl"), Sym("set_token_fmt"), Sym(token)})}));
  }
  // The page's own title. The scene's `header.lbl` is a UILabel, so it is editable text (and a
  // locale token), not a baked-in texture: every page of ours renames it as it is entered. Retail's
  // own pages keep their own title, and the confirmation scene has no header label of ours.
  if (!panel.title_key.empty()) {
    enter.kids.push_back(Cmd({Sym("header.lbl"), Sym("set"), Sym("text_token"),
                              Sym(panel.title_key)}));
  }
  // The open-time caption (owner request, 2026-10-13): entering a page places the highlight on its
  // first visible row, so the overlay is told which row that is. Emitted here rather than inside
  // populate_menu because a commit re-runs populate and must not drag the caption back to row 0.
  enter.kids.push_back(EnterHelpWrite(panel));
  // The exit member, shared by every panel; a search page adds its own dismissal to it - `enter`
  // wrote `open`, so the exit is where the host is told the field is no longer the current surface
  // (the field and the answer stay in the session: `leave` is not `cancel`).
  Node exit_member = panel.opens_search
                         ? Arr({Sym("exit"), Cmd({Sym("shell_sfx"), Sym("clear_interceptor")}),
                                Write(StorePath("leave"))})
                         : Arr({Sym("exit"), Cmd({Sym("shell_sfx"), Sym("clear_interceptor")})});
  std::vector<Node> extras;
  extras.push_back(Arr({Sym("kAction_Up"), Cmd({Var("this"), Sym("move_up")})}));
  extras.push_back(Arr({Sym("kAction_Down"), Cmd({Var("this"), Sym("move_down")})}));
  std::vector<Node> left = DirectionCases(panel, false);
  if (!left.empty()) {
    std::vector<Node> kids = {Sym("switch"), Cmd({Sym(kList), Sym("selected_sym")})};
    for (Node& item : left) {
      kids.push_back(std::move(item));
    }
    extras.push_back(Arr({Sym("kAction_Left"), Cmd(std::move(kids))}));
    std::vector<Node> right = DirectionCases(panel, true);
    std::vector<Node> rkids = {Sym("switch"), Cmd({Sym(kList), Sym("selected_sym")})};
    for (Node& item : right) {
      rkids.push_back(std::move(item));
    }
    extras.push_back(Arr({Sym("kAction_Right"), Cmd(std::move(rkids))}));
  }

  const bool pause = g_emitting_pause;  // captured before the table is built
  const bool root = panel.screen == kRootScreen || panel.screen == kRootPauseScreen;
  std::vector<Node> members = {
      PopulateMenu(panel),
      // The panel's own navigation, and why it is not the engine's: see above.
      MoveMember(panel, "move_up", false),
      MoveMember(panel, "move_down", true),
      std::move(enter),
      GetFocusMember(),
      std::move(exit_member),
      Arr({Sym("on_select_msg"), Cmd({Sym("shell_sfx"), Sym("play_select")})}),
  };
  if (!SuppressesHelp(panel)) {
    // `ScrollHelpCases` is the whole `{switch {options.lst selected_sym} ...}` body.
    members.push_back(Arr({Sym("SCROLL_MSG"), Cmd(ScrollHelpCases(panel))}));
  }
  members.push_back(SelectRowCases(panel));
  members.push_back(Arr({Sym("BUTTON_DOWN_MSG"), ButtonTable(std::move(extras), root, pause)}));
  return Panel(panel.name, panel.scene, std::move(members));
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
  // The caption events: the hide, plus every row's canonical symbol (see HelpWrite). Both the
  // hide and the per-row writes are what the DTA emits, so the test below covers the whole
  // surface's caption traffic with the same grammar the boot will use.
  add(Path("help", "-", "-"));
  for (const PanelSpec& panel : AllPanels()) {
    for (const Row& row : panel.rows) {
      add(Path("help", row.symbol, "-"));
    }
  }
  // The restart prompt's state: true once this session has committed a change that needs one. Asked
  // by the merged root's Cancel, which is the only place the prompt belongs.
  for (const auto* token : {"commit.ok", "restart.pending", "restart.ok"})
    add(Path("snapshot", token, "b:1"));
  // And why a save failed, for the one page whose job is to say so. Derived from the panels that
  // declare a reason rather than listed by hand, so a new reason cannot be added without its probe
  // (the table is what the test parses with the bridge's grammar).
  for (const PanelSpec& panel : AllPanels()) {
    for (const auto& [reason, message] : panel.message_by_reason) {
      (void)message;
      add(Path("snapshot", "commit.failed", "t:" + reason));
    }
  }
  for (const PanelSpec& panel : AllPanels()) {
    for (const Row& row : panel.rows) {
      // A capture or path-action row carries its write in `path`, not in `setting`; the branches
      // below are the rows of this surface with no setting at all.
      if (row.setting.empty() && row.kind != Kind::kCapture && row.kind != Kind::kPathAction) {
        continue;
      }
      switch (row.kind) {
        case Kind::kBool:
        case Kind::kBoolSubmenu:
        case Kind::kStateSubmenu:
        case Kind::kStateAction:
          add(QueryPath(row));
          if (row.inverted) add(Path("snapshot", row.setting, "b:0"));
          add(SetPath(row, "b:1"));
          add(SetPath(row, "b:0"));
          if (!row.path.empty()) add(row.path);
          break;
        case Kind::kCapture:
        case Kind::kPathAction:
          add(row.path);
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

std::string HelpTextFor(const std::string_view symbol) {
  if (symbol.empty()) {
    return {};
  }
  // The in-song pause list spells two rows with retail's own symbols (`pause_tutorial`,
  // `pause_controls`); they open the same pages the main-menu rows do, so they get the same text.
  if (symbol == "pause_controls") {
    return HelpTextFor("controls");
  }
  if (symbol == "pause_tutorial") {
    return HelpTextFor("tutorial");
  }
  // First match wins; the in-song twins duplicate the main-menu rows' symbols on purpose (one
  // symbol set feeds one label table in two contexts), and their texts are copied with the rows.
  for (const PanelSpec& panel : AllPanels()) {
    for (const Row& row : panel.rows) {
      if (row.symbol == symbol) {
        return row.description;
      }
    }
  }
  return {};
}

std::vector<std::string> HelpSymbols() {
  std::vector<std::string> out;
  for (const PanelSpec& panel : AllPanels()) {
    for (const Row& row : panel.rows) {
      if (std::find(out.begin(), out.end(), row.symbol) == out.end()) {
        out.push_back(row.symbol);
      }
    }
  }
  return out;
}

std::vector<std::string> ScreenBindingRows(const std::string_view screen) {
  if (screen.empty()) {
    return {};
  }
  // The page tree is fixed for the process and a binding row never depends on the boot's
  // configuration (the one configuration-dependent row, Play Current Song, is not a binding), so
  // the mapping is built once and the overlay's per-frame ask is a lookup. A page that has no
  // binding row at all is deliberately absent: an empty answer means "draw nothing there".
  static const std::map<std::string, std::vector<std::string>> kPages = [] {
    std::map<std::string, std::vector<std::string>> pages;
    for (const std::vector<PanelSpec>& context : {AllPanels(), InSongPanels()}) {
      for (const PanelSpec& panel : context) {
        std::vector<std::string> rows;
        bool any = false;
        for (const Row& row : panel.rows) {
          std::string token;
          if (row.kind == Kind::kStateSubmenu && profile::ParseInputRowToken(row.setting).valid()) {
            token = row.setting;
            any = true;
          }
          rows.push_back(std::move(token));
        }
        if (any) {
          pages.emplace(panel.screen, std::move(rows));
        }
      }
    }
    return pages;
  }();
  const auto found = kPages.find(std::string(screen));
  return found == kPages.end() ? std::vector<std::string>{} : found->second;
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

std::vector<std::string> PanelRowSymbols(std::string_view panel) {
  std::vector<std::string> out;
  for (const PanelSpec& candidate : AllPanels()) {
    if (candidate.name != panel) {
      continue;
    }
    for (const Row& row : candidate.rows) {
      out.push_back(row.symbol);
    }
    return out;
  }
  return out;
}

std::vector<std::string> PauseRootRows(const bool provider_active) {
  // The merged root's own order, in the **in-song spellings**: retail's pause switch dispatches
  // `pause_tutorial` and `pause_controls` (its in-song names for How to Play and Controls; the
  // main-menu page spells the same rows `tutorial` and `controls`), so reusing retail's cases is
  // what keeps those rows working without new bodies. `settings_gameplay` is dropped on a boot
  // whose provider gate answers false - the same rule the root panel applies to itself, applied
  // here to the list retail's pause panel fills.
  std::vector<std::string> rows{"pause_tutorial", "calibration", "settings_general",
                                "pause_controls", "audio_video_options"};
  if (provider_active) {
    rows.push_back("settings_gameplay");
  }
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

// The main menu's option array: an array holding both the first and the last retail row symbol.
// Transcribed from `menu_options::FindOptionArray` (which owns the same idea for the live edit); it
// is repeated rather than shared because this module must not depend on that one's file logic.
bool IsMenuOptionArray(const Node& node) {
  if (node.tag != dtb::kArray) {
    return false;
  }
  bool first = false;
  bool last = false;
  for (const Node& kid : node.kids) {
    first = first || (kid.tag == dtb::kSymbol && kid.text == "splash_start");
    last = last || (kid.tag == dtb::kSymbol && kid.text == "splash_exit");
  }
  return first && last;
}

// Erases `row` from every menu option array under `node`. True when something was removed.
bool RemoveFromOptionArrays(Node* node, const std::string_view row) {
  bool removed = false;
  if (node == nullptr) {
    return false;
  }
  if (IsMenuOptionArray(*node)) {
    const auto before = node->kids.size();
    std::erase_if(node->kids, [&](const Node& kid) {
      return kid.tag == dtb::kSymbol && kid.text == row;
    });
    removed = removed || node->kids.size() != before;
  }
  for (Node& kid : node->kids) {
    removed = RemoveFromOptionArrays(&kid, row) || removed;
  }
  return removed;
}

bool RemoveRow(std::vector<dtb::Node>* nodes, const std::string_view row, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || row.empty()) {
    return refuse("nothing to remove");
  }
  bool removed = false;
  Hit hit;
  FindInRoots(
      nodes, [&](const Node& node) { return IsRowCase(node) && IsSymbol(node.kids[0], row); },
      &hit);
  if (hit.found && hit.parent != nullptr && IsSwitchCommand(*hit.parent)) {
    hit.parent->kids.erase(hit.parent->kids.begin() + static_cast<ptrdiff_t>(hit.index));
    removed = true;
  }
  for (Node& root : *nodes) {
    removed = RemoveFromOptionArrays(&root, row) || removed;
  }
  if (!removed) {
    return refuse("the row is in neither a case table nor a menu option list");
  }
  if (reason != nullptr) reason->clear();
  return true;
}

// The P1 prototype's two edits (`AddPanelLoad` into ui/gen/ui.dtb, `AddMainMenuRow` into the splash
// option array) are gone with the Advanced Settings row they served: the merge re-points the retail
// `splash_options` / `pause_options` rows (RerouteRow above) instead, so a payload generated from the
// retail records has no trace of `advanced_settings`. The one row this package *adds* is the Play
// Current Song entry (`AddPlayCurrentSongRow`), which is a feature of the menu rather than a way in
// to the settings surface.

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

// The same operation on the main menu's switch, found by the case every splash record dispatches:
// `splash_start`. Appended, never inserted before a retail case (a switch takes its first match),
// and refused when the row already has a case.
bool AddMainMenuSwitchCase(std::vector<dtb::Node>* nodes, const std::string_view row,
                           const std::vector<dtb::Node>& body, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || row.empty() || body.empty()) {
    return refuse("nothing to add");
  }
  Hit hit;
  FindInRoots(
      nodes,
      [](const Node& node) {
        if (node.tag != dtb::kCommand || node.kids.size() < 2 ||
            !IsSymbol(node.kids[0], "switch")) {
          return false;
        }
        for (size_t i = 1; i < node.kids.size(); ++i) {
          if (IsRowCase(node.kids[i]) && IsSymbol(node.kids[i].kids[0], "splash_start")) {
            return true;
          }
        }
        return false;
      },
      &hit);
  if (!hit.found || hit.parent == nullptr) {
    return refuse("no {switch ...} dispatching the main menu's option list");
  }
  Node& table = hit.parent->kids[hit.index];
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

// Inserts a symbol into the main menu's option array, directly after `after_row`. The array is the
// one `IsMenuOptionArray` recognises (it holds both `splash_start` and `splash_exit`), which is the
// same identification the live edit uses (`menu_options::FindOptionArray`); the insertion is refused
// when the row is already an element, so a second pass cannot duplicate it.
bool InsertMenuRow(std::vector<dtb::Node>* nodes, const std::string_view after_row,
                   const std::string_view row, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || after_row.empty() || row.empty()) {
    return refuse("nothing to insert");
  }
  Hit hit;
  FindInRoots(nodes, [](const Node& node) { return IsMenuOptionArray(node); }, &hit);
  if (!hit.found || hit.parent == nullptr) {
    return refuse("no main menu option array to insert into");
  }
  Node& array = hit.parent->kids[hit.index];
  size_t anchor = array.kids.size();
  for (size_t i = 0; i < array.kids.size(); ++i) {
    if (IsSymbol(array.kids[i], row)) {
      return refuse("the option array already carries that row");
    }
    if (IsSymbol(array.kids[i], after_row)) {
      anchor = i;
    }
  }
  if (anchor == array.kids.size()) {
    return refuse("the option array does not carry the row to insert after");
  }
  array.kids.insert(array.kids.begin() + static_cast<ptrdiff_t>(anchor) + 1, Sym(row));
  if (reason != nullptr) reason->clear();
  return true;
}

// A "before another row" insertion was tried for the store entry and removed again: it produced a
// row whose label drew and whose navigation the list skipped (measured 2026-10-20; see
// AddRhythmVerseRow). The position that works is the retail store row's own, after `splash_dlc`.

// ------------------------------------------------------------------------------------------------
// The "Play Current Song" entry (owner feature, 2026-10-14)
// ------------------------------------------------------------------------------------------------
// The host identifies the advancing stream through file_exists probes. The song key
// MUST be a Symbol: SongMgr's native handler does not convert a String payload.
// Metadata is validated, made current on the song panel, mounted and passed through
// the title's song_chosen handler, preserving the retail/Ultimate flow. Missing audio
// or metadata gets an explicit message instead of a silent no-op or null dereference.
// See docs/engine/main-menu.md section 7.2 for the runtime evidence.
namespace {
constexpr char kPlaySongVar[] = "rbbz_play_song";
constexpr char kPlayIdVar[] = "rbbz_play_id";
constexpr char kPlayDataVar[] = "rbbz_play_data";

Node SetVar(const std::string_view name, Node value) {
  return Cmd({Sym("set"), Var(name), std::move(value)});
}

std::vector<Node> PlayCurrentSongBody(const std::vector<std::string>& songs) {
  // SongMgr::Handle at 0x8244938C reads a Symbol payload (0x823F94A0), not a
  // string. A string's DataArray pointer was being looked up as a symbol, returning
  // zero even for songs already in the library. Preserve the package's spelling.
  std::vector<Node> statements{
      Cmd({Sym("file_exists"), Str("rbbz-play/begin")}), SetVar(kPlaySongVar, Sym(""))};
  for (size_t i = 0; i < songs.size(); ++i) {
    statements.push_back(Cmd(
        {Sym("if"),
         Cmd({Sym("file_exists"), Str(std::string(kPlayProbePrefix) + std::to_string(i))}),
         SetVar(kPlaySongVar, Sym(songs[i]))}));
  }
  const auto unavailable = [](const char* reason) {
    return Cmd({Sym("do"),
        Cmd({Sym("file_exists"), Str(std::string("rbbz-play/result/") + reason)}),
        EnterScreen("settings_play_unavailable_screen")});
  };
  const Node choose = Cmd({Sym("do"),
      Cmd({Sym("file_exists"), Str("rbbz-play/result/resolved")}),
      // The metadata also becomes the panel's current song, so returning to the library
      // and the power-up flow agree with the song handed to the loading screen.
      Cmd({Sym("song_select_panel"), Sym("set_current_song"), Var(kPlayDataVar)}),
      Cmd({Sym("loading_screen"), Sym("set_song"), Var(kPlayDataVar)}),
      Cmd({Sym("song_select_panel"), Sym("mount_current_content"), Var(kPlayIdVar)}),
      Cmd({Sym("song_select_screen"), Sym("song_chosen"),
           Cmd({Sym("user_mgr"), Sym("get_participating_user")})})});
  statements.push_back(Cmd({Sym("if_else"), Cmd({Sym("!="), Var(kPlaySongVar), Sym("")}),
      Cmd({Sym("do"),
           SetVar(kPlayIdVar,
                  Cmd({Sym("song_mgr"), Sym("get_song_id_from_short_name"), Var(kPlaySongVar)})),
           Cmd({Sym("if_else"), Cmd({Sym("!="), Var(kPlayIdVar), Node::Int(0)}),
                Cmd({Sym("do"),
                     SetVar(kPlayDataVar, Cmd({Sym("song_mgr"), Sym("data"), Var(kPlayIdVar)})),
                     Cmd({Sym("if_else"), Var(kPlayDataVar), choose, unavailable("missing-data")})}),
                unavailable("missing-name")})}),
      unavailable("no-track")}));
  return statements;
}
}  // namespace

bool AddPlayCurrentSongRow(std::vector<dtb::Node>* nodes, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr) {
    return refuse("nothing to edit");
  }
  if (!g_configuration.play_current_song) {
    return refuse("the Play Current Song setting is off");
  }
  if (g_configuration.menu_music_songs.empty()) {
    return refuse("the boot has no DLC menu-music song to play");
  }
  // The row first - a refusal there leaves the record untouched - and then the case that
  // dispatches it. A caller that treats any refusal as fatal (settings/menu_payload.cpp does)
  // therefore never ships a row without its case.
  if (!InsertMenuRow(nodes, "splash_start", kPlayCurrentRow, reason)) {
    return false;
  }
  return AddMainMenuSwitchCase(nodes, kPlayCurrentRow,
                               PlayCurrentSongBody(g_configuration.menu_music_songs), reason);
}

bool AddDownloadContentRow(std::vector<dtb::Node>* nodes, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr) {
    return refuse("nothing to edit");
  }
  if (!g_configuration.download_content_row) {
    return refuse("the Download Content row is off");
  }
  // The shipped action, exactly as the retail record carries it. The option array also lists
  // `splash_dlc` as a bare symbol, so the case is told apart by its second element being a
  // statement rather than another row name.
  Hit hit;
  FindInRoots(
      nodes,
      [](const Node& node) {
        if (node.tag != dtb::kArray || node.kids.size() < 2 ||
            node.kids[0].tag != dtb::kSymbol || node.kids[0].text != "splash_dlc") {
          return false;
        }
        const auto tag = node.kids[1].tag;
        return tag == dtb::kCommand || tag == dtb::kArray || tag == dtb::kProperty;
      },
      &hit);
  if (!hit.found || hit.parent == nullptr) {
    return refuse("the splash record carries no downloadable-content case to restore");
  }
  // Copy before the tree is edited: InsertMenuRow reallocates the option array.
  const Node shipped = hit.parent->kids[hit.index];
  std::vector<Node> body(shipped.kids.begin() + 1, shipped.kids.end());
  if (body.empty()) {
    return refuse("the downloadable-content case carries no action");
  }
  // The row first - a refusal there leaves the record untouched - and then its case. RerouteRow
  // (called by menu_payload after this) can then take `splash_dlc` over for R7 without the retail
  // action being lost.
  if (!InsertMenuRow(nodes, "splash_dlc", kDownloadContentRow, reason)) {
    return false;
  }
  return AddMainMenuSwitchCase(nodes, kDownloadContentRow, body, reason);
}

bool AddRhythmVerseRow(std::vector<dtb::Node>* nodes, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr) {
    return refuse("nothing to edit");
  }
  if (!g_configuration.rhythmverse_row) {
    return refuse("the RhythmVerse setting is off");
  }
  // The retail array lists `splash_dlc` last, after `splash_exit`; the store entry is appended in
  // that same place, and it is the only position measured to be *selectable*: inserted before
  // `splash_exit` instead, the row drew its label and the list skipped it while navigating
  // (measured 2026-10-20, both a 3-down and a 4-down route landed on EXIT GAME while the highlight
  // shot showed the row on screen). With R5's hidden rows baked into this record (HideMenuRows),
  // the entry is also on the visible page - the seventh row that was off it is what put it there.
  const char* anchor = "splash_dlc";
  Hit hit;
  FindInRoots(nodes, [](const Node& node) { return IsSymbol(node, kDownloadContentRow); }, &hit);
  if (hit.found) {
    anchor = kDownloadContentRow;
  }
  if (!InsertMenuRow(nodes, anchor, kRhythmVerseRow, reason)) {
    return false;
  }
  // The store without Xbox LIVE: the retail gate is `is_participating_user_online`, and this row
  // deliberately asks the host instead. The one condition is the store's own master switch, read
  // live at press time so switching it off stops the press without a restart.
  //
  // P4's measurement switch (`store_search_diagnostic`) sends the same press into the authored key
  // pages instead, so the search can be driven without P3's front menu serving the store's Search
  // row. It is also the cleaner surface for that measurement: a jump issued from the retail store
  // screen's `enter` (the first attempt) delivers every later press twice, because the press that
  // enters a page is re-delivered to the page that has just become current - measured 2026-10-20,
  // one A press logged as two `search/key/a` writes in the same millisecond and one Down moved two
  // rows. Entered from the main menu like every settings page, the same pages behave.
  const std::string destination =
      g_configuration.store_search_diagnostic ? std::string(kSearchScreen) : "store_menu_screen";
  const std::vector<Node> body{
      Cmd({Sym("if"), Cmd({Sym("file_exists"), Str(kStoreEnabledQuery)}),
           Cmd({Sym("ui"), Sym("goto_screen"), Sym(destination)})})};
  return AddMainMenuSwitchCase(nodes, kRhythmVerseRow, body, reason);
}

// Removes the named rows from every main-menu option array - the same decision
// `menu_filter::HideRows` makes in the retail bytes, applied to the record this payload generates.
// It has to be here as well as there: the in-flight edit is refused for the splash record when it
// spans two ark reads and the guest has already copied the first half (measured 2026-10-20: the
// boot warned that an edit at ark offset 16041952 reached into 14368 bytes it did not carry and
// "was not delivered"), and with that edit missing the menu still draws the rows the player hid -
// which is what pushed P1's seventh row off the visible page.
bool HideMenuRows(std::vector<dtb::Node>* nodes, const std::vector<std::string>& rows,
                  std::string* reason) {
  auto refuse = [reason](std::string why) {
    if (reason != nullptr) *reason = std::move(why);
    return false;
  };
  if (nodes == nullptr) {
    return refuse("nothing to edit");
  }
  if (rows.empty()) {
    if (reason != nullptr) reason->clear();
    return true;
  }
  size_t removed = 0;
  for (const std::string& row : rows) {
    if (row.empty()) {
      continue;
    }
    for (dtb::Node& root : *nodes) {
      if (RemoveFromOptionArrays(&root, row)) {
        ++removed;
        break;
      }
    }
  }
  if (removed == 0) {
    return refuse("none of the named rows are in the option list");
  }
  if (reason != nullptr) reason->clear();
  return true;
}

const MenuConfiguration& CurrentMenuConfiguration() { return g_configuration; }
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

// The retail entries the merged surface rewrites. All four are drawn by retail's own AV page
// (`ui/av_options/gen/av_options.dtb`): its two checkboxes are `[av_overscan {if_else overscan
// enabled_token disabled_token}]` and the locale formats them as `Overscan | ENABLED` /
// `Bass Boost | DISABLED`. The merged surface's own booleans read `<label>: On`, so both halves of
// each string change - the format's `|` becomes `:`, and the state word loses its capitals.
std::vector<LocaleOverride> LocaleOverrides() {
  return {
      {"av_overscan", "Overscan | %s", "Overscan: %s"},
      {"av_bass_boost", "Bass Boost | %s", "Bass Boost: %s"},
      {"enabled_token", "ENABLED", "On"},
      {"disabled_token", "DISABLED", "Off"},
  };
}

bool ReplaceLocaleEntry(std::vector<dtb::Node>* nodes, const std::string_view key,
                        const std::string_view from, const std::string_view to,
                        std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || key.empty() || from.empty() || to.empty()) {
    return refuse("nothing to replace");
  }
  for (Node& root : *nodes) {
    if (root.tag != dtb::kArray || root.kids.size() < 2) {
      continue;
    }
    if (!IsSymbol(root.kids[0], key) || root.kids[1].tag != dtb::kString) {
      continue;
    }
    if (root.kids[1].text == to) {
      // Already rewritten - an idempotent second pass, not an error.
      if (reason != nullptr) reason->clear();
      return true;
    }
    if (root.kids[1].text != from) {
      // A locale that carries a different label for the key (a translation, or a title whose
      // string changed) is left exactly as it ships.
      if (reason != nullptr) *reason = "the entry carries a different label";
      return true;
    }
    root.kids[1].text = to;
    if (reason != nullptr) reason->clear();
    return true;
  }
  // The key is absent here (a locale that does not carry it): nothing to do.
  if (reason != nullptr) *reason = "the locale has no such entry";
  return true;
}

// Every locale entry the surface adds, again derived from the table. A row with an empty label is a
// retail row symbol whose text the installed locale already carries, so nothing is added for it.
std::vector<std::pair<std::string, std::string>> Labels() {
  std::vector<std::pair<std::string, std::string>> out;
  out.emplace_back("settings_restart_message", "Settings saved. Restart the game to apply these changes.");
  out.emplace_back("settings_save_error_message", "Settings could not be saved. Check that the settings folder is writable, then try again.");
  out.emplace_back("settings_save_error_provider", "Saved, but the Ultimate mod's own settings file could not be written. Close this message and try again.");
  out.emplace_back("settings_save_error_conflict", "Something else changed the settings file. Close this message and try again.");
  out.emplace_back("settings_save_error_other", "Settings could not be saved. Close this message and try again; if it keeps failing, restart the game.");
  out.emplace_back("settings_restart_error", "The game could not restart. Close this message and start the game again to apply your saved changes.");
  if (g_configuration.play_current_song && !g_configuration.menu_music_songs.empty()) {
    out.emplace_back("settings_play_unavailable", "The background song is not available to play. Please try again when another song starts.");
  }
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
        // "On"/"Off", not the main menu's capitals: every state suffix of this surface reads the
        // way its row label does (owner decision, 2026-10-13). A visibility row overrides both
        // halves with "Hidden"/"Visible".
        add(row.symbol + "_on", row.on_label.empty() ? row.label + ": On" : row.on_label);
        add(row.symbol + "_off", row.off_label.empty() ? row.label + ": Off" : row.off_label);
      } else if (row.kind == Kind::kStateSubmenu) {
        // A binding row's pair: whether the row is the player's or the compiled default.
        add(row.symbol + "_on", row.label + ": Custom");
        add(row.symbol + "_off", row.off_label.empty() ? row.label + ": Default"
                                                        : row.off_label);
      } else if (row.kind == Kind::kStateAction) {
        add(row.symbol + "_on", row.label);
        add(row.symbol + "_off", row.off_label.empty() ? row.label : row.off_label);
      } else if (row.kind == Kind::kSelect) {
        for (const Choice& choice : row.choices) {
          add(row.symbol + "_" + SymbolToken(choice.token), row.label + ": " + choice.label);
        }
      } else {
        add(row.symbol, row.label);
      }
    }
  }
  // The retail Audio page is reached through our route row rather than declared by a PanelSpec, so
  // its title is the one page title that is not derived from the table above.
  add("settings_title_audio", "AUDIO");
  // Every page title, derived from the same table: the key is stable per page name and the in-song
  // twin shares it, because both contexts draw the same words.
  for (const PanelSpec& panel : AllPanels()) {
    if (!panel.title_key.empty()) {
      add(panel.title_key, UpperAscii(panel.title));
    }
  }
  // The Play Current Song entry is not a row of this surface, so its label is not derived from the
  // table above: the main menu draws the text of the symbol `kPlayCurrentRow`, which is a locale key
  // of its own. Added only on the boot that builds the row, so a payload without the feature carries
  // no string for a row no menu has.
  if (g_configuration.play_current_song && !g_configuration.menu_music_songs.empty()) {
    add(kPlayCurrentRow, kPlayCurrentLabel);
  }
  // The restored Download Content entry (owner request, 2026-10-20) is the same kind of key: added
  // only on the boot that builds the main-menu row.
  if (g_configuration.download_content_row) {
    add(kDownloadContentRow, kDownloadContentLabel);
  }
  // The RhythmVerse Customs entry (plan P1) is the same kind of key: only the boot that builds the
  // row carries its string.
  if (g_configuration.rhythmverse_row) {
    add(kRhythmVerseRow, kRhythmVerseLabel);
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

// Appends to a retail panel's `{enter ...}` member. The panel definition is a `{new UIPanel <name>
// ...}` command; its `enter` member is a block whose first child is the `enter` symbol. Statements
// are appended at the end of that block, so the panel's own enter - which is what loads/fills the
// scene - has already run. See the header for why the pre-goto placement does not work.
std::vector<Node> AudioPageEnterStatements() {
  return {
      Node::Command({Node::Symbol("checkbox.lst"), Node::Symbol("set_local_pos"),
                     Node::Var("rbbz_audio_x"), Node::Var("rbbz_audio_y"),
                     Node::Var("rbbz_audio_z")}),
      Node::Command({Node::Symbol("header.lbl"), Node::Symbol("set"),
                     Node::Symbol("text_token"), Node::Symbol("settings_title_audio")}),
  };
}

// P4's earlier measurement entry (a `{ui goto_screen <search>}` appended to the *retail store
// screen's* `{enter}`) is retired: a jump issued from that screen re-delivers every later press to
// the page that has just become current (measured 2026-10-20 - two `search/key/a` writes in one
// frame, one Down moving two rows). `store_search_diagnostic` now routes the RhythmVerse main-menu
// row straight at the keys instead, which behaves like every other page of the surface.

bool AppendPanelEnterStatements(std::vector<dtb::Node>* nodes, std::string_view panel_name,
                                const std::vector<dtb::Node>& statements, std::string* reason) {
  auto refuse = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (nodes == nullptr || panel_name.empty()) {
    return refuse("nothing to edit");
  }
  if (statements.empty()) {
    return refuse("no statements to append");
  }
  // The definition is usually a root, and `FindBlock` starts at a node's *children*, so test
  // the root itself before searching inside it. (A root that is the definition command is the
  // normal case for `av_options.dtb`; searching only children made this edit refuse every real
  // record.) Both kinds are accepted: a UIPanel (the retail Audio page) and a UIScreen (the
  // retail store screen, whose `enter` is where the search measurement hooks in).
  const auto is_panel = [&](const Node& node) {
    return node.tag == dtb::kCommand && node.kids.size() >= 3 &&
           IsSymbol(node.kids[0], "new") &&
           (IsSymbol(node.kids[1], "UIPanel") || IsSymbol(node.kids[1], "UIScreen")) &&
           IsSymbol(node.kids[2], panel_name);
  };
  Node* definition = nullptr;
  for (Node& root : *nodes) {
    if (is_panel(root)) {
      definition = &root;
      break;
    }
    Hit hit;
    FindBlock(&root, is_panel, &hit);
    if (hit.found && hit.parent != nullptr && hit.index < hit.parent->kids.size()) {
      definition = &hit.parent->kids[hit.index];
      break;
    }
  }
  if (definition == nullptr) {
    return refuse("no {new UIPanel|UIScreen <name> ...} definition for that panel");
  }
  Node* enter = nullptr;
  for (Node& member : definition->kids) {
    if ((member.tag == dtb::kArray || member.tag == dtb::kCommand) && !member.kids.empty() &&
        IsSymbol(member.kids[0], "enter")) {
      enter = &member;
      break;
    }
  }
  if (enter == nullptr) {
    return refuse("the panel has no {enter ...} member to append to");
  }
  for (const Node& statement : statements) {
    enter->kids.push_back(statement);
  }
  if (reason != nullptr) reason->clear();
  return true;
}

}  // namespace rb_blitz::ui::screen
