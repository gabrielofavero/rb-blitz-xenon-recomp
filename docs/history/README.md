# History

| Document | What it is |
| --- | --- |
| [`bringup-log.md`](bringup-log.md) | The append-only bring-up log: every phase from the first codegen run to the current settings work, in the order it happened, each entry naming what was measured and what it changed. |

## How the log is written

- **Newest first.** A new entry is prepended as an `## <subject> (<date>)` section at the top, so the
  most recent work is the first thing a reader sees; everything below it is history.
- **One section per subject**, not per day: a phase that took three sessions is one section.
- **Every claim carries its evidence** — a measurement, a `file:line`, a log line, or an explicit
  "not proven".
- **Blockers are numbered** (`B-001` … `B-0xx`) and are cited from code comments
  (`config/functions.toml`, `CMakeLists.txt`) and from
  [`../reference/known-issues.md`](../reference/known-issues.md). Use the outline view or search for
  `B-0xx` to find one.

## Phase index

The sections are H2 headings, so the editor's outline lists them in order. Searching for the headings
below is the fastest way in.

| Phase | Sections |
| --- | --- |
| Scaffold and first boots (2026-09-07 → 09-10) | *Scaffold and generate*, *Blockers* (`B-001`…`B-007`), *Toolchain notes*, *Close analysis and compile*, *Handoff to the boot phase*, *Guest entry and stable offline boot*. |
| Menus, content, saves (2026-09-19 → 09-20) | *Menus, content discovery, input, saves*, *First songs played to the end*, *Decision: online unblocking is not our job*, *First host unit tests: the B-009 key path*, *The fingerprint file gains consumers*, *B-012: the Ultimate install*, *Acceptance: one named song*, *Close-out: frame pacing and input polling*, *Codegen: the B-003/B-006 root causes*. |
| Native mouse navigation (2026-09-22, then 10-01) | *Mouse navigation* passes two, three and four. |
| Release readiness (2026-09-27 → 09-30) | *Clean-checkout smoke test*, *Freezing the toolchain*, *The runtime half of the wrong-data check*, *Auditing the distributable*, *The Debug preset's assert policy*, *How a corrupt `default.xex` fails*, *`B-006`: the last forced entry*, *`B-010`'s two standing limits*, *The guest's longjmp/setjmp*. |
| Hook hygiene and coverage (2026-10-01) | *Hook hygiene: the faithful behaviour of every hook*, *An eighth host test target*, *The last three host-decidable hook decisions*, *Scripted coverage for the routes only a boot reached*, *Close-out: the fixes implied by the open known-issues*. |
| Enhancement groundwork (2026-10-03 → 10-04) | *The naming campaign opens*, *`d3d12_readback_resolve` moves to the runtime profile*, *DLC reaches the guest, and the alpha save/DLC acceptance*, *A config key that matches no cvar is reported*, *Enhancement toggles*, *The probe*, *The observation harness and its noise floor*, *The observation baseline*. |
| Saves offline (2026-10-10) | *Offline player persistence* — the section at the top of the file. |

## Where later work went

The log stops describing work in detail where a dedicated document took over:

| Since | Read instead |
| --- | --- |
| 2026-10-10 | [`../plans/cancelled/2026-10-10-song-cache-plan.md`](../plans/cancelled/2026-10-10-song-cache-plan.md) records the former DLC cache and refresh design; [`../guides/dlc.md`](../guides/dlc.md) describes today's scan and native refresh. |
| 2026-10-10 | [`../plans/finished/2026-10-09-save-persistence-findings.md`](../plans/finished/2026-10-09-save-persistence-findings.md) for the save checkpoint mechanics. |
| 2026-10-10 | [`../settings/plan.md`](../settings/plan.md) and [`../settings/status.md`](../settings/status.md) for the in-game settings program (P0–P11). |
| 2026-10-12 | [`../settings/graphics.md`](../settings/graphics.md) for resolution, the capability policy and custom resolutions. |
| 2026-10-13 | [`../settings/status.md`](../settings/status.md) for the owner punch list, the launcher cutover, boot speed and the Vulkan enablement. |
