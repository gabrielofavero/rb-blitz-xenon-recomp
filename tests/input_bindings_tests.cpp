// SPDX-License-Identifier: GPL-2.0-only
//
// P7's host-side tests (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md §14):
// the `input.*` row grammar, the listening session's rules (release first, wrong kind keeps
// listening, cancel, timeout), and the store's one-writer save against a real temporary profile.
//
// SDK-free and SDL-free: the capture is fed plain frames and the store reads and writes the
// profile module's own document, exactly as the game's write hook does.

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "input/input_capture.h"
#include "input/input_store.h"
#include "profile/input_rows.h"
#include "profile/profile.h"
#include "profile/remap.h"

namespace {

using rb_blitz::profile::InputRowKind;
using rb_blitz::profile::InputRowToken;

namespace capture = rb_blitz::input::capture;

rb_blitz::profile::remap::Source Source(const char* text) {
  const auto parsed = rb_blitz::profile::remap::ParseSource(text);
  return parsed.value_or(rb_blitz::profile::remap::Source{});
}

capture::Observation Frame() {
  capture::Observation frame;
  frame.focused = true;
  return frame;
}

void TestTokenGrammar() {
  rb_blitz::test::BeginCase("input rows: tokens and symbols");
  CHECK_TRUE(rb_blitz::profile::IsInputToken("input.general.a"));
  CHECK_FALSE(rb_blitz::profile::IsInputToken("inputx.general.a"));

  const InputRowToken general = rb_blitz::profile::ParseInputRowToken("input.general.left_shoulder");
  CHECK_TRUE(general.kind == InputRowKind::kGeneralBinding);
  CHECK_TRUE(general.name == "left_shoulder");
  CHECK_TRUE(rb_blitz::profile::ParseInputRowToken("input.general.guide").kind ==
             InputRowKind::kNone);
  CHECK_TRUE(rb_blitz::profile::ParseInputRowToken("input.controller.first_note").kind ==
             InputRowKind::kControllerBinding);
  CHECK_TRUE(rb_blitz::profile::ParseInputRowToken("input.keyboard_mouse.pause").kind ==
             InputRowKind::kKeyboardMouseBinding);
  CHECK_TRUE(rb_blitz::profile::ParseInputRowToken("input.reset.controller").kind ==
             InputRowKind::kResetController);

  // A canonical symbol round-trips to the row the caption event names.
  const std::string symbol =
      rb_blitz::profile::InputRowSymbol("input.general.left_shoulder");
  CHECK_TRUE(symbol == "settings_bind_general_left_shoulder");
  const InputRowToken back = rb_blitz::profile::ParseInputRowSymbol(symbol);
  CHECK_TRUE(back.kind == InputRowKind::kGeneralBinding);
  CHECK_TRUE(back.name == "left_shoulder");
  CHECK_TRUE(rb_blitz::profile::BindingRowLabel(back) == "Left bumper");
  CHECK_TRUE(rb_blitz::profile::ParseInputRowSymbol("settings_vsync").kind ==
             InputRowKind::kNone);
}

void TestCaptureReleasesFirst() {
  rb_blitz::test::BeginCase("capture: the activation press is not captured");
  capture::Arm({"input.general.a", {rb_blitz::profile::remap::SourceKind::kPad,
                                    rb_blitz::profile::remap::SourceKind::kKey,
                                    rb_blitz::profile::remap::SourceKind::kMouse}});
  // The Confirm press is still held when the dialog opens: it must not become the binding.
  capture::Observation activation = Frame();
  activation.pad.buttons = 0x1000;  // A
  capture::Tick tick = capture::Observe(activation);
  CHECK_FALSE(tick.captured.has_value());
  // Release: a neutral frame arms the listener.
  tick = capture::Observe(Frame());
  CHECK_FALSE(tick.captured.has_value());
  // The next real press is captured.
  capture::Observation press = Frame();
  press.pad.buttons = 0x2000;  // B
  tick = capture::Observe(press);
  CHECK_TRUE(tick.captured.has_value());
  CHECK_TRUE(tick.captured->source.kind == rb_blitz::profile::remap::SourceKind::kPad);
  CHECK_TRUE(tick.captured->source.name == "b");
  CHECK_FALSE(capture::Armed());
}

void TestCaptureWrongKind() {
  rb_blitz::test::BeginCase("capture: a wrong kind stays listening and says so");
  capture::Arm({"input.controller.first_note", {rb_blitz::profile::remap::SourceKind::kPad}});
  capture::Tick tick = capture::Observe(Frame());
  CHECK_FALSE(tick.captured.has_value());

  capture::Observation key = Frame();
  key.any_key_down = true;
  key.new_keys.push_back("J");
  tick = capture::Observe(key);
  CHECK_FALSE(tick.captured.has_value());
  CHECK_TRUE(capture::StatusText().find("pad") != std::string::npos);

  // Behind the message the session waits for neutral again; the pad press after that lands.
  capture::Observation release = Frame();
  tick = capture::Observe(release);
  CHECK_FALSE(tick.captured.has_value());
  capture::Observation pad = Frame();
  pad.pad.buttons = 0x0010;  // Start
  tick = capture::Observe(pad);
  CHECK_TRUE(tick.captured.has_value());
  CHECK_TRUE(tick.captured->source.name == "start");
}

void TestCaptureCancelAndTimeout() {
  rb_blitz::test::BeginCase("capture: cancel leaves nothing armed");
  capture::Arm({"input.general.a", {rb_blitz::profile::remap::SourceKind::kKey}});
  CHECK_TRUE(capture::Armed());
  capture::Cancel();
  CHECK_FALSE(capture::Armed());
  capture::Observation key = Frame();
  key.any_key_down = true;
  key.new_keys.push_back("K");
  CHECK_FALSE(capture::Observe(key).captured.has_value());
}

void TestCaptureWrongKindHoldsToCancel() {
  rb_blitz::test::BeginCase("capture: holding a wrong-kind input is the cancel gesture");
  bool held = true;
  capture::Arm({"input.controller.first_note", {rb_blitz::profile::remap::SourceKind::kPad}});
  capture::Observation neutral = Frame();
  neutral.still_held = [&held](const rb_blitz::profile::remap::Source&) { return held; };
  CHECK_FALSE(capture::Observe(neutral).captured.has_value());
  // A key is the wrong kind here, but its press still starts the hold timer: a player whose
  // controller is missing must be able to leave the page (plan §14.3's cancel gesture).
  capture::Observation wrong = Frame();
  wrong.any_key_down = true;
  wrong.new_keys.push_back("G");
  wrong.still_held = [&held](const rb_blitz::profile::remap::Source&) { return held; };
  capture::Tick tick = capture::Observe(wrong);
  CHECK_FALSE(tick.captured.has_value());
  CHECK_TRUE(tick.leave == false);
  std::this_thread::sleep_for(capture::kHoldToCancel + std::chrono::milliseconds(50));
  tick = capture::Observe(wrong);
  CHECK_TRUE(tick.leave);
  CHECK_TRUE(tick.cancelled);
  CHECK_FALSE(capture::Armed());
}

// The live filter can answer "is that source still down?", and with that answer a capture waits
// for the release: a tap binds the control, a hold past capture::kHoldToCancel cancels the dialog
// (plan §14.3's unambiguous cancel gesture - which is what keeps Back and Escape assignable).
void TestCaptureHoldToCancel() {
  rb_blitz::test::BeginCase("capture: a release binds, a long hold cancels");
  bool held = true;
  auto oracle = [&held](const rb_blitz::profile::remap::Source&) { return held; };

  capture::Arm({"input.general.b", {rb_blitz::profile::remap::SourceKind::kPad}});
  capture::Observation activation = Frame();
  activation.pad.buttons = 0x1000;  // A, the press that opened the page
  activation.still_held = oracle;
  CHECK_FALSE(capture::Observe(activation).captured.has_value());
  capture::Observation neutral = Frame();
  neutral.still_held = oracle;
  held = false;
  CHECK_FALSE(capture::Observe(neutral).captured.has_value());

  // The press itself is not the answer while the oracle still says "down".
  held = true;
  capture::Observation press = Frame();
  press.pad.buttons = 0x2000;  // B
  press.still_held = oracle;
  capture::Tick tick = capture::Observe(press);
  CHECK_FALSE(tick.captured.has_value());
  CHECK_TRUE(capture::Armed());
  CHECK_TRUE(capture::StatusText().find("Release") != std::string::npos);

  // Released before the threshold: that was a tap, so it binds.
  held = false;
  capture::Observation release = Frame();
  release.still_held = oracle;
  tick = capture::Observe(release);
  CHECK_TRUE(tick.captured.has_value());
  CHECK_TRUE(tick.captured->source.name == "b");
  CHECK_FALSE(tick.cancelled);
  CHECK_FALSE(capture::Armed());

  // Held past it: the dialog leaves with nothing bound.
  capture::Arm({"input.general.b", {rb_blitz::profile::remap::SourceKind::kPad}});
  capture::Cancel();
  capture::Arm({"input.general.b", {rb_blitz::profile::remap::SourceKind::kPad}});
  capture::Observation quiet = Frame();
  quiet.still_held = oracle;
  held = false;
  capture::Observe(quiet);
  held = true;
  capture::Observation down = Frame();
  down.pad.buttons = 0x2000;
  down.still_held = oracle;
  CHECK_FALSE(capture::Observe(down).captured.has_value());
  std::this_thread::sleep_for(capture::kHoldToCancel + std::chrono::milliseconds(120));
  capture::Observation still_down = Frame();
  still_down.pad.buttons = 0x2000;
  still_down.still_held = oracle;
  tick = capture::Observe(still_down);
  CHECK_FALSE(tick.captured.has_value());
  CHECK_TRUE(tick.leave);
  CHECK_TRUE(tick.cancelled);
  CHECK_FALSE(capture::Armed());
  // Nothing is left armed, so the next press is not swallowed by a dead session.
  held = false;
  CHECK_FALSE(capture::Observe(Frame()).captured.has_value());
  capture::Cancel();
}

// --- the store ---------------------------------------------------------------------------------

std::filesystem::path ScratchDir() {
  const auto dir = std::filesystem::temp_directory_path() / "rb_blitz_input_bindings_tests";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::string Slurp(const std::filesystem::path& path) {
  std::FILE* file = std::fopen(path.string().c_str(), "rb");
  if (file == nullptr) return {};
  std::string text;
  char buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) != 0) text.append(buffer, got);
  std::fclose(file);
  return text;
}

