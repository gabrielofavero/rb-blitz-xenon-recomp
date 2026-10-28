// SPDX-License-Identifier: GPL-2.0-only
//
// P7's host-side tests (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md §14):
// the `input.*` row grammar, the listening session's rules (release first, wrong kind keeps
// listening, the 3-second countdown), and the store's one-writer save against a real temporary
// profile.
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
#include "input/input_icons.h"
#include "input/last_device.h"
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

void TestEffectiveIcons() {
  rb_blitz::test::BeginCase("input icons: effective order, family and revision");
  namespace icons = rb_blitz::input::icons;
  namespace remap = rb_blitz::profile::remap;
  auto input = std::make_shared<rb_blitz::input::EffectiveInput>();
  input->general.Set(remap::Target::kA,
                     {Source("pad:y"), Source("key:Return"), Source("pad:a"),
                      Source("key:space")});
  rb_blitz::input::PublishInput(input);
  const auto row = rb_blitz::profile::ParseInputRowToken("input.general.a");
  CHECK_TRUE(icons::CaptureActive().active_family == icons::Family::kKeyboardMouse);
  const auto pad = icons::Capture("retail", icons::Family::kController);
  const auto all = icons::Ordered(pad, row);
  input->general.Set(remap::Target::kA, {Source("pad:b")});
  CHECK_TRUE(icons::First(pad, row).slot == "y");
  CHECK_EQ(all.size(), 4u);
  CHECK_TRUE(all[0].slot == "y");
  CHECK_TRUE(all[1].art == icons::Art::kBuiltIn);
  CHECK_TRUE(all[1].legend == "Enter");
  CHECK_TRUE(!all[1].rgba.empty());
  CHECK_TRUE(icons::First(pad, row).slot == "y");
  rb_blitz::controller_schemes::Registry packages;
  rb_blitz::controller_schemes::Scheme ps3;
  ps3.id = "ps3";
  ps3.buttons["y"] = {1, 1, {9, 8, 7, 255}};
  packages.schemes.emplace("ps3", std::move(ps3));
  const auto imported = icons::Capture("ps3", icons::Family::kController, &packages);
  CHECK_TRUE(icons::First(imported, row).art == icons::Art::kPackageImage);
  CHECK_TRUE(icons::First(imported, row).rgba == std::vector<uint8_t>({9, 8, 7, 255}));
  rb_blitz::input::last_device::SelectScheme("ps3");
  rb_blitz::input::last_device::Focus(true);
  rb_blitz::profile::remap::PadState real_pad{};
  real_pad.buttons = 0x8000;
  CHECK_TRUE(rb_blitz::input::last_device::Pad(true, real_pad).has_value());
  const auto active = icons::CaptureActive(&packages);
  CHECK_TRUE(active.active_family == icons::Family::kController);
  CHECK_TRUE(active.controller_scheme == "ps3");
  CHECK_TRUE(active.device_revision > 0);
  CHECK_TRUE(icons::First(active, row).art == icons::Art::kPackageImage);
  CHECK_TRUE(icons::First(icons::Capture("steam-deck", icons::Family::kController, &packages), row)
                 .diagnostic.find("unavailable") != std::string::npos);
  const auto keyboard = icons::Capture("ps3", icons::Family::kKeyboardMouse);
  CHECK_TRUE(icons::First(keyboard, row).legend == "Enter");
  CHECK_TRUE(icons::Ordered(keyboard, row)[1].rgba == all[1].rgba);
  for (const char* scheme : {"retail", "ps3", "xbox-series", "playstation-series",
                             "nintendo-switch", "steam-controller", "steam-deck", "custom"}) {
    const auto choice = icons::Capture(scheme, icons::Family::kKeyboardMouse);
    CHECK_TRUE(icons::First(choice, row).rgba == icons::First(keyboard, row).rgba);
  }
  CHECK_TRUE(icons::Resolve(Source("key:semicolon"), "retail").art == icons::Art::kBuiltIn);
  const auto unknown_key = icons::Resolve(Source("key:~"), "retail");
  CHECK_TRUE(unknown_key.art == icons::Art::kUnsupported);
  CHECK_TRUE(!unknown_key.diagnostic.empty());
  auto gameplay = rb_blitz::input::CopyCurrentInput();
  gameplay->controller.Set(rb_blitz::profile::gameplay_remap::Action::kFirstNote,
                           {Source("pad:x"), Source("pad:y")});
  gameplay->keyboard_mouse.Set(rb_blitz::profile::gameplay_remap::Action::kFirstNote,
                                {Source("key:q"), Source("mouse:left")});
  rb_blitz::input::PublishInput(gameplay);
  const auto game_pad = icons::Capture("retail", icons::Family::kController);
  const auto game_keys = icons::Capture("retail", icons::Family::kKeyboardMouse);
  CHECK_TRUE(icons::First(game_pad, rb_blitz::profile::gameplay_remap::Action::kFirstNote).slot == "x");
  CHECK_TRUE(icons::First(game_keys, rb_blitz::profile::gameplay_remap::Action::kFirstNote).legend == "Q");
  CHECK_TRUE(icons::Ordered(game_keys, rb_blitz::profile::gameplay_remap::Action::kFirstNote).size() == 2);
  auto pad_only = rb_blitz::input::CopyCurrentInput();
  pad_only->general.Set(remap::Target::kA, {Source("pad:y")});
  rb_blitz::input::PublishInput(pad_only);
  CHECK_TRUE(icons::First(icons::Capture("retail", icons::Family::kKeyboardMouse), row).art ==
             icons::Art::kUnbound);

  auto preset = rb_blitz::input::CopyCurrentInput();
  preset->controller.Reset(rb_blitz::profile::gameplay_remap::Action::kFirstNote);
  preset->controller.SetControllerPreset(0);
  rb_blitz::input::PublishInput(preset);
  const auto before_preset = icons::First(icons::Capture("retail", icons::Family::kController),
                                          rb_blitz::profile::gameplay_remap::Action::kFirstNote);
  preset = rb_blitz::input::CopyCurrentInput();
  preset->controller.SetControllerPreset(1);
  rb_blitz::input::PublishInput(preset);
  const auto after_preset = icons::First(icons::Capture("retail", icons::Family::kController),
                                         rb_blitz::profile::gameplay_remap::Action::kFirstNote);
  CHECK_TRUE(before_preset.slot != after_preset.slot);

  auto edited = rb_blitz::input::CopyCurrentInput();
  edited->general.Set(remap::Target::kA, {Source("mouse:right"), Source("key:space")});
  rb_blitz::input::PublishInput(edited);
  const auto after = icons::Capture("retail", icons::Family::kKeyboardMouse);
  CHECK_TRUE(after.revision > keyboard.revision);
  CHECK_TRUE(icons::First(after, row).legend == "Right Click");
  CHECK_TRUE(icons::First(keyboard, row).legend == "Enter");
  edited = rb_blitz::input::CopyCurrentInput();
  edited->general.Set(remap::Target::kA, {});
  rb_blitz::input::PublishInput(edited);
  CHECK_TRUE(icons::First(icons::Capture("retail", icons::Family::kController), row).art ==
             icons::Art::kUnbound);
  edited = rb_blitz::input::CopyCurrentInput();
  edited->general.Reset(remap::Target::kA);
  rb_blitz::input::PublishInput(edited);
  CHECK_TRUE(icons::First(icons::Capture("retail", icons::Family::kController), row).slot ==
             "confirm_a");
  const auto trigger = rb_blitz::profile::ParseInputRowToken("input.general.left_trigger");
  CHECK_TRUE(icons::First(pad, trigger).slot == "left_trigger");
}

