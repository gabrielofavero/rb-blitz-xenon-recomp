# In-progress plans

Plans that are actively being worked. A plan moves here from [`../todo/`](../todo/) when work starts
and leaves for [`../finished/`](../finished/) when it is delivered — one `git mv`, keeping its
`YYYY-MM-DD-` prefix.

Song Load Guard **finished on 2026-10-26** and moved to
[`2026-10-13-load-guard.md`](../finished/2026-10-13-load-guard.md).
The owner confirmed fatal-load recovery, Can't Hold Us playback, 30s timeout/dialog,
restored background music and subsequent playback. The final external-restart
design supersedes same-process cancellation. Its five parts preserve the earlier
records, so these old citations still resolve:

| Old document | Now |
| --- | --- |
| `2026-10-13-load-guard-findings.md` | [`2026-10-13-load-guard.md`](../finished/2026-10-13-load-guard.md) part 1 |
| `2026-10-13-load-guard-prompts.md` | [`2026-10-13-load-guard.md`](../finished/2026-10-13-load-guard.md) part 2 |
| `2026-10-25-load-guard-runtime-findings.md` | [`2026-10-13-load-guard.md`](../finished/2026-10-13-load-guard.md) part 3 |

The RhythmVerse plan is now filed under [`finished/`](../finished/) as
[`2026-10-13-rhythmverse-store-plan.md`](../finished/2026-10-13-rhythmverse-store-plan.md) —
the RhythmVerse customs catalogue inside the retail DOWNLOAD CONTENT store (P0 → P6 → P1 → P2 → P3 →
P4 → P5 → P7 → P8). It **finished on 2026-10-22** — its acceptance harness is green in all four legs
on one build (`out/store-acceptance/p8-final/`, 69 steps, 0 failures) — and was **reopened the same
day** for the owner's pass: **§9** carries fourteen prompts, **P9-1 … P9-14**, one per adjustment
(the master switch's default and its restart prompt, the settings reorganisation, the store's labels
and footer, the LT/RT tabs on Your Songs, the boot warm-up with its offline popup, the move to the
title's own keyboard search, the two main-menu/refresh row renames of P9-12, P9-13's
charter/release-date/audio-type/download block in the details page's download box, and P9-14's
A Refresh on the download toast, landed 2026-10-24 and still owing its same-session boot). Those
prompts are owed; the records in §5 are what shipped before the pass, and the P8 close-out record still
says which rows were measured by a probe rather than by the harness.

The shader-cache program was delivered 2026-10-25 and moved to
[`../finished/2026-10-10-shader-cache-plan.md`](../finished/2026-10-10-shader-cache-plan.md).
P0-P9 implementation and packaging are complete; the unproven installed UI,
dynamic coverage and hardware gates remain explicit in
[`../../engine/shader-cache.md`](../../engine/shader-cache.md) parts 8-9 and the backlog.

The most recent plan before it,
[`2026-10-19-interface-gameplay-mapping-restructure-plan.md`](../finished/2026-10-19-interface-gameplay-mapping-restructure-plan.md) —
Interface Mapping versus always-on Gameplay Mapping, preset-aware controller defaults, inferred
Custom, the 3→1 listening countdown, row-level binding text, and new Mouse & Keyboard defaults —
was delivered on 2026-10-20: P1–P5 all landed, ending with the countdown and the live proof.

The in-game settings program outgrew the plan format and lives with its subject; its live tracker is
[`../../settings/status.md`](../../settings/status.md).

See [`../README.md`](../README.md) for the status taxonomy.

Binding rows are now implemented in the guest, and the plan moved to `finished/`;
gameplay verification is deferred at the owner's request. See
[`2026-10-22-in-game-binding-rows-plan.md`](../finished/2026-10-22-in-game-binding-rows-plan.md).
