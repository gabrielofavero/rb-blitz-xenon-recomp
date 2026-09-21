# Local patches for the pinned ReXGlue SDK

`rexglue-sdk/` is a pinned submodule of the **upstream**
`https://github.com/rexglue/rexglue-sdk.git` (pinned at `c94f5eb`). Local edits
must **not** be committed inside the submodule: the parent gitlink would then
reference a commit that does not exist on the upstream remote, so a fresh
checkout could not initialize the SDK (breaking the Milestone 0 reproducibility
guarantee).

Instead, SDK fixes are kept here as patch files and applied to the pinned
checkout.

## Patches

| Patch | Purpose | Upstream candidate |
| --- | --- | --- |
| `rexglue-sdk/0001-xthread-log-once-core-count-warning.patch` | `XThread::SetActiveCpu` re-logged "Too few processor cores" on every thread CPU assignment (~11k lines, ~580 KB per boot). Log it once. | yes |
| `rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch` | The SDK has no host format and no load shader for the 32-bit guest texture formats (`k_32`, `k_32_32`, `k_32_32_32_32`), which are 0.32 fixed point when `num_format = 0`; `CreateTexture` then returned `nullptr` and the SRV sampled (0,0,0,0), making every alpha-blended 3D element invisible (B-010 in [../docs/bringup-log.md](../docs/bringup-log.md)). Maps them to the float host formats with the existing word-mover load shaders, carries `num_format` in `TextureKey`, and converts the guest words to IEEE float bits on the CPU into an upload-pool staging buffer bound as the load shader's source. Integer (`num_format = 1`) textures still fail to create. | yes |
| `rexglue-sdk/0003-trace-ntwritefile-and-scatter-reads.patch` | `NtWriteFile_entry` and `NtReadFileScatter_entry` in `src/kernel/xboxkrnl/xboxkrnl_io.cpp` called neither `REXKRNL_IMPORT_TRACE` nor `REXKRNL_IMPORT_RESULT`, unlike `NtReadFile_entry` — so `--log_level=trace` showed every guest read and **no** guest write, which reads as "the title never writes" when the truth is "writes are not logged". Adds both trace points to both entry points. The write trace is what makes the Milestone 4 persistence evidence (`[NtWriteFile] … len=0x400` while the title authors a fresh `globaloptions` payload) visible; the scatter trace is inert so far — no `NtReadFileScatter` call has been observed in any run. | yes |
| `rexglue-sdk/0004-trace-frame-swaps-and-input-polls.patch` | Nothing in a vanilla log measures frame pacing or input polling: a run has no fps, frame-time or poll-rate line anywhere, and the only frame-ish number in it belongs to the payload overlay. Adds three `--log_level=trace` points — `[VdSwap]` in `VdSwap_entry` (`src/kernel/xboxkrnl/xboxkrnl_video.cpp`, every guest frame submitted, with the host-microsecond delta and the guest tick), `[XE_SWAP]` in `ExecutePacketType3_XE_SWAP` (`src/graphics/command_processor.cpp`, each frame that reached the host as a present) and `[XamInputGetState]` in `XamInputGetState_entry` (`src/kernel/xam/xam_input.cpp`, every guest poll of a pad, with the button word the guest was handed). They back the Milestone 5 frame-pacing and input-polling measurement in [../docs/bringup-log.md](../docs/bringup-log.md). | yes |
| `rexglue-sdk/0005-codegen-skip-stamp-and-gapfill-refinement.patch` | Three codegen defects behind B-003/B-006 in [../docs/known-issues.md](../docs/known-issues.md). **(1) Skipped builds.** `ManifestConfig::WriteSdkVersionStamp` (`src/codegen/manifest.cpp`) rewrote the manifest unconditionally after `RecompileProject` had already written `codegen.build.stamp`, so the manifest — a codegen input — was newest again on the next `ninja`, and the codegen edge re-ran with nothing changed (the Debug tree's stamped re-run cost ~390 s; a real Release regeneration measures 160.4 s here); it now returns early when the `sdk_version` line already matches, leaving the manifest mtime alone. `ComputeInputFingerprint` (`src/codegen/output_stamp.cpp`) also hashed the input list in caller order and with the caller's spelling, so the same tree fingerprinted differently depending on whether the manifest was named relative or absolute; inputs are now sorted and deduped by canonical key, and a `codegen=<n>` behaviour version (`kCodegenBehaviourVersion`) joins the `sdk=` line so a future analyser change can force exactly one regeneration. **(2) Missing gap-fill entries.** `splitRegionOnTerminators` (`src/codegen/phase_gapfill.cpp`) did not treat `bctr` as a terminator, and `gapFillCodeRegions` built its callable set once before the region loop and never revisited a segmentation it had already registered — a thunk slot that is only reachable indirectly and ends in `bctr`, or a boundary a later segment exposed inside an earlier coarse one, stayed interior to that segment and was never registered, so calling it trapped with `[FATAL] Call to invalid or unregistered function`. Gap filling is now a withdraw/refine fixpoint: each pass gives back the previous pass's own `GAP_FILL` registrations (never a real function), re-splits against the callables they revealed, and stops when a pass reproduces the previous segmentation (≤ 8 passes, then a warning). **(3) Dangling call targets.** Withdrawing a registration exposed a pre-existing use-after-free: `CallTarget::ToFunction` (`include/rex/codegen/function_types.h`) holds a raw `FunctionNode*`, and `FunctionGraph::removeFunction` erased the owning `unique_ptr` without touching the edges that pointed at it, so emission read `targetFn->name()` from freed memory and printed a symbol that belonged to an unrelated function, which is what the link's `undefined symbol` errors named. `removeFunction` now rewinds every affected edge back into an address-keyed unresolved jump (`FunctionNode::rewindEdgesTo`), which the graph re-resolves to whatever ends up owning that address — the re-registered function, an internal label, or an explicit `FATAL` line instead of freed memory. `tryResolveAgainst` / `tryResolveAgainstImport` also stopped promoting every re-resolved jump to a tail call, so a `bl` site no longer loses its call. **(4) Measured.** Applying the patch forces exactly one regeneration per project — the `codegen=` behaviour version in the fingerprint goes from absent to 2, so every existing stamp mismatches once, by design; after that an unchanged tree is a 0.4 s skip instead of a 160 s Release analysis (measured on this project: `0 written, 0 unchanged, 1 module(s) up to date`), and `ninja` no longer schedules the codegen edge at all because the manifest mtime is left alone. With `config/functions.toml` trimmed to its three oldest entries it also took codegen's own gap-fill discovery from 0 of the other 21 addresses to 20. | yes |

## Apply / verify

```powershell
# Apply every patch set to the pinned SDK checkout (idempotent; safe to re-run).
# `-ExecutionPolicy Bypass` is required on this machine, where the policy is
# Restricted and a bare `.\scripts\...ps1` fails with UnauthorizedAccess.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\apply_sdk_patches.ps1

# Same, without touching the working tree
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\apply_sdk_patches.ps1 -Check
```

The script reports each patch as *already applied*, *applied now*, or *failed*,
and warns if the SDK checkout no longer matches the gitlink recorded in the
parent commit. Manual equivalent, from the repository root:

```powershell
# Use absolute paths: `git -C <dir>` resolves relative ones against <dir>.
$p = (Resolve-Path patches\rexglue-sdk\0001-xthread-log-once-core-count-warning.patch).Path

git -C rexglue-sdk apply --reverse --check $p   # exit 0 => already applied
git -C rexglue-sdk apply --check         $p     # exit 0 => applies cleanly
git -C rexglue-sdk apply                 $p     # apply it
```

The patches are intentionally **not** applied automatically by the build: apply
them once after a fresh submodule checkout.

## Why the parent repository stays clean

`.gitmodules` marks the submodule:

```ini
[submodule "rexglue-sdk"]
	ignore = dirty
```

so an applied patch no longer shows up as a modified submodule forever. `git
status`, `git diff` and `git status --porcelain=v2` (what editor SCM panels use)
in the parent report nothing for the SDK, while the submodule itself still shows
` M` on the files the patch set touches (`src/system/xthread.cpp` for 0001;
`include/rex/graphics/d3d12/shared_memory.h`,
`include/rex/graphics/pipeline/texture/cache.h`,
`src/graphics/d3d12/texture_cache.cpp`, `src/graphics/pipeline/texture/cache.cpp`
for 0002; `src/kernel/xboxkrnl/xboxkrnl_io.cpp` for 0003;
`src/kernel/xboxkrnl/xboxkrnl_video.cpp`, `src/graphics/command_processor.cpp`
and `src/kernel/xam/xam_input.cpp` for 0004; `src/codegen/manifest.cpp`,
`src/codegen/output_stamp.cpp`, `src/codegen/phase_gapfill.cpp`,
`src/codegen/function_graph.cpp` and `include/rex/codegen/function_node.h` for
0005).

`ignore = dirty` hides work-tree edits only. A **moved gitlink is still
reported**: verified 2026-09-19 by pointing the index entry at a different
commit, which produced `MM rexglue-sdk`. That is the case that matters — a pin
bump must never be silent.

Because the setting also hides *accidental* SDK edits, `apply_sdk_patches.ps1`
audits the work tree on every run: each modified file must match a patch here
(the `index` line is ignored, since its abbreviation length follows
`core.abbrev`). Anything else is listed as `UNEXPECTED` and the script exits 1.

```
SDK work tree      : 14 patched, 0 UNEXPECTED, 0 untracked
    patched    : include/rex/codegen/function_node.h
    patched    : include/rex/graphics/d3d12/shared_memory.h
    patched    : include/rex/graphics/pipeline/texture/cache.h
    patched    : src/codegen/function_graph.cpp
    patched    : src/codegen/manifest.cpp
    patched    : src/codegen/output_stamp.cpp
    patched    : src/codegen/phase_gapfill.cpp
    patched    : src/graphics/command_processor.cpp
    patched    : src/graphics/d3d12/texture_cache.cpp
    patched    : src/graphics/pipeline/texture/cache.cpp
    patched    : src/kernel/xam/xam_input.cpp
    patched    : src/kernel/xboxkrnl/xboxkrnl_io.cpp
    patched    : src/kernel/xboxkrnl/xboxkrnl_video.cpp
    patched    : src/system/xthread.cpp
```

Untracked files are listed for information only, not treated as drift. To bypass
the setting and see the raw state:

```powershell
git -C rexglue-sdk status --porcelain
```

When a patch lands upstream, drop the file here and bump the submodule pin.

## Making the SDK tree sane on Windows

An applied patch is not the only reason the SDK submodule can look dirty. Sixteen
tracked entries under `rexglue-sdk/thirdparty/` are symbolic links upstream:

| Repo | Entries | If flattened |
| --- | --- | --- |
| `libmspack` | 15 — `cabextract/mspack/*.c`, `*.h` and `ChangeLog`, each pointing into the real source tree of the same repo (e.g. `cabextract/mspack/lzxd.c` → `../../libmspack/mspack/lzxd.c`) | **Build-breaking.** `rexglue-sdk/thirdparty/CMakeLists.txt` compiles `cabextract/mspack/lzxd.c`, which becomes 29 bytes of link text instead of 30,796 bytes of source. |
| `o1heap` | 1 — `CLAUDE.md` → `AGENTS.md` | Documentation only. |

This machine cannot create symlinks: the shell is not elevated, Windows Developer
Mode is off, and both `New-Item -ItemType SymbolicLink` and `cmd mklink` fail. The
`rexglue-sdk`, `libmspack` and `o1heap` repos carry no local `core.symlinks`
setting, so they inherit `false` from the system gitconfig
(`C:/Program Files/Git/etc/gitconfig`) and a checkout writes the *link text* into a
plain file.

Do **not** "fix" this by setting `core.symlinks true` in a submodule. Git then
calls `symlink()`, which is unavailable here: the checkout fails with
`error: unable to create symlink cabextract/mspack/lzxd.c: Function not
implemented`, exits 255, and leaves the file **missing** — worse than the flattened
stub. (Verified 2026-09-19; the root repo's `core.symlinks=true`, left over from
B-001, only affects the root repo, which contains no symlinks.)

Expand the flattened files from their targets instead:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1 -NoIndexMarks
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1 -ClearMarks
```

`repair_flat_symlinks.ps1` walks every submodule (and the root repo), follows each
index entry with mode `120000`, and copies the target's content over the flattened
file only when the content differs — so it is idempotent and safe to re-run. A
healthy tree prints `content already correct : 16` and
`repaired from target    : 0`. It then marks exactly those paths
`--skip-worktree`, because the real difference (a type change, `120000` →
`100644`) cannot be resolved on this machine and would otherwise show as permanent
dirt. Use `-ClearMarks` before editing libmspack sources by hand if you want to see
your edits in `git status`, and `-NoIndexMarks` if you do not want the index
touched at all.

After both scripts, `git -C rexglue-sdk status --porcelain` should show only the
files named in the patch set (the `0001`/`0002`/`0003`/`0004` rows above). The parent should be
clean: `.gitmodules`
sets `submodule.rexglue-sdk.ignore = dirty`, so the applied patch is no longer
reported as a modified submodule, and `apply_sdk_patches.ps1` audits whatever that
setting hides. Re-run the scripts after `git submodule update`, a fresh clone, or
any checkout that rewrites the SDK tree. Background:
[../docs/bringup-log.md](../docs/bringup-log.md) B-001 and
[../docs/known-issues.md](../docs/known-issues.md).
