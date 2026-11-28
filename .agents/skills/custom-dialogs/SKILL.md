---
name: custom-dialogs
description: Design, implement, or review in-game dialogs, search prompts, confirmations, progress panels, and modal sheets in Rock Band Blitz. Use when changing action buttons, backdrop dimming, focus, or dismissal in these surfaces.
---

# Custom dialogs

Reuse the surface that already owns the interaction. For guest dialogs, use
the title's panels, labels, widgets, and scene records. Follow the host overlay
authorization in [AGENTS.md](../../../AGENTS.md) before adding host-drawn UI.

## Visual defaults

- Give blocking dialogs a viewport-wide black backdrop at about 55% opacity
  and a near-opaque dark panel. Keep the backdrop independent of panel size
  and safe-area padding. Keep the dimming similar across transitions such as
  search entry to search progress. Use one backdrop for the visible modal stack.
- Use a shared action height and targets of at least 44 × 44 logical pixels in
  portrait and landscape. Match the visible and clickable bounds. Give paired
  actions equal heights and balanced widths. A single action may fill the row in
  a narrow dialog. Measure guest controls after projection into logical window
  coordinates.
- Keep the panel inside the usable viewport. Measure the actual font and
  longest supported labels. Wrap or scroll long body content while keeping
  actions visible. Keep the field and actions reachable above the platform
  keyboard.
- Distinguish focused, hovered, pressed, and disabled actions. Use labels
  that name the action, such as Search, Cancel, or Back, and match the shortcut
  hints to those labels. Use local backgrounds and allow background input
  around passive captions, toasts, and compact controls.

## Focus and dismissal

- While a modal is open, route keyboard, controller, mouse, and touch to the
  top dialog. Block outside input from reaching the background. A dismissible
  sheet can close on an outside tap that it consumes.
- Focus the text field for search. For a consequential confirmation, put
  initial focus on the safe action when there is a choice. Preserve the
  existing guest action and return routes. Use the existing Back and Cancel
  mappings when the operation supports dismissal.
- Suppress held opening and closing inputs through release, then restore focus
  to the appropriate screen or control. Check guest controller ownership
  separately from ImGui capture, especially for dialogs registered directly
  through `AddDialog`.

## Where to start

- Guest store dialogs already share a 56% shade and full-surface targets in
  portrait. See [scene geometry](../../../src/ui/input_font_scene.cpp),
  [guest layout](../../../src/ui/portrait_menu.h), and
  [native hit testing](../../../src/input/native_menu.cpp). Settings and
  Messages use a shared confirmation scene defined in
  [settings_screen.cpp](../../../src/ui/settings_screen.cpp).
- For existing host dialogs, see [portrait recovery](../../../src/ui/portrait_dialog.cpp)
  for a single action and [Achievements](../../../src/ui/achievements_overlay.cpp)
  for a scrollable browser. [keyboard_theme.cpp](../../../src/ui/keyboard_theme.cpp)
  supplies the search theme. Search layout and input behavior live in the
  [active ReXGlue patch stack](../../../patches/rexglue-sdk-v0.11/README.md).
  Reuse the owning patch for SDK changes and follow the repository's patch
  guidance.

Use the existing tests closest to the affected route. Check narrow portrait
and landscape layouts, long text, visible and clickable bounds, and held
inputs on open and close. For search, check submission and cancellation with
the platform keyboard open where supported. Follow
[AGENTS.md](../../../AGENTS.md) for builds and live UI testing, and report
which views and devices were verified.
