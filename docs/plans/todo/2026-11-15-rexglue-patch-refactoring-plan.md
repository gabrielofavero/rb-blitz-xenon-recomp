# ReXGlue SDK patch refactoring

**Status (2026-11-15): planning only.** The initial source and replay audit is
recorded below. No patches have been removed, consolidated or renumbered by this
plan. **P0 must refresh the assessment against the checkout present when work
begins**, including patches added after this document was written.

## Request and intended result

Reduce the maintenance cost of the downstream SDK changes while preserving the
game's supported behavior. Determine which changes remain necessary, which later
patches replace, which code has no current consumers, and which diagnostics are
still useful. Consolidate related implementations and strengthen reproducibility.
The objective is a justified, reviewable stack, without an arbitrary patch-count
target.

The implementation lives in [the patch directory](../../../patches/rexglue-sdk),
with its standing instructions in [patches/README.md](../../../patches/README.md).
The [build guide](../../guides/build-and-run.md),
[shader-cache study](../../engine/shader-cache.md), and
[Vulkan performance study](../../engine/vulkan-performance.md) supply integration
and historical evidence.

## Initial assessment — 2026-11-15

These observations describe the audit snapshot, not a permanent inventory.

| Observation | Evidence and interpretation |
| --- | --- |
| 72 patch files, numbered through 0073 | 0004 was absent; the README records its trace work under 0019. 0073 was untracked work in progress. The highest number is not the patch count. |
| SDK pinned at `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` | Parent gitlink and SDK HEAD matched. This is the baseline to refresh before implementation. |
| Full replay matched the checkout | `python3 scripts/setup/apply_sdk_patches.py --check` reported `SDK patches: 72 replayed, 0 files updated`. |
| 126 distinct paths, 15 newly added files, 7,018 net lines | Disposable replay of the series against pristine pinned source. This is a substantial downstream SDK variant. |
| No identical patch changes found | Patch identity comparison and source inspection. This does not prove the absence of semantic duplication. |
| No single patch could simply disappear | In 72 disposable omission checks, 28 caused a later patch to fail application; 44 replayed but changed final source. No omission produced an identical final tree. Context-only dependencies can also cause application failure. |
| Changes concentrate in shared files | `src/ui/window_sdl.cpp` and `src/graphics/vulkan/command_processor.cpp` were each touched by 11 patches; Vulkan and D3D12 pipeline caches by nine and eight respectively. |

The additional inventory, line-origin and omission analysis used temporary audit
scripts outside the repository; those scripts are not a maintained test or durable
evidence artifact. P0 must reproduce these results and save its commands, script
and output under an ignored `out/sdk-patch-refactor/` evidence directory. The
checked-in patch applier remains the reproducible baseline check.

**Assessment:** the count does not demonstrate lost code control. The pinned
upstream source, ordered replay, final-content comparison and component tests are
useful controls. The maintenance debt comes from accumulated corrections, shared
file dependencies, unused functionality and inconsistent documentation. A smaller
stack is valuable when it makes those responsibilities clearer.

The audit inspected source, replay and selected consumers. It did not build or
run the game, prove every change behaviorally necessary, measure tracing overhead,
or compare the stack with newer upstream revisions.

### Partial replacements and consolidation candidates

| Patch family | Initial finding | Proposed treatment, subject to P0 |
| --- | --- | --- |
| 0002 + 0022 | 0022 replaces Vulkan's CPU texture conversion with GPU conversion. 0002 retains shared format support and D3D12 conversion. | Consolidate final conversion support while preserving both backends. Do not delete 0002 wholesale. |
| 0018 + 0046 + 0055 | Incomplete and indirect GPU packet recovery evolved together; 0055 replaces the retry-count policy with a time bound. | Present the final recovery implementation as a coherent unit, retaining packet checks and indirect failure propagation. |
| 0021 + 0027, with 0029/0030/0031/0034 | The preparation lifecycle replaces substantial portions of preload setup but retains accounting. Application seams, audio scheduling and asynchronous publication have separate responsibilities. | Review boundaries and dependencies; combine corrections where useful without turning the entire shader program into one patch. |
| 0028 + 0044 | 0044 corrects replacement of a rejected native D3D12 cache entry introduced by 0028. | Fold the correction into the native-cache implementation. |
| 0016 + 0035 + 0048 + 0050 | Successive display-mode implementations and startup verification. Other window patches interact with this family. | Consolidate the final display state machine; inspect 0014/0017/0054/0058/0066/0072 and any newer work before deciding the boundary. |
| 0054 + 0066 | macOS focus recovery changes part of the earlier recovery policy; Windows still needs forced reconnection. | Keep platform-specific behavior explicit when combining or reorganizing. |
| 0065 + 0071 | 0071 extends the arithmetic fix from fragment to vertex shaders and advances translator version 13 to 14. | Combine the final lowering while preserving the latest version and cache invalidation contract. |
| 0020 + 0026 + 0060/0061/0062/0068, plus trace portions of 0073 | Shared instrumentation, identities, default policy and backend attribution accumulated over time. | Separate instrumentation implementation from optional defaults and scheduling behavior where that helps review. |

