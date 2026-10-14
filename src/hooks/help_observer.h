// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The host-side half of the help captions (owner request, 2026-10-13) for the pages whose DTA is
// **not** ours: the retail screens the merged Help & Options surface routes into (the audio/video
// page, How to Play, the calibration welcome screen, the controller mapping screen) and the
// in-song pause panel's own inline list. Those pages publish no `help` event, so the host watches
// the guest's own UI state once per frame instead:
//
//   * the screen and focus pointers come from the UIManager hook in src/input/native_menu.cpp,
//     which already refreshes them every frame for the mouse navigation;
//   * the selected row is `UIListState::Selected` (0x824E3008) of the focused list - the same
//     measured call the mouse navigation uses - and is only read when the previous frame's draw
//     identified that exact component as the focused *list*, so a slider (which has no list state
//     at +176) is never probed;
//   * the resolution rules and the retail text table are pure and live in src/ui/help_topics.h.
//
// The observer acts only on the screens that table knows and never clears a caption on a screen
// the guest's own panels publish for (see the ownership rule in the .cpp). Everything it derives
// is published through src/ui/help_overlay.h, which is also where the guest's own caption events
// arrive - one overlay, two sources.

#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace rb_blitz::hooks {

// One frame of UIManager state, as the Poll hook saw it. All zero/false when there is no screen.
struct HelpFrameObservation {
  uint32_t screen = 0;        // the current screen object
  uint32_t focus = 0;         // the focused component
  uint32_t focused_list = 0;  // the list last frame's draw identified as focused (0 = none)
  bool allowed = false;       // no transition in flight, and a menu surface (see native_menu.cpp)
};

// Called once per guest frame, from the UIManager::Poll hook, after the frame's input has been
// dispatched. Runs on the guest thread and touches no SDK state beyond the measured reads above.
void HelpObserverTick(PPCContext& ctx, uint8_t* base, const HelpFrameObservation& observation);

}  // namespace rb_blitz::hooks
