// SPDX-License-Identifier: GPL-2.0-only
#include "input/last_device.h"

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
  std::cout << "last_device: OK\n";
}
