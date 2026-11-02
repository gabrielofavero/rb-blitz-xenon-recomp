// SPDX-License-Identifier: GPL-2.0-only
#include "input/last_device.h"

#include <cstdlib>
#include <iostream>

using rb_blitz::input::last_device::Family;
using rb_blitz::input::last_device::Tracker;
using rb_blitz::input::last_device::kMouseButtonAndWheelSelectKeyboardLayout;
using rb_blitz::input::last_device::kMouseMotionSelectsKeyboardLayout;
using rb_blitz::profile::remap::PadState;

namespace {
void Check(bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}

int main() {
  Tracker tracker;
  Check(tracker.Current().family == Family::kController, "boot family");
  Check(tracker.Current().scheme == "retail", "boot layout");
  PadState pad{};
  pad.buttons = 0x1000;
  Check(!tracker.Pad(true, pad, 10), "unfocused pad");
  tracker.Focus(true);
  Check(!tracker.Pad(true, pad, 11), "held pad after focus");
  tracker.Pad(true, {}, 12);
  Check(!tracker.Pad(true, pad, 20), "physical button keeps boot controller family");
  tracker.Pad(true, {}, 21);
  Check(!tracker.Pad(false, pad, 22), "unconnected synthetic state is ignored");
  tracker.Pad(true, pad, 23);
  Check(!tracker.Pad(true, pad, 30), "held pad repeats");
  Check(tracker.KeyDown(80), "first physical key selects keyboard family");
  Check(!tracker.Tick(119), "no family switch is pending");
  Check(!tracker.Tick(120), "first key needs no debounce");
  Check(!tracker.KeyDown(130), "same-family key does not repeat switch");
  Check(!tracker.Pad(true, pad, 240), "held pad cannot steal family");
  Check(tracker.Current().family == Family::kKeyboardMouse, "keyboard family");
  tracker.Pad(true, {}, 241);
  pad = {}; pad.left_trigger = 31;
  Check(tracker.Pad(true, pad, 250), "trigger edge");
  tracker.Pad(true, {}, 251);
  Check(!tracker.MouseMove(100, 100, 360), "first mouse position");
  Check(!tracker.MouseMove(101, 102, 361), "tiny mouse motion");
  Check(tracker.MouseMove(106, 109, 362) == kMouseMotionSelectsKeyboardLayout,
        "mouse motion follows layout switch policy");
  Check(tracker.MouseButton(363) ==
        (kMouseButtonAndWheelSelectKeyboardLayout && !kMouseMotionSelectsKeyboardLayout),
        "mouse button selects keyboard art after motion alone");
  Check(!tracker.MouseWheel(364), "same-family or disabled mouse wheel");
  tracker.SelectScheme("deck");
  Check(tracker.Current().scheme == "deck", "selected scheme");
  Check(tracker.Current().family ==
        ((kMouseMotionSelectsKeyboardLayout || kMouseButtonAndWheelSelectKeyboardLayout) ?
             Family::kKeyboardMouse : Family::kController), "mouse follows layout switch policy");
  Check(tracker.KeyDown(365) ==
        !(kMouseMotionSelectsKeyboardLayout || kMouseButtonAndWheelSelectKeyboardLayout),
        "key selects keyboard art when mouse has not already done so");
  tracker.Pad(true, {}, 470);
  pad = {}; pad.left_x = 0x4001;
  Check(tracker.Pad(true, pad, 480), "stick threshold");
  Check(tracker.Current().scheme == "deck", "pad does not change scheme");
  tracker.Focus(false);
  Check(!tracker.MouseButton(600), "unfocused button");
  Check(!tracker.KeyDown(601), "unfocused key");
  Check(!tracker.MouseMove(150, 150, 602), "unfocused motion");
  Check(tracker.Current().family == Family::kController, "focus loss keeps family");
  tracker.Focus(true);
  Check(tracker.MouseButton(700) == kMouseButtonAndWheelSelectKeyboardLayout,
        "focused mouse button follows layout switch policy");
  Check(!tracker.MouseWheel(701), "focused mouse wheel keeps controller art");
  Check(tracker.KeyDown(802) == !kMouseButtonAndWheelSelectKeyboardLayout,
        "focused key selects keyboard art if mouse did not");
  Check(tracker.Current().scheme == "deck", "keyboard does not change scheme");

  Tracker wheel;
  wheel.Focus(true);
  Check(!wheel.Pad(true, pad, 10), "wheel fixture starts controller");
  Check(!wheel.MouseMove(0, 0, 120), "wheel fixture first mouse position");
  Check(wheel.MouseMove(100, 100, 121) == kMouseMotionSelectsKeyboardLayout,
        "pointer motion follows its independent policy");
  Check(wheel.MouseWheel(230) ==
        (kMouseButtonAndWheelSelectKeyboardLayout && !kMouseMotionSelectsKeyboardLayout),
        "wheel input selects keyboard art after pointer movement");
  Check(wheel.Current().family ==
        ((kMouseMotionSelectsKeyboardLayout || kMouseButtonAndWheelSelectKeyboardLayout) ?
             Family::kKeyboardMouse : Family::kController), "wheel layout family");

  Tracker cancel;
  cancel.Focus(true);
  Check(cancel.KeyDown(0), "cancel fixture first selects keyboard");
  Check(cancel.Pad(true, pad, 210), "cancel fixture selects controller");
  Check(!cancel.KeyDown(240), "opposite family becomes pending");
  cancel.Pad(true, {}, 241);
  Check(!cancel.Pad(true, pad, 242), "controller activity cancels pending key");
  Check(!cancel.Tick(400), "cancelled key never switches later");
  Check(cancel.Current().family == Family::kController, "controller remains active");
  Check(cancel.KeyDown(410), "key after controller window switches immediately");

  Tracker blurred;
  blurred.Focus(true);
  Check(blurred.KeyDown(0), "blur fixture first selects keyboard");
  Check(blurred.Pad(true, pad, 210), "blur fixture selects controller");
  Check(!blurred.MouseButton(240), "mouse event cannot switch inside debounce window");
  blurred.Focus(false);
  Check(!blurred.Tick(310), "focus loss cancels pending switch");
  Check(blurred.Current().family == Family::kController, "focus loss preserves family");
  std::cout << "last_device: OK\n";
}
