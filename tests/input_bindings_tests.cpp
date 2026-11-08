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
#include "ui/input_icon_rows.h"
#include "ui/input_font_scene.h"

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

void TestExtraButtonCapture() {
  rb_blitz::test::BeginCase("extra SDL button capture and release");
  namespace remap = rb_blitz::profile::remap;
  capture::Arm({"input.controller.first_note", {remap::SourceKind::kPad,
                                                remap::SourceKind::kSdl}});
  auto frame = Frame();
  frame.extra_available = 1;
  frame.extra_held = 1;
  CHECK_FALSE(capture::Observe(frame).captured.has_value());  // opening press
  frame.extra_held = 0;
  CHECK_FALSE(capture::Observe(frame).captured.has_value());  // neutral
  frame.extra_held = 1;
  const auto tick = capture::Observe(frame);
  CHECK_TRUE(tick.captured.has_value());
  CHECK_TRUE(remap::FormatSource(tick.captured->source) == "sdl:button:misc1");
  CHECK_FALSE(capture::Armed());
  capture::Arm({"input.controller.first_note", {remap::SourceKind::kSdl}});
  frame.extra_available = 0;
  CHECK_FALSE(capture::Observe(frame).captured.has_value());
  frame.extra_held = 0;
  CHECK_FALSE(capture::Observe(frame).captured.has_value());
  frame.extra_held = 1;
  CHECK_FALSE(capture::Observe(frame).captured.has_value()); // unavailable source
  capture::Cancel();
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

void TestLibraryHeaderDefaults() {
  rb_blitz::test::BeginCase("library headers: Page Up and Page Down drive trigger actions");
  namespace remap = rb_blitz::profile::remap;
  const auto* previous = remap::DefaultSources(remap::Target::kLeftTrigger);
  const auto* next = remap::DefaultSources(remap::Target::kRightTrigger);
  CHECK_TRUE(previous && previous->size() == 2 &&
             (*previous)[0] == Source("pad:left_trigger") &&
             (*previous)[1] == Source("key:PageUp"));
  CHECK_TRUE(next && next->size() == 2 &&
             (*next)[0] == Source("pad:right_trigger") &&
             (*next)[1] == Source("key:PageDown"));
  const auto keyboard = rb_blitz::input::icons::Capture(
      "retail", rb_blitz::input::icons::Family::kKeyboard);
  const auto controller = rb_blitz::input::icons::Capture(
      "retail", rb_blitz::input::icons::Family::kController);
  for (const auto [name, key] :
       {std::pair{"left_trigger", "Page Up"},
        std::pair{"right_trigger", "Page Down"}}) {
    const rb_blitz::profile::InputRowToken row{
        rb_blitz::profile::InputRowKind::kGeneralBinding, name};
    CHECK_TRUE(rb_blitz::input::icons::First(keyboard, row).legend == key);
    CHECK_TRUE(rb_blitz::input::icons::First(controller, row).source.kind ==
               remap::SourceKind::kPad);
  }
  remap::PadState out;
  remap::Apply(remap::Table{}, {},
      [](void*, const remap::Source& source) { return source == Source("key:PageUp"); },
      nullptr, &out);
  CHECK_TRUE(out.left_trigger == 0xFF && out.right_trigger == 0);
  remap::PadState pad;
  pad.right_trigger = 0xFF;
  remap::Apply(remap::Table{}, pad, nullptr, nullptr, &out);
  CHECK_TRUE(out.left_trigger == 0 && out.right_trigger == 0xFF);
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
  CHECK_TRUE(icons::CaptureActive().active_family == icons::Family::kController);
  const auto pad = icons::Capture("retail", icons::Family::kController);
  const auto all = icons::Ordered(pad, row);
  input->general.Set(remap::Target::kA, {Source("pad:b")});
  CHECK_TRUE(icons::First(pad, row).slot == "y_button");
  CHECK_EQ(all.size(), 4u);
  CHECK_TRUE(all[0].slot == "y_button");
  CHECK_TRUE(all[1].art == icons::Art::kBuiltIn);
  CHECK_TRUE(all[1].legend == "Enter");
  CHECK_TRUE(!all[1].rgba.empty());
  CHECK_TRUE(icons::First(pad, row).slot == "y_button");
  rb_blitz::controller_schemes::Registry packages;
  rb_blitz::controller_schemes::Scheme playstation3;
  playstation3.id = "playstation-3";
  playstation3.buttons["y_button"] = {1, 1, {9, 8, 7, 255}};
  packages.schemes.emplace("playstation-3", std::move(playstation3));
  const auto authored = icons::Capture("playstation-3", icons::Family::kController, &packages);
  CHECK_TRUE(icons::First(authored, row).art == icons::Art::kPackageImage);
  CHECK_TRUE(icons::First(authored, row).rgba == std::vector<uint8_t>({9, 8, 7, 255}));
  rb_blitz::input::last_device::SelectScheme("playstation-3");
  rb_blitz::input::last_device::Focus(true);
  rb_blitz::profile::remap::PadState real_pad{};
  real_pad.buttons = 0x8000;
  CHECK_TRUE(!rb_blitz::input::last_device::Pad(true, real_pad).has_value());
  const auto active = icons::CaptureActive(&packages);
  CHECK_TRUE(active.active_family == icons::Family::kController);
  CHECK_TRUE(active.controller_scheme == "playstation-3");
  CHECK_TRUE(active.device_revision > 0);
  CHECK_TRUE(icons::First(active, row).art == icons::Art::kPackageImage);
  CHECK_TRUE(icons::First(icons::Capture("steam-deck", icons::Family::kController, &packages), row)
                 .diagnostic.find("unavailable") != std::string::npos);
  const auto keyboard = icons::Capture("playstation-3", icons::Family::kKeyboard);
  CHECK_TRUE(icons::First(keyboard, row).legend == "Enter");
  CHECK_TRUE(icons::Ordered(keyboard, row)[1].rgba == all[1].rgba);
  for (const char* scheme : {"retail", "playstation-3", "xbox-series", "playstation-series",
                             "nintendo-switch", "steam-controller", "steam-deck", "custom"}) {
    const auto choice = icons::Capture(scheme, icons::Family::kKeyboard);
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
  const auto game_keys = icons::Capture("retail", icons::Family::kKeyboard);
  CHECK_TRUE(icons::First(game_pad, rb_blitz::profile::gameplay_remap::Action::kFirstNote).slot == "x_button");
  CHECK_TRUE(icons::First(game_keys, rb_blitz::profile::gameplay_remap::Action::kFirstNote).legend == "Q");
  CHECK_TRUE(icons::Ordered(game_keys, rb_blitz::profile::gameplay_remap::Action::kFirstNote).size() == 2);
  auto pad_only = rb_blitz::input::CopyCurrentInput();
  pad_only->general.Set(remap::Target::kA, {Source("pad:y")});
  rb_blitz::input::PublishInput(pad_only);
  CHECK_TRUE(icons::First(icons::Capture("retail", icons::Family::kKeyboard), row).slot ==
             "y_button");

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

  // The loading tip's Power-up image follows Gameplay Mapping, not the
  // interface X target that happens to share its retail glyph.
  auto powerup = rb_blitz::input::CopyCurrentInput();
  powerup->general.Set(remap::Target::kX, {Source("pad:b")});
  powerup->controller.Set(rb_blitz::profile::gameplay_remap::Action::kPowerup,
                          {Source("pad:y"), Source("pad:x")});
  powerup->keyboard_mouse.Set(rb_blitz::profile::gameplay_remap::Action::kPowerup,
                               {Source("key:q"), Source("key:x")});
  rb_blitz::input::PublishInput(powerup);
  const auto powerup_pad = icons::Capture("retail", icons::Family::kController);
  const auto powerup_keys = icons::Capture("retail", icons::Family::kKeyboard);
  CHECK_TRUE(icons::First(powerup_pad, rb_blitz::profile::gameplay_remap::Action::kPowerup).slot == "y_button");
  CHECK_TRUE(icons::First(powerup_keys, rb_blitz::profile::gameplay_remap::Action::kPowerup).legend == "Q");
  CHECK_TRUE(icons::First(powerup_pad, rb_blitz::profile::ParseInputRowToken("input.general.x")).slot ==
             "b_button");

  auto edited = rb_blitz::input::CopyCurrentInput();
  edited->general.Set(remap::Target::kA, {Source("mouse:right"), Source("key:space")});
  rb_blitz::input::PublishInput(edited);
  const auto after = icons::Capture("retail", icons::Family::kKeyboard);
  CHECK_TRUE(after.revision > keyboard.revision);
  CHECK_TRUE(icons::First(after, row).legend == "Space");
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
             "a_button");
  const auto trigger = rb_blitz::profile::ParseInputRowToken("input.general.left_trigger");
  CHECK_TRUE(icons::First(pad, trigger).slot == "left_trigger");
}

void TestHintFallbacks() {
  rb_blitz::test::BeginCase("input hints: mouse, keyboard, controller fallback and none art");
  namespace icons = rb_blitz::input::icons;
  namespace remap = rb_blitz::profile::remap;
  namespace gameplay = rb_blitz::profile::gameplay_remap;
  auto input = std::make_shared<rb_blitz::input::EffectiveInput>();
  input->general.Set(remap::Target::kX,
                     {Source("pad:x"), Source("key:X"), Source("mouse:x1")});
  input->general.Set(remap::Target::kY, {Source("pad:y"), Source("key:Y")});
  input->general.Set(remap::Target::kStart, {Source("pad:start"), Source("key:Space")});
  input->general.Set(remap::Target::kBack, {Source("mouse:wheel_up")});
  input->general.Set(remap::Target::kRightShoulder, {});
  input->controller.Set(gameplay::Action::kPowerup, {Source("pad:y")});
  input->keyboard_mouse.Set(gameplay::Action::kPowerup,
                            {Source("key:Space"), Source("mouse:middle")});
  input->controller.Set(gameplay::Action::kFirstNote, {});
  input->keyboard_mouse.Set(gameplay::Action::kFirstNote, {Source("mouse:right")});
  input->controller.Set(gameplay::Action::kSecondNote, {Source("pad:b")});
  input->keyboard_mouse.Set(gameplay::Action::kSecondNote, {});
  rb_blitz::input::PublishInput(input);
  const auto controller = icons::Capture("retail", icons::Family::kController);
  const auto keyboard = icons::Capture("retail", icons::Family::kKeyboard);
  const auto mouse = icons::Capture("retail", icons::Family::kMouse);
  const auto x = rb_blitz::profile::ParseInputRowToken("input.general.x");
  const auto y = rb_blitz::profile::ParseInputRowToken("input.general.y");
  const auto start = rb_blitz::profile::ParseInputRowToken("input.general.start");
  const auto back = rb_blitz::profile::ParseInputRowToken("input.general.back");
  const auto shoulder = rb_blitz::profile::ParseInputRowToken("input.general.right_shoulder");
  CHECK_TRUE(icons::First(controller, x).source.name == "x");
  CHECK_TRUE(icons::First(keyboard, x).source.kind == remap::SourceKind::kKey);
  CHECK_TRUE(icons::First(mouse, x).source.kind == remap::SourceKind::kMouse);
  CHECK_TRUE(icons::First(mouse, y).source.kind == remap::SourceKind::kKey);
  CHECK_TRUE(icons::First(keyboard, start).source.name == "Space");
  CHECK_TRUE(icons::First(controller, back).source.kind == remap::SourceKind::kMouse);
  CHECK_TRUE(icons::First(mouse, shoulder).art == icons::Art::kUnbound);
  CHECK_EQ(icons::First(mouse, shoulder).width, 32u);
  CHECK_TRUE(icons::First(mouse, shoulder).rgba[3] == 0);
  CHECK_TRUE(icons::First(mouse, shoulder).rgba[(4 * 32 + 4) * 4 + 3] == 255);
  CHECK_TRUE(icons::First(mouse, gameplay::Action::kPowerup).source.name == "middle");
  CHECK_TRUE(icons::First(keyboard, gameplay::Action::kPowerup).source.name == "Space");
  CHECK_TRUE(icons::First(controller, gameplay::Action::kPowerup).source.name == "y");
  CHECK_TRUE(icons::First(controller, gameplay::Action::kFirstNote).source.name == "right");
  CHECK_TRUE(icons::First(mouse, gameplay::Action::kSecondNote).source.name == "b");
  CHECK_TRUE(icons::Hint(mouse, {rb_blitz::profile::InputRowKind::kGeneralBinding, "a"})
                 .source.name == "left");
  CHECK_TRUE(icons::Hint(mouse, {rb_blitz::profile::InputRowKind::kGeneralBinding, "b"})
                 .source.name == "right");
  CHECK_TRUE(icons::SplashStartHint(mouse).source.kind == remap::SourceKind::kMouse);
  CHECK_TRUE(icons::SplashStartHint(mouse).source.name == "left");
  CHECK_TRUE(icons::SplashStartHint(keyboard).source.name == "Space");
  CHECK_TRUE(icons::SplashStartHint(controller).source.name == "start");
  input = rb_blitz::input::CopyCurrentInput();
  input->general.Set(remap::Target::kY, {Source("key:~"), Source("pad:y")});
  rb_blitz::input::PublishInput(input);
  CHECK_TRUE(icons::First(icons::Capture("retail", icons::Family::kKeyboard), y).source.kind ==
             remap::SourceKind::kPad);
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
    const std::string name(remap::PadDirectionName(direction));
    const auto icon = icons::Resolve({remap::SourceKind::kPad,
                                      name}, "retail");
    const std::string slot = name.starts_with("lstick_") ?
        "left_stick_" + name.substr(7) : "right_stick_" + name.substr(7);
    CHECK_TRUE(icon.art == icons::Art::kSchemeSlot);
    CHECK_TRUE(icon.slot == slot);
    CHECK_TRUE(icon.rgba.empty());
  }
  for (const char* name : {"left", "right", "middle", "x1", "x2", "wheel_up", "wheel_down"}) {
    const auto icon = icons::Resolve({remap::SourceKind::kMouse, name}, "retail");
    CHECK_TRUE(icon.art == icons::Art::kBuiltIn);
    CHECK_TRUE(!icon.rgba.empty());
  }
  const auto wheel_up = icons::Resolve({remap::SourceKind::kMouse, "wheel_up"}, "retail");
  const auto wheel_down = icons::Resolve({remap::SourceKind::kMouse, "wheel_down"}, "retail");
  CHECK_EQ(wheel_up.width, 68u);
  CHECK_EQ(wheel_up.height, 92u);
  CHECK_TRUE(wheel_up.rgba != wheel_down.rgba);
  CHECK_TRUE(icons::Resolve({remap::SourceKind::kPad, "steam_back_grip"}, "steam-deck").art ==
             icons::Art::kUnsupported);
  rb_blitz::controller_schemes::Registry registry;
  auto& deck = registry.schemes["steam-deck"];
  deck.schema_version = 2;
  rb_blitz::controller_schemes::InputImage qam;
  qam.source = "sdl:button:misc1";
  qam.image = {8, 8, std::vector<uint8_t>(8 * 8 * 4, 255)};
  deck.inputs.push_back(qam);
  registry.has_keyboard_mouse = true;
  registry.keyboard_mouse.inputs.push_back({"enter", "key:Return", "", "",
      "Enter_Key_Dark.png", {}, {}, {9, 9, std::vector<uint8_t>(9 * 9 * 4, 255)}});
  registry.keyboard_mouse.inputs.push_back({"middle", "mouse:middle", "", "",
      "mouse_middle.png", {}, {}, {68, 92, std::vector<uint8_t>(68 * 92 * 4, 48)}});
  const auto authored_up = icons::Resolve({remap::SourceKind::kMouse, "wheel_up"},
                                           "retail", &registry);
  const auto authored_down = icons::Resolve({remap::SourceKind::kMouse, "wheel_down"},
                                             "retail", &registry);
  CHECK_TRUE(authored_up.art == icons::Art::kBuiltIn);
  CHECK_TRUE(authored_up.rgba != authored_down.rgba);
  CHECK_TRUE(authored_up.rgba != wheel_up.rgba);
  registry.keyboard_mouse.inputs.push_back({"wheel_up", "mouse:wheel_up", "", "",
      "mouse_wheel_up.png", {}, {}, {68, 92, std::vector<uint8_t>(68 * 92 * 4, 91)}});
  registry.keyboard_mouse.inputs.push_back({"wheel_down", "mouse:wheel_down", "", "",
      "mouse_wheel_down.png", {}, {}, {68, 92, std::vector<uint8_t>(68 * 92 * 4, 92)}});
  const auto packaged_up = icons::Resolve({remap::SourceKind::kMouse, "wheel_up"},
                                           "retail", &registry);
  const auto packaged_down = icons::Resolve({remap::SourceKind::kMouse, "wheel_down"},
                                             "retail", &registry);
  CHECK_TRUE(packaged_up.art == icons::Art::kPackageImage);
  CHECK_TRUE(packaged_down.art == icons::Art::kPackageImage);
  CHECK_TRUE(packaged_up.rgba != packaged_down.rgba);
  CHECK_TRUE(icons::Resolve({remap::SourceKind::kSdl, "button:misc1"}, "steam-deck",
                          &registry).art == icons::Art::kPackageImage);
  CHECK_TRUE(icons::Resolve({remap::SourceKind::kKey, "Return"}, "steam-deck",
                          &registry).width == 9);
  CHECK_TRUE(icons::Resolve({remap::SourceKind::kKey, "Return"}, "retail",
                          &registry).width == 9);
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

void TestRestoreWaitsForRestart() {
  rb_blitz::test::BeginCase("whole-settings restore saves controls without applying them live");
  const auto path = ScratchDir() / "launcher.toml";
  int live_switches = 0;
  int publications = 0;
  rb_blitz::input::InputStoreOptions options;
  options.keyboard_support = false;
  options.mouse_support = true;
  options.apply_switch = [&](std::string_view, bool, std::string*) {
    ++live_switches;
    return true;
  };
  options.on_published = [&] { ++publications; };
  auto store = rb_blitz::input::InputStore::Load(path, WriteFixture(path), options);
  std::string error;
  CHECK_TRUE(store->StageBool("input.keyboard_support", true, &error));
  CHECK_TRUE(store->StageResetAll("input.reset.general", &error));
  CHECK_TRUE(store->CommitForRestart(&error));
  CHECK_EQ(live_switches, 0);
  CHECK_EQ(publications, 0);
  const std::string saved = Slurp(path);
  CHECK_TRUE(saved.find("\na = ") == std::string::npos);
  CHECK_TRUE(saved.find("keyboard_support") == std::string::npos);
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

void TestImageRows() {
  std::vector<bool> used_codes(256);
  for (uint32_t i = 0; i < rb_blitz::ui::input_font::kIconCount; ++i) {
    const auto code = rb_blitz::ui::input_font::GlyphCode(i);
    CHECK_TRUE(code != 0 && code < used_codes.size());
    if (code < used_codes.size()) {
      CHECK_FALSE(used_codes[code]);
      used_codes[code] = true;
    }
  }
  rb_blitz::test::BeginCase("input images: native row order, refresh and overflow");
  namespace icons = rb_blitz::input::icons;
  namespace remap = rb_blitz::profile::remap;
  auto snapshot = std::make_shared<rb_blitz::input::EffectiveInput>();
  snapshot->general.Set(remap::Target::kA,
                        {Source("pad:y"), Source("key:Return"), Source("pad:a")});
  auto view = icons::Capture("retail", icons::Family::kController);
  view.input = snapshot;
  const std::vector<std::string> symbols{
      "settings_bind_general_a", "settings_vsync"};
  const std::vector<std::string> fallback{"Confirm: Y, Enter, A", "settings_vsync"};
  const auto page = rb_blitz::ui::input_icon_rows::Build(symbols, fallback, view);
  CHECK_FALSE(page.overflow);
  CHECK_EQ(page.lines.size(), 2u);
  CHECK_TRUE(page.lines[1] == fallback[1]);
  CHECK_EQ(page.icons.size(), 3u);
  CHECK_TRUE(page.icons[0].slot == "y_button");
  CHECK_TRUE(page.icons[1].legend == "Enter");
  CHECK_TRUE(page.icons[2].slot == "a_button");
  CHECK_TRUE(page.lines[0].find("\xC2\x80 \xC2\x81 \xC2\x82") != std::string::npos);
  CHECK_TRUE(page.lines[0] ==
      "Confirm: <alt1>\xC2\x80 \xC2\x81 \xC2\x82</alt1>");

  rb_blitz::test::BeginCase("input images: binding pages share stable glyph slots");
  snapshot->music.Set("next", {Source("key:MediaTrackNext")});
  const std::vector<std::vector<std::string>> panels{
      {"settings_bind_general_a"}, {"settings_bind_music_next"}};
  const auto catalog = rb_blitz::ui::input_icon_rows::BuildCatalog(panels, view);
  CHECK_EQ(catalog.icons.size(), 4u);
  const auto music_page = rb_blitz::ui::input_icon_rows::Build(
      panels[1], {"Next Track: Media Next"}, view, {}, &catalog);
  CHECK_TRUE(music_page.lines[0] == "Next Track: <alt1>\xC2\x83</alt1>");
  snapshot->general.Set(remap::Target::kA,
      {Source("key:Return"), Source("pad:y"), Source("pad:a")});
  snapshot->music.Set("next", {Source("key:MediaTrackPrevious")});
  const auto changed = rb_blitz::ui::input_icon_rows::BuildCatalog(panels, view, &catalog);
  CHECK_EQ(changed.icons.size(), 5u);
  CHECK_TRUE(changed.icons[0].source.name == "y");
  CHECK_TRUE(changed.icons[1].source.name == "Return");
  CHECK_TRUE(changed.icons[4].source.name == "MediaTrackPrevious");
  const auto changed_page = rb_blitz::ui::input_icon_rows::Build(
      panels[1], {"Next Track: Media Previous"}, view, {}, &changed);
  CHECK_TRUE(changed_page.lines[0] == "Next Track: <alt1>\xC2\x84</alt1>");

  // A v2 row must keep physical-source identity through resolution and glyph assignment.
  // In particular, the two SDL extras have no legacy XInput sheet slot to fall back to.
  rb_blitz::controller_schemes::Registry packages;
  rb_blitz::controller_schemes::Scheme deck;
  deck.id = "deck";
  deck.schema_version = 2;
  auto add_image = [](rb_blitz::controller_schemes::Scheme* scheme,
                      const char* source, const uint8_t red) {
    rb_blitz::controller_schemes::InputImage entry;
    entry.source = source;
    entry.id = source;
    entry.image = {1, 1, {red, 0, 0, 255}};
    scheme->inputs.push_back(std::move(entry));
  };
  add_image(&deck, "sdl:button:misc1", 17);
  add_image(&deck, "sdl:button:left_paddle1", 34);
  packages.schemes.emplace(deck.id, std::move(deck));
  rb_blitz::controller_schemes::Scheme xbox;
  xbox.id = "xbox-series";
  xbox.schema_version = 2;
  add_image(&xbox, "pad:start", 85);
  add_image(&xbox, "pad:back", 102);
  packages.schemes.emplace(xbox.id, std::move(xbox));
  packages.has_keyboard_mouse = true;
  packages.keyboard_mouse.schema_version = 2;
  add_image(&packages.keyboard_mouse, "key:Return", 51);
  add_image(&packages.keyboard_mouse, "mouse:left", 68);
  add_image(&packages.keyboard_mouse, "key:MediaTrackNext", 71);
  add_image(&packages.keyboard_mouse, "key:MediaTrackPrevious", 72);
  add_image(&packages.keyboard_mouse, "key:MediaPlayPause", 73);
  snapshot->general.Set(remap::Target::kA,
      {Source("sdl:button:misc1"), Source("key:Return"),
       Source("sdl:button:left_paddle1"), Source("mouse:left")});
  auto v2 = icons::Capture("deck", icons::Family::kController, &packages);
  v2.input = snapshot;
  const auto mixed = rb_blitz::ui::input_icon_rows::Build(symbols, fallback, v2);
  CHECK_FALSE(mixed.overflow);
  CHECK_EQ(mixed.icons.size(), 4u);
  if (mixed.icons.size() == 4) {
    constexpr uint8_t expected_red[] = {17, 51, 34, 68};
    for (size_t i = 0; i < 4; ++i) {
      CHECK_TRUE(mixed.icons[i].art == icons::Art::kPackageImage);
      CHECK_TRUE(mixed.icons[i].slot.empty());
      CHECK_TRUE(mixed.icons[i].rgba ==
          std::vector<uint8_t>({expected_red[i], 0, 0, 255}));
    }
  }
  CHECK_TRUE(mixed.lines[0] ==
      "Confirm: <alt1>\xC2\x80 \xC2\x81 \xC2\x82 \xC2\x83</alt1>");

  const auto retail_start = icons::Resolve(Source("pad:start"), "retail", &packages);
  const auto retail_back = icons::Resolve(Source("pad:back"), "retail", &packages);
  CHECK_TRUE(retail_start.art == icons::Art::kSchemeSlot);
  CHECK_TRUE(retail_back.art == icons::Art::kSchemeSlot);
  CHECK_TRUE(retail_start.slot == "start_button");
  CHECK_TRUE(retail_back.slot == "back_button");
  CHECK_TRUE(retail_start.rgba.empty());
  CHECK_TRUE(retail_back.rgba.empty());
  const auto series_start = icons::Resolve(Source("pad:start"), "xbox-series", &packages);
  CHECK_TRUE(series_start.art == icons::Art::kPackageImage);
  CHECK_TRUE(series_start.rgba == std::vector<uint8_t>({85, 0, 0, 255}));
  const auto media_next = icons::Resolve(Source("key:MediaTrackNext"), "retail", &packages);
  const auto media_previous = icons::Resolve(Source("key:MediaTrackPrevious"), "retail", &packages);
  const auto media_pause = icons::Resolve(Source("key:MediaPlayPause"), "retail", &packages);
  CHECK_TRUE(media_next.art == icons::Art::kPackageImage && media_next.rgba[0] == 71);
  CHECK_TRUE(media_previous.art == icons::Art::kPackageImage && media_previous.rgba[0] == 72);
  CHECK_TRUE(media_pause.art == icons::Art::kPackageImage && media_pause.rgba[0] == 73);
  CHECK_TRUE(media_next.rgba != media_previous.rgba);
  CHECK_TRUE(media_next.rgba != media_pause.rgba);
  CHECK_TRUE(icons::Resolve(Source("key:MediaTrackNext"), "retail").art ==
             icons::Art::kBuiltIn);

  auto rebound = std::make_shared<rb_blitz::input::EffectiveInput>();
  rebound->general.Set(remap::Target::kA, {Source("pad:b"), Source("key:space")});
  view.input = rebound;
  const auto updated = rb_blitz::ui::input_icon_rows::Build(symbols, fallback, view);
  CHECK_EQ(updated.icons.size(), 2u);
  CHECK_TRUE(updated.icons[0].slot == "b_button");
  CHECK_TRUE(updated.icons[1].legend == "Space");
  rebound->general.Set(remap::Target::kA, {});
  const auto empty = rb_blitz::ui::input_icon_rows::Build(symbols, fallback, view);
  CHECK_EQ(empty.icons.size(), 1u);
  CHECK_TRUE(empty.icons[0].art == icons::Art::kUnbound);

  rebound->general.Set(remap::Target::kA, {Source("pad:a")});
  const std::vector<std::string> many_symbols(55, symbols[0]);
  const std::vector<std::string> many_fallback(55, fallback[0]);
  const auto repeated = rb_blitz::ui::input_icon_rows::Build(
      many_symbols, many_fallback, view);
  CHECK_FALSE(repeated.overflow);
  CHECK_EQ(repeated.icons.size(), 1u);
  CHECK_TRUE(repeated.lines.back() == "Confirm: <alt1>\xC2\x80</alt1>");

  std::vector<remap::Source> distinct;
  for (uint32_t i = 0; i < 46; ++i)
    distinct.push_back({remap::SourceKind::kKey, "Key" + std::to_string(i)});
  rebound->general.Set(remap::Target::kA, distinct);
  const auto above_old_limit = rb_blitz::ui::input_icon_rows::Build(symbols, fallback, view);
  CHECK_FALSE(above_old_limit.overflow);
  CHECK_EQ(above_old_limit.icons.size(), 6u);
  CHECK_TRUE(above_old_limit.lines[0].find(" (1/8)") != std::string::npos);
  const auto next_window = rb_blitz::ui::input_icon_rows::Build(
      symbols, fallback, view, {6});
  CHECK_EQ(next_window.icons.size(), 6u);
  CHECK_TRUE(next_window.icons[0].source.name == "Key6");
  CHECK_TRUE(next_window.lines[0].find(" (2/8)") != std::string::npos);
  distinct.clear();
  for (uint32_t i = 0; i < rb_blitz::ui::input_font::kIconCount; ++i)
    distinct.push_back({remap::SourceKind::kKey, "Key" + std::to_string(i)});
  rebound->general.Set(remap::Target::kA, distinct);
  const auto expanded = rb_blitz::ui::input_icon_rows::Build(symbols, fallback, view);
  CHECK_FALSE(expanded.overflow);
  CHECK_EQ(expanded.icons.size(), 6u);
  CHECK_TRUE(expanded.lines[0].find(" (1/30)") != std::string::npos);
  distinct.push_back({remap::SourceKind::kKey, "Key180"});
  rebound->general.Set(remap::Target::kA, distinct);
  const auto last_window = rb_blitz::ui::input_icon_rows::Build(
      symbols, fallback, view, {180});
  CHECK_FALSE(last_window.overflow);
  CHECK_EQ(last_window.icons.size(), 1u);
  CHECK_TRUE(last_window.icons[0].source.name == "Key180");
  CHECK_TRUE(last_window.lines[0].find(" (31/31)") != std::string::npos);
  distinct.resize(9);
  rebound->general.Set(remap::Target::kA, distinct);
  const auto clamped_window = rb_blitz::ui::input_icon_rows::Build(
      symbols, fallback, view, {180});
  CHECK_EQ(clamped_window.window_starts[0], 6u);
  CHECK_EQ(clamped_window.icons.size(), 3u);
  CHECK_TRUE(clamped_window.icons[0].source.name == "Key6");
  CHECK_TRUE(clamped_window.lines[0].find(" (2/2)") != std::string::npos);
  distinct.resize(8);
  rebound->general.Set(remap::Target::kA, distinct);
  const auto eight = rb_blitz::ui::input_icon_rows::Build(symbols, fallback, view, {6});
  CHECK_EQ(eight.window_starts[0], 0u);
  CHECK_EQ(eight.icons.size(), 8u);
  CHECK_TRUE(eight.lines[0].find(" (1/") == std::string::npos);
  distinct.push_back({remap::SourceKind::kKey, "Key8"});
  for (uint32_t i = 9; i <= 180; ++i)
    distinct.push_back({remap::SourceKind::kKey, "Key" + std::to_string(i)});
  rebound->general.Set(remap::Target::kA, distinct);

  // Exhausting the font on one row must not erase unrelated image rows.
  rebound->general.Set(remap::Target::kB, {Source("pad:b")});
  const std::vector<std::string> mixed_symbols{
      "settings_bind_general_b", "settings_bind_general_a"};
  const std::vector<std::string> mixed_fallback{
      "Go Back: B", "Confirm: all 181 bindings"};
  const auto partial = rb_blitz::ui::input_icon_rows::Build(
      mixed_symbols, mixed_fallback, view);
  CHECK_FALSE(partial.overflow);
  CHECK_EQ(partial.icons.size(), 7u);
  CHECK_TRUE(partial.icons[0].slot == "b_button");
  CHECK_TRUE(partial.lines[0] == "Go Back: <alt1>\xC2\x80</alt1>");
  CHECK_TRUE(partial.lines[1].find(" (1/31)") != std::string::npos);

  const auto recovered = rb_blitz::ui::input_icon_rows::Build(
      {mixed_symbols[1], mixed_symbols[0]},
      {mixed_fallback[1], mixed_fallback[0]}, view);
  CHECK_FALSE(recovered.overflow);
  CHECK_EQ(recovered.icons.size(), 7u);
  CHECK_TRUE(recovered.lines[0].find(" (1/31)") != std::string::npos);
  CHECK_TRUE(recovered.lines[1] == "Go Back: <alt1>\xC2\x86</alt1>");
}

}  // namespace

int main() {
  TestImageRows();
  TestEffectiveIcons();
  TestHintFallbacks();
  TestIconSourceCatalog();
  TestMusicBindings();
  TestLibraryHeaderDefaults();
  TestTokenGrammar();
  TestCaptureReleasesFirst();
  TestExtraButtonCapture();
  TestCaptureWrongKind();
  TestCaptureCancelAndTimeout();
  TestCaptureCountdown();
  TestCaptureBindsImmediately();
  TestStoreResetAndSave();
  TestRestoreWaitsForRestart();
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
