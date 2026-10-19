// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// A host-authored pad press for the in-game Controls surface
// (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md §14.3, P7).
//
// The listening dialog has to leave itself when a capture succeeds, and the plan wants that
// without the player pressing anything. The native pointer's queue is not usable for it: that
// queue follows Mouse Support, and a capture must work with the mouse off. So the host raises a
// real pad bit for two polls, exactly the delivery a player's press gets - the guest's own
// `kAction_Cancel` runs, the dialog's Back target takes over, and no synthetic confirmation is
// invented.
//
// One pulse at a time, by design: an event source that could stack pulses would let a fast
// capture leave two dialogs, and a release that never lands would leave the guest holding B.

#pragma once

#include <cstdint>

namespace rb_blitz::input {

// Raises `bits` in the next polls the state filter delivers for player one: two polls with the
// bits set, then one without (the same shape the native pointer's own A/B pulse uses).
void PulseHostPad(uint16_t bits);

// The bits to add to this poll, or 0. Ages the pulse; only the player-one filter calls it.
uint16_t TakeHostPadPulse();

}  // namespace rb_blitz::input
