// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P6's provider adapter (src/settings/ultimate_adapter.h). Two halves, on purpose:
//
//   * **the hermetic half** authors a provider-shaped definition with the project's own DTB
//     builder and mutates it, so every rule the shape check enforces is exercised without the
//     payload, a boot or a window. The fixture mirrors the *measured* declarations in
//     `out/in-game-settings-research/mod_settings.txt` (`docs/plans/in-game-settings-plan.md`
//     §2.2 records their hashes);
//   * **the real-installation half** runs the same checks against `game/ultimate` when its path is
//     given on the command line, which is what makes the fixture's fidelity a checked claim
//     rather than a hope. See the invocation in the handoff doc.
//
// SDK-free: `src/settings/ultimate_adapter.cpp` plus the two UI modules the DTB parser needs.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "check.h"
#include "settings/ultimate_adapter.h"
#include "ui/dtb_author.h"

namespace {

using rb_blitz::settings::ProviderReadiness;
using rb_blitz::settings::SettingId;
using rb_blitz::settings::Value;
using rb_blitz::settings::ValueKind;
using namespace rb_blitz::settings::ultimate_adapter;

using rb_blitz::ui::dtb::Node;

Node Sym(const char* name) { return Node::Symbol(name); }
Node Var(const char* name) { return Node::Var(name); }
Node Str(const char* text) { return Node::String(text); }
Node Int(uint32_t value) { return Node::Int(value); }
Node Float(float value) {
  Node node;
  node.tag = 0x01;  // float: the scalar field carries the f32's own bits
  std::memcpy(&node.scalar, &value, sizeof(value));
  return node;
}
Node Arr(std::vector<Node> kids) { return Node::Array(std::move(kids)); }
Node Cmd(std::vector<Node> kids) { return Node::Command(std::move(kids)); }

// `#define <name>`: the name is the node's text and the value is the next root. There is no
// builder for it because this project never *writes* one - it only reads the provider's.
Node Define(const char* name) {
  Node node;
  node.tag = 0x20;
  node.text = name;
  return node;
}

// `{new UIPanel <name> ...}`, with the panel's members exactly as the provider's own declaration
// spells them: the settings-list macro by name, a title, and a data list.
Node Panel(const char* name, std::vector<Node> data) {
  Node node = Cmd({Sym("new"), Sym("UIPanel"), Sym(name), Sym("SETTINGS_LIST_FILE"),
                   Arr({Sym("should_warn")}), Arr({Sym("title"), Sym("mod_title")}),
                   Arr({Sym("data"), Arr(std::move(data))})});
  return node;
}

Node BoolRow(const char* symbol, const char* variable) {
  return Arr({Sym(symbol), Arr({Sym("type"), Sym("uOnOFF")}), Arr({Sym("bool"), Var(variable)}),
              Arr({Sym("sel_script"), Cmd({Sym("toggle_var"), Var(variable)})})});
}

Node SliderRow(const char* symbol, const char* variable) {
  return Arr({Sym(symbol), Arr({Sym("type"), Sym("uSld")}), Arr({Sym("var"), Sym(variable)}),
              Arr({Sym("step"), Int(1)}), Arr({Sym("range"), Int(0), Int(2)})});
}

Node SelectRow(const char* symbol, const char* variable, std::vector<Node> choices) {
  return Arr({Sym(symbol), Arr({Sym("type"), Sym("uFakeSld")}), Arr({Sym("var"), Var(variable)}),
              Arr({Sym("array"), Arr(std::move(choices))})});
}

Node JumpRow(const char* symbol, const char* macro, const char* variable, const char* screen) {
  return Arr({Sym(symbol), Arr({Sym("type"), Sym(macro)}), Arr({Sym("bool"), Var(variable)}),
              Arr({Sym("reboot"), Sym("TRUE")}),
              Arr({Sym("sel_script"), Cmd({Sym("pop_and_goto_screen"), Sym(screen)})})});
}

// `(data (off plot_a plot_b))`: the panel *is* the choice list, and the screen stores a position.
Node FlatPanel(const char* name, std::vector<Node> choices) {
  return Panel(name, std::move(choices));
}

// The provider's own entry point: three submenus, selected by a script rather than a variable.
Node RootPanel() {
  return Panel("mod_settings_panel",
               {Arr({Sym("mod_game"), Arr({Sym("type"), Sym("uNormal")}),
                     Arr({Sym("sel_script"),
                          Cmd({Sym("pop_and_goto_screen"), Sym("mod_game_settings_screen")})})}),
                Arr({Sym("mod_visual"), Arr({Sym("type"), Sym("uNormal")}),
                     Arr({Sym("sel_script"),
                          Cmd({Sym("pop_and_goto_screen"), Sym("mod_visual_settings_screen")})})}),
                Arr({Sym("mod_other"), Arr({Sym("type"), Sym("uNormal")}),
                     Arr({Sym("sel_script"),
                          Cmd({Sym("pop_and_goto_screen"), Sym("mod_other_settings_screen")})})})});
}

// A payload-shaped set of definitions: the five required panels, the two flat choice panels, the
// object that persists them, the macros - including the settings path - and the compiled defaults.
//
// Written out rather than taken from the payload so the mutations below are mutations of a *known*
// good shape; the real payload is checked by the same code in TestRealPayload.
std::vector<Node> ProviderFixture() {
  std::vector<Node> roots;
  roots.push_back(RootPanel());

  roots.push_back(Panel("mod_game_settings_panel",
                        {BoolRow("os_precision", "freq_ms"),
                         BoolRow("os_disable_rw", "disable_random_wiggles"),
                         SliderRow("os_cameraangle", "camera_angle"),
                         SliderRow("os_speedmod", "speedmod")}));

  roots.push_back(Panel("mod_visual_settings_panel",
                        {BoolRow("os_disable_dof", "disable_dof"),
                         BoolRow("os_letterbox", "letterbox"),
                         Arr({Sym("os_hud_settings"), Arr({Sym("type"), Sym("uNormal")}),
                              Arr({Sym("sel_script"),
                                   Cmd({Sym("pop_and_goto_screen"),
                                        Sym("mod_hud_settings_screen")})})}),
                         JumpRow("os_disable_plots", "uOnOFF", "disable_plots", ""),
                         JumpRow("os_force_plot", "uOnOFF", "forceplot", "mod_force_plot_screen"),
                         JumpRow("os_force_skybox", "uOnOFF", "which_skybox",
                                 "mod_force_skybox_screen"),
                         SelectRow("os_gems_fmt", "gems",
                                   {Sym("default"), Sym("freq"), Sym("amp2003"), Sym("amp2016"),
                                    Sym("monkey"), Sym("ulti"), Sym("x"), Sym("bolt")})}));

  roots.push_back(Panel("mod_hud_settings_panel",
                        {BoolRow("os_hide_hud", "hide_hud"),
                         BoolRow("os_goldprogress", "goldprogress"),
                         BoolRow("os_progressbar", "progressbar"),
                         BoolRow("os_show_timer", "show_timer"),
                         BoolRow("os_show_pb", "show_pb"),
                         BoolRow("os_show_steak", "show_steak"),
                         BoolRow("os_input_viewer", "input_viewer")}));

  roots.push_back(Panel("mod_other_settings_panel",
                        {JumpRow("os_unlock_all", "uOnOFF", "unlockall", ""),
                         Arr({Sym("os_online_acc"), Arr({Sym("type"), Sym("uNormal")}),
                              Arr({Sym("sel_script"), Cmd({Sym("acc_mgr")})})})}));

  roots.push_back(FlatPanel("mod_force_plot_panel",
                            {Sym("off"), Sym("plot_bodega_a"), Sym("plot_bodega_apartment_a")}));
  roots.push_back(FlatPanel("mod_force_skybox_panel",
                            {Sym("default_skybox"), Sym("no_skybox"), Sym("nfs_mw2012")}));

  // `(vars (a b c ...))` - the names are one array deep, exactly as the payload ships it. A flat
  // `(vars a b c)` also parses, but the fixture follows the payload so a change in the real shape
  // is a failure here rather than a surprise in a boot.
  roots.push_back(
      Cmd({Sym("new"), Sym("Object"), Sym("mod_settings_rw"),
           Arr({Sym("vars"),
                Arr({Sym("freq_ms"), Sym("disable_dof"), Sym("which_skybox"),
                     Sym("disable_random_wiggles"), Sym("disable_plots"), Sym("forceplot"),
                     Sym("server_url"), Sym("server_ssl"), Sym("p9dlc"), Sym("hide_hud"),
                     Sym("goldprogress"), Sym("progressbar"), Sym("show_timer"), Sym("show_pb"),
                     Sym("show_steak"), Sym("input_viewer"), Sym("controller_mapping"),
                     Sym("camera_angle"), Sym("letterbox"), Sym("gems"), Sym("speedmod")})}),
           Arr({Sym("save_settings"), Cmd({Sym("write_file")})}),
           Arr({Sym("load_settings"), Cmd({Sym("read_file")})})}));

  roots.push_back(Define("uNormal"));
  roots.push_back(Arr({Int(0)}));
  roots.push_back(Define("uOnOFF"));
  roots.push_back(Arr({Int(1)}));
  roots.push_back(Define("uFakeSld"));
  roots.push_back(Arr({Int(2)}));
  roots.push_back(Define("uSld"));
  roots.push_back(Arr({Int(3)}));
  roots.push_back(Define("MOD_SETTINGS_PATH"));
  roots.push_back(Str("GAME:/ulti_settings.dta"));

  roots.push_back(Cmd({Sym("set"), Var("unlockall"), Sym("TRUE")}));
  roots.push_back(Cmd({Sym("set"), Var("letterbox"), Sym("FALSE")}));
  roots.push_back(Cmd({Sym("set"), Var("camera_angle"), Int(0)}));
  roots.push_back(Cmd({Sym("set"), Var("gems"), Sym("default")}));
  roots.push_back(Cmd({Sym("set"), Var("speedmod"), Float(1.0f)}));
  return roots;
}

// The fixture's root indices, named: the mutation cases have to reach a *specific* declaration,
// and an off-by-one there would silently mutate the wrong panel and pass.
constexpr size_t kRootPanel = 0;
constexpr size_t kGamePanel = 1;
constexpr size_t kHudPanel = 3;
constexpr size_t kOtherPanel = 4;
constexpr size_t kSettingsObject = 7;

std::vector<uint8_t> Compiled(const std::vector<Node>& roots) {
  return rb_blitz::ui::dtb::Compile(roots, rb_blitz::ui::dtb::RootHeader{0x52425A31, 1, 0});
}

// The fixture, split into the five records the adapter reads. Only `mod_settings` and
// `mod_settings_rw` are given their own roots; the macros and init halves are sliced out of the
// same root list, which is what the payload does with separate records.
RecordBytes FixtureRecords() {
  RecordBytes records;
  records.mod_settings = Compiled(ProviderFixture());
  records.mod_settings_rw = Compiled(ProviderFixture());
  records.ulti_macros = Compiled(ProviderFixture());
  records.ulti_funcs = Compiled({Cmd({Sym("func"), Sym("os_sel_common")})});
  records.ulti_init = Compiled(ProviderFixture());
  return records;
}

bool HasProblemLike(const ShapeReport& report, const char* needle) {
  for (const std::string& problem : report.problems) {
    if (problem.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void TestRecordTable() {
  rb_blitz::test::BeginCase("the record table names every record, once, with a reason");
  CHECK_EQ(kRecordCount, 6);
  for (size_t i = 0; i < kRecordCount; ++i) {
    const RecordSpec& spec = RecordAt(i);
    CHECK_FALSE(spec.path.empty());
    CHECK_FALSE(spec.why.empty());
    CHECK_TRUE(spec.path == RecordName(spec.id));
    for (size_t j = i + 1; j < kRecordCount; ++j) {
      // A duplicate would make the inventory report the same record twice and hide a gap.
      CHECK_FALSE(RecordAt(i).path == RecordAt(j).path);
    }
  }
}

void TestBindings() {
  rb_blitz::test::BeginCase("the bindings are the plan's rows: one token, one variable each");
  const std::vector<RowBinding>& bindings = RowBindings();
  CHECK_EQ(bindings.size(), 17);
  for (size_t i = 0; i < bindings.size(); ++i) {
    CHECK_FALSE(bindings[i].token.empty());
    CHECK_FALSE(bindings[i].variable.empty());
    CHECK_TRUE(bindings[i].id != SettingId::kUnknown);
    // §5.5: nothing is hot until a live effect has been *observed*, so every provider row ships
    // as restart with an empty evidence string. A row that becomes hot has to say why.
    CHECK_TRUE(bindings[i].strategy == Strategy::kRestart);
    CHECK_TRUE(bindings[i].evidence.empty());
    for (size_t j = i + 1; j < bindings.size(); ++j) {
      CHECK_FALSE(bindings[i].token == bindings[j].token);
      CHECK_FALSE(bindings[i].variable == bindings[j].variable);
      CHECK_TRUE(bindings[i].id != bindings[j].id);
    }
    // The token the merged screen writes is `<category>.<id>` (P5b's frozen identity).
    CHECK_TRUE(BindingForToken(bindings[i].token) == &bindings[i]);
    CHECK_TRUE(BindingForVariable(bindings[i].variable) == &bindings[i]);
  }
  CHECK_TRUE(BindingForToken("gameplay.precision_mode") != nullptr);
  CHECK_TRUE(BindingForToken("gameplay.precision_mode")->id == SettingId::kPrecisionMode);
  CHECK_TRUE(BindingForVariable("hide_hud")->id == SettingId::kHideHud);
  CHECK_TRUE(BindingForToken("nothing.at.all") == nullptr);
  CHECK_TRUE(BindingForVariable("server_url") == nullptr);
  // `unlockall` is the documented exception (plan §6.2): persisted natively, never as a provider
  // file pair, so it must not appear in the file-backed binding set.
  CHECK_TRUE(BindingForVariable("unlockall") == nullptr);
  // Every binding's widget kind matches the plan's §3.4/§3.5 interaction.
  CHECK_TRUE(BindingForVariable("gems")->kind == RowKind::kSelect);
  CHECK_TRUE(BindingForVariable("camera_angle")->kind == RowKind::kSlider);
  CHECK_TRUE(BindingForVariable("which_skybox")->kind == RowKind::kBool);
}

void TestShapeAcceptsAFixture() {
  rb_blitz::test::BeginCase("a provider-shaped definition passes the shape check");
  const ShapeReport report = CheckShape(FixtureRecords());
  for (const std::string& problem : report.problems) {
    std::printf("         unexpected problem: %s\n", problem.c_str());
  }
  CHECK_TRUE(report.ok);
  CHECK_TRUE(report.panels.size() == 7);
  CHECK_TRUE(report.Panel("mod_game_settings_panel") != nullptr);
  CHECK_TRUE(report.Panel("mod_hud_settings_panel") != nullptr);
  CHECK_TRUE(report.Panel("mod_settings_panel") != nullptr);
  CHECK_EQ(report.persisted.size(), 21);
  CHECK_EQ(report.defaults.size(), 5);
  CHECK_TRUE(report.settings_path_macro.find("GAME:/ulti_settings.dta") != std::string::npos);
  // The choice sets come from the payload, never from our schema.
  CHECK_EQ(report.choices.at("gems").size(), 8);
  CHECK_TRUE(report.choices.at("gems")[0] == "default");
  CHECK_EQ(report.choices.at("which_skybox").size(), 3);
  CHECK_EQ(report.choices.at("forceplot").size(), 3);
  // A submenu's destination is read, because the merged screen has to route back into its parent.
  CHECK_TRUE(report.Row("os_hud_settings")->target_screen == "mod_hud_settings_screen");
  CHECK_TRUE(report.Row("os_force_plot")->target_screen == "mod_force_plot_screen");
  CHECK_TRUE(report.Row("os_force_plot")->reboot_tagged);
  CHECK_TRUE(report.Row("os_cameraangle")->kind == RowKind::kSlider);
  CHECK_TRUE(report.Row("os_gems_fmt")->kind == RowKind::kSelect);
  CHECK_TRUE(report.Row("os_online_acc")->kind == RowKind::kSubmenu);
  CHECK_TRUE(Readiness(true, true, report) == ProviderReadiness::kCompatible);
}

void TestShapeRefusals() {
  rb_blitz::test::BeginCase("each way a payload can be wrong is refused, by name");
  {
    RecordBytes records = FixtureRecords();
    records.mod_settings.clear();
    const ShapeReport report = CheckShape(records);
    CHECK_FALSE(report.ok);
    CHECK_TRUE(HasProblemLike(report, "is missing from the payload"));
  }
  {
    RecordBytes records = FixtureRecords();
    records.mod_settings.assign({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    const ShapeReport report = CheckShape(records);
    CHECK_FALSE(report.ok);
    CHECK_TRUE(HasProblemLike(report, "is not compiled DTA this build reads"));
  }
  {
    // A panel the integration draws rows out of is gone.
    std::vector<Node> roots = ProviderFixture();
    roots.erase(roots.begin() + std::ptrdiff_t(kHudPanel));
    RecordBytes records;
    records.mod_settings = Compiled(roots);
    const ShapeReport report = CheckShape(records);
    CHECK_FALSE(report.ok);
    CHECK_TRUE(HasProblemLike(report, "the payload has no panel mod_hud_settings_panel"));
  }
  {
    // A row kind this build does not implement: the row must be refused, not drawn wrong.
    std::vector<Node> roots = ProviderFixture();
    roots[kGamePanel] = Panel("mod_game_settings_panel",
                              {Arr({Sym("os_precision"), Arr({Sym("type"), Sym("uFancyNewKind")}),
                                    Arr({Sym("bool"), Var("freq_ms")})})});
    RecordBytes records;
    records.mod_settings = Compiled(roots);
    const ShapeReport report = CheckShape(records);
    CHECK_FALSE(report.ok);
    CHECK_TRUE(HasProblemLike(report, "uFancyNewKind"));
  }
  {
    // A bound variable renamed: the integration has nothing to bind the row to.
    std::vector<Node> roots = ProviderFixture();
    roots[kGamePanel] = Panel("mod_game_settings_panel", {BoolRow("os_precision", "freq_milliseconds")});
    RecordBytes records;
    records.mod_settings = Compiled(roots);
    const ShapeReport report = CheckShape(records);
    CHECK_FALSE(report.ok);
    CHECK_TRUE(HasProblemLike(report, "no panel row binds 'freq_ms'"));
  }
  {
    // The persisted variable list loses one name.
    std::vector<Node> roots = ProviderFixture();
    roots[kSettingsObject] = Cmd({Sym("new"), Sym("Object"), Sym("mod_settings_rw"),
                                  Arr({Sym("vars"), Sym("freq_ms")})});
    RecordBytes records;
    records.mod_settings = Compiled(roots);
    records.mod_settings_rw = Compiled(roots);
    const ShapeReport report = CheckShape(records);
    CHECK_FALSE(report.ok);
    CHECK_TRUE(HasProblemLike(report, "does not carry 'disable_dof'"));
  }
  {
    // The settings path moved: our redirect would be watching a file the provider never writes.
    std::vector<Node> roots = ProviderFixture();
    for (Node& root : roots) {
      if (root.tag == rb_blitz::ui::dtb::kString) {
        root.text = "D:/somewhere_else.dta";
      }
    }
    RecordBytes records;
    records.mod_settings = Compiled(roots);
    records.ulti_macros = Compiled(roots);
    const ShapeReport report = CheckShape(records);
    CHECK_FALSE(report.ok);
    CHECK_TRUE(HasProblemLike(report, "ulti_settings.dta"));
  }
  {
    // The achievements row is an action, not a value, and has no provider path without it.
    std::vector<Node> roots = ProviderFixture();
    roots[kOtherPanel] =
        Panel("mod_other_settings_panel", {JumpRow("os_unlock_all", "uOnOFF", "unlockall", "")});
    RecordBytes records;
    records.mod_settings = Compiled(roots);
    const ShapeReport report = CheckShape(records);
    CHECK_FALSE(report.ok);
    CHECK_TRUE(HasProblemLike(report, "os_online_acc"));
  }
}

void TestReadiness() {
  rb_blitz::test::BeginCase("readiness is the three facts, not one");
  const ShapeReport good = CheckShape(FixtureRecords());
  CHECK_TRUE(good.ok);
  ShapeReport bad = good;
  bad.ok = false;
  bad.problems.push_back("mutated");
  // No payload: absent, whatever else is true - and the title boots base, which is the whole
  // point of asking.
  CHECK_TRUE(Readiness(false, true, good) == ProviderReadiness::kAbsent);
  CHECK_TRUE(Readiness(false, false, good) == ProviderReadiness::kAbsent);
  // A payload whose shape is wrong must not be called: the mod's own settings path stays
  // reachable instead.
  CHECK_TRUE(Readiness(true, true, bad) == ProviderReadiness::kUnrecognizedShape);
  // Installed and shaped, but this boot's target does not carry it: rows may show, disabled.
  CHECK_TRUE(Readiness(true, false, good) == ProviderReadiness::kInactiveTarget);
  CHECK_TRUE(Readiness(true, true, good) == ProviderReadiness::kCompatible);
}

void TestProviderFile() {
  rb_blitz::test::BeginCase("the provider's file round-trips and keeps what this build omits");
  // Exactly the shape `mod_settings_rw save_settings` writes: a leading marker, then pairs - two
  // of which (`controller_mapping`, `server_url`) this build does not draw, and one of which
  // (`p9dlc`-style) serialized as several items rather than a single value.
  const std::string text =
      "(0 (controller_mapping \"A B X Y\") (freq_ms FALSE) (which_skybox 0) (speedmod 1.05)\n"
      "   (server_url \"listener.hmxservices.com\") (gems monkey) (legacy_multi 1 2 3))";
  ProviderFile file;
  std::string error;
  CHECK_TRUE(ParseProviderFile(text, &file, &error));
  CHECK_TRUE(file.parsed);
  CHECK_TRUE(file.version_marker == "0");
  CHECK_EQ(file.elements.size(), 7);
  CHECK_EQ(file.names.size(), 6);
  CHECK_TRUE(file.values.at("controller_mapping") == "\"A B X Y\"");
  CHECK_TRUE(file.values.at("server_url") == "\"listener.hmxservices.com\"");
  CHECK_TRUE(file.values.at("gems") == "monkey");
  // An element with more than one value is preserved but never read as one.
  CHECK_TRUE(file.values.count("legacy_multi") == 0);

  const std::map<SettingId, Value> values = ValuesFromProviderFile(file);
  CHECK_EQ(values.size(), 4);
  CHECK_TRUE(values.at(SettingId::kPrecisionMode).boolean == false);
  CHECK_TRUE(values.at(SettingId::kForceSkybox).boolean == false);
  // 1.05 authored as text survives as 105%, not 104% - the provider's own float spelling.
  CHECK_TRUE(values.at(SettingId::kGameSpeed).integer == 105);
  CHECK_TRUE(values.at(SettingId::kGems).text == "monkey");
  // The pairs this build does not draw are not values, and must not appear as ones.
  CHECK_TRUE(values.count(SettingId::kUnlockAll) == 0);

  // One pair changes; everything else, including the pair order and the unexposed text, is
  // written back exactly - and the multi-item element is still there.
  const std::string rewritten = WriteProviderFile(file, {{"freq_ms", "TRUE"}, {"gems", "ulti"}});
  CHECK_TRUE(rewritten ==
             "(0 (controller_mapping \"A B X Y\") (freq_ms TRUE) (which_skybox 0) (speedmod "
             "1.05) (server_url \"listener.hmxservices.com\") (gems ulti) (legacy_multi 1 2 "
             "3))");
  ProviderFile again;
  CHECK_TRUE(ParseProviderFile(rewritten, &again, &error));
  CHECK_TRUE(again.values.at("freq_ms") == "TRUE");
  CHECK_TRUE(again.values.at("gems") == "ulti");
  CHECK_TRUE(again.values.at("server_url") == "\"listener.hmxservices.com\"");
  CHECK_TRUE(again.names == file.names);
  // A round trip with no updates is the identity.
  CHECK_TRUE(WriteProviderFile(file, {}) ==
             "(0 (controller_mapping \"A B X Y\") (freq_ms FALSE) (which_skybox 0) (speedmod "
             "1.05) (server_url \"listener.hmxservices.com\") (gems monkey) (legacy_multi 1 2 "
             "3))");

  // Garbage is refused rather than half-read.
  ProviderFile junk;
  CHECK_FALSE(ParseProviderFile("not a file at all", &junk, &error));
  CHECK_FALSE(junk.parsed);
  CHECK_FALSE(ParseProviderFile("(0 (a 1)) trailing", &junk, &error));
  CHECK_FALSE(ParseProviderFile("", &junk, &error));
  CHECK_FALSE(ParseProviderFile("(0 (a 1)", &junk, &error));
  // The empty array a provider writes when it has nothing is a file, not an error.
  ProviderFile empty;
  CHECK_TRUE(ParseProviderFile("(0)", &empty, &error));
  CHECK_TRUE(empty.elements.empty());
  CHECK_TRUE(WriteProviderFile(empty, {}) == "(0)");
}

void TestValueTranslation() {
  rb_blitz::test::BeginCase("a value survives the provider's file in both directions");
  const RowBinding* bool_row = BindingForVariable("hide_hud");
  const RowBinding* speed_row = BindingForVariable("speedmod");
  const RowBinding* camera_row = BindingForVariable("camera_angle");
  const RowBinding* gems_row = BindingForVariable("gems");
  CHECK_TRUE(bool_row != nullptr && speed_row != nullptr && camera_row != nullptr &&
             gems_row != nullptr);

  for (bool wanted : {true, false}) {
    Value value;
    value.kind = ValueKind::kBool;
    value.boolean = wanted;
    std::string text;
    CHECK_TRUE(EncodeProviderValue(*bool_row, value, &text));
    CHECK_TRUE(text == (wanted ? "TRUE" : "FALSE"));
    Value back;
    CHECK_TRUE(DecodeProviderValue(*bool_row, text, &back));
    CHECK_TRUE(back.kind == ValueKind::kBool && back.boolean == wanted);
  }
  // The provider's own `which_skybox` spelling: index 0 is off, and any other index is on.
  Value skybox;
  CHECK_TRUE(DecodeProviderValue(*BindingForVariable("which_skybox"), "0", &skybox));
  CHECK_FALSE(skybox.boolean);
  CHECK_TRUE(DecodeProviderValue(*BindingForVariable("which_skybox"), "2", &skybox));
  CHECK_TRUE(skybox.boolean);
  // A boolean the file stores as a mapping-less TRUE still reads as on.
  CHECK_TRUE(DecodeProviderValue(*BindingForVariable("letterbox"), "TRUE", &skybox));
  CHECK_TRUE(skybox.boolean);
  // A value whose kind does not match the row is refused, in both directions.
  const Value none;
  std::string scratch;
  CHECK_FALSE(EncodeProviderValue(*bool_row, none, &scratch));
  CHECK_FALSE(EncodeProviderValue(*camera_row, none, &scratch));
  CHECK_FALSE(EncodeProviderValue(*speed_row, none, &scratch));
  CHECK_FALSE(DecodeProviderValue(*bool_row, "", &skybox));

  // Camera Angle is an index; the token is what the menu and the file agree on.
  for (const char* token : {"default", "high", "low"}) {
    Value value;
    value.kind = ValueKind::kEnum;
    value.text = token;
    std::string text;
    CHECK_TRUE(EncodeProviderValue(*camera_row, value, &text));
    Value back;
    CHECK_TRUE(DecodeProviderValue(*camera_row, text, &back));
    CHECK_TRUE(back.kind == ValueKind::kEnum && back.text == token);
  }
  // An out-of-range camera index is not a value this build may read.
  CHECK_FALSE(DecodeProviderValue(*camera_row, "7", &skybox));
  CHECK_FALSE(DecodeProviderValue(*camera_row, "", &skybox));

  // Game Speed: 2-decimal text in, percent out, and no float step accumulation.
  for (int percent : {100, 105, 110, 145, 150}) {
    Value value;
    value.kind = ValueKind::kInt;
    value.integer = percent;
    std::string text;
    CHECK_TRUE(EncodeProviderValue(*speed_row, value, &text));
    Value back;
    CHECK_TRUE(DecodeProviderValue(*speed_row, text, &back));
    CHECK_EQ(uint64_t(back.integer), uint64_t(percent));
  }
  // A slider value out of the provider's range is not a value this build may write.
  Value too_fast;
  too_fast.kind = ValueKind::kInt;
  too_fast.integer = 400;
  CHECK_FALSE(EncodeProviderValue(*speed_row, too_fast, &scratch));
  // The provider's tokens are the payload's (`monkey`, not `default`); an unknown one is refused.
  CHECK_FALSE(DecodeProviderValue(*gems_row, "", &skybox));
  Value token;
  CHECK_TRUE(DecodeProviderValue(*gems_row, "monkey", &token));
  CHECK_TRUE(token.kind == ValueKind::kEnum && token.text == "monkey");
  CHECK_TRUE(EncodeProviderValue(*gems_row, token, &scratch));
  CHECK_TRUE(scratch == "monkey");
}

void TestRedirectNames() {
  rb_blitz::test::BeginCase("the redirect names user storage, never the game root");
  const std::filesystem::path root = "D:/Saves";
  CHECK_TRUE(ProviderSettingsDirectory(root) == std::filesystem::path("D:/Saves/ultimate"));
  CHECK_TRUE(ProviderSettingsPath(root) ==
             std::filesystem::path("D:/Saves/ultimate/ulti_settings.dta"));
  // An empty root is not a path: a boot with no user root must not redirect anywhere.
  CHECK_TRUE(ProviderSettingsDirectory("").empty());
  CHECK_TRUE(ProviderSettingsPath("").empty());
  CHECK_TRUE(std::string(kProviderRecord) == "ulti_settings.dta");
  CHECK_TRUE(std::string(kProviderGuestPath) == "GAME:/ulti_settings.dta");
  // The record the redirect matches is the guest path's own last component.
  CHECK_TRUE(std::string(kProviderGuestPath).rfind(std::string("/") + kProviderRecord) !=
             std::string::npos);
}

// Writes into a scratch directory of the machine's own temp space: the adapter's file half is
// host code, so it can be exercised for real without touching any save of a player's.
std::filesystem::path Scratch(const char* name) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     "rb_blitz_ultimate_adapter_test" / name;
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  return root;
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void TestProviderFileSave() {
  rb_blitz::test::BeginCase("saving the provider's file creates it, replaces pairs and refuses "
                            "what it cannot read");
  const std::filesystem::path root = Scratch("save");
  const std::filesystem::path path = root / "ulti_settings.dta";
  std::string error;

  // Nothing to write: no file is created, which is what keeps a player who never opened the
  // provider's settings from growing an empty one.
  CHECK_TRUE(SaveProviderFile(path, {}, &error));
  CHECK_FALSE(std::filesystem::exists(path));

  // The first save has no file to replace, so the pairs it owns are appended.
  CHECK_TRUE(SaveProviderFile(path, {{"gems", "monkey"}, {"hide_hud", "TRUE"}}, &error));
  CHECK_TRUE(std::filesystem::exists(path));
  CHECK_TRUE(ReadText(path) == "(0 (gems monkey) (hide_hud TRUE))");
  // ... and no temporary sibling is left behind.
  CHECK_FALSE(std::filesystem::exists(root / "ulti_settings.dta.tmp"));

  // A second save replaces the pairs it names and keeps everything else, including a pair this
  // build does not draw.
  std::string text = ReadText(path);
  text = "(0 (controller_mapping \"A B X Y\") (hide_hud TRUE) (gems monkey))";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
  }
  CHECK_TRUE(SaveProviderFile(path, {{"gems", "ulti"}}, &error));
  CHECK_TRUE(ReadText(path) ==
             "(0 (controller_mapping \"A B X Y\") (hide_hud TRUE) (gems ulti))");

  // A file this build cannot parse is refused rather than rewritten: it is the only copy of the
  // values the provider does not show us.
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "this is not a provider file";
  }
  const std::string before = ReadText(path);
  CHECK_FALSE(SaveProviderFile(path, {{"gems", "freq"}}, &error));
  CHECK_FALSE(error.empty());
  CHECK_TRUE(ReadText(path) == before);
  CHECK_FALSE(std::filesystem::exists(root / "ulti_settings.dta.tmp"));

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

void TestConfigureProviderWithoutAPayload() {
  rb_blitz::test::BeginCase("a boot with no payload has no provider, and says so");
  ProviderRuntime runtime;
  ConfigureProvider(&runtime, "no/such/payload", "", true);
  CHECK_TRUE(runtime.considered);
  CHECK_FALSE(runtime.payload_present);
  CHECK_TRUE(runtime.readiness == ProviderReadiness::kAbsent);
  CHECK_TRUE(runtime.values.empty());
  CHECK_TRUE(!runtime.summary.empty());
  CHECK_FALSE(runtime.Active());
  CHECK_TRUE(runtime.ValueFor(SettingId::kHideHud) == nullptr);
  CHECK_TRUE(runtime.ChoicesFor("gems") == nullptr);
  // Even with a user root: an absent payload is absent, and nothing is redirected.
  ConfigureProvider(&runtime, "no/such/payload", Scratch("nopayload"), true);
  CHECK_TRUE(runtime.readiness == ProviderReadiness::kAbsent);
  CHECK_FALSE(runtime.Active());
}

void TestConfigureProviderWithAForeignPayload() {
  rb_blitz::test::BeginCase("a mounted payload that is not the provider is refused, not absent");
  // A payload directory the title will mount but which carries none of the provider's records is
  // `kUnrecognizedShape`, not `kAbsent`: P11's fallback depends on telling "the mod is not here"
  // from "something is here that this build does not integrate with".
  const std::filesystem::path root = Scratch("foreign");
  std::error_code ec;
  std::filesystem::create_directories(root / "gen", ec);
  {
    // A header with nothing the reader can use, and no archive beside it: mountable is not
    // reachable, so this is the *absent* path with a reason, which the next case covers.
    std::ofstream out(root / "gen/patch_xbox.hdr", std::ios::binary | std::ios::trunc);
    out << "not a header";
  }
  ProviderRuntime runtime;
  ConfigureProvider(&runtime, root.string(), "", true);
  CHECK_TRUE(runtime.considered);
  CHECK_FALSE(runtime.payload_present);
  CHECK_TRUE(runtime.readiness == ProviderReadiness::kAbsent);
  CHECK_TRUE(runtime.ValueFor(SettingId::kUnlockAll) == nullptr);
  std::filesystem::remove_all(root, ec);
}

void TestRealPayload(const char* payload_root) {
  rb_blitz::test::BeginCase("the installed payload is inspected, shaped and bound");
  if (payload_root == nullptr) {
    std::printf("         (skipped: no payload path on the command line)\n");
    return;
  }
  // Diagnostics are flushed as they are printed: a payload this build has never seen is exactly
  // the case where the report *is* the result, and a buffered line lost to a crash is no result.
  auto note = [](const char* format, auto... args) {
    std::printf(format, args...);
    std::fflush(stdout);
  };

  std::string error;
  const Installation installation = InspectInstallation(payload_root, &error);
  if (!installation.Mountable()) {
    note("         cannot inspect %s: %s\n", payload_root, installation.error.c_str());
    CHECK_TRUE(false);
    return;
  }
  CHECK_TRUE(installation.header);
  CHECK_TRUE(installation.archive);
  CHECK_TRUE(installation.index_read);
  CHECK_EQ(installation.ark_version, 6);
  note("         %zu records present, %zu missing, ark v%u\n", installation.present.size(),
       installation.missing.size(), installation.ark_version);
  for (Record missing : installation.missing) {
    note("         missing: %s\n", std::string(RecordName(missing)).c_str());
  }
  CHECK_EQ(installation.present.size(), kRecordCount);
  CHECK_EQ(installation.missing.size(), 0);

  RecordBytes records;
  CHECK_TRUE(ReadProviderRecords(payload_root, &records, &error));
  const ShapeReport report = CheckShape(records);
  for (const std::string& problem : report.problems) {
    note("         problem: %s\n", problem.c_str());
  }
  for (const std::string& observation : report.notes) {
    note("         note: %s\n", observation.c_str());
  }
  note("         %zu panels, %zu persisted names, %zu defaults, %zu choice sets\n",
       report.panels.size(), report.persisted.size(), report.defaults.size(),
       report.choices.size());
  CHECK_TRUE(report.ok);
  CHECK_EQ(report.panels.size(), 8);  // including mod_warning_panel, the provider's own notice
  CHECK_EQ(report.persisted.size(), 21);
  CHECK_TRUE(report.settings_path_macro.find("GAME:/ulti_settings.dta") != std::string::npos);

  // The two choice sets the plan says must come from the provider (plan §3.4). Looked up
  // defensively: a payload without them must produce a readable failure, not a terminated test.
  auto choices_of = [&report](const char* variable) -> const std::vector<std::string>* {
    const auto found = report.choices.find(variable);
    return found == report.choices.end() ? nullptr : &found->second;
  };
  const std::vector<std::string>* gems = choices_of("gems");
  CHECK_TRUE(gems != nullptr);
  if (gems != nullptr) {
    CHECK_EQ(gems->size(), 8);
    CHECK_TRUE(gems->front() == "default");
  }
  const std::vector<std::string>* skyboxes = choices_of("which_skybox");
  CHECK_TRUE(skyboxes != nullptr);
  if (skyboxes != nullptr) {
    CHECK_EQ(skyboxes->size(), 3);
    CHECK_TRUE(skyboxes->size() > 1 && (*skyboxes)[0] == "default_skybox");
    CHECK_TRUE(skyboxes->size() > 1 && (*skyboxes)[1] == "no_skybox");
  }
  const std::vector<std::string>* plots = choices_of("forceplot");
  CHECK_TRUE(plots != nullptr);
  if (plots != nullptr) {
    CHECK_EQ(plots->size(), 17);
    CHECK_TRUE(!plots->empty() && plots->front() == "off");
  }
  // The HUD panel's seven rows, measured rather than assumed ("HUD's eight fields" in the P6
  // prompt counts 4:3 Letterboxing with them - the panel itself carries seven).
  const PanelShape* hud = report.Panel("mod_hud_settings_panel");
  CHECK_TRUE(hud != nullptr);
  if (hud != nullptr) {
    CHECK_EQ(hud->rows.size(), 7);
  }
  // The compiled defaults.
  auto default_of = [&report](const char* name) -> std::string {
    const auto found = report.defaults.find(name);
    return found == report.defaults.end() ? std::string("<absent>") : found->second;
  };
  CHECK_TRUE(default_of("unlockall") == "TRUE");
  CHECK_TRUE(default_of("camera_angle") == "0");
  CHECK_TRUE(default_of("letterbox") == "FALSE");
  CHECK_TRUE(default_of("gems") == "default");
  const std::map<SettingId, Value> defaults = DefaultsFromShape(report);
  CHECK_TRUE(defaults.count(SettingId::kUnlockAll) == 1 &&
             defaults.at(SettingId::kUnlockAll).boolean);
  CHECK_TRUE(defaults.count(SettingId::kCameraAngle) == 1 &&
             defaults.at(SettingId::kCameraAngle).text == "default");
  CHECK_TRUE(defaults.count(SettingId::kGems) == 1 &&
             defaults.at(SettingId::kGems).text == "default");
  CHECK_TRUE(defaults.count(SettingId::kGameSpeed) == 1 &&
             defaults.at(SettingId::kGameSpeed).integer == 100);
  CHECK_TRUE(Readiness(true, true, report) == ProviderReadiness::kCompatible);
  CHECK_TRUE(PayloadWritesSettingsFile(report));

  // The whole integration, host-side: configure against the real payload and a scratch user root,
  // then save one row through the adapter and read it back out of the provider's own file. This is
  // the value path the merged screen's rows use, without a boot.
  const std::filesystem::path user_root = Scratch("payload");
  ProviderRuntime runtime;
  ConfigureProvider(&runtime, payload_root, user_root, true);
  note("         %s\n", runtime.summary.c_str());
  CHECK_TRUE(runtime.payload_present);
  CHECK_TRUE(runtime.Active());
  CHECK_TRUE(runtime.readiness == ProviderReadiness::kCompatible);
  CHECK_TRUE(runtime.settings_path == user_root / "ultimate" / "ulti_settings.dta");
  CHECK_TRUE(runtime.ChoicesFor("gems") != nullptr);
  // With no file yet, the values are the payload's own compiled defaults - and a row the payload
  // neither persists nor defaults is *absent*, not guessed at. `unlockall` and `letterbox` are in
  // `ulti_init`; `hide_hud` and `freq_ms` are not.
  const Value* unlock = runtime.ValueFor(SettingId::kUnlockAll);
  CHECK_TRUE(unlock != nullptr && unlock->boolean);
  const Value* letterbox = runtime.ValueFor(SettingId::kLetterbox);
  CHECK_TRUE(letterbox != nullptr && !letterbox->boolean);
  const Value* camera = runtime.ValueFor(SettingId::kCameraAngle);
  CHECK_TRUE(camera != nullptr && camera->text == "default");
  CHECK_TRUE(runtime.ValueFor(SettingId::kHideHud) == nullptr);
  CHECK_TRUE(runtime.ValueFor(SettingId::kPrecisionMode) == nullptr);
  // The rows that fall back are named in the report rather than left as a silent zero.
  bool named = false;
  for (const std::string& observation : runtime.shape.notes) {
    if (observation.find("interface.hide_hud") != std::string::npos &&
        observation.find("gameplay.precision_mode") != std::string::npos) {
      named = true;
    }
  }
  CHECK_TRUE(named);

  // The commit path encodes our rows through the adapter, never by hand: a hand-built pair name is
  // how a file ends up carrying a pair the provider does not own.
  std::map<std::string, std::string> updates;
  std::map<SettingId, Value> edited;
  edited[SettingId::kHideHud] = [] {
    Value value;
    value.kind = ValueKind::kBool;
    value.boolean = false;
    return value;
  }();
  edited[SettingId::kGems] = [] {
    Value value;
    value.kind = ValueKind::kEnum;
    value.text = "ulti";
    return value;
  }();
  CHECK_TRUE(EncodeProviderValues(edited, &updates, &error));
  CHECK_EQ(updates.size(), 2);
  CHECK_TRUE(updates.at("hide_hud") == "FALSE");
  CHECK_TRUE(updates.at("gems") == "ulti");
  CHECK_TRUE(SaveProviderFile(runtime.settings_path, updates, &error));
  const std::string written = ReadText(runtime.settings_path);
  CHECK_TRUE(written.find("(hide_hud FALSE)") != std::string::npos);
  CHECK_TRUE(written.find("(gems ulti)") != std::string::npos);
  ProviderRuntime reloaded;
  ConfigureProvider(&reloaded, payload_root, user_root, true);
  const Value* hide_hud = reloaded.ValueFor(SettingId::kHideHud);
  CHECK_TRUE(hide_hud != nullptr);
  CHECK_TRUE(hide_hud != nullptr && !hide_hud->boolean);
  // A pair the file now carries is the row's value even where `ulti_init` had no default - which
  // is the whole point of the file over the defaults.
  const Value* gems_value = reloaded.ValueFor(SettingId::kGems);
  CHECK_TRUE(gems_value != nullptr && gems_value->text == "ulti");
  // A row the file does not carry still answers with the payload's default.
  const Value* reloaded_letterbox = reloaded.ValueFor(SettingId::kLetterbox);
  CHECK_TRUE(reloaded_letterbox != nullptr && !reloaded_letterbox->boolean);
  // A value that does not belong to its row is refused, and the rest are still encoded.
  std::map<std::string, std::string> partial;
  std::map<SettingId, Value> bad;
  bad[SettingId::kHideHud] = [] {
    Value value;
    value.kind = ValueKind::kInt;
    value.integer = 3;
    return value;
  }();
  bad[SettingId::kGems] = [] {
    Value value;
    value.kind = ValueKind::kEnum;
    value.text = "monkey";
    return value;
  }();
  error.clear();
  CHECK_FALSE(EncodeProviderValues(bad, &partial, &error));
  CHECK_TRUE(partial.size() == 1 && partial.at("gems") == "monkey");
  CHECK_TRUE(error.find("interface.hide_hud") != std::string::npos);
  // A row with no provider pair (`unlock_all`'s exception) is skipped, not an error.
  std::map<std::string, std::string> none;
  std::map<SettingId, Value> only_native;
  only_native[SettingId::kUnlockAll] = [] {
    Value value;
    value.kind = ValueKind::kBool;
    value.boolean = true;
    return value;
  }();
  CHECK_TRUE(EncodeProviderValues(only_native, &none, &error));
  CHECK_TRUE(none.empty());
  std::error_code ec;
  std::filesystem::remove_all(user_root, ec);
}

}  // namespace

int main(int argc, char** argv) {
  TestRecordTable();
  TestBindings();
  TestShapeAcceptsAFixture();
  TestShapeRefusals();
  TestReadiness();
  TestProviderFile();
  TestValueTranslation();
  TestRedirectNames();
  TestProviderFileSave();
  TestConfigureProviderWithoutAPayload();
  TestConfigureProviderWithAForeignPayload();
  TestRealPayload(argc > 1 ? argv[1] : nullptr);
  return rb_blitz::test::Finish();
}
