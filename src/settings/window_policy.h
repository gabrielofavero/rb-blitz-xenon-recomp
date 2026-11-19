// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P2 - the window's own size: the arithmetic that decides how large the game's *window* is, kept
// apart from the render size the guest is asked for.
//
// Two decisions live here, and both are pure:
//
//   1. Fitting. A fixed-size window whose client area is the effective render size can still be
//      larger than the desktop once it has a title bar and borders - and on a display whose mode
//      the render size equals exactly (2560x1080 on this machine's 21:9 panel) it always is. So the
//      decorated window is fitted into the display's *usable* area (the work area: no taskbar, no
//      dock), preserving the requested aspect. Aspect preservation is the whole point: shrinking
//      the window is fine because the presenter scales the image, but changing the shape would put
//      back the black bars this project removed.
//   2. Unit conversion. SDL creates Cocoa windows in points and Windows windows in pixels.
//      The desktop and frame measurements use backing pixels. The SDK converts
//      startup dimensions to the native window units once at its provider boundary.
//
// What is deliberately *not* here: which display is the target, whether the row is fullscreen,
// borderless or windowed, and where the frame metrics come from. Those are the boot's - and the
// SDK's, for the window mode itself, which is not this project's to define.
//
// The measured facts this shape rests on are in docs/settings/graphics.md (part 3, custom
// resolutions): the render path renders any aspect correctly once the frame, the layout space and
// the presenter all agree, and the presenter derives its output shape from the *video mode* aspect
// (part 1 Ã‚Â§3), which is why the boot writes the video mode beside the window size.

#pragma once

#include <cstdint>

#include "settings/model.h"

namespace rb_blitz::settings {

// The desktop a windowed game window has to live in, as the host measured it. Everything is in
// *physical* pixels: that is the space SDL creates a window in on Windows and the space the Win32
// frame metrics are expressed in.
struct DesktopMetrics {
  // False when nothing could be measured (an unsupported host, or a failed monitor query).
  // Nothing is fitted then: a window is created at exactly the size that was asked for, which is
  // what a build without this information has always done.
  bool available = false;
  // The display's usable area (work area): screen minus the taskbar or dock.
  DisplayMode work_area;
  // The window decorations: what the decorated window adds to the client area
  // (`window size - client size` at this scale).
  DisplayMode frame_extent;
  // The display scale in percent (`dpi * 100 / 96`): 100 is 1:1, 150 is Windows' 150%.
  uint32_t scale_percent = 100;
};

// The client area a fixed-size window should ask for.
struct ClientFit {
  DisplayMode client;   // physical pixels, aspect-preserved
  bool shrunk = false;  // true when the requested size did not fit and `client` is smaller
};

// The smallest client area this project asks for. Below this the picture is not playable, so the
// fit refuses to go smaller: an absurdly small work area produces this floor rather than a sliver.
inline constexpr DisplayMode kMinClientSize{640, 360};

// Fits `requested` (the effective render size, physical pixels) into `desktop`. The requested size
// is returned unchanged when the decorated window fits as asked - the common case, and the reason a
// 16:9 boot at a size the display can hold is untouched. Otherwise the largest size of the same
// aspect that fits is returned.
//
// With no measurement (`desktop.available == false`) the request is returned unchanged: an
// unmeasured desktop is not evidence that the request does not fit.
ClientFit FitClientToDesktop(const DisplayMode& requested, const DesktopMetrics& desktop);

// Equivalent aspect within the SDK video-mode cvar bounds (640x480..4095x4095).
// Only describes presentation; it never changes the guest render allocation.
DisplayMode PresentationVideoMode(const DisplayMode& render);
// Seed profile-based windows independently of quality tiers. Portrait follows
// the profile's aspect basis; landscape keeps the native display window.
DisplayMode ProfileWindowSource(DisplayMode native, DisplayMode profile_basis, bool portrait);
// The upright viewport cropped from a landscape source, retaining its full height.
DisplayMode PortraitCropSize(const DisplayMode& source);

// Backing pixels -> layout points for a host window that uses point coordinates.
// Rounded to nearest, never below 1. `scale_percent` is `dpi * 100 / 96`, so 100 is the identity.
DisplayMode PhysicalToLogical(const DisplayMode& physical, uint32_t scale_percent);

// True when the two sizes share an aspect within `tolerance_per_mille`. Used to report whether the
// window really shows the render size's shape, so a mismatch is visible in the log rather than
// inferred from a screenshot.
bool SameAspect(const DisplayMode& lhs, const DisplayMode& rhs, int32_t tolerance_per_mille = 1);

}  // namespace rb_blitz::settings
