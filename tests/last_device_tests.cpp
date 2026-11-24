// SPDX-License-Identifier: GPL-2.0-only
#include "input/last_device.h"
#include "input/start_prompt.h"
#include "input/calibration_text.h"
#include "input/native_menu.h"

#include <cstdlib>
#include <iostream>

using rb_blitz::input::last_device::Family;
using rb_blitz::input::last_device::Tracker;
using rb_blitz::profile::remap::PadState;

namespace {
void Check(bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}

int main() {
#if defined(RB_BLITZ_IOS) || defined(__ANDROID__)
  Check(rb_blitz::input::last_device::Current().using_touch, "process defaults to touch on mobile");
#else
  Check(!rb_blitz::input::last_device::Current().using_touch, "process defaults to physical input on desktop");
#endif
  Tracker tracker;
  Check(tracker.Current().family == Family::kController, "boot controller");
  Check(tracker.Current().scheme == "retail", "boot layout");
  PadState pad{};
  pad.buttons = 0x1000;
  Check(!tracker.Pad(true, pad, 10), "unfocused pad");
  tracker.Focus(true);
  Check(!tracker.Pad(true, pad, 11), "held pad after focus");
  tracker.Pad(true, {}, 12);
  Check(!tracker.Pad(true, pad, 20), "pad keeps boot family");
  Check(tracker.KeyDown(80), "key selects keyboard");
  Check(tracker.Current().family == Family::kKeyboard, "keyboard family");
  Check(!tracker.KeyDown(130), "repeated key keeps keyboard");
  Check(!tracker.MouseButton(150), "mouse event waits for debounce");
  Check(tracker.Tick(180), "deferred mouse event selects mouse");
  Check(tracker.Current().family == Family::kMouse, "mouse family");
  Check(!tracker.MouseWheel(200), "wheel keeps mouse family");
  Check(!tracker.KeyDown(240), "keyboard event waits for debounce");
  Check(tracker.Tick(280), "deferred key selects keyboard");
  Check(tracker.Current().family == Family::kKeyboard, "keyboard after mouse");
  tracker.Pad(true, {}, 400);
  pad = {}; pad.left_trigger = 31;
  Check(tracker.Pad(true, pad, 410), "trigger edge selects controller");
  Check(tracker.Current().family == Family::kController, "controller after key");
  tracker.SelectScheme("deck");
  Check(tracker.Current().scheme == "deck", "selected scheme");
  Check(!tracker.MouseMove(100, 100, 520), "first pointer position");
  Check(!tracker.MouseMove(130, 130, 530), "motion does not select hints");
  Check(tracker.MouseWheel(540), "wheel selects mouse");
  tracker.Focus(false);
  Check(!tracker.KeyDown(650), "unfocused key");
  Check(!tracker.MouseButton(651), "unfocused click");
  Check(tracker.Current().family == Family::kMouse, "focus loss keeps mouse");
  tracker.Focus(true);
  Check(tracker.KeyDown(760), "focused key selects keyboard");
  Check(tracker.Current().scheme == "deck", "family changes preserve scheme");

  Tracker cancel;
  cancel.Focus(true);
  Check(cancel.KeyDown(0), "cancel fixture selects keyboard");
  Check(cancel.MouseButton(210), "cancel fixture selects mouse");
  Check(!cancel.KeyDown(240), "opposite event becomes pending");
  Check(!cancel.MouseButton(242), "mouse activity cancels pending key");
  Check(!cancel.Tick(400), "cancelled key never switches");
  Check(cancel.Current().family == Family::kMouse, "mouse remains active");

  Tracker blurred;
  blurred.Focus(true);
  Check(blurred.KeyDown(0), "blur fixture selects keyboard");
  Check(blurred.MouseButton(210), "blur fixture selects mouse");
  Check(!blurred.KeyDown(240), "key waits inside debounce");
  blurred.Focus(false);
  Check(!blurred.Tick(310), "focus loss cancels pending switch");
  Check(blurred.Current().family == Family::kMouse, "focus loss preserves mouse");

  Tracker touch;
  touch.SelectScheme("deck");
  Check(!touch.Touch(0), "unfocused touch");
  touch.Focus(true);
  Check(touch.Touch(10), "first touch publishes touch source without changing controller hints");
  Check(touch.Current().using_touch, "touch owns touch HUD visibility");
  Check(touch.MouseButton(200), "real click still selects mouse");
  Check(touch.Touch(220), "touch source is immediate while art waits for layout debounce");
  Check(touch.Tick(300), "deferred touch selects controller");
  Check(touch.Current().family == Family::kController, "touch uses controller hints");
  Check(touch.Current().scheme == "deck", "touch preserves controller scheme");
  Check(touch.MouseButton(320), "real click clears touch visibility while art waits for debounce");
  Check(!touch.Current().using_touch, "physical mouse hides touch HUD immediately");
  Check(touch.Touch(330), "touch reclaims visibility and cancels pending mouse layout");
  Check(!touch.Tick(400), "pending mouse layout stays cancelled");
  Check(touch.MouseButton(500), "real click selects mouse after touch");
  Check(touch.Touch(610), "touch restores controller after mouse");
  Check(touch.KeyDown(720), "keyboard still selects keyboard after touch");
  Check(touch.Touch(830), "touch restores controller after keyboard");
  touch.Focus(false);
  Check(!touch.Touch(940), "blurred touch cannot switch layout");
  touch.Focus(true);
  Check(touch.MouseWheel(1050), "real wheel still selects mouse after touch");
  Check(!touch.Current().using_touch, "physical wheel clears touch source");
  touch.Touch(1200);
  Check(!touch.Pad(true, {}, 1300), "idle connected pad does not steal touch source");
  Check(touch.Current().using_touch, "idle pad preserves touch visibility");
  pad = {}; pad.buttons = 0x1000;
  Check(touch.Pad(true, pad, 1400), "physical controller edge clears touch even with same hint family");
  Check(!touch.Current().using_touch, "physical controller hides touch HUD");
  Check(touch.Current().family == Family::kController, "touch and pad share controller art");
  touch.Touch(1600);
  Check(!touch.Pad(true, pad, 1700), "a held controller button cannot steal a new touch source");
  Check(touch.Current().using_touch, "touch stays active against held controller input");
  Check(touch.KeyDown(1800), "keyboard clears touch source");
  Check(!touch.Current().using_touch, "keyboard restores native HUD");

  Tracker mobile(true);
  Check(mobile.Current().using_touch, "mobile launches with touch prompts before focus or a tap");
  mobile.Focus(true);
  Check(!mobile.Pad(true, {}, 0), "connected idle controller preserves mobile launch prompts");
  Check(mobile.KeyDown(10), "keyboard replaces mobile launch prompts immediately");
  Check(!mobile.Current().using_touch, "keyboard restores physical prompts on mobile");
  mobile.Touch(200);
  Check(!mobile.MouseMove(100, 100, 300), "first real mouse position establishes origin");
  Check(!mobile.MouseMove(105, 100, 310), "small mouse jitter keeps touch prompts");
  Check(mobile.MouseMove(120, 100, 320), "real mouse motion leaves touch mode");
  Check(!mobile.Current().using_touch && mobile.Current().family == Family::kMouse,
        "real mouse motion restores mouse prompts");
  Check(!mobile.MouseMove(150, 100, 330), "ordinary mouse motion keeps existing hint policy");

  Tracker emulated;
  emulated.SetMouseAsTouch(true);
  Check(emulated.Current().using_touch, "mouse emulation starts with touch before focus");
  Check(!emulated.MouseMove(0, 0, 0), "unfocused emulated mouse is ignored");
  emulated.Focus(true);
  const auto touch_revision = emulated.Current().revision;
  emulated.MouseButton(100);
  emulated.MouseWheel(200);
  emulated.MouseMove(100, 100, 300);
  emulated.MouseMove(200, 200, 400);
  Check(emulated.Current().using_touch && emulated.Current().family == Family::kController,
        "mouse movement, click and wheel keep touch interface");
  Check(emulated.Current().revision == touch_revision, "continued emulated touch avoids refresh churn");
  const rb_blitz::input::MenuBounds footer{.2f, .85f, .4f, .95f};
  const rb_blitz::input::MenuPoint pointer{.3f, .9f};
  Check(rb_blitz::input::MenuFooterHovered(pointer, footer, true, false),
        "physical mouse hovers footer and preset surfaces while emulating touch");
  Check(emulated.Current().using_touch, "mouse hover keeps emulated touch captions");
  Check(!rb_blitz::input::MenuFooterHovered(pointer, footer, true, true),
        "released touchscreen pointer does not leave a hover behind");
  Check(rb_blitz::input::MenuFooterHovered(pointer, footer, true, true, true),
        "held touchscreen pointer highlights the same surfaces");
  Check(emulated.KeyDown(500), "real keyboard still replaces emulated touch");
  Check(!emulated.Current().using_touch, "keyboard selects physical interface");
  Check(emulated.MouseMove(200, 200, 510), "first mouse movement restores touch immediately");
  Check(emulated.Current().using_touch, "mouse restores touch inside family debounce");
  Check(emulated.Tick(600), "emulated touch commits deferred controller art");
  Check(emulated.Current().family == Family::kController, "emulated touch uses controller art");
  Check(emulated.Pad(true, pad, 700), "real controller still clears emulated touch");
  Check(!emulated.Current().using_touch, "controller selects physical interface");
  Check(emulated.MouseWheel(800), "emulated wheel restores touch from controller");
  emulated.Focus(false);
  Check(!emulated.MouseButton(900), "unfocused emulated click is ignored");
  emulated.Focus(true);
  emulated.SetMouseAsTouch(false);
  Check(emulated.MouseButton(1000), "disabling emulation restores physical mouse clicks");
  Check(!emulated.Current().using_touch && emulated.Current().family == Family::kMouse,
        "ordinary mouse policy survives emulation");

  using rb_blitz::input::TouchStartPrompt;
  Check(TouchStartPrompt("PRESS <alt1>\xC2\x80</alt1> TO START") == "TOUCH TO START",
        "splash removes private button glyph");
  Check(TouchStartPrompt("PRESS <alt1>A</alt1>TO BEGIN") == "TOUCH TO BEGIN",
        "loading inserts missing retail glyph spacing");
  Check(TouchStartPrompt("Press START to continue") == "Touch to continue",
        "continue preserves sentence casing");
  Check(TouchStartPrompt("Press <alt1>\xC2\x8D</alt1> to continue") == "Touch to continue",
        "continue removes remapped Start glyph");
  Check(TouchStartPrompt("PULSA <alt1>A</alt1>PARA EMPEZAR") == "TOCA PARA EMPEZAR",
        "Spanish keeps original action");
  Check(TouchStartPrompt("PRESSIONE <alt1>A</alt1> PARA COMEÇAR") == "TOQUE PARA COMEÇAR",
        "Portuguese keeps original action");
  Check(TouchStartPrompt("DRÜCKE <alt1>A</alt1>, UM ZU BEGINNEN") == "TIPPE, UM ZU BEGINNEN",
        "German keeps punctuation");
  Check(TouchStartPrompt("DRÜCKE <alt1>A</alt1>") == "TIPPE", "German splash has no added action");
  Check(TouchStartPrompt("PREMI <alt1>A</alt1> PER INIZIARE") == "TOCCA PER INIZIARE",
        "Italian keeps original action");
  Check(TouchStartPrompt("APPUIE SUR <alt1>A</alt1> POUR DÉMARRER") == "TOUCHE POUR DÉMARRER",
        "French keeps original action");
  Check(TouchStartPrompt("PRESS <alt1>A") == "PRESS <alt1>A", "malformed markup is preserved");
  Check(TouchStartPrompt("Loading...") == "Loading...", "other text is preserved");
  Check(TouchStartPrompt("Press <alt>X</alt> to double all score multipliers.") ==
        "Touch to double all score multipliers.", "retail powerup button markup also supports touch");
  const std::string jackpot = "Press <alt1>\xC2\x86</alt1> to start a 3X point Jackpot. "
      "Play perfectly to win it all, but miss one note and you'll lose the Jackpot! (Each use requires Energy.)";
  const std::string touch_jackpot = "Touch to start a 3X point Jackpot. "
      "Play perfectly to win it all, but miss one note and you'll lose the Jackpot! (Each use requires Energy.)";
  rb_blitz::input::StartPromptText powerup;
  Check(powerup.Update(jackpot, true) == touch_jackpot,
        "powerup touch text preserves the complete explanation without a button glyph");
  Check(powerup.Update(touch_jackpot, false) == jackpot,
        "switching input while reading restores the original powerup instruction");
  rb_blitz::input::StartPromptText prompt;
  const std::string original = "PRESS <alt1>A</alt1>TO BEGIN";
  Check(prompt.Update(original, false) == original, "physical boot preserves prompt exactly");
  Check(prompt.Update(original, true) == "TOUCH TO BEGIN", "visible prompt switches to touch");
  Check(prompt.Update("TOUCH TO BEGIN", true) == "TOUCH TO BEGIN", "touch remains stable across frames");
  Check(prompt.Update("TOUCH TO BEGIN", false) == original, "physical input restores exact original prompt");
  Check(prompt.Update("TOUCH TO BEGIN", false) == original,
        "another label with the same token also restores its button prompt");
  Check(prompt.Update(original, true) == "TOUCH TO BEGIN", "touch can switch back repeatedly");
  Check(prompt.Update("Press START to continue", true) == "Touch to continue",
        "guest reassignment replaces saved text even at the same address");
  Check(prompt.Update(original, true) == "TOUCH TO BEGIN", "scene reload captures the new label");
  Check(prompt.Update("TOUCH TO BEGIN", false) == original, "scene reload restores its own original");
  using rb_blitz::input::TouchCalibrationInstruction;
  for (const auto& [physical, touch] : std::array<std::pair<std::string_view, std::string_view>, 7>{{
      {"Hear a drum hit, press the <alt1>A</alt1> button.", "Hear a drum hit, tap the screen."},
      {"Needle hits either side, press the <alt1>\xC2\x81</alt1> button.", "Needle hits either side, tap the screen."},
      {"Drücke jedes Mal die <alt1>A</alt1>-Taste, wenn du einen Trommelschlag hörst.",
       "Tippe jedes Mal auf den Bildschirm, wenn du einen Trommelschlag hörst."},
      {"pulsa el botón <alt1>A</alt1>.", "toca la pantalla."},
      {"appuie sur la touche <alt1>A</alt1> quand l'aiguille touche chaque côté de la jauge.",
       "touche l'écran quand l'aiguille touche chaque côté de la jauge."},
      {"premi <alt1>A</alt1>.", "tocca lo schermo."},
      {"pressione o botão <alt1>A</alt1>.", "toque na tela."}}})
    Check(TouchCalibrationInstruction(physical) == touch, "calibration preserves localized timing and removes the button");
  Check(TouchCalibrationInstruction("Drücke jedes Mal die <alt1>A</alt1> -Taste, wenn die Nadel ausschlägt.") ==
        "Tippe jedes Mal auf den Bildschirm, wenn die Nadel ausschlägt.", "German video prompt accepts its authored spacing");
  Check(TouchCalibrationInstruction("press the <alt1>A button.") == "press the <alt1>A button.",
        "malformed calibration markup is preserved");
  rb_blitz::input::CalibrationPromptText calibration;
  const std::string audio = "Hear a drum hit, press the <alt1>A</alt1> button.";
  const auto tapped = calibration.Update(audio, true, false);
  Check(tapped == "Hear a drum hit, tap the screen.", "touch audio replaces button wording");
  Check(calibration.Update(tapped, false, false) == audio, "controller restores original calibration wording");
  Check(calibration.Update("pressione o botão <alt1>A</alt1>.", true, false) == "toque na tela.",
        "locale changes refresh calibration wording");
  rb_blitz::input::CalibrationPromptText result;
  Check(result.Update("Lag Calibration Successful", true, true) == "Lag Calibration\nSuccessful",
        "mobile result has two balanced lines");
  Check(result.Update("Lag Calibration\nSuccessful", true, true) == "Lag Calibration\nSuccessful",
        "mobile result remains stable between draws");
  Check(result.Update("Lag Calibration\nSuccessful", false, true) == "Lag Calibration Successful",
        "physical input restores single-line result");
  std::cout << "last_device: OK\n";
}
