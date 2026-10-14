// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The caption table for the pages the merged Help & Options surface **routes into**: the retail
// screens it re-points its own rows at (`av_options`, `tutorials`, `cal_welcome`,
// `controller_config`) and the in-song pause panel's own inline list.
//
// Why there are two tables (owner request, 2026-10-13). Every row of the *generated* surface
// carries its text in src/ui/settings_screen.cpp, because that module owns the row table and the
// DTA it generates publishes the highlighted row's symbol through the settings channel. The pages
// below are not ours: their DTA is retail's, their rows are filled by retail's own `{enter}`
// bodies, and none of them writes a `help` event. For those, the host observes the guest's own UI
// state once per frame (src/hooks/help_observer.cpp) and looks the row up in *this* table:
//
//   * the screen's name and the focused component's name are read from the guest objects;
//   * the selected row is the focused list's logical index, which is also the order the retail
//     record fills its list with - `av_options`' `checkbox.lst` is `set_data (av_overscan
//     av_active_track_volume av_sound_volume av_bass_boost)`, `tutorials`' `options.lst` is the
//     two-element `tutorial_list`, and the in-song pause list is the merged root's own rows;
//   * a page that is not a row list at all gets one **screen-level** caption (calibration's
//     welcome screen explains the flow; the controller mapping is a horizontal picker).
//
// What is deliberately absent, so nobody re-derives it: `cal_welcome`'s mode list comes from a
// *code* provider (`calibration_modes_provider`), so its row order is not readable from the
// records and it gets the screen-level caption instead of a guessed order; `credits` is not a
// setting and gets none.
//
// SDK-free: the resolution rules are pure and pinned by tests without a boot, the same reason
// src/hooks/settings_bridge.* and src/ui/settings_screen.* are separate from their hooks.

#pragma once

#include <string>
#include <string_view>

namespace rb_blitz::ui::help {

// One frame's worth of guest UI state, as the observer read it.
struct Observation {
  // The current screen's object name, e.g. "av_options_screen". Empty when there is no screen.
  std::string_view screen;
  // The focused component's object name, e.g. "checkbox.lst" - meaningful only when `index >= 0`,
  // because the observer only reads the name of a component it has positively identified as the
  // focused *list* of the immediately preceding frame's draw.
  std::string_view focus;
  // The focused list's selected logical row; -1 when the focus is not a list (or none is drawn).
  int index = -1;
  // The Ultimate provider gate, which decides the in-song pause list's row set (five or six).
  bool provider_active = false;
};

// The caption for one frame's observation, or empty when the observation has none. Pure, so the
// tests can walk every supported screen/index pair without a window or a boot.
std::string CaptionFor(const Observation& observation);

// True when the screen is one this table describes. The observer may only clear a caption it
// published when it left one of these *and* the destination is not a guest screen (see below).
bool KnowsScreen(std::string_view screen);

// True when the guest's own generated panels publish captions for that screen (every screen in
// src/ui/settings_screen.cpp, both contexts). A caption must never be cleared when one of these
// becomes current: the guest's own entry event has already published the destination's caption,
// and the observer runs after it in the same frame.
bool IsGuestScreen(std::string_view screen);

// The caption registered for a row's canonical symbol - the guest channel's event carries one of
// these - or empty when there is none. Generated rows resolve through
// `screen::HelpTextFor`; the retail rows named in this table resolve here.
std::string TextForSymbol(std::string_view symbol);

}  // namespace rb_blitz::ui::help
