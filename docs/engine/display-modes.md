# Display modes and capture recovery

## Editable window mode and resizing (2026-11-18)

Render profiles keep the native window size independent from render detail.
Startup fits the native display into the desktop; changing profiles does not
resize an existing window. The active frontbuffer aspect controls fitting, so
720p in an ultrawide window has side bars. See the
[live display correction](../plans/finished/2026-11-18-live-display-profile-fixes.md).

[static] Startup fullscreen configuration selects the initial mode. It no longer
makes Window Mode read External: the existing guest Keep/Revert preview takes
runtime ownership, except when the SDK explicitly freezes or locks the settings.
The boot replaces the old fixed-window policy with resizing, including repair
of an older saved `window_resizable=false`; explicit environment/CLI controls
retain priority. Native host attachment receives that resizable flag.

Resolution profiles also update render targets and presentation aspect at guest
frame boundaries. See [the current profile route](../settings/graphics.md#interface-and-gameplay-profiles-2026-11-18).
Native resize and mode-preview visual acceptance remain unverified.

The [transition integrity follow-up](../plans/finished/2026-11-18-resolution-transition-integrity.md)
moves replacement ahead of native tile capture. A recorded transition and
`rb_blitz_007.log` exposed old resolve rectangles applied to a smaller new
surface. Pause and native song confirmations now retain Gameplay resolution;
only entering Help & Options uses Interface during a paused song. These
corrections still require owner visual acceptance on the rebuilt game.

The subsequent [native-aspect policy](../plans/finished/2026-11-18-native-aspect-resolution-policy.md)
keeps scene proportions fixed for the session. Resolution lists use native
proportions by default; Force 16:9 changes both lists after restarting. Live
quality changes preserve the window size and current aspect. The earlier
camera refresh hooks are retired, since the next run still reported unchanged
proportions. Before releasing old textures, replacement now refreshes native
RndTex wrappers and all matching sampler bindings to avoid dangling pointers.
The loading crash dump records corrupted UIColor teardown; attributing that
specific corruption to a particular writer remains unproven.


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

The display-mode implementation (original 0035, now
[`0016`](../../patches/rexglue-sdk/0016-desktop-display-mode.patch)) adds:

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

## Guest Window Mode preview (2026-11-06)

[tree] The consolidated
[`0016`](../../patches/rexglue-sdk/0016-desktop-display-mode.patch) includes the
original 0048 UI-thread transition/rollback, 0050 startup verification and 0051
UI-only successful-paint counter. A guest-output present that started before the
switch cannot confirm Keep. Original 0049's general present counter was removed
on 2026-11-17 after a complete caller search found no readers; the UI-only
counter remains. The [patch ledger](../../patches/README.md#current-stack-and-lineage-2026-11-17)
records the source-equivalence evidence and all original IDs.
The authored Window Mode row in `src/ui/settings_screen.cpp` now opens a guest
Keep/Revert page. `src/settings/display_runtime.cpp` marks the old mode before switching and
rolls it back on Revert or a 15-second timeout. Keep requires a later successful present and
the requested native mode, then saves through `ServiceSink` and acknowledges the running
mode. Boot consumes an abandoned marker and retries removal after a verified frame.

[measured] A disposable D3D12 run in `out/_swap/hot-settings/runtime-acceptance/` on 2026-11-06
observed all six directed Windowed/Borderless/Full Screen transitions with Keep and an explicit
Revert. The 15-second timeout restored the saved mode and, after a rebuild, the confirmation page
exited through its `display.preview_active` poll. The same six transitions, Revert and timeout
were observed on Vulkan in the disposable profile. Later launches without a CLI display override
restored saved Full Screen on both backends, and input reached Help & Options before a kept return
to Windowed. [measured] On 2026-11-07, the repeated cycles, placement, focus/input after
minimize/restore and Alt+Tab, and a visible Vulkan Full Screen relaunch passed on the interactive
desktop (`out/_swap/hot-settings/c1-2026-11-07/`). Desktop Duplication captured the Vulkan
picture where ordinary captures were black or stale. SDK patch 0054 restored Vulkan menu input
after minimize/restore. [unverified] Other DPI or monitor configurations. The schema now marks Window Mode
immediate through this authored preview/Keep route; direct setting writes cannot bypass it.

## Hosted drawable dimensions (2026-11-18)

The Windows persistent host captures the SDL client rectangle before removing
the child frame and reparenting the window. The attach request carries those
dimensions so the host adds its own decorations exactly once. Measuring after
reparenting counted the former frame as drawable space and caused horizontal
presentation padding. This behavior is shared by D3D12 and Vulkan hosted
windows; other platforms retain their native hosts.

[measured] The v0.11 candidate release build and native window inventory in
`out/rexglue-migration/window-preserved-client-size-r18.json` report a 1712 by
720 captionless child, matching the guest render size. The corresponding title
capture under `post-smoke-ui-final-r18/recovery/fixture/route-shots/00-title.png`
was visually reviewed without the extra top and bottom strips. Other DPI and
monitor configurations remain unverified.
