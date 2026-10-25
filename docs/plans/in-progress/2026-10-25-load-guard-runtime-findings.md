# Song Load Guard implementation evidence

**2026-10-25 — in progress.** Implements the controller and read-only prerequisites of
[the proposal](../todo/2026-10-13-load-guard.md). Automatic recovery, its setting and its
popup are **not connected**: native cancellation and cleanup still need measurement.
Controller unit tests do not establish that it is safe to cancel the guest loader.

## 1. Current implementation

`src/loading/load_guard.{h,cpp}` is an SDK-free state machine with injected monotonic
milliseconds, per-attempt policy and generation, Off/30s/60s deadlines, completion-before-
commit precedence, latched supported errors, explicit cancellation/cleanup/return/popup
acknowledgements, a ten-second recovery budget, and explicit suspension rebasing. It has
no worker, guest pointers, input injection, exception handler or resource cleanup of its own.
The production guest adapter is deliberately still absent.

`src/loading/load_trace.cpp` supplies pass-through hooks. `song_load_trace=true` is a
restart-scoped, disabled-by-default diagnostic. Configuration is read on the host before
guest launch; wrappers never query cvars or call an additional guest function. The existing
`UIManager::Poll` hook supplies the read-only heartbeat independently of mouse navigation.

| Address | Static ABI / observation | Evidence status |
| --- | --- | --- |
| `0x822C3428` | `r3=LoadingScreen`; setup enter/return, before/after the native body | Static: slot +116 of vtable `0x8203DD64`; earliest selected setup candidate, not yet a proven full-load start |
| `0x822C3878` | `r3=LoadingScreen`; sets native state `+72` to 3 and launches the game load | Static: native body and callers in `0x822C39D0` |
| `0x822C38F8` | `r3=result cell`, `r4=LoadingScreen`, `r5=caller-prepared message`; completion enter/return | Static: original called exactly once; no fabricated messages |
| `0x822C3F48` | `r3=LoadingScreen`, original `r4` retained; unload enter/return | Static: slot +124; clears content subscription, vector, native state and invokes base unload; does not by itself prove all cleanup |
| `0x824B2958` | `r3=UIManager`; observer runs after the original poll | Shared existing strong hook; reads current screen `+44` and transition `+16` |

The trace reports generation, monotonic elapsed time, guest thread, caller, screen and
native state. Bookkeeping is thread-local: cross-thread lifecycle events, if found, must
be reconciled explicitly rather than silently assuming UI ownership. After unload, the
observer stops reading the old screen. These hooks observe; none cancels, suppresses a
late callback, navigates or catches a fault.

## 2. Can't Hold Us package investigation

**Read-only inspection, 2026-10-25.** Local package:
`Macklemore & Ryan Lewis - Can't Hold Us (ft Ray Dalton)`, 22,573,056 bytes,
SHA-256 `e736c981e1ec87577a3d13d37f90efce281c8cb2211327d2c5bd5e630e28d983`.
Ignored extracted inspection inputs are under `out/load-guard/`.

All blocks consumed by the seven file entries matched their stored STFS SHA-1 digests.
No required declared data extent ran off its chain. This checks stored-byte consistency,
not package authenticity, script compatibility or successful game decoding.

| Entry under `songs/` | Declared size | Finding |
| --- | ---: | --- |
| `cantholdus/gen/cantholdus.milo_xbox` | 127,701 | Present, stored ChunkStream header |
| `cantholdus/gen/cantholdus_keep.png_xbox` | 349,588 | Present |
| `cantholdus/gen/cantholdus_weights.bin` | 15,706 | Present |
| `cantholdus/cantholdus.mid` | 929,681 | MThd, 12 MTrk extents covering the file, each ending with end-of-track |
| `cantholdus/cantholdus.mogg` | 20,945,466 | Unencrypted version 10, OggS at declared offset 4,660; Vorbis header declares 10 channels at 44,100 Hz |
| `cantholdus/cantholdus.pan` | 0 | One allocated block with zero logical bytes |
| `songs.dta` | 2,967 | Present; custom metadata declares `game_origin rb4_dlc` and a `context` script accessing `$world`/PostProc |

The MIDI contains the drum/bass/guitar/vocals/harmony/BEAT/VENUE/EVENTS tracks and a
769,290-byte LIPSYNC1 track. Header/extent checks do not validate every event or prove
that Blitz supports this custom conversion. Metadata pans/vols/cores each have ten values,
consistent with the audio channel count. The context script is an investigation lead,
**not an established crash cause**.

Existing boot `out/build/win-amd64-release/logs/rb_blitz_342.log` contains
`STFS failed to read correct block-chain for entry cantholdus.pan, read 0 blocks, expected 1`.
The SDK constructs readable blocks from logical file length and then compares their count
to allocated blocks. Zero length explains this warning without establishing damaged data.
That boot continues opening other packages afterwards; the warning is not evidence of
the owner's song-start failure. No package was modified or blacklisted.

## 3. Runtime test handoff

The owner chose to navigate/test manually and requested that Codex boot the game.
The rebuilt executable was launched with `--song_load_trace=true --log_flush_interval=1`,
using the normal library and preferences. No synthetic input was sent.

Diagnostic executable SHA-256:
`4d05daf521bae40a3e3078ec285ec7d69c06e5a7662cdd599f85997f49bc0d33`.
Active handoff log: `out/build/win-amd64-release/logs/rb_blitz_349.log`.
Unhandled-fault capture: `out/load-guard/manual-fault.log` and, if a fault occurs,
`out/load-guard/manual-fault.dmp`. The debugger passes first-chance access violations
through to the runtime; it supplies diagnostics, never recovery.

Initial sandboxed launches failed before guest launch because normal settings/save paths
were inaccessible (`HostPathDevice::Initialize` filesystem exception). Launching with normal
user access passed that point. This host startup failure is unrelated to song loading.
A temporary debugger attachment was restarted with local symbols to avoid symbol-server
waits. The diagnostic window was confirmed open. None of this is a cancellation test.

Still required from the manual run: failure phase and fault/result, whether guest UI polls
continue, one native B cancellation if possible, cleanup/resource/callback evidence and
subsequent known-good-song playback. Native main-menu transition and popup dispatch remain
unproven. Retail/Ultimate differences, auto-start, explicit suspension lifecycle and recovery
races remain unvalidated in the guest.

## 4. Checks and decision

`cmake --build --preset win-amd64-release` passed. The `load_guard` and `load_trace` CTests
passed. The trace test executes the real wrappers against original-function doubles and
isolated memory, verifying exactly-once forwarding, unchanged arguments and preserved
original results with tracing both off and on. The controller uses fake time, covering both
boundaries, Off, completion at expiry, wait-for-A, stale generations, manual exit, native
failed completion, error/timeout races, late completion, recovery failure and pause rebasing.

**Decision: no-go for enabling automatic guest cancellation yet.** There is no established
recoverable-error boundary and no supported-error table entry. In particular, Can't Hold Us
is not classified as corrupt or guard-recoverable. Finish the manual reproduction and trace
the native cancellation/ownership protocol before wiring the adapter, player setting and
native main-menu popup. No universal fault interception is implemented.