rb_blitz::profile::ProfileLoadResult WriteFixture(const std::filesystem::path& path) {
  rb_blitz::profile::Profile profile;
  profile.remap = {{"a", "pad:a, key:A", rb_blitz::profile::ValueStyle::kBasic},
                   {"start", "pad:start", rb_blitz::profile::ValueStyle::kBasic}};
  profile.gameplay_remap_controller = {
      {"first_note", "pad:a", rb_blitz::profile::ValueStyle::kBasic}};
  profile.settings = {{"keyboard_support", "true", rb_blitz::profile::ValueStyle::kBare}};
  std::string error;
  CHECK_TRUE(rb_blitz::profile::SaveProfile(path, profile, &error));
  return rb_blitz::profile::LoadProfile(path);
}

void TestStoreResetAndSave() {
  rb_blitz::test::BeginCase("store: reset one row, save, preserve everything else");
  const auto dir = ScratchDir();
  const auto path = dir / "launcher.toml";
  rb_blitz::input::InputStoreOptions options;
  options.keyboard_support = true;
  options.mouse_support = true;
  auto store = rb_blitz::input::InputStore::Load(path, WriteFixture(path), options);
  CHECK_TRUE(store->Matches("input.general.a", true));
  CHECK_TRUE(store->Matches("input.reset.general", true));

  std::string error;
  CHECK_TRUE(store->StageReset("input.general.a", &error));
  CHECK_TRUE(store->Commit(&error));
  CHECK_FALSE(store->Matches("input.general.a", true));
  CHECK_TRUE(store->Matches("input.general.start", true));
  const std::string text = Slurp(path);
  CHECK_TRUE(text.find("start = ") != std::string::npos);
  CHECK_TRUE(text.find("\na = ") == std::string::npos);

  // The Reset All row is disabled once the table has no overrides.
  CHECK_TRUE(store->StageResetAll("input.reset.general", &error));
  CHECK_FALSE(store->Matches("input.reset.general", true));
}

