# In-progress plans

Plans that are actively being worked. A plan moves here from [`../todo/`](../todo/) when work starts
and leaves for [`../finished/`](../finished/) when it is delivered — one `git mv`, keeping its
`YYYY-MM-DD-` prefix.

The last plan filed here, [`2026-10-13-rhythmverse-store-plan.md`](../finished/2026-10-13-rhythmverse-store-plan.md) —
the RhythmVerse customs catalogue inside the retail DOWNLOAD CONTENT store (P0 → P6 → P1 → P2 → P3 →
P4 → P5 → P7 → P8) — **moved to [`../finished/`](../finished/) on 2026-10-22**: its acceptance
harness is green in all four legs on one build (`out/store-acceptance/p8-final/`, 69 steps,
0 failures), so the switch, the store's Recent/Search/download/install, the song on the title's own
next-boot list and the control boot's silence are all measured rather than owed. The same run found
two defects that are now fixed (§5/P8's close-out record), and its acceptance matrix still says
which rows were measured by a probe rather than by the harness.

This folder is empty until the next plan starts being worked.

The most recent plan before it,
[`2026-10-19-interface-gameplay-mapping-restructure-plan.md`](../finished/2026-10-19-interface-gameplay-mapping-restructure-plan.md) —
Interface Mapping versus always-on Gameplay Mapping, preset-aware controller defaults, inferred
Custom, the 3→1 listening countdown, row-level binding text, and new Mouse & Keyboard defaults —
was delivered on 2026-10-20: P1–P5 all landed, ending with the countdown and the live proof.

The in-game settings program outgrew the plan format and lives with its subject; its live tracker is
[`../../settings/status.md`](../../settings/status.md).

See [`../README.md`](../README.md) for the status taxonomy.
