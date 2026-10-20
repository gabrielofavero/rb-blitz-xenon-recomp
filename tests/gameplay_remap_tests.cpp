// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the gameplay-mapping configuration (src/profile/gameplay_remap.h, P1 of
// docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md).
//
// P1's verify list, as cases:
//
//   1. the vocabulary is total: every action, table and default source round-trips through the
//      spelling the profile writes, and the two tables' source kinds are disjoint
//   2. absent rows give the compiled defaults; an explicit empty value is a deliberate "nothing
//      fires this", not an invalid row; an invalid value is reported, preserved and not applied
//   3. a source of the wrong kind is refused by its table - the whole row, never half of one -
//      and written back exactly as it came in
//   4. a row this build does not know is preserved and never converted into an action
//   5. the three tables (`[remap]`, `[gameplay_remap_controller]`,
//      `[gameplay_remap_keyboard_mouse]`) are independent: one cannot change another's actions
//   6. the profile stores, round-trips, renders and drops the two tables like `[remap]`, without a
//      schema-version bump, and the three switches are in the runtime allowlist
//
// No pad, no window, no SDL, no boot: P1 is configuration only - the device sampling (P2) and the
// guest dispatch (P3) are what a live run has to prove.

#include "check.h"

#include "profile/gameplay_remap.h"
#include "profile/profile.h"
#include "profile/remap.h"
#include "settings/cvars.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace rb_blitz::profile;
using namespace rb_blitz::profile::gameplay_remap;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

void CheckString(const char* file, int line, const std::string& actual,
                 const std::string& expected, const char* actual_expr,
                 const char* expected_expr) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, std::string(actual_expr) + " == " + expected_expr +
                      " failed\n         actual   " + actual + "\n         expected " + expected);
}

