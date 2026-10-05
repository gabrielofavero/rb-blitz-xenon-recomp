// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for the button-remap vocabulary (src/launcher/remap.h, D16).
//
// D16's verify list, as cases:
//
//   1. the grammar is total: every control round-trips through the text the profile writes, and
//      text this build does not understand is refused rather than guessed at
//   2. a table survives the profile's own format, including a row a newer launcher wrote
//   3. a stock control passes through, a bound one is rewritten, and several sources for one
//      control are a list rather than a priority
//   4. every binding reads the pad as it was, so a swap is a swap and not a chain
//
// No pad, no window, no SDL: the device side of the remap is the callback this file hands
// `Apply` (src/input/remap.cpp), so everything the game would otherwise have to be running for
// is a plain struct here.

#include "check.h"

#include "launcher/profile.h"
#include "launcher/remap.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace rb_blitz::launcher;
using namespace rb_blitz::launcher::remap;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

// check.h compares integers, so strings get their own reporter that prints both sides.
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

void CheckNotContains(const char* file, int line, const std::string& haystack,
                      const std::string& needle) {
  ++g_checks;
  if (haystack.find(needle) != std::string::npos) {
    Fail(file, line, "expected not to find \"" + needle + "\" in\n         " + haystack);
  }
}

#define CHECK_CONTAINS(haystack, needle) CheckContains(__FILE__, __LINE__, (haystack), (needle))
#define CHECK_NOT_CONTAINS(haystack, needle) \
  CheckNotContains(__FILE__, __LINE__, (haystack), (needle))

// A scratch directory, removed when the test finishes.
struct Scratch {
  fs::path base;

