# Engine compatibility and release evidence

**Status (2026-10-31):** planning only. This consolidates the still-open engine
research and compatibility work formerly listed in the backlog.
The original observations remain in the linked records; none of the acceptance
routes below is claimed to have passed by this plan.

## Prompt order and execution

| Prompt | Dependency | Result |
| --- | --- | --- |
| E1 | Installed Ultimate payload | Scripted Ultimate and vanilla control routes |
| E2 | A new Ultimate payload release | Hash comparison and, only if needed, a re-derived patch table |
| E3a | Current release build and installed payload | Cold/warm and recovery matrix |
| E3b | E3a baseline | Real missing-recipe recovery measurement |
| E3c | A reproducible song-start stall | Cause separated from shader compilation |
| E4 | Compile-trace source and P8 parser | Distinct helper-key counts |
| E5a | Proved Blitz function identities | Named symbols with evidence |
| E5b | RB3DX catalogue | Blitz applicability decisions, starting with `NewFile` |
| E5c | Audio fixtures | Repeatable chroma/speed/distortion regression check |
| E6 | Release candidate and required runner | Installer and first-release evidence |

E3a–E3c are the shader-preparation campaign formerly grouped as E3; E5a–E5c
are the engine-knowledge campaign formerly grouped as E5. They can run
independently unless a prompt names a dependency. Build the latest code before
any game/UI acceptance. Before navigating the game or driving its UI, ask
whether the owner wants to run that test and wait for the answer; host checks
and builds can proceed independently.

### Prompt E1 — Script installed Ultimate acceptance

**Starting evidence:** the installed payload was observed by hand on
2026-09-20, while [ultimate-compat.md](../../guides/ultimate-compat.md) §10
still lacks a scripted boot → menu → song route.

**Do:** add a repeatable installed-payload route with a vanilla control.
Record payload and base-image hashes, the launch configuration, logs, route
steps and song outcome. Keep all game data local and read-only.

**Exit:** the script demonstrates gameplay on both routes and makes criterion
1 auditable. If a route fails, preserve the failing log and narrow the
compatibility boundary instead of declaring acceptance.

### Prompt E2 — Gate a changed Ultimate executable

**Trigger:** a new payload release. This prompt does not run for the known
`390e0ae0…` `default.xex`.

**Do:** compare the image hash and shape with [ultimate-compat.md](../../guides/ultimate-compat.md)
§3. If the image changed, re-derive and review the 12-byte table from both
images, then rerun E1. If it did not change, record the match.

**Exit:** the release comparison and any changed offsets/bytes are reproducible;
no patch table is changed on an unverified assumption.

### Prompt E3a — Installed shader preparation matrix

**Starting evidence:** [shader-cache.md](../../engine/shader-cache.md) parts
8–9 record D3D12 campaign runs and partial Vulkan coverage. Installed
cold/warm, native Vulkan reuse, alternate paths and other hardware remain
unproven.

**Do:** run a bounded matrix on D3D12 and Vulkan: cold/warm, native reuse,
retail and Ultimate, cancellation, focus, trial and error routes. Include
another hardware configuration when available. Record per-run logs, pipeline
counts, frame timing and audio/input behavior. Keep unavailable hardware
marked unmeasured.

**Exit:** every attempted row has a verdict and evidence path; the release
claim stays limited to measured rows and dynamic recipes remain discoverable.

### Prompt E3b — Recover from an actual missing recipe

**Dependency:** E3a establishes the control run.

**Do:** arrange a genuinely missing portable recipe on SDK patch 0034 and
observe frame continuity, eventual effects, audio and input recovery. Compare
against a fully covered warm run. Add any discovered dynamic scene/material
pairing to the catalogue and rerun the affected check.

**Exit:** a real miss recovers or produces a narrow defect with timing and
logs. Do not claim universal first-song/no-stutter coverage.

### Prompt E3c — Isolate a non-compilation song-start stall

**Trigger:** a reproducible stall remains after E3a/E3b or in another song
start. Trace guest, render, I/O and compile events over the same interval;
compare cold and warm controls.

**Exit:** the stall has a measured owning stage and a separately tracked fix
or an explicit unresolved trace. A compile miss is not inferred merely from
song-start timing.

### Prompt E4 — Count distinct compile helper identities

**Do:** add a per-(phase, event) distinct-key count to the SDK
`shader_trace: summary` line and the P8 report parser. Add focused checks for
repeated and new identities with detail suppression active.

**Exit:** the trace can measure “1,473 calls, one identity” directly. Until
then, [shader-cache.md](../../engine/shader-cache.md) P8 keeps its
total-based inference label.

### Prompt E5a — Name proved Blitz functions

**Do:** select functions with established address and behavior evidence.
Name them in `config/functions.toml`, recording the naming rule, evidence
and confidence in [symbols.md](../../reference/symbols.md) in the same change.

**Exit:** generated names resolve as expected and no function is named solely
from a sibling project's address or guess.

### Prompt E5b — Triage RB3DX groups and `NewFile`

**Do:** check groups 2–10 in [rb3-references.md](../../reference/rb3-references.md)
against Blitz one by one. Prioritize the P1 `NewFile` lead: prove the Blitz
entry, flags and asset-root behavior before any hook. Record adopted, rejected
and still-unproven decisions in the catalogue.

**Exit:** each group has a Blitz-specific disposition; adopted code has a
guest address, behavioral evidence, tests where appropriate and a §9
provenance row.

### Prompt E5c — Add audio verification

**Do:** turn [rb3-references.md](../../reference/rb3-references.md) §7.1's
chroma correlation, speed and distortion method into a repeatable regression
check with known-good fixtures and a documented tolerance.

**Exit:** the check distinguishes an altered decode/playback path from its
control and records fixture provenance.

### Prompt E6 — First-release packaging evidence

**Trigger:** a release candidate and a self-hosted Windows runner with the
fingerprinted local dump. See the [release model](../finished/2026-10-15-release-model.md)
and [known-issues.md](../../reference/known-issues.md).

**Do:** build the one-file installer, exercise install/launch, verify its
caption interactively where unattended coverage skips it, and run the tag →
release workflow. Record file digests as the artifact identity; a modified
worktree's HEAD is not the compiled content.

**Exit:** the installer and workflow each have a recorded result and matching
artifact digests. Publishing remains a separate release action.

## Boundaries

The framerate and UI pixel investigations already have [framerate](2026-10-25-framerate-expansion-plan.md)
and [pixel](2026-10-29-ui-pixel-imperfections-investigation-plan.md) plans. The
button layout work stays in its [active plan](../in-progress/2026-10-19-button-layout-hotswap-plan.md).
Online services remain a permanent non-goal. Keep game data read-only, SDK changes
as generic patch files, generated code regenerated, and Blitz-specific behavior in
project code; [patches/README.md](../../../patches/README.md) and
[known-issues.md](../../reference/known-issues.md) retain the standing constraints.
