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
  Check(tracker.Current().family == Family::kKeyboardMouse, "boot family");
  Check(tracker.Current().scheme == "retail", "boot layout");
  PadState pad{};
  pad.buttons = 0x1000;
  Check(!tracker.Pad(true, pad, 10), "unfocused pad");
  tracker.Focus(true);
  Check(!tracker.Pad(true, pad, 11), "held pad after focus");
  tracker.Pad(true, {}, 12);
  Check(tracker.Pad(true, pad, 20), "physical button edge");
  tracker.Pad(true, {}, 21);
  Check(!tracker.Pad(false, pad, 22), "unconnected synthetic state is ignored");
  tracker.Pad(true, pad, 23);
  Check(!tracker.Pad(true, pad, 30), "held pad repeats");
  Check(!tracker.KeyDown(80), "debounce keyboard");
  Check(!tracker.Tick(119), "pending family waits for debounce");
  Check(tracker.Tick(120), "pending key is not discarded");
  Check(!tracker.KeyDown(130), "same-family key does not repeat switch");
  Check(!tracker.Pad(true, pad, 240), "held pad cannot steal family");
  Check(tracker.Current().family == Family::kKeyboardMouse, "keyboard family");
  tracker.Pad(true, {}, 241);
  pad = {}; pad.left_trigger = 31;
  Check(tracker.Pad(true, pad, 250), "trigger edge");
  tracker.Pad(true, {}, 251);
  Check(!tracker.MouseMove(100, 100, 360), "first mouse position");
  Check(!tracker.MouseMove(101, 102, 361), "tiny mouse motion");
  Check(tracker.MouseMove(106, 109, 362), "cumulative mouse motion");
  tracker.SelectScheme("deck");
  Check(tracker.Current().scheme == "deck", "selected scheme");
  Check(tracker.Current().family == Family::kKeyboardMouse, "scheme does not change family");
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
  Check(tracker.MouseButton(700), "focused mouse button");
  Check(!tracker.MouseWheel(701), "same family wheel");
  Check(tracker.Current().scheme == "deck", "keyboard does not change scheme");

  Tracker cancel;
  cancel.Focus(true);
  Check(cancel.Pad(true, pad, 10), "cancel fixture starts controller");
  Check(!cancel.KeyDown(40), "opposite family becomes pending");
  cancel.Pad(true, {}, 41);
  Check(!cancel.Pad(true, pad, 42), "controller activity cancels pending key");
  Check(!cancel.Tick(200), "cancelled key never switches later");
  Check(cancel.Current().family == Family::kController, "controller remains active");
  Check(cancel.KeyDown(210), "key after controller window switches immediately");

  Tracker blurred;
  blurred.Focus(true);
  Check(blurred.Pad(true, pad, 10), "blur fixture starts controller");
  Check(!blurred.MouseButton(40), "mouse event waits inside debounce window");
  blurred.Focus(false);
  Check(!blurred.Tick(110), "focus loss cancels pending switch");
  Check(blurred.Current().family == Family::kController, "focus loss preserves family");
  std::cout << "last_device: OK\n";
}
