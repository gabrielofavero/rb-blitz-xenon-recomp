# Display modes and capture recovery

Investigation, 2026-10-25, following a report of lasting performance loss after
opening Windows screen capture in Borderless mode.

The latest local run, `rb_blitz_336.log`, used D3D12 with `fullscreen=false` and
`borderless=true`. The SDL implementation implements Borderless by removing the
window decorations and covering `SDL_GetDisplayBounds`, without calling
`SDL_SetWindowFullscreen(true)`. Full Screen uses SDL desktop fullscreen without
requesting a display mode. D3D12 creates a windowed flip-discard swap chain and
disables DXGI's automatic Alt+Enter behavior. Neither option requests DXGI
exclusive fullscreen.

Borderless does not prevent Windows from changing the presentation path. When
other desktop content covers an independently flipping window, the compositor
can change how frames reach the display. See Microsoft's
[flip model documentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model).
The existing log has no present timings or focus-transition records, so it does
not establish the cause of the lasting slowdown.

Two concrete gaps were found: there was no Alt+Enter handler, and focus regain
did not refresh the presentation connection. D3D12 also treated
`DXGI_STATUS_MODE_CHANGED` as ordinary success, though Microsoft's
[DXGI status documentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-status)
calls for resizing the swap-chain buffers after that status.

SDK patch
[`0035`](../../patches/rexglue-sdk/0035-borderless-hotkey-presentation-recovery.patch)
adds:

- Alt+Enter (including keypad Enter) toggles Windowed and Borderless. From Full
  Screen it returns to Windowed. Repeats and the chord's key release are consumed
  before input listeners, so Enter cannot also activate a guest menu item.
- Borderless becomes explicit desired window state. Switching modes preserves
  the windowed client size and position; desktop-sized resize events cannot
  overwrite the desired windowed size. The shortcut changes this session's
  window, leaving the saved Display preference in control of the next launch.
- Focus regain refreshes the presentation connection even with identical
  surface dimensions. D3D12 consequently runs its existing synchronized
  `ResizeBuffers` path; Vulkan recreates its swap chain. Rendering is handed
  back through the existing presenter thread-ownership mechanism.
- D3D12 mode-change status requests reconnection. An in-progress mode change
  skips that presentation attempt and retries on subsequent frames.
- Startup, mode transitions, and focus return log the requested mode, actual
  SDL fullscreen/border flags, and pixel dimensions. Presentation recovery also
  emits a log line.

`display_mode` exercises the production SDL key handler and desired-state
transitions without creating a native window. It covers repeat suppression,
release after Alt, keypad Enter, focus loss mid-chord, and mutual exclusion of
Borderless and Full Screen. It does not validate native geometry or performance.
The release build and CTest's `display_mode`, `window_policy`, and
`settings_graphics` checks passed. The owner chose to perform runtime acceptance
personally.

Runtime acceptance remains required: start Windowed, toggle to Borderless and
back, confirm geometry restoration, then repeat Alt+Tab and the reported capture
shortcut during gameplay and compare performance before and after returning.
The owner must choose who drives that test under `AGENTS.md`. Focus recovery is
a candidate remedy, not a confirmed explanation or verified fix for the reported
capture slowdown.
