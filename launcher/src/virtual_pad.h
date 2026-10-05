// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// A scripted virtual pad, for checking A3 on a desk with no pad on it (--test-pad).
//
// SDL can make a joystick that is not backed by hardware, and the launcher has to be the one to
// press it: a virtual joystick lives inside the process that attached it, and its buttons are set
// by a call that process makes. So this exists - attach a pad after the window is up (which is
// what makes "arrives mid-session" testable), press it on a schedule, take it away again - and
// what comes out is a --focus-log in which the ring's moves and the pad's arrival are readable
// without a controller, a camera or a pair of hands.
//
// It is a diagnostic and not a feature: nothing is attached unless --test-pad names a script, and
// nothing about it is reachable from the UI.
//
// One wrinkle worth stating: SDL reads a pad's raw inputs through the *mapping* it has for that
// pad, and a virtual pad's hardware id matches nothing in the mapping database. So the pad brings
// its own mapping - installed before the pad is opened, because a mapping only reaches an opened
// pad through a reload - which is also what lets `family=` name the pad's family and glyphs.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

namespace rb_blitz::launcher {

class VirtualPad {
 public:
  VirtualPad() = default;
  ~VirtualPad();
  VirtualPad(const VirtualPad&) = delete;
  VirtualPad& operator=(const VirtualPad&) = delete;

  // Sets the schedule up and attaches the pad. The script is a list of steps separated by `;`:
  //
  //   attach, detach        the pad arrives or leaves (it arrives at the start, so a script that
  //                         opens with `detach` is how "it goes away and comes back" is written)
  //   up, down, left, right the D-pad, held for the step's length
  //   a, b, x, y, lb, rb, start, back
  //                         the buttons, held for the step's length
  //   lstick=X,Y            the left stick, held until another step moves it
  //   family=xbox|sony      the family the pad says it is, which decides the glyphs the bar draws
  //
  // A step is `name` or `name:milliseconds`, 120 ms when it does not say; each step begins once
  // the one before has finished and been let go of for 400 ms. False - with the reason in `error`
  // - when a step is not one of these, because a typo in a command line should say so rather than
  // look like a pad that does nothing.
  bool Start(std::string_view script, std::string* error);

  // Presses and releases whatever is due. `now_seconds` is the same monotonic clock the pad source
  // reads (SDL_GetTicks), so a schedule written in milliseconds means milliseconds.
  void Update(double now_seconds);

  bool attached() const { return instance_ != 0; }

  // True once the last step has been played *and let go of*. A script whose last step is a press
  // has to keep being updated until that release lands, or the button stays down for the rest of
  // the session - and a direction held down is a ring that walks by itself, which is exactly what
  // a stuck pad looks like.
  bool finished() const { return next_ >= steps_.size() && !holding_; }

  // Takes the pad away. The destructor does this too, but the destructor runs at the end of main,
  // after SDL_Quit - so a caller that shuts SDL down itself has to say this first, or it is
  // closing a handle SDL has already freed.
  void Stop();

 private:
  // One thing to do. A step is either an event (the pad arrives or leaves) or a state that is held
  // for a while and then let go of, which is what makes a press a press rather than a leftover.
  struct Step {
    enum class Kind { kAttach, kDetach, kButton, kHat, kStick } kind = Kind::kAttach;
    // For kButton and kHat: which one, by SDL's own numbering. For kButton the number is the raw
    // joystick button the pad's mapping binds to the control, which the mapping installed above
    // fixes.
    int index = 0;
    // For kHat and kStick: the value to hold. A hat is an SDL_HAT_* mask, a stick an axis reading.
    int value = 0;
    double seconds = 0.0;
  };

  void Attach();
  void Apply(const Step& step, bool down);

  SDL_JoystickID instance_ = 0;
  SDL_Joystick* joystick_ = nullptr;
  // The `type:`/`face:` fields the pad's mapping carries, which is how a script makes the pad say
  // it is a PlayStation pad (empty for a pad that says nothing, which is a pad whose glyphs fall
  // back to the 360's letters).
  std::string family_;
  std::vector<Step> steps_;
  std::size_t next_ = 0;
  // When the step being held must be let go of, and when the next one may start.
  double release_at_ = 0.0;
  double next_at_ = 0.0;
  bool holding_ = false;
};

}  // namespace rb_blitz::launcher
