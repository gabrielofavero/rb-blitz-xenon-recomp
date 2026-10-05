// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The pad as a device: which pads are open, what they are called, and what they are asking for
// (docs/plans/launcher-plan.md A3, D6; the rules themselves live in pad_nav.h).
//
// SDL3 is the only input library here, and it is already linked and initialised (D1, main.cpp's
// SDL_INIT_GAMEPAD): the game itself reads a pad through SDL and the same mapping database, so a
// pad that works in the launcher works in the game, and a second library would be a second set of
// mappings to keep in step. The launcher opens pads for the same reason the game does.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include "nav.h"
#include "pad_nav.h"

namespace rb_blitz::launcher {

// The pads the launcher holds open.
//
// SDL reports a pad's state only through an *opened* handle: SDL_GetGamepads lists what is
// connected, but SDL_GetGamepadFromID answers with nothing for a pad nobody has opened, so asking
// SDL to read a button without opening the pad first silently reads nothing at all and looks
// exactly like a pad that is not being pressed. One object opens them, then, and everything that
// reads a pad asks here - the focus ring (A3) and the remap capture (C5) both do.
//
// Refresh is once a frame and matches the open set against what SDL says is connected, which is
// what makes plugging a pad in mid-session work: the launcher's event pump already belongs to
// ImGui (main.cpp), so there is no device-add event for us to subscribe to, and reading the
// device list costs one allocation a frame.
class PadRegistry {
 public:
  PadRegistry() = default;
  ~PadRegistry();
  PadRegistry(const PadRegistry&) = delete;
  PadRegistry& operator=(const PadRegistry&) = delete;

  // Opens what has appeared since the last call and closes what has gone. Cheap enough for a
  // frame, and idempotent: calling it twice opens nothing the second time.
  void Refresh();

  // The open pads, in the order SDL reports them. Never holds a null entry.
  const std::vector<SDL_Gamepad*>& pads() const { return pads_; }
  std::size_t count() const { return pads_.size(); }

 private:
  std::vector<SDL_Gamepad*> pads_;
};

// One pad's worth of "what is it asking for", in the launcher's vocabulary (pad_nav.h). Exposed
// because the merge is the source's business but the reading is a plain question about SDL.
PadNavState ReadPad(SDL_Gamepad* pad);

// The pad behind the shell's input seam (A3). It merges every open pad into one PadNavState - two
// pads holding Down is one Down, and either may take over mid-session because neither is "the"
// pad, which is all a single-player launcher needs to say about device selection - and then asks
// PadNavModel what that state means. No widget learns that a pad exists: what leaves here is a
// NavAction, the same thing the keyboard produces.
class GamepadNavSource : public NavSource {
 public:
  explicit GamepadNavSource(PadRegistry& pads);

  // Reads the pads and returns this frame's action, or kNone. A pad that has gone is forgotten on
  // the way past, so a direction held when it was unplugged does not leave the ring waiting for a
  // release that can no longer arrive.
  NavAction Poll() override;

  // True when a pad did something on the frame Poll last read, including a press that produced no
  // action because a modal owned the keyboard. The shell reads it to know the pad is the device
  // that last moved (D6's last-device-to-move-wins).
  bool saw_input() const { return saw_input_; }

  // What the bar's hint line calls this pad's buttons (A2), from the pad's own mapping-database
  // labels where there are any. The pad they describe is the last one that was used, so a second
  // pad takes the glyphs over by being pressed. With no pad open they are the 360's letters, so
  // the bar can be drawn without asking whether a pad is there.
  PadButtonNames names() const { return names_; }

  // The name of the pad the labels came from, for the focus trace: "which pad was that?" is the
  // first question a report about a pad asks.
  const std::string& name() const { return name_; }

 private:
  PadRegistry& pads_;
  PadNavModel model_;
  bool saw_input_ = false;
  PadButtonNames names_;
  std::string name_;
  // Which of the registry's pads the labels come from: the last one that asked for something.
  std::size_t label_index_ = 0;
};

}  // namespace rb_blitz::launcher
