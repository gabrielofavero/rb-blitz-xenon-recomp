// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace rb_blitz::settings {
// The guest's own screen size, set by the host before the guest starts.
//
// Two facts measured on the pinned retail image decide the shape of this seam
// (docs/plans/in-game-settings-resolution-findings.md, docs/plans/ultrawide-findings.md):
//
//   * `sub_823F3698` chooses the height itself - 720, or 540 when bit 0 of its own +796 is
//     set - and then *derives* the width from that height, as `height * 16 / 9` or
//     `height * 4 / 3`. The aspect is therefore not a parameter the title has: a width that
//     is not 16:9 (or 4:3) of the height only exists if the host puts it there.
//   * The frame the title allocates, and the layout it builds on top of it, both come out of
//     that one function, so the values have to be in registers while it runs - poking the
//     fields afterwards does not move the frame (measured, P2).
//
// `SetBootRenderFrame` is the product seam: the boot computes the guest size for the
// requested render size and hands it over once, before the title builds its screen object.
// Both values are plain atomics the hooks read; nothing here touches the cvar registry, the
// store or a file, because the hooks run on the guest thread.
//
// A width of 0 means "the guest derives it" - the shipping behaviour for the 16:9 sizes, and
// the reason the mechanism can be added without changing an existing boot.
void SetBootRenderFrame(uint32_t width, uint32_t height);
void SetBootRenderHeight(uint32_t height);
uint32_t BootRenderWidth();
uint32_t BootRenderHeight();

// The other half of a non-16:9 frame: the space the title lays its UI out in.
//
// The title does not derive that space from the frame. Two 9/16 floats in its own image (guest
// 0x8201E4CC and 0x8201E4D0, the literal pool of the screen-space mapping) are what maps a layout
// x into the 16:9-shaped space, so a wider frame is drawn *stretched* into it. Measured
// 2026-10-12, one boot each way, frame 1680x720: with the pair left alone a UI disc came out
// 1.381x as wide as it is tall (58x42 capture pixels) - exactly the 1680/1280 the frame grew by -
// and with the pair at 1/2.3333 the same disc is round (44x42, the 16:9 reference's 44x42).
//
// This writes 1/aspect into both, once the frame is one the title would not have chosen by itself.
// A 16:9 (and the title's own 4:3) frame is left completely alone. `base` is the guest memory
// base the caller's hook received.
void WriteGuestLayoutAspect(uint8_t* base);
}