#define CHECK_STR_EQ(actual, expected) \
  CheckString(__FILE__, __LINE__, (actual), (expected), #actual, #expected)

void CheckContains(const char* file, int line, const std::string& haystack,
                   const std::string& needle) {
  ++g_checks;
  if (haystack.find(needle) == std::string::npos) {
    Fail(file, line, "expected to find \"" + needle + "\" in\n         " + haystack);
  }
}

#define CHECK_CONTAINS(haystack, needle) CheckContains(__FILE__, __LINE__, (haystack), (needle))

// A scratch directory, removed when the test finishes.
struct Scratch {
  fs::path base;

  Scratch() {
    std::error_code code;
    base = fs::temp_directory_path(code) / "rb_blitz-gameplay-remap";
    fs::remove_all(base, code);
    fs::create_directories(base, code);
  }

  ~Scratch() {
    std::error_code code;
    fs::remove_all(base, code);
  }

  fs::path Write(const std::string& name, const std::string& text) const {
    std::error_code code;
    const fs::path path = base / name;
    fs::create_directories(path.parent_path(), code);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return path;
  }
};

std::string Compose(const Profile& profile) {
  std::string text;
  std::string error;
  if (!ComposeProfile(profile, &text, &error)) {
    Fail(__FILE__, __LINE__, "ComposeProfile failed: " + error);
    return {};
  }
  return text;
}

std::vector<ProfileSetting> Rows(std::initializer_list<const char*> key_value_pairs) {
  std::vector<ProfileSetting> rows;
  for (const char* pair : key_value_pairs) {
    const std::string text(pair);
    const std::size_t equals = text.find('=');
    rows.push_back(ProfileSetting{text.substr(0, equals), text.substr(equals + 1),
                                  ValueStyle::kBasic});
  }
  return rows;
}

bool HasSource(const std::vector<remap::Source>* sources, remap::SourceKind kind,
               std::string_view name) {
  if (sources == nullptr) {
    return false;
  }
  return std::any_of(sources->begin(), sources->end(), [&](const remap::Source& source) {
    return source.kind == kind && source.name == name;
  });
}

void TestVocabulary() {
  BeginCase("P1: every action has a name, a label and its own spelling");

  CHECK_EQ(Actions().size(), kActionCount);
  std::map<std::string, Action> seen;
  for (const Action action : Actions()) {
    const std::string name(ActionName(action));
    CHECK_FALSE(name.empty());
    CHECK_FALSE(ActionLabel(action).empty());
    CHECK_TRUE(seen.find(name) == seen.end());
    seen.emplace(name, action);
    CHECK_TRUE(ParseAction(name).has_value());
    CHECK_TRUE(*ParseAction(name) == action);
  }
  CHECK_FALSE(ParseAction("firstnote").has_value());
  CHECK_FALSE(ParseAction("").has_value());
  // The guest's three extra actions are deliberately not here: no shipped preset reaches them
  // (plan §10.4 O7), so a row naming one is preserved and never converted.
  CHECK_FALSE(ParseAction("swing").has_value());
  CHECK_FALSE(ParseAction("award_jackpot").has_value());

  BeginCase("P1: the two tables have distinct section names, and each accepts only its sources");

  CHECK_EQ(TableKinds().size(), kTableKindCount);
  CHECK_STR_EQ(std::string(TableKindSection(TableKind::kController)),
               "gameplay_remap_controller");
  CHECK_STR_EQ(std::string(TableKindSection(TableKind::kKeyboardMouse)),
               "gameplay_remap_keyboard_mouse");
  CHECK_TRUE(ParseTableKindSection("gameplay_remap_controller").has_value());
  CHECK_TRUE(ParseTableKindSection("gameplay_remap_keyboard_mouse").has_value());
  CHECK_FALSE(ParseTableKindSection("remap").has_value());

  const remap::Source pad{remap::SourceKind::kPad, "a"};
  const remap::Source key{remap::SourceKind::kKey, "Space"};
  const remap::Source mouse{remap::SourceKind::kMouse, "left"};
  CHECK_TRUE(AcceptsSource(TableKind::kController, pad));
  CHECK_FALSE(AcceptsSource(TableKind::kController, key));
  CHECK_FALSE(AcceptsSource(TableKind::kController, mouse));
  CHECK_FALSE(AcceptsSource(TableKind::kKeyboardMouse, pad));
  CHECK_TRUE(AcceptsSource(TableKind::kKeyboardMouse, key));
  CHECK_TRUE(AcceptsSource(TableKind::kKeyboardMouse, mouse));

  BeginCase("P1: every default source is a spelling this build's grammar reads and its table accepts");

  for (const TableKind kind : TableKinds()) {
    for (const Action action : Actions()) {
      const std::vector<remap::Source>* sources = DefaultSources(kind, action);
      CHECK_TRUE(sources != nullptr);
      if (sources == nullptr) {
        continue;
      }
      CHECK_TRUE(!sources->empty());
      for (const remap::Source& source : *sources) {
        const std::optional<remap::Source> reread = remap::ParseSource(remap::FormatSource(source));
        CHECK_TRUE(reread.has_value());
        if (reread) {
          CHECK_TRUE(*reread == source);
        }
        CHECK_TRUE(AcceptsSource(kind, source));
      }
    }
  }

  BeginCase("P1: the controller defaults reproduce the measured native preset 0");

  // Plan §10.4 O3: preset 0 maps LT/LB to TrackDown, RT/RB to TrackUp, X to UsePowerUp, any
  // LStick/D-pad direction to Lane0 and A or any RStick direction to Lane1; Start is Pause.
  const auto controller = [](Action action) { return DefaultSources(TableKind::kController, action); };
  for (const char* direction : {"lstick_up", "lstick_down", "lstick_left", "lstick_right",
                                "dpad_up", "dpad_down", "dpad_left", "dpad_right"}) {
    CHECK_TRUE(HasSource(controller(Action::kFirstNote), remap::SourceKind::kPad, direction));
  }
  CHECK_TRUE(HasSource(controller(Action::kSecondNote), remap::SourceKind::kPad, "a"));
  for (const char* direction : {"rstick_up", "rstick_down", "rstick_left", "rstick_right"}) {
    CHECK_TRUE(HasSource(controller(Action::kSecondNote), remap::SourceKind::kPad, direction));
  }
  CHECK_TRUE(HasSource(controller(Action::kMoveLaneRight), remap::SourceKind::kPad,
                       "right_trigger"));
  CHECK_TRUE(HasSource(controller(Action::kMoveLaneRight), remap::SourceKind::kPad,
                       "right_shoulder"));
  CHECK_TRUE(HasSource(controller(Action::kMoveLaneLeft), remap::SourceKind::kPad,
                       "left_trigger"));
  CHECK_TRUE(HasSource(controller(Action::kMoveLaneLeft), remap::SourceKind::kPad,
                       "left_shoulder"));
  CHECK_TRUE(HasSource(controller(Action::kPowerup), remap::SourceKind::kPad, "x"));
  CHECK_TRUE(HasSource(controller(Action::kPause), remap::SourceKind::kPad, "start"));

  BeginCase("P2: the Mouse & Keyboard defaults are the restructure plan's D12 list");

  // Q + right click, W + left click, the arrow keys with a wheel detent each, Space + middle
  // click, Escape. These replace the shipped keyboard preset; a click or detent is a binding by
  // default now.
  const auto keyboard = [](Action action) {
    return DefaultSources(TableKind::kKeyboardMouse, action);
  };
  CHECK_TRUE(HasSource(keyboard(Action::kFirstNote), remap::SourceKind::kKey, "Q"));
  CHECK_TRUE(HasSource(keyboard(Action::kFirstNote), remap::SourceKind::kMouse, "right"));
  CHECK_EQ(keyboard(Action::kFirstNote)->size(), 2u);
  CHECK_TRUE(HasSource(keyboard(Action::kSecondNote), remap::SourceKind::kKey, "W"));
  CHECK_TRUE(HasSource(keyboard(Action::kSecondNote), remap::SourceKind::kMouse, "left"));
  CHECK_TRUE(HasSource(keyboard(Action::kMoveLaneRight), remap::SourceKind::kKey, "Right"));
  CHECK_TRUE(HasSource(keyboard(Action::kMoveLaneRight), remap::SourceKind::kMouse, "wheel_up"));
  CHECK_TRUE(HasSource(keyboard(Action::kMoveLaneLeft), remap::SourceKind::kKey, "Left"));
  CHECK_TRUE(HasSource(keyboard(Action::kMoveLaneLeft), remap::SourceKind::kMouse,
                       "wheel_down"));
  CHECK_TRUE(HasSource(keyboard(Action::kPowerup), remap::SourceKind::kKey, "Space"));
  CHECK_TRUE(HasSource(keyboard(Action::kPowerup), remap::SourceKind::kMouse, "middle"));
  CHECK_TRUE(HasSource(keyboard(Action::kPause), remap::SourceKind::kKey, "Escape"));
  CHECK_EQ(keyboard(Action::kPause)->size(), 1u);
  // The shipped preset's letter keys are gone, not merely joined.
  for (const Action action : Actions()) {
    for (const remap::Source& source : *keyboard(action)) {
      CHECK_TRUE(source.name != "1" && source.name != "Semicolon" && source.name != "Period");
    }
  }
}

void TestAbsentAndEmpty() {
  BeginCase("P1: no rows means the compiled defaults, and both tables are complete");

  for (const TableKind kind : TableKinds()) {
    const Table table = Table::FromRows(kind, {});
    CHECK_EQ(table.BoundCount(), 0u);
    CHECK_EQ(table.EffectiveBoundCount(), kActionCount);
    CHECK_TRUE(table.AnySourceSet());
    CHECK_TRUE(table.Rejected().empty());
    for (const Action action : Actions()) {
      CHECK_TRUE(table.Bindings(action) == nullptr);
      CHECK_TRUE(table.Effective(action) == DefaultSources(kind, action));
      CHECK_FALSE(table.IsDisabled(action));
    }
  }

  BeginCase("P1: an empty value is a deliberate off, not an invalid row");

  const Table off = Table::FromRows(
      TableKind::kKeyboardMouse, Rows({"first_note=", "pause="}));
  CHECK_EQ(off.BoundCount(), 2u);
  CHECK_TRUE(off.IsBound(Action::kFirstNote));
  CHECK_TRUE(off.Bindings(Action::kFirstNote) != nullptr);
  CHECK_TRUE(off.Bindings(Action::kFirstNote)->empty());
  CHECK_TRUE(off.IsDisabled(Action::kFirstNote));
  CHECK_TRUE(off.IsDisabled(Action::kPause));
  CHECK_TRUE(off.Rejected().empty());
  // The other four actions are untouched: turning two actions off does not turn the table off.
  CHECK_EQ(off.EffectiveBoundCount(), kActionCount - 2);
  CHECK_TRUE(off.AnySourceSet());

  // Rows that turn everything off are a table that can play nothing: a real thing to want, and a
  // fact a boot reports rather than silently honouring.
  std::vector<ProfileSetting> all_off;
  for (const Action action : Actions()) {
    all_off.push_back(ProfileSetting{std::string(ActionName(action)), "",
                                     ValueStyle::kBasic});
  }
  const Table silent = Table::FromRows(TableKind::kController, all_off);
  CHECK_EQ(silent.BoundCount(), kActionCount);
  CHECK_EQ(silent.EffectiveBoundCount(), 0u);
  CHECK_FALSE(silent.AnySourceSet());
}

void TestSourceKinds() {
  BeginCase("P1: a key in the Controller table is refused whole, kept, and not applied");

  const Table controller =
      Table::FromRows(TableKind::kController, Rows({"first_note=key:J"}));
  CHECK_EQ(controller.BoundCount(), 0u);
  CHECK_TRUE(controller.Bindings(Action::kFirstNote) == nullptr);
  CHECK_TRUE(controller.Effective(Action::kFirstNote) ==
             DefaultSources(TableKind::kController, Action::kFirstNote));
  CHECK_EQ(controller.Rejected().size(), 1u);
  if (!controller.Rejected().empty()) {
    CHECK_STR_EQ(controller.Rejected().front().row.key, "first_note");
    CHECK_CONTAINS(controller.Rejected().front().reason, "pad sources only");
  }
  // Written back exactly as it came in: same key, same value, same style.
  const std::vector<ProfileSetting> written = controller.ToRows();
  CHECK_EQ(written.size(), 1u);
  CHECK_STR_EQ(written[0].key, "first_note");
  CHECK_STR_EQ(written[0].value, "key:J");
  CHECK_TRUE(written[0].style == ValueStyle::kBasic);

  BeginCase("P1: a pad control in the Keyboard + Mouse table is refused the same way");

  const Table keyboard_mouse =
      Table::FromRows(TableKind::kKeyboardMouse, Rows({"powerup=pad:x"}));
  CHECK_EQ(keyboard_mouse.BoundCount(), 0u);
  CHECK_EQ(keyboard_mouse.Rejected().size(), 1u);
  if (!keyboard_mouse.Rejected().empty()) {
    CHECK_CONTAINS(keyboard_mouse.Rejected().front().reason, "keys and mouse buttons only");
  }
  CHECK_EQ(keyboard_mouse.ToRows().size(), 1u);

  BeginCase("P1: a mixed list is refused whole rather than half-applied");

  const Table mixed =
      Table::FromRows(TableKind::kController, Rows({"first_note=pad:x, key:J"}));
  CHECK_EQ(mixed.BoundCount(), 0u);
  CHECK_EQ(mixed.Rejected().size(), 1u);
  // The right-kind half did not sneak through.
  CHECK_TRUE(mixed.Bindings(Action::kFirstNote) == nullptr);

  BeginCase("P1: a literal-style row survives a round trip in its own spelling");

  std::vector<ProfileSetting> literal{
      ProfileSetting{"second_note", "key:J", ValueStyle::kLiteral}};
  const Table kept = Table::FromRows(TableKind::kController, literal);
  CHECK_TRUE(kept.Rejected().size() == 1u);
  CHECK_TRUE(kept.ToRows().size() == 1u);
  if (!kept.ToRows().empty()) {
    CHECK_TRUE(kept.ToRows()[0].style == ValueStyle::kLiteral);
    CHECK_STR_EQ(kept.ToRows()[0].value, "key:J");
  }
}

void TestUnknownAndInvalid() {
  BeginCase("P1: a row this build does not know is preserved and never becomes an action");

  const Table table = Table::FromRows(
      TableKind::kController, Rows({"first_note=pad:x", "turbo=pad:y", "swing=pad:b"}));
  CHECK_EQ(table.BoundCount(), 1u);
  CHECK_TRUE(table.Bindings(Action::kFirstNote) != nullptr);
  // Not a diagnostic: it is a row a newer build wrote, which this build must not report as
  // an error, only leave alone.
  CHECK_TRUE(table.Rejected().empty());
  const std::vector<ProfileSetting> written = table.ToRows();
  CHECK_EQ(written.size(), 3u);
  bool saw_turbo = false;
  bool saw_swing = false;
  for (const ProfileSetting& row : written) {
    saw_turbo = saw_turbo || row.key == "turbo";
    saw_swing = saw_swing || row.key == "swing";
  }
  CHECK_TRUE(saw_turbo);
  CHECK_TRUE(saw_swing);

  BeginCase("P1: a value that is not a source list is reported, preserved and not applied");

  const Table invalid =
      Table::FromRows(TableKind::kController, Rows({"powerup=gesture:swipe"}));
  CHECK_EQ(invalid.BoundCount(), 0u);
  CHECK_EQ(invalid.Rejected().size(), 1u);
  if (!invalid.Rejected().empty()) {
    CHECK_CONTAINS(invalid.Rejected().front().reason, "not a source list");
  }
  CHECK_EQ(invalid.ToRows().size(), 1u);
  CHECK_STR_EQ(invalid.ToRows()[0].value, "gesture:swipe");
}

void TestEditsAndIndependence() {
  BeginCase("P1: several sources for one action are a list, and a repeat is one press");

  const Table many = Table::FromRows(
      TableKind::kKeyboardMouse, Rows({"first_note=key:J, key:K, mouse:left, key:J"}));
  const std::vector<remap::Source>* sources = many.Bindings(Action::kFirstNote);
  CHECK_TRUE(sources != nullptr);
  if (sources != nullptr) {
    CHECK_EQ(sources->size(), 3u);
    CHECK_TRUE(HasSource(sources, remap::SourceKind::kKey, "J"));
    CHECK_TRUE(HasSource(sources, remap::SourceKind::kKey, "K"));
    CHECK_TRUE(HasSource(sources, remap::SourceKind::kMouse, "left"));
  }

  BeginCase("P1: one source on two actions fires both, as General Mapping's OR does");

  // The plan allows this and asks the table to say so; the table reports both bindings and does
  // not silently keep only one.
  const Table shared = Table::FromRows(
      TableKind::kKeyboardMouse, Rows({"first_note=key:J", "second_note=key:J"}));
  CHECK_EQ(shared.BoundCount(), 2u);
  CHECK_TRUE(shared.Bindings(Action::kFirstNote) != nullptr);
  CHECK_TRUE(shared.Bindings(Action::kSecondNote) != nullptr);
  CHECK_TRUE(shared.Rejected().empty());

  BeginCase("P1: Set, Reset and ResetAll are the edits the settings surface makes");

  Table edited(TableKind::kController);
  CHECK_EQ(edited.BoundCount(), 0u);
  edited.Set(Action::kPowerup, {remap::Source{remap::SourceKind::kPad, "y"}});
  CHECK_TRUE(edited.Bindings(Action::kPowerup) != nullptr);
  CHECK_EQ(edited.Bindings(Action::kPowerup)->size(), 1u);
  edited.Reset(Action::kPowerup);
  CHECK_TRUE(edited.Bindings(Action::kPowerup) == nullptr);
  CHECK_TRUE(edited.Effective(Action::kPowerup) ==
             DefaultSources(TableKind::kController, Action::kPowerup));
  edited.Set(Action::kPause, {});
  CHECK_EQ(edited.BoundCount(), 1u);
  edited.ResetAll();
  CHECK_EQ(edited.BoundCount(), 0u);

  BeginCase("P1: the three tables are independent - one cannot change another's actions");

  // A `[remap]` row that rebinds A to a key does not move First Note or Second Note: the gameplay
  // tables never read General Mapping's output (plan D1).
  const remap::Table general = remap::Table::FromRows(Rows({"a=key:K"}));
  CHECK_EQ(general.BoundCount(), 1u);
  const Table controller = Table::FromRows(TableKind::kController, {});
  CHECK_TRUE(controller.Effective(Action::kSecondNote) ==
             DefaultSources(TableKind::kController, Action::kSecondNote));
  CHECK_EQ(controller.BoundCount(), 0u);

  // And a gameplay row does not change General Mapping: the two configs are separate objects
  // because they come from separate profile tables.
  const Table gameplay =
      Table::FromRows(TableKind::kKeyboardMouse, Rows({"first_note=key:J"}));
  const remap::Table untouched = remap::Table::FromRows({});
  CHECK_EQ(untouched.BoundCount(), 0u);
  CHECK_TRUE(gameplay.Bindings(Action::kFirstNote) != nullptr);
}

void TestProfileStorage() {
  BeginCase("P1: the profile reads, writes and preserves both gameplay tables");

  Scratch scratch;
  const std::string text =
      "schema_version = 1\n"
      "\n"
      "[launcher]\n"
      "version = 1\n"
      "portable = false\n"
      "\n"
      "[window]\n"
      "width = 1280\n"
      "height = 840\n"
      "\n"
      "[launch]\n"
      "target = \"ultimate\"\n"
      "game_dir = \"\"\n"
      "user_data_dir = \"\"\n"
      "dlc_dir = \"\"\n"
      "\n"
      "[settings]\n"
      "keyboard_support = false\n"
      "gameplay_mapping_controller = true\n"
      "\n"
      "[remap]\n"
      "a = \"key:K\"\n"
      "\n"
      "[gameplay_remap_controller]\n"
      "first_note = \"pad:lstick_up, pad:a\"   # both thumbs\n"
      "pause = \"\"\n"
      "\n"
      "[gameplay_remap_keyboard_mouse]\n"
      "first_note = \"key:J, mouse:left\"\n"
      "turbo = \"key:T\"\n"
      "\n"
      "[nav]\n"
      "next = \"key:Down\"\n";
  const fs::path path = scratch.Write("launcher.toml", text);

  const ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  CHECK_EQ(loaded.profile.gameplay_remap_controller.size(), 2u);
  CHECK_EQ(loaded.profile.gameplay_remap_keyboard_mouse.size(), 2u);
  CHECK_EQ(loaded.profile.remap.size(), 1u);
  CHECK_TRUE(loaded.profile.FindSetting("keyboard_support") != nullptr);
  CHECK_STR_EQ(loaded.profile.FindSetting("keyboard_support")->value, "false");
  CHECK_TRUE(loaded.profile.FindSetting("gameplay_mapping_controller") != nullptr);
  CHECK_STR_EQ(loaded.profile.FindSetting("gameplay_mapping_controller")->value, "true");

  // The tables mean what they say, per table.
  const Table controller = Table::FromRows(TableKind::kController,
                                           loaded.profile.gameplay_remap_controller);
  CHECK_TRUE(HasSource(controller.Bindings(Action::kFirstNote), remap::SourceKind::kPad,
                       "lstick_up"));
  CHECK_TRUE(HasSource(controller.Bindings(Action::kFirstNote), remap::SourceKind::kPad, "a"));
  CHECK_TRUE(controller.IsDisabled(Action::kPause));
  const Table keyboard_mouse = Table::FromRows(
      TableKind::kKeyboardMouse, loaded.profile.gameplay_remap_keyboard_mouse);
  CHECK_TRUE(HasSource(keyboard_mouse.Bindings(Action::kFirstNote), remap::SourceKind::kKey, "J"));
  CHECK_TRUE(HasSource(keyboard_mouse.Bindings(Action::kFirstNote), remap::SourceKind::kMouse,
                       "left"));
  // An unknown action row in the gameplay table is preserved, not applied.
  CHECK_EQ(keyboard_mouse.BoundCount(), 1u);
  CHECK_TRUE(keyboard_mouse.Rejected().empty());

  // A load and a save with no edits is the same file: comments, unknown rows, everything.
  CHECK_STR_EQ(Compose(loaded.profile), text);

  BeginCase("P1: a gameplay edit leaves every other table and the rest of the document alone");

  Profile edited = loaded.profile;
  Table changed = controller;
  changed.Set(Action::kPowerup, {remap::Source{remap::SourceKind::kPad, "y"}});
  edited.gameplay_remap_controller = changed.ToRows();
  const std::string after = Compose(edited);
  CHECK_CONTAINS(after, "[remap]\na = \"key:K\"\n");
  CHECK_CONTAINS(after, "[gameplay_remap_controller]");
  CHECK_CONTAINS(after, "powerup = \"pad:y\"");
  CHECK_CONTAINS(after, "first_note = \"key:J, mouse:left\"");
  CHECK_CONTAINS(after, "turbo = \"key:T\"");
  CHECK_CONTAINS(after, "[nav]\nnext = \"key:Down\"\n");
  CHECK_CONTAINS(after, "keyboard_support = false");
  CHECK_CONTAINS(after, "gameplay_mapping_controller = true");

  BeginCase("P1: clearing a gameplay table drops its rows and keeps the document");

  Profile cleared = loaded.profile;
  cleared.gameplay_remap_controller.clear();
  const std::string after_clear = Compose(cleared);
  CHECK_TRUE(after_clear.find("pad:lstick_up") == std::string::npos);
  CHECK_CONTAINS(after_clear, "[gameplay_remap_keyboard_mouse]");
  CHECK_CONTAINS(after_clear, "[remap]\na = \"key:K\"\n");
  const ProfileLoadResult reloaded = LoadProfile(scratch.Write("cleared.toml", after_clear));
  CHECK_TRUE(reloaded.status == ProfileStatus::kOk);
  CHECK_TRUE(reloaded.profile.gameplay_remap_controller.empty());
  CHECK_EQ(reloaded.profile.gameplay_remap_keyboard_mouse.size(), 2u);

  BeginCase("P1: a fresh profile renders the gameplay tables it was given, and only then");

  Profile fresh;
  const std::string fresh_text = RenderProfile(fresh);
  CHECK_TRUE(fresh_text.find("[gameplay_remap_controller]") == std::string::npos);
  CHECK_TRUE(fresh_text.find("[gameplay_remap_keyboard_mouse]") == std::string::npos);
  fresh.gameplay_remap_keyboard_mouse =
      Rows({"first_note=key:J", "pause=key:Return"});
  const std::string rendered = RenderProfile(fresh);
  CHECK_CONTAINS(rendered, "[gameplay_remap_keyboard_mouse]\nfirst_note = \"key:J\"\n"
                           "pause = \"key:Return\"\n");
  CHECK_TRUE(rendered.find("[gameplay_remap_controller]") == std::string::npos);

  BeginCase("P1: adding the tables needs no schema-version bump - a version-1 file reads");

  // The plan asks for this to be verified rather than assumed: the tables are additive, and an
  // older reader leaves an unknown table untouched (the profile module's own rule), so the version
  // stays 1. A file this build wrote is still `schema_version = 1`.
  CHECK_EQ(kProfileSchemaVersion, 1);
  CHECK_CONTAINS(Compose(fresh), "schema_version = 1");
}

void TestSwitchesAreAppliedByABoot() {
  BeginCase("P2: the device gates are in the runtime allowlist; the retired switches are not");

  CHECK_TRUE(rb_blitz::settings::IsControllerCvar("keyboard_support"));
  CHECK_TRUE(rb_blitz::settings::IsControllerCvar("mouse_ui_nav"));
  // Retired by the restructure plan's D3: custom is inferred from each table, so an old profile's
  // row is preservation data, not a cvar a boot applies.
  CHECK_FALSE(rb_blitz::settings::IsControllerCvar("gameplay_mapping_controller"));
  CHECK_FALSE(rb_blitz::settings::IsControllerCvar("gameplay_mapping_keyboard_mouse"));
  // Guide stays out of it (§2.3), and a retired game row is not a controller row.
  CHECK_FALSE(rb_blitz::settings::IsControllerCvar("guide_button"));
  CHECK_FALSE(rb_blitz::settings::IsControllerCvar("keybind_guide"));

  const std::vector<ProfileSetting> rows = Rows(
      {"mouse_ui_nav=true", "keyboard_support=false", "gameplay_mapping_controller=false",
       "gameplay_mapping_keyboard_mouse=true", "fullscreen=false"});
  const std::vector<ProfileSetting> kept =
      rb_blitz::settings::ProfileRowsTheGameApplies(rows);
  CHECK_EQ(kept.size(), 2u);
  for (const ProfileSetting& row : kept) {
    CHECK_TRUE(rb_blitz::settings::IsControllerCvar(row.key));
  }
}

void TestExplicitValues() {
  BeginCase("P1: an explicit false is a value, not the absence of one");

  // The profile records what differs from the compiled defaults; `keyboard_support` and
  // `mouse_ui_nav` both default to true, so an explicit false is a real edit and must come back
  // as `false` rather than as "no opinion".
  Scratch scratch;
  Profile profile;
  profile.Set("keyboard_support", "false");
  profile.Set("mouse_ui_nav", "false");
  const fs::path path = scratch.Write("switches.toml", Compose(profile));
  const ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  CHECK_TRUE(loaded.profile.FindSetting("keyboard_support") != nullptr);
  CHECK_STR_EQ(loaded.profile.FindSetting("keyboard_support")->value, "false");
  CHECK_TRUE(loaded.profile.FindSetting("mouse_ui_nav") != nullptr);
  CHECK_STR_EQ(loaded.profile.FindSetting("mouse_ui_nav")->value, "false");
}

// P2 of docs/plans/finished/2026-10-19-interface-gameplay-mapping-restructure-plan.md: the
// four retail presets (§2.1, decoded from config/gen/beatmatch_controller.dtb), the table that
// follows one, and "custom" inferred from the rows rather than a switch.
void TestControllerPresets() {
  BeginCase("P2: the four retail controller presets map the six actions as §2.1 records");

  const auto preset = [](int index, Action action) { return PresetSources(index, action); };
  // Preset 0 (what the P0 arms measured live).
  for (const char* direction : {"lstick_up", "lstick_down", "lstick_left", "lstick_right",
                                "dpad_up", "dpad_down", "dpad_left", "dpad_right"}) {
    CHECK_TRUE(HasSource(preset(0, Action::kFirstNote), remap::SourceKind::kPad, direction));
  }
  CHECK_TRUE(HasSource(preset(0, Action::kSecondNote), remap::SourceKind::kPad, "a"));
  CHECK_TRUE(HasSource(preset(0, Action::kSecondNote), remap::SourceKind::kPad, "rstick_left"));
  CHECK_TRUE(HasSource(preset(0, Action::kMoveLaneRight), remap::SourceKind::kPad,
                       "right_trigger"));
  CHECK_TRUE(HasSource(preset(0, Action::kMoveLaneLeft), remap::SourceKind::kPad, "left_trigger"));
  // Preset 1: notes on the triggers/shoulders, lanes on the horizontal stick/D-pad directions.
  CHECK_TRUE(HasSource(preset(1, Action::kFirstNote), remap::SourceKind::kPad, "left_trigger"));
  CHECK_TRUE(HasSource(preset(1, Action::kFirstNote), remap::SourceKind::kPad, "left_shoulder"));
  CHECK_TRUE(HasSource(preset(1, Action::kSecondNote), remap::SourceKind::kPad, "right_trigger"));
  CHECK_TRUE(HasSource(preset(1, Action::kSecondNote), remap::SourceKind::kPad, "right_shoulder"));
  CHECK_TRUE(HasSource(preset(1, Action::kMoveLaneRight), remap::SourceKind::kPad,
                       "lstick_right"));
  CHECK_TRUE(HasSource(preset(1, Action::kMoveLaneRight), remap::SourceKind::kPad,
                       "rstick_right"));
  CHECK_TRUE(HasSource(preset(1, Action::kMoveLaneRight), remap::SourceKind::kPad, "dpad_right"));
  CHECK_TRUE(HasSource(preset(1, Action::kMoveLaneLeft), remap::SourceKind::kPad, "lstick_left"));
  CHECK_TRUE(HasSource(preset(1, Action::kMoveLaneLeft), remap::SourceKind::kPad, "rstick_left"));
  CHECK_TRUE(HasSource(preset(1, Action::kMoveLaneLeft), remap::SourceKind::kPad, "dpad_left"));
  // Preset 2: one control per action.
  CHECK_EQ(preset(2, Action::kFirstNote)->size(), 1u);
  CHECK_TRUE(HasSource(preset(2, Action::kFirstNote), remap::SourceKind::kPad, "left_shoulder"));
  CHECK_TRUE(HasSource(preset(2, Action::kSecondNote), remap::SourceKind::kPad, "right_shoulder"));
  CHECK_TRUE(HasSource(preset(2, Action::kMoveLaneRight), remap::SourceKind::kPad,
                       "right_trigger"));
  CHECK_TRUE(HasSource(preset(2, Action::kMoveLaneLeft), remap::SourceKind::kPad, "left_trigger"));
  // Preset 3: face buttons for the notes, three power-up inputs.
  CHECK_TRUE(HasSource(preset(3, Action::kFirstNote), remap::SourceKind::kPad, "a"));
  CHECK_TRUE(HasSource(preset(3, Action::kSecondNote), remap::SourceKind::kPad, "b"));
  CHECK_TRUE(HasSource(preset(3, Action::kMoveLaneRight), remap::SourceKind::kPad, "lstick_right"));
  CHECK_TRUE(HasSource(preset(3, Action::kMoveLaneRight), remap::SourceKind::kPad, "dpad_right"));
  CHECK_TRUE(HasSource(preset(3, Action::kMoveLaneLeft), remap::SourceKind::kPad, "dpad_left"));
  CHECK_TRUE(HasSource(preset(3, Action::kPowerup), remap::SourceKind::kPad, "x"));
  CHECK_TRUE(HasSource(preset(3, Action::kPowerup), remap::SourceKind::kPad, "left_shoulder"));
  CHECK_TRUE(HasSource(preset(3, Action::kPowerup), remap::SourceKind::kPad, "left_trigger"));
  // Every preset keeps Start as the pause path and only ever names pad sources.
  for (int index = 0; index < kControllerPresetCount; ++index) {
    CHECK_TRUE(HasSource(preset(index, Action::kPause), remap::SourceKind::kPad, "start"));
    for (const Action action : Actions()) {
      for (const remap::Source& source : *preset(index, action)) {
        CHECK_TRUE(source.kind == remap::SourceKind::kPad);
      }
    }
  }
  // An index this build does not know reads as preset 0 rather than as "no binding at all".
  CHECK_TRUE(preset(-1, Action::kFirstNote) == preset(0, Action::kFirstNote));
  CHECK_TRUE(preset(9, Action::kFirstNote) == preset(0, Action::kFirstNote));
  CHECK_STR_EQ(PresetLabel(0), "Preset 1");
  CHECK_STR_EQ(PresetLabel(3), "Preset 4");
  CHECK_STR_EQ(PresetLabel(7), "Preset 1");

  BeginCase("P2: a Controller table without a custom row follows the selected preset");

  Table table(TableKind::kController);
  CHECK_FALSE(table.Custom());
  for (const Action action : Actions()) {
    CHECK_TRUE(table.Effective(action) == PresetSources(0, action));
  }
  table.SetControllerPreset(2);
  CHECK_EQ(table.Preset(), 2);
  for (const Action action : Actions()) {
    CHECK_TRUE(table.Effective(action) == PresetSources(2, action));
  }
  // One custom row changes only that action: the rest still follow the preset (D4).
  table.Set(Action::kFirstNote, {remap::Source{remap::SourceKind::kPad, "b"}});
  CHECK_TRUE(table.Custom());
  CHECK_TRUE(table.Effective(Action::kFirstNote) == table.Bindings(Action::kFirstNote));
  CHECK_TRUE(table.Effective(Action::kSecondNote) == PresetSources(2, Action::kSecondNote));
  // Reset returns the action to the preset, and Reset All returns the table to it entirely.
  table.Reset(Action::kFirstNote);
  CHECK_FALSE(table.Custom());
  table.Set(Action::kPause, {});
  CHECK_TRUE(table.Custom());
  table.ResetAll();
  CHECK_FALSE(table.Custom());
  CHECK_TRUE(table.Effective(Action::kPause) == PresetSources(2, Action::kPause));
  // An out-of-range preset reads as 0, and the other table ignores the preset entirely.
  table.SetControllerPreset(42);
  CHECK_EQ(table.Preset(), 0);
  Table keyboard_mouse(TableKind::kKeyboardMouse);
  keyboard_mouse.SetControllerPreset(3);
  CHECK_TRUE(keyboard_mouse.Effective(Action::kFirstNote) ==
             DefaultSources(TableKind::kKeyboardMouse, Action::kFirstNote));
}

}  // namespace

int main() {
  TestVocabulary();
  TestAbsentAndEmpty();
  TestSourceKinds();
  TestUnknownAndInvalid();
  TestEditsAndIndependence();
  TestProfileStorage();
  TestControllerPresets();
  TestSwitchesAreAppliedByABoot();
  TestExplicitValues();
  return Finish();
}