void TestStoreSwitchAndRecovery() {
  rb_blitz::test::BeginCase("store: switches save, locks and the last-route rule refuse");
  const auto dir = ScratchDir();
  const auto path = dir / "launcher.toml";
  rb_blitz::input::InputStoreOptions options;
  options.keyboard_support = true;
  options.mouse_support = false;
  options.pad_connected = [] { return false; };
  auto store = rb_blitz::input::InputStore::Load(path, WriteFixture(path), options);
  std::string error;
  // Keyboard is the only usable device on this pad-less machine: turning it off is refused.
  CHECK_FALSE(store->StageBool("input.keyboard_support", false, &error));
  CHECK_TRUE(error.find("no usable input") != std::string::npos);
  CHECK_TRUE(store->keyboard_support());

  // The two gameplay switches are retired (restructure plan D3): the tokens do not exist, so a
  // menu cannot stage them and nothing writes them back.
  CHECK_FALSE(store->IsToken("input.custom_controller"));
  CHECK_FALSE(store->IsToken("input.custom_keyboard_mouse"));
  CHECK_FALSE(store->StageBool("input.custom_controller", true, &error));
  CHECK_TRUE(store->IsToken("input.keyboard_support"));

  rb_blitz::input::InputStoreOptions locked = options;
  locked.keyboard_locked = true;
  auto other = rb_blitz::input::InputStore::Load(path, rb_blitz::profile::LoadProfile(path),
                                                 locked);
  CHECK_FALSE(other->StageBool("input.keyboard_support", false, &error));
  CHECK_TRUE(error.find("command line") != std::string::npos);
}

