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

The flat-library scan now starts in `OnPreSetup`, after settings and paths are
final and before graphics presentation setup. `dlc::Configure` waits for and
publishes the same result before guest launch. It falls back to a synchronous
scan if the configuration changed or the early scan failed. The log reports
both scan time and the time `Configure` actually waited. The startup catalogue
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