void TestIconSourceCatalog() {
  rb_blitz::test::BeginCase("input icons: every accepted physical source has a distinct image route");
  namespace icons = rb_blitz::input::icons;
  namespace remap = rb_blitz::profile::remap;
  for (const auto target : remap::Targets()) {
    const auto icon = icons::Resolve({remap::SourceKind::kPad,
                                      std::string(remap::TargetName(target))}, "retail");
    CHECK_TRUE(icon.art == icons::Art::kSchemeSlot || icon.art == icons::Art::kBuiltIn);
    CHECK_TRUE(!icon.slot.empty() || !icon.rgba.empty());
  }
  for (const auto direction : remap::PadDirections()) {
    const auto icon = icons::Resolve({remap::SourceKind::kPad,
                                      std::string(remap::PadDirectionName(direction))}, "retail");
    CHECK_TRUE(icon.art == icons::Art::kBuiltIn);
    CHECK_TRUE(!icon.rgba.empty());
  }
  for (const char* name : {"left", "right", "middle", "x1", "x2", "wheel_up", "wheel_down"}) {
    const auto icon = icons::Resolve({remap::SourceKind::kMouse, name}, "retail");
    CHECK_TRUE(icon.art == icons::Art::kBuiltIn);
    CHECK_TRUE(!icon.rgba.empty());
  }
  CHECK_TRUE(icons::Resolve({remap::SourceKind::kPad, "steam_back_grip"}, "steam-deck").art ==
             icons::Art::kUnsupported);
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

  // D11: the wrong-kind press neither pauses nor resets the count - the countdown's seconds are
  // still drawn beside the sentence.
  CHECK_TRUE(capture::StatusText().find("Closing in") != std::string::npos);

  // A pad press on the very next frame lands: the wrong-kind press does not send the session back
  // to waiting for neutral.
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

void TestCaptureCountdown() {
  rb_blitz::test::BeginCase("capture: the 3-second countdown is the only way out");
  capture::Arm({"input.general.a", {rb_blitz::profile::remap::SourceKind::kPad}});
  // The activation press does not start the clock; neither does holding it.
  capture::Observation activation = Frame();
  activation.pad.buttons = 0x1000;  // A
  CHECK_FALSE(capture::Observe(activation).captured.has_value());
  CHECK_TRUE(capture::StatusText().find("release") != std::string::npos);

  // The neutral frame starts the listening window at 3.
  CHECK_FALSE(capture::Observe(Frame()).captured.has_value());
  CHECK_TRUE(capture::StatusText().find("Closing in 3") != std::string::npos);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  CHECK_TRUE(capture::StatusText().find("Closing in 2") != std::string::npos);
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  CHECK_TRUE(capture::StatusText().find("Closing in 1") != std::string::npos);

  // A wrong-kind press inside the window is feedback only: the count keeps running beside it.
  capture::Observation wrong = Frame();
  wrong.any_key_down = true;
  wrong.new_keys.push_back("G");
  capture::Tick tick = capture::Observe(wrong);
  CHECK_FALSE(tick.captured.has_value());
  CHECK_TRUE(capture::StatusText().find("keyboard key") != std::string::npos);
  CHECK_TRUE(capture::StatusText().find("Closing in 1") != std::string::npos);

  // Three seconds after neutral, the next observation leaves the dialog with nothing bound.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  tick = capture::Observe(Frame());
  CHECK_FALSE(tick.captured.has_value());
  CHECK_TRUE(tick.leave);
  CHECK_FALSE(capture::Armed());
  CHECK_TRUE(capture::StatusText().empty());
  capture::Cancel();
}

// D11: the binding decision does not wait for a release. A press is the answer on its own frame,
// which is also what makes a hold harmless: it binds rather than cancelling.
void TestCaptureBindsImmediately() {
  rb_blitz::test::BeginCase("capture: a valid press binds on its own frame, no release needed");
  capture::Arm({"input.general.b", {rb_blitz::profile::remap::SourceKind::kPad}});
  capture::Observation activation = Frame();
  activation.pad.buttons = 0x1000;  // A, the press that opened the page
  CHECK_FALSE(capture::Observe(activation).captured.has_value());
  CHECK_FALSE(capture::Observe(Frame()).captured.has_value());  // neutral starts the count

  // B is still held on the frame it arrives; it is the answer anyway.
  capture::Observation press = Frame();
  press.pad.buttons = 0x2000;
  const capture::Tick tick = capture::Observe(press);
  CHECK_TRUE(tick.captured.has_value());
  CHECK_TRUE(tick.captured->source.name == "b");
  CHECK_FALSE(tick.leave);
  CHECK_FALSE(capture::Armed());

  // The still-held B in the next frame is not a second capture.
  CHECK_FALSE(capture::Observe(press).captured.has_value());
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

// P3 (interface/gameplay mapping restructure plan, D2): the Interface page draws nine rows, but
// Reset All is still the whole `[remap]` table's reset - all sixteen targets, the seven the page
// no longer shows included. This is the store half; the settings-surface suite pins the single
// `bind-reset-all/input.reset.general` path the row writes.
void TestResetAllCoversHiddenTargets() {
  rb_blitz::test::BeginCase("store: Reset All restores all sixteen interface targets");
  const auto dir = ScratchDir();
  const auto path = dir / "launcher.toml";
  rb_blitz::profile::Profile profile;
  for (const auto target : rb_blitz::profile::remap::Targets()) {
    profile.remap.push_back({std::string(rb_blitz::profile::remap::TargetName(target)), "pad:a",
                             rb_blitz::profile::ValueStyle::kBasic});
  }
  std::string error;
  CHECK_TRUE(rb_blitz::profile::SaveProfile(path, profile, &error));
  rb_blitz::input::InputStoreOptions options;
  options.keyboard_support = true;
  options.mouse_support = true;
  auto store = rb_blitz::input::InputStore::Load(path, rb_blitz::profile::LoadProfile(path), options);
  CHECK_TRUE(store != nullptr);
  if (store == nullptr) {
    return;
  }
  CHECK_TRUE(store->Matches("input.reset.general", true));
  // Every target is bound, including the seven the page does not draw.
  CHECK_TRUE(store->Matches("input.general.a", true));
  CHECK_TRUE(store->Matches("input.general.x", true));
  CHECK_TRUE(store->Matches("input.general.left_shoulder", true));
  CHECK_TRUE(store->Matches("input.general.right_trigger", true));
  CHECK_TRUE(store->Matches("input.general.left_thumb", true));
  CHECK_TRUE(store->StageResetAll("input.reset.general", &error));
  CHECK_TRUE(store->Commit(&error));
  for (const auto target : rb_blitz::profile::remap::Targets()) {
    CHECK_FALSE(store->Matches(rb_blitz::profile::GeneralBindingToken(target), true));
  }
  CHECK_FALSE(store->Matches("input.reset.general", true));
  // The profile on disk holds no `[remap]` rows at all - the reset reached the hidden controls,
  // not only the nine rows the page lists.
  CHECK_TRUE(rb_blitz::profile::LoadProfile(path).profile.remap.empty());
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
  rb_blitz::test::BeginCase("store: Add Key appends defaults, Replace restarts a row, Reset restores");
  const auto dir = ScratchDir();
  const auto path = dir / "launcher.toml";
  rb_blitz::input::InputStoreOptions options;
  auto store = rb_blitz::input::InputStore::Load(path, WriteFixture(path), options);
  std::string error;
  // The fixture has no `second_note` row, so that action holds its default (W + left click).
  CHECK_FALSE(store->Matches("input.keyboard_mouse.second_note", true));
  CHECK_TRUE(store->Describe("input.keyboard_mouse.second_note").find("W, Left Click") !=
             std::string::npos);
  CHECK_TRUE(store->Describe("input.keyboard_mouse.second_note").find("Add Key or Replace") !=
             std::string::npos);

  // Add Key preserves the default stack.
  CHECK_TRUE(store->BeginCapture("input.keyboard_mouse.second_note", &error));
  CHECK_TRUE(store->CompleteCapture("input.keyboard_mouse.second_note", Source("key:Z"), &error));
  const std::string once = Slurp(path);
  CHECK_TRUE(once.find("second_note = \"key:W, mouse:left, key:Z\"") != std::string::npos);
  CHECK_TRUE(once.find("wheel") == std::string::npos);
  CHECK_TRUE(store->Describe("input.keyboard_mouse.second_note").find("Custom binding") !=
             std::string::npos);

  // Replace starts the list over, and Add Key appends to it.
  CHECK_TRUE(store->BeginCaptureReplace("input.keyboard_mouse.second_note", &error));
  CHECK_TRUE(store->CompleteCapture("input.keyboard_mouse.second_note", Source("key:Z"), &error));
  CHECK_TRUE(store->BeginCapture("input.keyboard_mouse.second_note", &error));
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
  CHECK_TRUE(store->BeginCaptureReplace("input.keyboard_mouse.move_lane_right", &error));
  CHECK_TRUE(store->CompleteCapture("input.keyboard_mouse.move_lane_right",
                                    Source("mouse:wheel_up"), &error));
  CHECK_TRUE(Slurp(path).find("move_lane_right = \"mouse:wheel_up\"") != std::string::npos);
  CHECK_TRUE(store->Describe("input.keyboard_mouse.move_lane_right").find("Scroll Up") !=
             std::string::npos);
}

void TestFirstControllerBindNeedsConfirmation() {
  rb_blitz::test::BeginCase("store: first pad bind waits for consent and disables preset fallbacks");
  const auto path = ScratchDir() / "launcher.toml";
  rb_blitz::profile::Profile profile;
  std::string error;
  CHECK_TRUE(rb_blitz::profile::SaveProfile(path, profile, &error));
  auto store = rb_blitz::input::InputStore::Load(
      path, rb_blitz::profile::LoadProfile(path), {});
  const auto token = "input.keyboard_mouse.first_note";
  CHECK_TRUE(store->BeginCapture(token, &error));
  CHECK_TRUE(store->CompleteCapture(token, Source("pad:b"), &error));
  CHECK_TRUE(store->HasPendingControllerCapture());
  CHECK_FALSE(store->controller_custom());
  CHECK_TRUE(store->ResolvePendingControllerCapture(false, &error));
  CHECK_FALSE(store->HasPendingControllerCapture());
  CHECK_FALSE(store->controller_custom());
  CHECK_TRUE(store->BeginCapture(token, &error));
  CHECK_TRUE(store->CompleteCapture(token, Source("pad:b"), &error));
  CHECK_TRUE(store->ResolvePendingControllerCapture(true, &error));
  CHECK_TRUE(store->controller_custom());
  CHECK_FALSE(store->HasPendingControllerCapture());
  CHECK_TRUE(Slurp(path).find("first_note = \"pad:b\"") != std::string::npos);
  const auto snapshot = rb_blitz::input::CurrentInput();
  using Action = rb_blitz::profile::gameplay_remap::Action;
  CHECK_TRUE(snapshot->controller.Effective(Action::kSecondNote)->empty());
  CHECK_FALSE(snapshot->keyboard_mouse.IsBound(Action::kFirstNote));
  capture::Cancel();
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

  // P3 (interface/gameplay mapping restructure plan): Interface Mapping names the pad controls
  // after what the interface does, and the token behind the row does not change. The drawn row,
  // the overlay's replacement line and the help caption all read this one spelling.
  using rb_blitz::profile::remap::Target;
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kA) == "Confirm");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kB) == "Go Back");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kY) == "Shortcuts");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kBack) == "Sort");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kStart) == "Search");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kDpadUp) == "Up");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kDpadDown) == "Down");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kDpadLeft) == "Left");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kDpadRight) == "Right");
  // The seven controls the page hides keep the pad's own name (D2).
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kLeftShoulder) == "Left bumper");
  CHECK_TRUE(rb_blitz::profile::InterfaceTargetLabel(Target::kX) == "X");
  const InputRowToken confirm_row = rb_blitz::profile::ParseInputRowToken("input.general.a");
  CHECK_TRUE(rb_blitz::profile::BindingRowText(confirm_row, &list) ==
             "Confirm: Q, Right Click");
  // The token is still the target's own: a profile written before the rename binds the same row.
  CHECK_TRUE(confirm_row.kind == InputRowKind::kGeneralBinding);
  CHECK_TRUE(confirm_row.name == "a");
}

