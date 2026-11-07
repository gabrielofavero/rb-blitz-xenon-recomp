# Large DLC startup latency, 2026-11-07

The owner's 1,777-package library produced a long black startup window. The
latest logs separate two costs:

| Log | Flat-library header scan | Guest launch to content discovery |
| --- | ---: | ---: |
| `rb_blitz_566.log` | 160 ms | 13 s |
| `rb_blitz_569.log` | 157 ms | 40 s |
| `rb_blitz_570.log` | 32,092 ms | Discovery had not begun after 90 s; the window was closed |

`rb_blitz_570.log` spent 32 seconds reading the library before the guest could
launch. After launch, the background store worker encoded covers continuously
while the game was still trying to open its first screen. This is a competing
startup workload, not part of the title's content discovery.

The first mitigation started the flat-library scan in `OnPreSetup`, after settings
and paths were final and before graphics presentation setup. At that point,
`dlc::Configure` still waited for and published the result before guest launch.
The log reported both scan time and the time `Configure` actually waited. The startup catalogue
worker no longer queues every cover; serving a store page still requests its
covers, and selection still promotes its own cover request.

The release build succeeded and eight focused headless DLC, discovery, menu
music, and store tests passed. These checks establish compilation and the
existing content paths. A live large-library boot is needed to measure the
remaining black interval and verify that the native discovery screen appears.

## Manual retest and crash

The owner's next boot (`rb_blitz_571.log`) remained black for roughly 40 seconds
between function-table creation and SDL's controller log. The flat DLC scan took
39,932 ms in parallel but `dlc::Configure` waited only 0.005 ms for it. The same
40-second gap appeared in warm-scan boots, so the scan is not its sole cause.
SDK patch `0052` logs function registration and SDL gamepad initialization
separately so the next boot can assign that gap to the actual stage.
The headless `rb_blitz_function_registration_probe` registered 38,450 mappings
in 37.1 ms, so registration alone does not explain a 40-second pause. The
probe does not include the graphical runtime or a real title window.

Boots `569` and `571` also ended at the same guest write to read-only image
address `0x8200DAD8` just after their first enumeration updates. The content
progress hook had called the guest's Flow push function on the enumeration
workers, concurrently with the main guest UI traversal. It now records counts
on those workers and applies the latest percent from the main guest poll. This
removes that concurrent guest UI mutation; a live retest is needed to establish
whether it eliminates the crash.

## Large-library retest and follow-up

The owner's next run, `rb_blitz_572.log`, reached the main menu without that
crash, but the pre-launch window was still black and could be marked Not
Responding. Its timings identify the costs:

| Stage | Elapsed |
| --- | ---: |
| Flat-library header scan | 92.0 s, including 81.2 s blocked in `dlc::Configure` |
| Controller-scheme discovery | 65.9 s, opening and decoding 1,169 PNGs |
| Menu payload generation | 7.4 s |
| Guest launch to first native splash | 55.3 s |
| Native content discovery to usable menu | 66.1 s |

Function registration and SDL gamepad initialization together took about two
seconds in that run. A headless function-registration probe and the `0052` SDK
stage timings agree that the earlier unexplained gap was not the function table.

The flat library now stores raw package identities in a checksum-protected
cache under the runtime cache root, keyed by path, size and modification time.
The boot walk still notices added, changed and removed packages. On a first
boot without that cache, header I/O starts at the guest's native discovery
state, before it creates the content enumerators. The native panel continues
polling and drawing while the worker scans. A quick pre-launch directory walk
supplies candidate paths to the menu-music metadata catalog; only validated
packages reach the content manager. A cached scan starts during graphics setup
and is published before launch when ready, or at the same native barrier when
still running. This removes the 81-second UI-thread wait measured in 572.

The bundled controller artwork is packed into one cache file per scheme at
build time. A headless discovery of all twelve packed schemes took 2.43 s;
the files total 8.0 MB. User-installed schemes continue to validate from their
source files. Detailed per-bitmap launch logging is now opt-in, with bounded
texture-load timing summaries left on. SDL pumps window messages between the
remaining pre-launch stages and during menu payload generation.

The saved menu-music catalog now validates package paths on a host worker
during graphics setup. If validation is still running when the guest is ready,
startup continues and the music worker publishes the catalog before selecting
a library song. The generated settings menu archive is a per-process temporary
file: its 55 MB write now checks that the stream closed before mounting it,
without forcing a durable disk flush before launch. The next boot log separates
menu record build, archive assembly and archive write time, and reports bounded
guest file-read and DTB-scan totals during the remaining pre-splash interval.

The full release build and ten focused headless tests pass. The new binary
still needs the owner's live large-library run to measure the black interval,
verify that the native discovery panel keeps drawing during a first scan, and
check the warm-cache path. The separate 66-second native content discovery
remains to be measured after this change.

## Native discovery's frame-paced cost

In `rb_blitz_572.log`, the title received 3,554 successful aggregate items from
two 1,777-item enumerators. Discovery began at 11:03:00.437, the last item
arrived at 11:03:59.938, and the usable menu appeared at 11:04:06.492.
That is roughly one item per rendered frame, plus 6.6 seconds after enumeration.
The host had already set V-Sync to false and Vulkan selected immediate
presentation mode; this run still averaged about 60 item requests per second.

The SDK's XAM handler copies one 328-byte aggregate item into the title's
fixed 512-byte reply buffer. Two items require 656 bytes. An earlier
four-item batch experiment confirmed that the title keeps this buffer at 512
bytes, so increasing the enumerator's advertised batch size cannot safely
reduce the call count. The connect panel's state-29 poll (`0x822CB618`) only
tests whether the content manager's `+44` field is 1 via `0x82375048` and
advances the panel after completion. Repeating that poll within a frame would
not issue another XAM request and could repeat transition side effects. The
66-second run therefore cannot be shortened by XAM batching or repeated panel
polling; changes to the guest's request schedule or rendering cost would need
separate in-game validation.

## Black startup window follow-up

The owner's next boots, `rb_blitz_573.log` and `rb_blitz_574.log`, reached the
menu and confirmed a much faster warm DLC scan. Boot 574 reused all 1,777
package identities and finished its header scan in 13 ms. Its game window
still stayed black for about 59 seconds before the first native splash. About
18 seconds were synchronous post-XEX setup; the largest remaining interval was
about 28 seconds of low-CPU guest startup with only three file reads totaling
22 ms. The current trace does not distinguish a guest wait from a slow file
open, so no wait semantics were changed.

The boot window now paints a circular lime autosave motif reconstructed from
the title's spinner strip in the installed `spinner_autosave.milo_xbox` before
opening the window. Its eight-wedge wrap, material scroll, and slower transform
rotation follow the scene's authored animation keys. The
startup paint is an SDK opt-in; while synchronous setup runs, its paint pump
services one queued paint at a time and leaves input and deferred callbacks
queued until the normal event loop starts. Controller scheme discovery calls
the pump at each package boundary; the generated menu archive write calls it
about every 50 ms. The icon is
cleared when the startup logo scene is loading and its next guest image has
been published, when a stable native panel and any guest image are observed,
or as soon as the native splash panel is ready. This keeps it off the startup
logos and splash even when the presenter counter has already advanced.
Ultimate provider setup now reads the five indexed archive records directly,
instead of reading the entire 14 MB archive; this targets a measured 5-second
warm run stall in that setup stage.

The installed-art extractor, provider adapter, menu payload, controller scheme,
and DLC headless checks pass. The owner will verify the early paint and window
responsiveness in a live boot; these checks do not substitute for that visual
test.