  Scratch() {
    std::error_code code;
    base = fs::temp_directory_path(code) / "rb_blitz-launcher-remap";
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

// What the profile would write, which is the whole point of the round trip.
std::string Compose(const Profile& profile) {
  std::string text;
  std::string error;
  if (!ComposeProfile(profile, &text, &error)) {
    Fail(__FILE__, __LINE__, "ComposeProfile failed: " + error);
    return {};
  }
  return text;
}

// What a "device" is for these tests: the set of sources that are down, as the callback sees
// them. A pad source is answered from the state `Apply` was given, so only keys and mouse
// buttons arrive here.
struct FakeDevice {
  std::vector<Source> down;

  static bool IsDown(void* context, const Source& source) {
    const auto* device = static_cast<const FakeDevice*>(context);
    for (const Source& candidate : device->down) {
      if (candidate == source) {
        return true;
      }
    }
    return false;
  }
};

PadState Run(const Table& table, const PadState& pad, FakeDevice* device = nullptr) {
  FakeDevice none;
  PadState out;
  Apply(table, pad, &FakeDevice::IsDown, device == nullptr ? &none : device, &out);
  return out;
}

std::vector<ProfileSetting> Bindings(std::initializer_list<const char*> key_value_pairs) {
  std::vector<ProfileSetting> rows;
  for (const char* pair : key_value_pairs) {
    const std::string text(pair);
    const std::size_t equals = text.find('=');
    rows.push_back(ProfileSetting{text.substr(0, equals), text.substr(equals + 1),
                                  ValueStyle::kBasic});
  }
  return rows;
}

void TestVocabulary() {
  BeginCase("D16: every control has a name, a label, and one bit of its own");

  CHECK_EQ(Targets().size(), 17);
  std::map<std::string, Target> seen;
  uint16_t union_of_bits = 0;
  for (const Target target : Targets()) {
    const std::string name(TargetName(target));
    CHECK_FALSE(name.empty());
    CHECK_FALSE(TargetLabel(target).empty());
    // The names are what `[remap]` spells, so two controls sharing one would make the table
    // ambiguous.
    CHECK_TRUE(seen.find(name) == seen.end());
    seen.emplace(name, target);
    CHECK_TRUE(ParseTarget(name).has_value());
    CHECK_TRUE(*ParseTarget(name) == target);

    // The triggers are the exception and are not bits at all: the guest reads them as analog
    // values, so a binding writes their byte instead.
    if (TargetIsTrigger(target)) {
      CHECK_EQ(TargetBit(target), 0);
    } else {
      CHECK_TRUE(TargetBit(target) != 0);
      CHECK_EQ(union_of_bits & TargetBit(target), 0);
      union_of_bits = static_cast<uint16_t>(union_of_bits | TargetBit(target));
    }
  }
  // The 15 digital controls and Guide fill the guest's button field except for one bit, 0x0800,
  // which no 360 control occupies - so this is the field's whole shape, not a coincidence.
  CHECK_EQ(union_of_bits, 0xF7FF);

  BeginCase("D16: a control is a source as well as a target, with the same name");

  for (const char* name : {"a", "left_shoulder", "left_trigger", "dpad_up", "guide"}) {
    const auto source = ParseSource(std::string("pad:") + name);
    CHECK_TRUE(source.has_value());
    CHECK_TRUE(source->kind == SourceKind::kPad);
    CHECK_STR_EQ(FormatSource(*source), std::string("pad:") + name);
  }
  CHECK_STR_EQ(FormatSource(*ParseSource("key:space")), "key:space");
  CHECK_STR_EQ(FormatSource(*ParseSource("mouse:left")), "mouse:left");

  BeginCase("D16: the grammar refuses what it does not understand, in every half");

  CHECK_FALSE(ParseTarget("a ").has_value());
  CHECK_FALSE(ParseTarget("A").has_value());
  CHECK_FALSE(ParseTarget("").has_value());
  CHECK_FALSE(ParseSource("").has_value());
  CHECK_FALSE(ParseSource("a").has_value());         // no prefix
  CHECK_FALSE(ParseSource("pad:").has_value());      // no control
  CHECK_FALSE(ParseSource("pad:trigger").has_value());
  CHECK_FALSE(ParseSource("wheel:up").has_value());  // a device this build has no reader for
  CHECK_FALSE(ParseSource("mouse:back").has_value());
  CHECK_FALSE(ParseSource("key:").has_value());
  CHECK_FALSE(ParseSource("key:space bar").has_value());

  // A key name is SDL's and this module deliberately does not link SDL, so any single word is
  // accepted here and resolved by the game. That is why a key a newer build wrote does not make
  // an older build reject the whole file.
  const auto key = ParseSource("key:space");
  CHECK_TRUE(key.has_value());
  CHECK_STR_EQ(key->name, "space");
}

void TestTableRoundTrip() {
  BeginCase("D16: a table survives the profile's own spelling");

  const std::vector<ProfileSetting> rows = Bindings({"y=pad:x, key:space", "b="});
  const Table table = Table::FromRows(rows);
  CHECK_EQ(table.BoundCount(), 2);
  CHECK_TRUE(table.IsBound(Target::kY));
  CHECK_TRUE(table.IsBound(Target::kB));
  CHECK_TRUE(table.IsDisabled(Target::kB));
  CHECK_TRUE(table.AnySourceSet());

  BeginCase("D16: a table survives the profile's own spelling, read half");

  CHECK_TRUE(table.Bindings(Target::kY) != nullptr);
  CHECK_EQ(table.Bindings(Target::kY)->size(), 2);
  // Nothing said about a control means the pad's own behaviour, which is not the same thing as
  // a control bound to nothing.
  CHECK_TRUE(table.Bindings(Target::kA) == nullptr);
  CHECK_FALSE(table.IsBound(Target::kA));

  BeginCase("D16: a table survives the profile's own spelling, written half");

  // Written back in the panel's order rather than the file's, and with the same values.
  const std::vector<ProfileSetting> written = table.ToRows();
  CHECK_EQ(written.size(), 2);
  CHECK_STR_EQ(written[0].key, "b");
  CHECK_STR_EQ(written[0].value, "");
  CHECK_STR_EQ(written[1].key, "y");
  CHECK_STR_EQ(written[1].value, "pad:x, key:space");
  CHECK_TRUE(written[1].style == ValueStyle::kBasic);

  // The same source twice is one press, not two.
  const Table deduped = Table::FromRows(Bindings({"a=key:space, key:space"}));
  CHECK_TRUE(deduped.Bindings(Target::kA) != nullptr);
  CHECK_EQ(deduped.Bindings(Target::kA)->size(), 1);

  BeginCase("D16: a row this build does not understand is kept, not dropped");

  const std::vector<ProfileSetting> mixed =
      Bindings({"a=pad:b", "turbo=pad:x", "x=gesture:swipe"});
  const Table kept = Table::FromRows(mixed);
  CHECK_EQ(kept.BoundCount(), 1);
  const std::vector<ProfileSetting> kept_rows = kept.ToRows();
  CHECK_EQ(kept_rows.size(), 3);
  bool saw_unknown_key = false;
  bool saw_unknown_source = false;
  for (const ProfileSetting& row : kept_rows) {
    saw_unknown_key = saw_unknown_key || row.key == "turbo";
    saw_unknown_source = saw_unknown_source || row.value == "gesture:swipe";
  }
  CHECK_TRUE(saw_unknown_key);
  CHECK_TRUE(saw_unknown_source);

  BeginCase("D16: a value that is only half understood is left alone entirely");

  // Applying "pad:x" and quietly dropping "gesture:swipe" would look like a binding that works
  // when half of what the user wrote does not.
  const Table untouched = Table::FromRows(Bindings({"y=pad:x, gesture:swipe"}));
  CHECK_EQ(untouched.BoundCount(), 0);
  CHECK_EQ(untouched.ToRows().size(), 1);

  BeginCase("D16: Set and Reset are the two edits the panel makes");

  Table edited = Table::FromRows(rows);
  edited.Set(Target::kA, {Source{SourceKind::kPad, "b"}});
  CHECK_TRUE(edited.Bindings(Target::kA) != nullptr);
  CHECK_EQ(edited.Bindings(Target::kA)->size(), 1);
  edited.Reset(Target::kA);
  CHECK_TRUE(edited.Bindings(Target::kA) == nullptr);
  CHECK_EQ(edited.BoundCount(), 2);
  edited.ResetAll();
  CHECK_EQ(edited.BoundCount(), 0);
  // A table nobody bound anything in renders no rows at all.
  CHECK_TRUE(edited.ToRows().empty());
}

void TestRewrite() {
  BeginCase("D16: an unbound control is the pad's own, and a bound one is the binding's");

  PadState pad;
  pad.buttons = static_cast<uint16_t>(TargetBit(Target::kA) | TargetBit(Target::kY));
  pad.left_trigger = 200;

  // No rows at all: the state passes through, which is what "never opened the panel" has to
  // mean.
  const PadState stock = Run(Table{}, pad);
  CHECK_EQ(stock.buttons, pad.buttons);
  CHECK_EQ(stock.left_trigger, pad.left_trigger);

  // Y bound to the left trigger: A is still A, and the pad's own Y no longer reaches the game -
  // which is the half a driver could not have done, because merging only ever adds.
  const Table table = Table::FromRows(Bindings({"y=pad:left_trigger"}));
  pad.left_trigger = 0;
  const PadState rewritten = Run(table, pad);
  CHECK_EQ(rewritten.buttons & TargetBit(Target::kA), TargetBit(Target::kA));
  CHECK_EQ(rewritten.buttons & TargetBit(Target::kY), 0);

  // And it comes back from the trigger, at the same moment the game would call it pressed.
  pad.left_trigger = kTriggerThreshold;
  CHECK_EQ(Run(table, pad).buttons & TargetBit(Target::kY), 0);
  pad.left_trigger = static_cast<uint8_t>(kTriggerThreshold + 1);
  CHECK_EQ(Run(table, pad).buttons & TargetBit(Target::kY), TargetBit(Target::kY));

  BeginCase("D16: several sources for one control are a list, not a priority");

  const Table many = Table::FromRows(Bindings({"a=pad:b, key:space, mouse:left"}));
  FakeDevice device;
  const PadState none;
  CHECK_EQ(Run(many, none, &device).buttons & TargetBit(Target::kA), 0);

  PadState pad_b;
  pad_b.buttons = TargetBit(Target::kB);
  CHECK_EQ(Run(many, pad_b, &device).buttons & TargetBit(Target::kA), TargetBit(Target::kA));

  device.down = {Source{SourceKind::kKey, "space"}};
  CHECK_EQ(Run(many, none, &device).buttons & TargetBit(Target::kA), TargetBit(Target::kA));

  device.down = {Source{SourceKind::kMouse, "left"}};
  CHECK_EQ(Run(many, none, &device).buttons & TargetBit(Target::kA), TargetBit(Target::kA));

  device.down = {Source{SourceKind::kMouse, "right"}};
  CHECK_EQ(Run(many, none, &device).buttons & TargetBit(Target::kA), 0);

  BeginCase("D16: every binding reads the pad as it was, so a swap is a swap");

  const Table swap = Table::FromRows(Bindings({"a=pad:b", "b=pad:a"}));
  PadState only_a;
  only_a.buttons = TargetBit(Target::kA);
  const PadState swapped = Run(swap, only_a);
  // Both bindings saw the pad's original A, so A comes out clear and B comes out set - rather
  // than the chain A -> B -> A that reading the output would produce.
  CHECK_EQ(swapped.buttons & TargetBit(Target::kA), 0);
  CHECK_EQ(swapped.buttons & TargetBit(Target::kB), TargetBit(Target::kB));

  BeginCase("D16: a trigger target is driven to its end, and a disabled control is silent");

  const Table triggers = Table::FromRows(Bindings({"right_trigger=pad:a", "left_trigger="}));
  PadState resting;
  resting.buttons = TargetBit(Target::kA);
  resting.left_trigger = 255;
  const PadState driven = Run(triggers, resting);
  CHECK_EQ(driven.right_trigger, 0xFF);
  // Bound to nothing: nothing presses it, so it reads as released even though the pad's own
  // trigger is held.
  CHECK_EQ(driven.left_trigger, 0);

  const PadState released = Run(triggers, PadState{});
  CHECK_EQ(released.right_trigger, 0x00);
  CHECK_EQ(released.left_trigger, 0x00);
}

void TestProfileKeepsTheTable() {
  BeginCase("D16: the profile module reads `[remap]`, writes it, and preserves the document");

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
      "fullscreen = false\n"
      "\n"
      "[remap]\n"
      "y = \"pad:x, key:space\"   # the user's swap\n"
      "b = \"\"\n";
  const fs::path path = scratch.Write("launcher.toml", text);

  const ProfileLoadResult loaded = LoadProfile(path);
  CHECK_TRUE(loaded.status == ProfileStatus::kOk);
  CHECK_EQ(loaded.profile.remap.size(), 2);
  CHECK_STR_EQ(loaded.profile.remap[0].key, "y");
  CHECK_STR_EQ(loaded.profile.remap[1].key, "b");
  CHECK_EQ(Table::FromRows(loaded.profile.remap).BoundCount(), 2);

  // A load and a save with no edits is the same file, comment and all.
  CHECK_STR_EQ(Compose(loaded.profile), text);

  BeginCase("D16: a control the panel unbinds loses its row, and nothing else changes");

  Profile edited = loaded.profile;
  Table table = Table::FromRows(edited.remap);
  table.Reset(Target::kB);
  table.Set(Target::kY, {Source{SourceKind::kPad, "x"}});
  edited.remap = table.ToRows();
  const std::string after = Compose(edited);
  CHECK_NOT_CONTAINS(after, "b = \"\"");
  CHECK_NOT_CONTAINS(after, "key:space");
  CHECK_CONTAINS(after, "[remap]");
  CHECK_CONTAINS(after, "y = \"pad:x\"");
  // The rest of the document is untouched, which is what makes a remap edit safe to save.
  CHECK_CONTAINS(after, "fullscreen = false");
  CHECK_CONTAINS(after, "[settings]");

  BeginCase("D16: a profile whose bindings all went keeps the document and drops the rows");

  Profile empty = loaded.profile;
  empty.remap.clear();
  const std::string cleared = Compose(empty);
  CHECK_NOT_CONTAINS(cleared, "pad:x");
  CHECK_NOT_CONTAINS(cleared, "key:space");
  CHECK_NOT_CONTAINS(cleared, "b = \"\"");
  CHECK_CONTAINS(cleared, "fullscreen = false");
  // What is left is a `[remap]` line with nothing under it - the same thing an emptied
  // `[settings]` table leaves, because the writer drops the keys it owns and not the heading
  // they were under. It still parses, and it is a table with no bindings, which is the state
  // that was asked for.
  const fs::path cleared_path = scratch.Write("cleared.toml", cleared);
  const ProfileLoadResult reloaded_cleared = LoadProfile(cleared_path);
  CHECK_TRUE(reloaded_cleared.status == ProfileStatus::kOk);
  CHECK_TRUE(reloaded_cleared.profile.remap.empty());

  BeginCase("D16: a fresh profile renders the table it was given, and only then");

  Profile fresh;
  CHECK_NOT_CONTAINS(RenderProfile(fresh), "[remap]");
  fresh.remap = Bindings({"a=pad:b"});
  const std::string rendered = RenderProfile(fresh);
  CHECK_CONTAINS(rendered, "[remap]\na = \"pad:b\"\n");

  BeginCase("D16: adding a `[remap]` table to a document that has none keeps the document's shape");

  const std::string plain =
      "schema_version = 1\n"
      "\n"
      "[launcher]\n"
      "version = 1\n"
      "portable = false\n"
      "\n"
      "[settings]\n"
      "fullscreen = false\n";
  const fs::path plain_path = scratch.Write("plain.toml", plain);
  Profile plain_profile = LoadProfile(plain_path).profile;
  plain_profile.remap = Bindings({"a=key:Space"});
  const std::string with_remap = Compose(plain_profile);
  // The header is a line of its own, which is the whole risk in appending a table to a
  // document whose last line is a key: `fullscreen = false[remap]` is not a table.
  CHECK_CONTAINS(with_remap, "\n[remap]\n");
  CHECK_CONTAINS(with_remap, "fullscreen = false\n");
  CHECK_CONTAINS(with_remap, "a = \"key:Space\"\n");

  // And the file it wrote is the strongest statement of that: it reads back as a profile with
  // the setting and the binding, not as one line with a bracket in it.
  const fs::path with_remap_path = scratch.Write("with-remap.toml", with_remap);
  const ProfileLoadResult reloaded_with_remap = LoadProfile(with_remap_path);
  CHECK_TRUE(reloaded_with_remap.status == ProfileStatus::kOk);
  CHECK_EQ(reloaded_with_remap.profile.remap.size(), 1);
  CHECK_STR_EQ(reloaded_with_remap.profile.remap[0].value, "key:Space");
  CHECK_TRUE(reloaded_with_remap.profile.FindSetting("fullscreen") != nullptr);

  BeginCase("D16: a document with no trailing newline still gets a table of its own");

  // The case a hand-edited file actually hits: nothing after the last key, so the table has to
  // bring its own line break. Without it the header lands on the end of `fullscreen = false`.
  const fs::path tight_path = scratch.Write("tight.toml", std::string(plain, 0, plain.size() - 1));
  Profile tight_profile = LoadProfile(tight_path).profile;
  tight_profile.remap = Bindings({"a=key:Space"});
  const std::string tight = Compose(tight_profile);
  CHECK_CONTAINS(tight, "\n[remap]\n");
  CHECK_CONTAINS(tight, "fullscreen = false\n");
  CHECK_NOT_CONTAINS(tight, "false[remap]");
  CHECK_NOT_CONTAINS(tight, "[remap]\n\n");
  const ProfileLoadResult reloaded_tight = LoadProfile(scratch.Write("tight-out.toml", tight));
  CHECK_TRUE(reloaded_tight.status == ProfileStatus::kOk);
  CHECK_EQ(reloaded_tight.profile.remap.size(), 1);

  BeginCase("D16: the table is loadable again from what was written");

  const fs::path round_trip = scratch.Write("round-trip.toml", Compose(fresh));
  const ProfileLoadResult reloaded = LoadProfile(round_trip);
  CHECK_TRUE(reloaded.status == ProfileStatus::kOk);
  CHECK_EQ(reloaded.profile.remap.size(), 1);
  CHECK_EQ(Table::FromRows(reloaded.profile.remap).BoundCount(), 1);
}

}  // namespace

int main() {
  TestVocabulary();
  TestTableRoundTrip();
  TestRewrite();
  TestProfileKeepsTheTable();
  return Finish();
}
