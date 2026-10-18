// SPDX-License-Identifier: GPL-2.0-only
//
// P7's host-side tests (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md §14):
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
  options.custom_controller = false;
  options.custom_keyboard_mouse = true;
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

  // The Controller page's custom switch saves and is queryable before and after.
  CHECK_TRUE(store->StageBool("input.custom_controller", true, &error));
  CHECK_TRUE(store->Matches("input.custom_controller", true));
  CHECK_TRUE(store->Commit(&error));
  CHECK_TRUE(store->Matches("input.custom_controller", true));
  CHECK_TRUE(Slurp(path).find("gameplay_mapping_controller = true") != std::string::npos);

  rb_blitz::input::InputStoreOptions locked = options;
  locked.keyboard_locked = true;
  auto other = rb_blitz::input::InputStore::Load(path, rb_blitz::profile::LoadProfile(path),
                                                 locked);
  CHECK_FALSE(other->StageBool("input.keyboard_support", false, &error));
  CHECK_TRUE(error.find("command line") != std::string::npos);
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
  // The caption describes the row's real list.
  CHECK_TRUE(store->Describe("settings_bind_controller_first_note").find("pad:a, pad:b") !=
             std::string::npos);
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
  TestStoreCaptureAssignment();
  std::printf("%s: %d check(s), %d failure(s)\n",
              rb_blitz::test::g_failures == 0 ? "PASS" : "FAIL", rb_blitz::test::g_checks,
              rb_blitz::test::g_failures);
  return rb_blitz::test::g_failures == 0 ? 0 : 1;
}