0034 and 0069 address different stalls: submission waits versus synchronous
MoltenVK placeholder creation. They are complementary, not interchangeable.
0070 upload batching and 0073 queue-submission policy are also distinct changes;
their proximity in numbering is not evidence of duplication.

### Retirement candidates and unresolved value

| Candidate | Initial source evidence | Required review |
| --- | --- | --- |
| 0049, `successful_present_sequence()` | No readers found. Current display confirmation reads 0051's `successful_ui_present_sequence()` in `src/rb_blitz_app.h`. | Search all current application, SDK, test and tool consumers again. If still unused, remove the API, storage and increments and regenerate dependent patch context. |
| 0057, animated startup shader preparation | Its added preparation route requires `has_shader_storage`. The title's `DeferShaderStoragePreparation()` always returns true, making that condition false in `rexglue-sdk/src/ui/rex_app.cpp`. | Recheck overrides, alternate launch routes and tests. Retire the unused route if there is no current requirement; preserve the title's post-A preparation and independent startup paint. |
| Part of 0053, `has_guest_output_ever_refreshed()` | No readers found for this boolean. The separate `guest_output_refresh_sequence()` is used by the boot icon. | Remove only the unused boolean API/storage/write if still unconsumed. Preserve the sequence and startup paint functionality. |
| 0052, boot timings | The patch README describes this as temporary diagnostic instrumentation for large-DLC startup. | Decide whether it remains useful operational evidence; lack of gameplay effect alone is not a reason to remove diagnostics. |
| Default-on tracing from 0060 and later spans | The audit found no overhead measurement. Bounded log output does not establish bounded timing overhead. | Keep default policy unchanged during source-equivalent consolidation. Any policy change needs a separate rationale and relevant timing evidence. |

Other audit gaps: the README still described obsolete concatenated-hunk ordering
restrictions despite the final-tree replay algorithm; the release workflow applied
patches but there was no dedicated ordinary push/PR replay gate; and
`tests/sdk_patch_stack_tests.py` exercised the PowerShell applier without equivalent
native Python applier coverage. Refresh these findings in P0.

## Decisions

- **D1 — Refresh before changing.** P0 is mandatory. New patches, consumer changes,
  pin changes and existing work in progress can invalidate every candidate above.
  Record changed conclusions explicitly rather than silently treating this list
  as approval to delete code.
- **D2 — Separate equivalent consolidation from retirement.** A consolidation must
  reproduce identical final source bytes against the same upstream pin. A retirement
  intentionally changes source and needs its own documented delta, usage evidence
  and affected validation. Preserve the original final tree as the comparison base.
- **D3 — Preserve upstream reproducibility.** Keep the existing pinned upstream plus
  patch model. Do not create local-only SDK commits, silently bump the pin, or move
  to a fork as part of this refactor. Assess upstream candidates separately; claiming
  a fix landed upstream requires verification at the actual proposed revision.
- **D4 — Keep the history navigable.** Record every old patch ID and its retained,
  folded, split or retired destination in the standing patch documentation. Preserve
  historical measurements and update active links. No global renumbering merely to
  close gaps; if new names are necessary, retain an explicit old-to-new map.
- **D5 — Keep responsibilities reviewable.** Group by behavior and subsystem, not
  chronological proximity. Retain platform guards, default behavior, cancellation,
  error handling, resource retirement and shader/native-cache version contracts.
- **D6 — Preserve unrelated work.** Inventory parent and SDK changes first. An
  untracked patch can be active work. Use disposable replay trees for experiments;
  never restore or overwrite unexplained SDK changes to make the audit pass.
- **D7 — Respect game/UI authorization.** This plan grants no permission to launch
  or navigate the game. Before any driven UI test, ask whether the owner wants to
  perform it and wait, unless the owner explicitly authorizes autonomous UI work
  for that scope. Build the latest code before requesting owner testing. Existing
  owner stops on launches must be honored. No new host overlay is needed here.

## Execution prompts and acceptance

| Prompt | Dependency | Deliverable and exit condition | Status |
| --- | --- | --- | --- |
| P0 — Refresh the audit | Current checkout | Complete inventory, replay evidence, semantic usage review, updated dispositions and revised execution scope. | Not started |
| P1 — Strengthen replay checks | P0 | Game-data-free CI replay check and meaningful applier regression coverage on native Python and PowerShell where available. | Not started |
| P2 — Retire confirmed unused changes | P0/P1 | Small, justified source deltas for candidates still unconsumed; dependent patches regenerated and affected checks passing. | Not started |
| P3 — Consolidate patch families | P2 | Coherent patches with identical final source before/after each consolidation, complete ID mapping and clean replay. | Not started |
| P4 — Documentation and integration | P1–P3 | Current patch instructions, verified links, build/test evidence and explicit remaining platform/UI limits. | Not started |

### Prompt P0 — Mandatory kickoff review (copy and run when work begins)

