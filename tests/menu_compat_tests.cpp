// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P10's menu-compatibility rules (src/settings/menu_compat.h), tested without a boot: the hide
// matrix §3.5 states in words, the fixed 50-song pool, the retired pool-size value, and the rename's
// scope. The one case that needs a real file - the effective-menu inventory - lives in
// tests/menu_options_tests.cpp, which already builds a synthetic splash record.

#include <string>
#include <vector>

#include "check.h"
#include "settings/menu_compat.h"
#include "settings/model.h"
#include "settings/schema.h"

namespace test = rb_blitz::test;

namespace {

using rb_blitz::settings::SettingId;
using rb_blitz::settings::Value;
using rb_blitz::settings::ValueKind;
using namespace rb_blitz::settings::menu_compat;

std::string Joined(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& name : names) {
    if (!out.empty()) {
      out += ",";
    }
    out += name;
  }
  return out;
}

HideRequest Base() {
  HideRequest request;
  request.defaults = DefaultHiddenRows();
  return request;
}

void MasterOffHidesNothing() {
  test::BeginCase("hide: the master off leaves every row alone, selection or not");

  HideRequest request = Base();
  request.master = false;
  request.selection_explicit = true;
  request.selected = {"splash_start", "splash_exit"};
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(decision.hidden.empty());
  CHECK_TRUE(decision.refused.empty());
  CHECK_FALSE(decision.refresh_row_left_visible);
  CHECK_FALSE(decision.alternate_refresh_needed);
  // The note says why, so a boot log explains the menu rather than reporting silence.
  CHECK_TRUE(decision.note.find("master is off") != std::string::npos);
}

void NoSelectionUsesTheDefaults() {
  test::BeginCase("hide: an absent selection uses the compiled default trio");

  HideRequest request = Base();
  request.master = true;
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(Joined(decision.hidden) == "splash_leaderboard,splash_achievements,splash_dlc");
  CHECK_TRUE(decision.note.find("hiding 3 row(s)") != std::string::npos);
}

void EmptySelectionHidesNothing() {
  test::BeginCase("hide: an explicit empty selection hides nothing, even with the master on");

  // The defect the plan names: an empty list used to mean "the defaults", so clearing the field
  // re-hid rows the player had just removed from it.
  HideRequest request = Base();
  request.master = true;
  request.selection_explicit = true;  // deliberately nothing
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(decision.hidden.empty());
  CHECK_TRUE(decision.note.find("selection is empty") != std::string::npos);
  CHECK_TRUE(decision.note.find("nothing is hidden") != std::string::npos);
}

void CustomSelectionIsHonoured() {
  test::BeginCase("hide: a custom selection is what gets hidden, de-duplicated and in order");

  HideRequest request = Base();
  request.master = true;
  request.selection_explicit = true;
  request.selected = {"splash_leaderboard", "splash_start", "splash_leaderboard", "",
                     "splash_exit"};
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(Joined(decision.hidden) == "splash_leaderboard,splash_start,splash_exit");
}

void ProtectedEntryIsRefusedByName() {
  test::BeginCase("hide: the merged settings entry is never hidden, and the refusal is reported");

  HideRequest request = Base();
  request.master = true;
  request.selection_explicit = true;
  request.selected = {"splash_options", "splash_start"};
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(Joined(decision.hidden) == "splash_start");
  CHECK_EQ(decision.refused.size(), 1u);
  CHECK_TRUE(decision.refused.front() == "splash_options");
  CHECK_TRUE(decision.note.find("protected entry") != std::string::npos);
  // The default trio does not name it, so the ordinary path never even asks.
  CHECK_TRUE(DecideHiddenRows(Base()).refused.empty());
}

void RefreshRowIsKeptForTheDefaults() {
  test::BeginCase("hide: the defaults keep the refresh row while R7 owns it");

  HideRequest request = Base();
  request.master = true;
  request.refresh_row_owns_refresh = true;
  const HideDecision decision = DecideHiddenRows(request);
  // The trio loses `splash_dlc`: that row is what refreshes the library in this run.
  CHECK_TRUE(Joined(decision.hidden) == "splash_leaderboard,splash_achievements");
  CHECK_TRUE(decision.refresh_row_left_visible);
  CHECK_FALSE(decision.alternate_refresh_needed);
  CHECK_TRUE(decision.note.find("refreshes the DLC library") != std::string::npos);
}

