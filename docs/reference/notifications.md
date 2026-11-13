# Player notifications

`ui/notifications.h` provides the thread-safe interface:
`Post(key, Level::kInfo/kWarning/kError, title, description)` and `Dismiss(key)`.
Use a stable key per interaction. The top-right overlay has a borderless black
background like the music player, a bold heading, smaller
description, and slide/fade animations. It never takes input and works independently
of Settings Labels. Font measurements bound titles to two lines and descriptions
to four lines, shortening overflow.

Notices last 6/8/10 seconds by severity, starting when displayed. Errors precede
warnings and information; ties keep arrival order. A matching key updates an
existing notice. Identical visible notices do not restart the timer; identical
completed notices have a 30-second cooldown. The queue holds 16 pending notices,
dropping lower-priority entries first. Technical details remain in logs.

## Interaction audit

These routes replace existing feedback. The final column identifies the few
important failures promoted from logs under the owner's exception.

| Interaction | Level | Previous route |
| --- | --- | --- |
| Catalogue loading without visible offers | Info | Status caption; page updates now coalesce. |
| Catalogue fetching disabled | Info | Status caption. |
| No catalogue offers for the filter | Info | Status caption. |
| Catalogue connection/HTTP/response failure | Error | Offline caption; retains store-surface gate. |
| Catalogue stops after some pages | Warning | Log only; usable results remain incomplete. |
| Search too short or RhythmVerse disabled | Warning | Validation/status caption. |
| Search retry backoff | Warning | Refusal/status caption or log when retaining earlier results. |
| Search count/no matches | Info | Terminal status caption. |
| Search network/HTTP/response failure | Error | Caption could disappear with keyboard closure. Stale generations stay silent. |
| Search stops after some pages | Warning | Log only; partial results remain usable. |
| Binding capture receives wrong device type | Warning | Status caption; countdown continues separately. |
| Captured binding cannot be staged/saved | Error | Status message disappeared after capture disarmed. Returns to row on failure too. |
| Controls change/reset/capture request refused | Warning | Log only; action otherwise silently does nothing. |
| No usable input / keyboard recovery succeeds | Warning / Info | Recovery captions; now triggered on state changes. |
| Keyboard recovery fails | Error | Log only. |
| Local progress restore allocation fails | Error | Log only; saved progress was not restored. |
| Local save cannot be read/validated | Error | Log only; file preserved, session writes disabled. |
| Local progress cannot be serialized/written | Error | Log only; new progress not saved, retries deduplicated. |
| Empty progress would overwrite existing save | Warning | Log only; previous save retained. |
| DLC discovery job lost or fails after retry | Error | Log only; downloaded songs unavailable. |
| Packages found but content manager unavailable | Error | Log only; discovered songs cannot be registered. |
| Update opens releases but cannot queue game close | Error | Log only; player must close before installing. |

Search's generic terminal popups are replaced by the same notifications, avoiding
duplicate empty/short/error reports. The guest loading/cancel/focus route stays.

## Retained interfaces

These already provide the appropriate persistent guidance or acknowledgement and
action routes. They do not produce duplicate notifications.

| Interaction | Interface |
| --- | --- |
| Settings row descriptions, availability explanations, apply/restart suffixes | Settings help captions. |
| Query being typed/search guidance | Input surface caption. |
| Listening/release instructions and binding countdown | Input surface caption. |
| Search request progress | Guest loading modal. |
| Download progress/completion/cancellation/failure | Guest download dialog. |
| Song deletion confirmation/progress/failure/refresh warning | Guest deletion dialog. |
| Settings save/conflict/provider errors, restart errors, display preview/Keep errors | Dedicated guest dialogs. |
| Controller capture confirmation, unavailable Play, achievement reset | Guest confirmations/errors. |
| Update browser launch failure | Guest label polls `update/open-failed`. |
| Graphics preparation failures and song-load recovery | Existing blocking loading/recovery flow. |
| Achievement unlock | Existing achievement toast. |

Developer probes, malformed internal requests, individual rejected DLC packages,
album-art placeholder fallbacks, optional art/font failures, cache persistence
warnings with a working session fallback, skipped menu-music tracks and routine
startup diagnostics remain logged. Successful operations gain no extra notices.

## Validation

The `notifications` test covers animation timing, priority, capacity, deduplication,
concurrent producers, catalogue surface gating/recovery and search failure after
keyboard closure. Input, store, settings and payload tests cover preserved routes.
In-game visual checks follow the repository's manual-testing preference.