```text
Review the current ReXGlue SDK patch stack before implementing the patch
refactoring plan. Locate the plan titled "ReXGlue SDK patch refactoring" in
docs/plans/; its creation date is 2026-11-15. Read it, AGENTS.md and the repository
documentation rules. This task is P0 review only; do not remove or consolidate
patches yet and do not launch or navigate the game.

The plan's original observations are a dated baseline, not current truth. There
may now be more patches, a different SDK pin, new consumers or corrections that
invalidate the proposed removals. Inspect the parent and SDK working trees and
preserve unrelated and in-progress work. Count actual patch files rather than
using the highest numeric ID; include untracked patches in the inventory and
record whether they are committed. Record parent commit, SDK pin/HEAD, patch
hashes and relevant existing changes so the audit snapshot can be identified.

Run the current applier in check mode. In disposable trees, replay every patch
against pristine pinned SDK source; identify duplicate changes, overlapping or
replaced hunks, final surviving changes and dependencies. Reproduce per-patch
omission checks, distinguishing application-context failures from semantic
dependencies. Neither a successful replay nor surviving source proves that a
feature is still needed. Search current application, SDK, test and tool consumers
and inspect reachable launch/configuration routes for each retirement candidate.

Review every patch, including additions after 0073. Reassess 0049's presentation
counter, the unused boolean in 0053, the startup preparation route in 0057,
temporary diagnostics, the consolidation families, applier consistency, CI and
documentation. Keep useful diagnostics distinct from dead code. Verify upstream
status only if making an upstream-based removal claim, against the exact revision;
do not change the pin as part of the audit.

Save commands, any audit scripts, manifests and outputs under an ignored dated
subdirectory of out/sdk-patch-refactor/. Update this plan with a dated P0 result:
one disposition per current patch (keep, fold, split, retire candidate or unresolved),
its reason/consumers/tests, old-to-proposed mappings, discrepancies from the
original assessment, and the revised P1-P4 scope. Mark uncertainty and unavailable
platform/runtime checks explicitly. Review counts and file identities again before
handoff; if the stack changed during the audit, refresh affected findings.

Deliver the updated assessment and concrete next implementation steps. Do not
interpret the dated candidate list as authorization to discard active work or
as proof that a removal is safe. Follow the documentation status/move rules and
run its checker after updating the plan and indexes.
```

### P1 — Replay and applier protection

Add an ordinary push/PR job that initializes the pinned SDK, applies the stack
from pristine source and checks the resulting tree and repeated invocation. It
must not require private game data, SDK compilation or a game launch. Cover the
repository's supported appliers and their intentional platform differences.

Regression cases should include a later patch replacing an earlier hunk, a new
file, incomplete application, changes outside patch coverage and conflicting local
edits. Verify detection as well as preservation of unrelated edits. Include
line-ending and pin-mismatch behavior where applicable. Reuse the existing stack
fixture where practical; exercise the production appliers rather than duplicating
their algorithm in assertions.

### P2 — Confirmed retirements

For each accepted retirement, record its current consumer search, reachable-route
analysis, exact intended source delta and affected tests. Regenerate dependent
patches from the intended final source instead of deleting a file and accepting
whatever still applies. Keep diagnostics policy changes separate. If a candidate
has acquired a consumer, retain it and correct this plan's disposition.

### P3 — Source-equivalent consolidation

Start with small correction families, then tackle display and preparation work
using P0's dependency map. Snapshot the complete resulting source and file set
before each family. After rewriting the family, replay the entire series against
the unchanged pin and compare it byte for byte with that snapshot, including new
files. Report additions/deletions explicitly. Do not use successful builds or
reverse-apply checks as a substitute for this equivalence gate.

Keep retirement deltas out of consolidation comparisons by using the accepted
post-P2 source as P3's baseline. Update active patch references and record original
IDs as each family finishes. Rerun affected component tests when a source delta or
unresolved concern warrants them.

### P4 — Integration, evidence and documentation

Update the patch README with the final inventory, responsibilities, dependencies,
retirement rationale, old-to-new mapping and actual replay algorithm. Correct stale
ordering restrictions. Update build instructions, CI references and engine records
whose current claims change; preserve historical measurements in finished plans.

Run clean replay and idempotence checks, the relevant applier/component tests and
the documentation checker. On Windows, build with
`cmake --build --preset win-amd64-release`; use the corresponding native Release
preset when validating another desktop platform. Use the Windows payload test
runner or its CTest registrations for Windows payload executables. Record skipped
or unavailable platforms separately from passes. Require a current build before
any owner UI acceptance handoff, without launching the game to fill evidence gaps.

## Completion and remaining uncertainty

Completion requires a disposition for every patch present at delivery, no
unexplained SDK drift, all accepted removals justified, source equivalence proved
for consolidations, replay CI and applier checks in place, and updated documentation
with passing available build/test gates. A final count is a result, not a quota.

Currently unresolved: which candidates remain unused when P0 runs; whether any
new patches change the proposed families; how much default tracing costs; whether
general fixes have landed upstream; and which platform/runtime checks will be
available. No runtime regression or performance improvement is claimed by this plan.