// D15: an old profile's `gameplay_mapping_*` row is read as a value the profile holds, preserved
// byte for byte, and never consulted for routing. Custom is the table's own fact.
void TestLegacyCustomKeysAreInert() {
  rb_blitz::test::BeginCase("store: a legacy gameplay_mapping switch is preserved and decides nothing");
  const auto dir = ScratchDir();
  const auto path = dir / "launcher.toml";
  rb_blitz::profile::ProfileLoadResult load = WriteFixture(path);
  load.profile.settings.push_back(
      {"gameplay_mapping_controller", "true", rb_blitz::profile::ValueStyle::kBare});
  load.profile.settings.push_back(
      {"gameplay_mapping_keyboard_mouse", "false", rb_blitz::profile::ValueStyle::kBare});
  std::string error;
  CHECK_TRUE(rb_blitz::profile::SaveProfile(path, load.profile, &error));

  rb_blitz::input::InputStoreOptions options;
  auto store = rb_blitz::input::InputStore::Load(path, rb_blitz::profile::LoadProfile(path), options);
  // The fixture has one Controller row, so the table is custom regardless of the old `true`.
  CHECK_TRUE(store->controller_custom());
  // And a fresh profile with no gameplay rows is not custom although the key says true.
  const auto empty_path = dir / "empty.toml";
  rb_blitz::profile::Profile empty;
  empty.settings = {{"gameplay_mapping_controller", "true", rb_blitz::profile::ValueStyle::kBare}};
  CHECK_TRUE(rb_blitz::profile::SaveProfile(empty_path, empty, &error));
  auto plain = rb_blitz::input::InputStore::Load(empty_path, rb_blitz::profile::LoadProfile(empty_path),
                                                 options);
  CHECK_FALSE(plain->controller_custom());
  CHECK_TRUE(plain->StageResetAll("input.reset.controller", &error));
  CHECK_TRUE(plain->Commit(&error));
  // The key survives a save this store made: user data is not deleted (D15).
  CHECK_TRUE(Slurp(empty_path).find("gameplay_mapping_controller = true") != std::string::npos);
}