void TestMusicBindings() {
  rb_blitz::test::BeginCase("music actions persist independently, rebase, cancel and reset");
  const auto path = ScratchDir() / "music.toml";
  auto load = WriteFixture(path);
  auto store = rb_blitz::input::InputStore::Load(path, load, {});
  store->PublishInitial();
  std::string error;
  CHECK_TRUE(store->IsToken("input.music.next"));
  CHECK_FALSE(store->IsToken("input.music.nope"));
  CHECK_TRUE(store->Describe("input.music.next").find("RB, Media Next") != std::string::npos);
  CHECK_TRUE(store->BeginCapture("input.music.next", &error));
  store->CancelCapture();
  CHECK_FALSE(capture::Armed());
  CHECK_FALSE(store->dirty());
  CHECK_TRUE(Slurp(path).find("[menu_music]") == std::string::npos);
  CHECK_TRUE(store->BeginCaptureReplace("input.music.next", &error));
  CHECK_TRUE(store->CompleteCapture("input.music.next", Source("key:N"), &error));
  CHECK_TRUE(store->BeginCapture("input.music.next", &error));
  CHECK_TRUE(store->CompleteCapture("input.music.next", Source("pad:x"), &error));
  CHECK_TRUE(store->CompleteCapture("input.music.next", Source("pad:x"), &error));
  const auto disk = rb_blitz::profile::LoadProfile(path);
  CHECK_TRUE(disk.usable());
  CHECK_EQ(disk.profile.menu_music.size(), 1u);
  CHECK_EQ(disk.profile.remap.size(), load.profile.remap.size());
  auto reloaded = rb_blitz::input::InputStore::Load(path, disk, {});
  reloaded->PublishInitial();
  CHECK_TRUE(reloaded->Describe("settings_bind_music_next").find("N, X") != std::string::npos);
  CHECK_EQ(rb_blitz::input::CurrentInput()->music.Effective("next")->size(), 2u);
  CHECK_FALSE(reloaded->CompleteCapture("input.music.previous", Source("mouse:left"), &error));
  const auto before = Slurp(path);
  CHECK_TRUE(reloaded->BeginCapture("input.music.next", &error));
  reloaded->CancelCapture();
  CHECK_TRUE(Slurp(path) == before);
  CHECK_TRUE(reloaded->StageReset("input.music.next", &error));
  CHECK_TRUE(reloaded->Commit(&error));
  CHECK_TRUE(reloaded->Describe("input.music.next").find("RB, Media Next") != std::string::npos);
  CHECK_TRUE(rb_blitz::profile::LoadProfile(path).profile.menu_music.empty());
  auto concurrent = rb_blitz::profile::LoadProfile(path);
  concurrent.profile.menu_music = {{"previous", "key:P", rb_blitz::profile::ValueStyle::kBasic},
                                   {"future_action", "future:input", rb_blitz::profile::ValueStyle::kBasic}};
  CHECK_TRUE(rb_blitz::profile::SaveProfile(path, concurrent.profile, &error));
  CHECK_TRUE(reloaded->CompleteCapture("input.music.next", Source("key:N"), &error));
  const auto merged = rb_blitz::profile::LoadProfile(path);
  CHECK_EQ(merged.profile.menu_music.size(), 3u);
  CHECK_TRUE(reloaded->Describe("input.music.previous").find("P") != std::string::npos);
  CHECK_TRUE(Slurp(path).find("future:input") != std::string::npos);
  std::string roundtrip;
  CHECK_TRUE(rb_blitz::profile::ComposeProfile(merged.profile, &roundtrip, &error));
  CHECK_TRUE(roundtrip == Slurp(path));

}

}  // namespace

int main() {
  TestEffectiveIcons();
  TestIconSourceCatalog();
  TestMusicBindings();
  TestTokenGrammar();
  TestCaptureReleasesFirst();
  TestCaptureWrongKind();
  TestCaptureCancelAndTimeout();
  TestCaptureCountdown();
  TestCaptureBindsImmediately();
  TestStoreResetAndSave();
  TestResetAllCoversHiddenTargets();
  TestStoreSwitchAndRecovery();
  TestLegacyCustomKeysAreInert();
  TestStoreCaptureAssignment();
  TestFirstAssignReplacesAndAppends();
  TestFirstControllerBindNeedsConfirmation();
  TestRowDisplayText();
  std::printf("%s: %d check(s), %d failure(s)\n",
              rb_blitz::test::g_failures == 0 ? "PASS" : "FAIL", rb_blitz::test::g_checks,
              rb_blitz::test::g_failures);
  return rb_blitz::test::g_failures == 0 ? 0 : 1;
}