void RefreshRowMayBeHiddenExplicitly() {
  test::BeginCase("hide: an explicit choice may hide the refresh row, and the caller is told");

  HideRequest request = Base();
  request.master = true;
  request.selection_explicit = true;
  request.selected = {"splash_dlc", "splash_leaderboard"};
  request.refresh_row_owns_refresh = true;
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(Joined(decision.hidden) == "splash_dlc,splash_leaderboard");
  CHECK_FALSE(decision.refresh_row_left_visible);
  CHECK_TRUE(decision.alternate_refresh_needed);
  CHECK_TRUE(decision.note.find("needs another one") != std::string::npos);
}

void RefreshEntryOffHidesTheRow() {
  test::BeginCase("hide: the refresh entry's own switch takes its row away with the defaults");

  // `general.refresh_music_library` off (owner request, 2026-10-15): the refresh row is the
  // feature's own entry, so the compiled default - which deliberately leaves it visible while R7
  // owns it - must not keep it. It is the player's choice, so nothing needs another row.
  HideRequest request = Base();
  request.master = true;
  request.refresh_row_owns_refresh = true;
  request.refresh_row_enabled = false;
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(Joined(decision.hidden) == "splash_leaderboard,splash_achievements,splash_dlc");
  CHECK_FALSE(decision.refresh_row_left_visible);
  CHECK_FALSE(decision.alternate_refresh_needed);
  CHECK_TRUE(decision.refresh_row_disabled);
  CHECK_TRUE(decision.note.find("entry is off") != std::string::npos);
}

void RefreshEntryOffAlsoHidesItFromACustomSelection() {
  test::BeginCase("hide: the switch applies to a custom selection that does not name the row");

  HideRequest request = Base();
  request.master = true;
  request.selection_explicit = true;
  request.selected = {"splash_start"};
  request.refresh_row_owns_refresh = true;
  request.refresh_row_enabled = false;
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(Joined(decision.hidden) == "splash_start,splash_dlc");
  CHECK_FALSE(decision.alternate_refresh_needed);
  CHECK_TRUE(decision.refresh_row_disabled);
}

void RefreshEntryOffIgnoresTheHideMaster() {
  test::BeginCase("hide: the switch hides its row with the hide master off too");

  // The master governs the hide *selection*: a player who never turned it on must still be able to
  // remove the refresh entry, which is what its own row in Help & Options is for.
  HideRequest request = Base();
  request.master = false;
  request.selection_explicit = true;
  request.selected = {"splash_start"};
  request.refresh_row_owns_refresh = true;
  request.refresh_row_enabled = false;
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(Joined(decision.hidden) == "splash_dlc");
  CHECK_TRUE(decision.note.find("master is off") != std::string::npos);
  CHECK_TRUE(decision.note.find("entry is off") != std::string::npos);
  CHECK_TRUE(decision.refresh_row_disabled);
  CHECK_FALSE(decision.alternate_refresh_needed);
  // With the entry on, the master-off answer is the untouched menu it has always been.
  HideRequest on = request;
  on.refresh_row_enabled = true;
  CHECK_TRUE(DecideHiddenRows(on).hidden.empty());
}

void RefreshEntryOffMeansNothingWithoutR7() {
  test::BeginCase("hide: the switch is inert while the row is not the refresh row");

  // R7 off (`enhancements_dlc_cache` off): `splash_dlc` is the title's own online row again, so the
  // refresh entry's switch says nothing about it - the compiled trio hides it as before.
  HideRequest request = Base();
  request.master = true;
  request.refresh_row_owns_refresh = false;
  request.refresh_row_enabled = false;
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_TRUE(Joined(decision.hidden) == "splash_leaderboard,splash_achievements,splash_dlc");
  CHECK_FALSE(decision.refresh_row_disabled);
  CHECK_FALSE(decision.alternate_refresh_needed);
  CHECK_FALSE(decision.refresh_row_left_visible);
}

void PoolIsFixedAtFifty() {
  test::BeginCase("pool: 50 songs, bounded by what the library can supply");

  CHECK_EQ(kMenuPoolMax, 50u);
  CHECK_EQ(PoolCap(200), 50u);
  CHECK_EQ(PoolCap(64), 50u);  // the old cvar's own maximum no longer means anything
  CHECK_EQ(PoolCap(50), 50u);
  CHECK_EQ(PoolCap(7), 7u);
  CHECK_EQ(PoolCap(1), 1u);
  CHECK_EQ(PoolCap(0), 0u);
}