void TestFirstAssignReplacesAndAppends() {
  rb_blitz::test::BeginCase("store: first Assign replaces a default, the next one appends, Reset restores");
  const auto dir = ScratchDir();
  const auto path = dir / "launcher.toml";
  rb_blitz::input::InputStoreOptions options;
  auto store = rb_blitz::input::InputStore::Load(path, WriteFixture(path), options);
  std::string error;
  // The fixture has no `second_note` row, so that action holds its default (W + left click).
  CHECK_FALSE(store->Matches("input.keyboard_mouse.second_note", true));
  CHECK_TRUE(store->Describe("input.keyboard_mouse.second_note").find("W, Left Click") !=
             std::string::npos);
  CHECK_TRUE(store->Describe("input.keyboard_mouse.second_note").find("Click to Replace") !=
             std::string::npos);

  // First Assign: the default list is replaced, not extended (D8).
  CHECK_TRUE(store->CompleteCapture("input.keyboard_mouse.second_note", Source("key:Z"), &error));
  const std::string once = Slurp(path);
  CHECK_TRUE(once.find("second_note = \"key:Z\"") != std::string::npos);
  CHECK_TRUE(once.find("wheel") == std::string::npos);
  CHECK_TRUE(store->Describe("input.keyboard_mouse.second_note").find("Custom binding") !=
             std::string::npos);

  // Second Assign on the now-custom row appends, and a duplicate is still one press.
  CHECK_TRUE(store->CompleteCapture("input.keyboard_mouse.second_note", Source("key:X"), &error));
  CHECK_TRUE(store->CompleteCapture("input.keyboard_mouse.second_note", Source("key:X"), &error));
  const std::string twice = Slurp(path);
  CHECK_TRUE(twice.find("second_note = \"key:Z, key:X\"") != std::string::npos);

  // Reset drops the row and the default's caption comes back.
  CHECK_TRUE(store->StageReset("input.keyboard_mouse.second_note", &error));
  CHECK_TRUE(store->Commit(&error));
  CHECK_TRUE(Slurp(path).find("second_note") == std::string::npos);
  CHECK_TRUE(store->Describe("input.keyboard_mouse.second_note").find("Default binding") !=
             std::string::npos);

  // A wheel detent binds like any other mouse source, and it persists in the grammar.
  CHECK_TRUE(store->CompleteCapture("input.keyboard_mouse.move_lane_right",
                                    Source("mouse:wheel_up"), &error));
  CHECK_TRUE(Slurp(path).find("move_lane_right = \"mouse:wheel_up\"") != std::string::npos);
  CHECK_TRUE(store->Describe("input.keyboard_mouse.move_lane_right").find("Scroll Up") !=
             std::string::npos);
}