void LegacyPoolCountIsIgnoredOutLoud() {
  test::BeginCase("pool: a legacy pool size is ignored, and said so");

  CHECK_TRUE(LegacyPoolCountNote(kLegacyPoolCountDefault).empty());
  CHECK_TRUE(LegacyPoolCountNote(3).empty());
  const std::string note = LegacyPoolCountNote(64);
  CHECK_TRUE(note.find("ignored") != std::string::npos);
  CHECK_TRUE(note.find("50") != std::string::npos);
  CHECK_TRUE(note.find("64") != std::string::npos);
  CHECK_FALSE(LegacyPoolCountNote(1).empty());
}

void RenameCannotReachTheMergedRoot() {
  test::BeginCase("rename: the retired mod-row rename is scoped to the mod's own key");

  CHECK_TRUE(RenameCoversRow("mod_settings"));
  CHECK_FALSE(RenameCoversRow("splash_options"));   // the protected merged entry
  CHECK_FALSE(RenameCoversRow("settings_panel"));   // our panel
  CHECK_FALSE(RenameCoversRow("settings_general"));
  CHECK_FALSE(RenameCoversRow(""));
  // The two constants are different rows, which is the guard the plan asks for.
  CHECK_FALSE(std::string(kProtectedMenuRow) == kRenameScopeKey);
}

void HideListSeamTakesTheInventoryAndKeepsTheEmptyCase() {
  test::BeginCase("seam: the Hide Main Menu row lists the real rows and distinguishes empty");

  const rb_blitz::settings::schema::SchemaRow* row =
      rb_blitz::settings::schema::FindRow(SettingId::kHiddenMainMenu);
  CHECK_TRUE(row != nullptr);
  if (row == nullptr) {
    return;
  }
  // A list row whose compiled default is *no selection at all*: `kNone`, not an empty list. That
  // difference is exactly the hide rule's "absent vs deliberately empty".
  CHECK_EQ(row->descriptor.value_kind == ValueKind::kList, true);
  CHECK_EQ(row->descriptor.compiled_default.kind == ValueKind::kNone, true);

  // P10's inventory is what the row's choices are built from; a conditional row is offered too,
  // because "this row exists only with macro X" is a fact the player has to be able to see.
  rb_blitz::settings::schema::SetMenuRows(
      {{"splash_start", "PLAY"},
       {"splash_leaderboard", "LEADERBOARDS"},
       {"splash_options", "HELP & OPTIONS"},
       {"splash_exit", "EXIT GAME"}});
  const std::vector<rb_blitz::settings::Choice> rows = rb_blitz::settings::schema::MenuRows();
  CHECK_EQ(rows.size(), 4u);
  CHECK_TRUE(rows.front().token == "splash_start");
  CHECK_TRUE(rows.front().label == "PLAY");
  CHECK_TRUE(rows[2].token == "splash_options");

  // The protected entry is in the list (the player sees it) and the decision refuses to hide it -
  // the two facts together are what makes "visible and non-hideable" true.
  HideRequest request = Base();
  request.master = true;
  request.selection_explicit = true;
  for (const rb_blitz::settings::Choice& choice : rows) {
    request.selected.push_back(choice.token);
  }
  const HideDecision decision = DecideHiddenRows(request);
  CHECK_EQ(decision.refused.size(), 1u);
  CHECK_TRUE(decision.refused.front() == "splash_options");
  CHECK_EQ(decision.hidden.size(), 3u);

  rb_blitz::settings::schema::SetMenuRows({});
}

}  // namespace

int main() {
  MasterOffHidesNothing();
  NoSelectionUsesTheDefaults();
  EmptySelectionHidesNothing();
  CustomSelectionIsHonoured();
  ProtectedEntryIsRefusedByName();
  RefreshRowIsKeptForTheDefaults();
  RefreshRowMayBeHiddenExplicitly();
  RefreshEntryOffHidesTheRow();
  RefreshEntryOffAlsoHidesItFromACustomSelection();
  RefreshEntryOffIgnoresTheHideMaster();
  RefreshEntryOffMeansNothingWithoutR7();
  PoolIsFixedAtFifty();
  LegacyPoolCountIsIgnoredOutLoud();
  RenameCannotReachTheMergedRoot();
  HideListSeamTakesTheInventoryAndKeepsTheEmptyCase();
  return test::Finish();
}