void TestStoreCaptureAssignment() {
  rb_blitz::test::BeginCase("store: a captured source is added once and persisted");
  const auto dir = ScratchDir();
  const auto path = dir / "launcher.toml";
  rb_blitz::input::InputStoreOptions options;
  auto store = rb_blitz::input::InputStore::Load(path, WriteFixture(path), options);
  CHECK_TRUE(store->BeginCapture("input.controller.first_note", nullptr));
  CHECK_TRUE(capture::Armed());
  capture::Cancel();
  std::string error;
  CHECK_TRUE(store->CompleteCapture("input.controller.first_note", Source("pad:b"), &error));
  CHECK_TRUE(store->CompleteCapture("input.controller.first_note", Source("pad:b"), &error));
  const std::string text = Slurp(path);
  CHECK_TRUE(text.find("first_note = \"pad:a, pad:b\"") != std::string::npos ||
             text.find("first_note = 'pad:a, pad:b'") != std::string::npos);
  // A key is refused by the Controller table, and the profile is untouched.
  CHECK_FALSE(store->CompleteCapture("input.controller.first_note", Source("key:J"), &error));
  CHECK_TRUE(error.find("does not take") != std::string::npos);
  // The caption describes the row's real list, in the display spelling the row uses (A, B).
  CHECK_TRUE(store->Describe("settings_bind_controller_first_note").find("A, B") !=
             std::string::npos);
}

// P1 (interface/gameplay restructure plan): a binding row draws its inputs as words, over the
// guest's own label. The grammar (`pad:a`) stays a file spelling; the row reads "A".
void TestRowDisplayText() {
  rb_blitz::test::BeginCase("binding rows: display names and the row line");
  const auto name = [](const char* text) {
    return rb_blitz::profile::SourceDisplayName(Source(text));
  };
  CHECK_TRUE(name("pad:a") == "A");
  CHECK_TRUE(name("pad:left_shoulder") == "LB");
  CHECK_TRUE(name("pad:left_thumb") == "L3");
  CHECK_TRUE(name("pad:dpad_right") == "D-pad Right");
  CHECK_TRUE(name("pad:lstick_up") == "L-Stick Up");
  CHECK_TRUE(name("key:q") == "Q");
  CHECK_TRUE(name("key:Return") == "Enter");
  CHECK_TRUE(name("key:right") == "Arrow Right");
  CHECK_TRUE(name("key:space") == "Space");
  CHECK_TRUE(name("key:F1") == "F1");
  CHECK_TRUE(name("mouse:right") == "Right Click");
  CHECK_TRUE(name("mouse:middle") == "Middle Click");

  const std::vector<rb_blitz::profile::remap::Source> list = {Source("key:q"),
                                                              Source("mouse:right")};
  CHECK_TRUE(rb_blitz::profile::BindingListText(list) == "Q, Right Click");
  const InputRowToken first_note =
      rb_blitz::profile::ParseInputRowToken("input.keyboard_mouse.first_note");
  CHECK_TRUE(rb_blitz::profile::BindingRowText(first_note, &list) ==
             "First Note: Q, Right Click");
  // A row with no effective binding (the analog triggers' pad pass-through) and an explicit empty
  // list both say so, rather than drawing a trailing colon.
  CHECK_TRUE(rb_blitz::profile::BindingRowText(first_note, nullptr) ==
             "First Note: (no inputs)");
  const std::vector<rb_blitz::profile::remap::Source> empty;
  CHECK_TRUE(rb_blitz::profile::BindingRowText(first_note, &empty) ==
             "First Note: (no inputs)");
  // A token that is not a binding row has no line at all.
  CHECK_TRUE(rb_blitz::profile::BindingRowText(InputRowToken{}, &list).empty());
}

}  // namespace

int main() {
  TestTokenGrammar();
  TestCaptureReleasesFirst();
  TestCaptureWrongKind();
  TestCaptureCancelAndTimeout();
  TestCaptureWrongKindHoldsToCancel();
  TestCaptureHoldToCancel();
  TestStoreResetAndSave();
  TestStoreSwitchAndRecovery();
  TestLegacyCustomKeysAreInert();
  TestStoreCaptureAssignment();
  TestFirstAssignReplacesAndAppends();
  TestRowDisplayText();
  std::printf("%s: %d check(s), %d failure(s)\n",
              rb_blitz::test::g_failures == 0 ? "PASS" : "FAIL", rb_blitz::test::g_checks,
              rb_blitz::test::g_failures);
  return rb_blitz::test::g_failures == 0 ? 0 : 1;
}
