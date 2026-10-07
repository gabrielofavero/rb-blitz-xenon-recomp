# Local patches for the pinned ReXGlue SDK

`rexglue-sdk/` is a pinned submodule of the **upstream**
`https://github.com/rexglue/rexglue-sdk.git` (pinned at `c94f5eb`). Local edits
must **not** be committed inside the submodule: the parent gitlink would then
reference a commit that does not exist on the upstream remote, so a fresh
checkout could not initialize the SDK (breaking the reproducibility
guarantee).

Instead, SDK fixes are kept here as patch files and applied to the pinned
checkout.

## Patches

| Patch | Purpose | Upstream candidate |
| --- | --- | --- |
| `rexglue-sdk/0001-xthread-log-once-core-count-warning.patch` | `XThread::SetActiveCpu` re-logged "Too few processor cores" on every thread CPU assignment (~11k lines, ~580 KB per boot). Log it once. | yes |
| `rexglue-sdk/0002-32-bit-fixed-point-texture-conversion.patch` | The SDK has no host format and no load shader for the 32-bit guest texture formats (`k_32`, `k_32_32`, `k_32_32_32_32`), which are 0.32 fixed point when `num_format = 0`; `CreateTexture` then returned `nullptr` and the SRV sampled (0,0,0,0), making every alpha-blended 3D element invisible (B-010 in [../docs/history/bringup-log.md](../docs/history/bringup-log.md)). Maps them to the float host formats with the existing word-mover load shaders, carries `num_format` in `TextureKey`, and converts the guest words to IEEE float bits on the CPU into an upload-pool staging buffer bound as the load shader's source. On 2026-09-30 the two limits the first cut left were removed: the conversion now covers `num_format = 1` (integer) as well as `0` (fraction), using the scaling rule of the SDK's own other number-format consumers (the `is_integer` mode of the vertex fetch and the integer surface number formats of the memory export both leave an integer component unscaled) and reporting the first use of each such format at `REXGPU_WARN`; and the staging buffer comes from a texture-cache-owned pool that allocates a page large enough for the request instead of requiring the whole converted texture and its mips to fit `GraphicsUploadBufferPool`'s 2 MiB default page — which is what `Page::size_` (the size each page was actually allocated with) in `GraphicsUploadBufferPool` and the page-size increase in `D3D12UploadBufferPool::Request` are for. | yes |
| `rexglue-sdk/0003-trace-ntwritefile-and-scatter-reads.patch` | `NtWriteFile_entry` and `NtReadFileScatter_entry` in `src/kernel/xboxkrnl/xboxkrnl_io.cpp` called neither `REXKRNL_IMPORT_TRACE` nor `REXKRNL_IMPORT_RESULT`, unlike `NtReadFile_entry` — so `--log_level=trace` showed every guest read and **no** guest write, which reads as "the title never writes" when the truth is "writes are not logged". Adds both trace points to both entry points. The write trace is what makes the persistence evidence (`[NtWriteFile] … len=0x400` while the title authors a fresh `globaloptions` payload) visible; the scatter trace is inert so far — no `NtReadFileScatter` call has been observed in any run. | yes |
| `rexglue-sdk/0004-trace-frame-swaps-and-input-polls.patch` | Nothing in a vanilla log measures frame pacing or input polling: a run has no fps, frame-time or poll-rate line anywhere, and the only frame-ish number in it belongs to the payload overlay. Adds three `--log_level=trace` points — `[VdSwap]` in `VdSwap_entry` (`src/kernel/xboxkrnl/xboxkrnl_video.cpp`, every guest frame submitted, with the host-microsecond delta and the guest tick), `[XE_SWAP]` in `ExecutePacketType3_XE_SWAP` (`src/graphics/command_processor.cpp`, each frame that reached the host as a present) and `[XamInputGetState]` in `XamInputGetState_entry` (`src/kernel/xam/xam_input.cpp`, every guest poll of a pad, with the button word the guest was handed). They back the frame-pacing and input-polling measurement in [../docs/history/bringup-log.md](../docs/history/bringup-log.md). | yes |
| `rexglue-sdk/0005-codegen-skip-stamp-and-gapfill-refinement.patch` | The three codegen defects behind B-003/B-006 in [../docs/known-issues.md](../docs/known-issues.md), plus the gap-fill rule that closed the last of that class (item 5, added 2026-09-29). **(1) Skipped builds.** `ManifestConfig::WriteSdkVersionStamp` (`src/codegen/manifest.cpp`) rewrote the manifest unconditionally after `RecompileProject` had already written `codegen.build.stamp`, so the manifest — a codegen input — was newest again on the next `ninja`, and the codegen edge re-ran with nothing changed (the Debug tree's stamped re-run cost ~390 s; a real Release regeneration measures 160.4 s here); it now returns early when the `sdk_version` line already matches, leaving the manifest mtime alone. `ComputeInputFingerprint` (`src/codegen/output_stamp.cpp`) also hashed the input list in caller order and with the caller's spelling, so the same tree fingerprinted differently depending on whether the manifest was named relative or absolute; inputs are now sorted and deduped by canonical key, and a `codegen=<n>` behaviour version (`kCodegenBehaviourVersion`) joins the `sdk=` line so a future analyser change can force exactly one regeneration. **(2) Missing gap-fill entries.** `splitRegionOnTerminators` (`src/codegen/phase_gapfill.cpp`) did not treat `bctr` as a terminator, and `gapFillCodeRegions` built its callable set once before the region loop and never revisited a segmentation it had already registered — a thunk slot that is only reachable indirectly and ends in `bctr`, or a boundary a later segment exposed inside an earlier coarse one, stayed interior to that segment and was never registered, so calling it trapped with `[FATAL] Call to invalid or unregistered function`. Gap filling is now a withdraw/refine fixpoint: each pass gives back the previous pass's own `GAP_FILL` registrations (never a real function), re-splits against the callables they revealed, and stops when a pass reproduces the previous segmentation (≤ 8 passes, then a warning). **(3) Dangling call targets.** Withdrawing a registration exposed a pre-existing use-after-free: `CallTarget::ToFunction` (`include/rex/codegen/function_types.h`) holds a raw `FunctionNode*`, and `FunctionGraph::removeFunction` erased the owning `unique_ptr` without touching the edges that pointed at it, so emission read `targetFn->name()` from freed memory and printed a symbol that belonged to an unrelated function, which is what the link's `undefined symbol` errors named. `removeFunction` now rewinds every affected edge back into an address-keyed unresolved jump (`FunctionNode::rewindEdgesTo`), which the graph re-resolves to whatever ends up owning that address — the re-registered function, an internal label, or an explicit `FATAL` line instead of freed memory. `tryResolveAgainst` / `tryResolveAgainstImport` also stopped promoting every re-resolved jump to a tail call, so a `bl` site no longer loses its call. **(4) Measured.** Applying the patch forces exactly one regeneration per project — the `codegen=` behaviour version in the fingerprint goes from absent to 3 (2 before the 2026-09-29 extension), so every existing stamp mismatches once, by design; after that an unchanged tree is a 0.4 s skip instead of a 160 s Release analysis (measured on this project: `0 written, 0 unchanged, 1 module(s) up to date`), and `ninja` no longer schedules the codegen edge at all because the manifest mtime is left alone. With `config/functions.toml` trimmed to its three oldest entries it also took codegen's own gap-fill discovery from 0 of the other 21 addresses to 20.; **(5) The unclaimed run** (added 2026-09-29). `splitRegionOnTerminators` proposes an entry only where a *segment starts*, and a segment whose start is an already registered address is skipped whole - so the 24-byte comparator `0x827EC038`, which follows the tail branch that ends `sub_827EBE08` and whose address is mentioned nowhere in the image (measured: no dword in the decrypted image equals it in either byte order, and no direct branch reaches it; the guest builds it in a register), stayed unregistered and the first indirect call into it trapped. Gap fill now also registers the runs of words inside a code region that no function's **extent** claims, when the word in front of the run leaves control (a return, an indirect branch, or an unconditional branch; a `bl` falls through and does not count). The extent rather than the blocks is the test: the words between a function's blocks still belong to it, and registering them as an entry shadows that function in `getFunctionContaining`'s nearest-base lookup - measured, the block-based first cut registered the dead `b` words `0x823DD6FC` and `0x823DD738` inside `sub_823DD6C0` and emission then lost the conditional branches to `0x823DD73C`. Measured with `config/functions.toml` trimmed to its three tail-branch entries: all 21 indirect-call addresses are registered and the table holds 38,457 entries against the pre-fix 38,438; those 21 entries are gone from the file, and with it emptied to a comment codegen still finds 23 of the historical 24 addresses and Validate names `0x8243C688` as the one it does not, which is why that entry stays (B-006 in [../docs/known-issues.md](../docs/known-issues.md)). | yes |
| `rexglue-sdk/0006-content-container-packages-and-extra-content-root.patch` | The content manager handled one form of content item: a directory. That is what this runtime installs (`InstallContent()` extracts a package, `CreateContent()` makes one), but not what a console — or a dumped DLC folder — stores, which is the STFS container itself; a package file was therefore skipped at enumeration and, if the guest opened one by name, mounted as a directory that is not one. Upstream Xenia picks a container-backed package for a file (`ContentPackageContainer`) and a directory-backed one otherwise (`ContentPackageDirectory`), and this restores that, including reading the license bits from the container when there is no `.header` file beside it. It also adds a second, read-only content root laid out as `<root>/<title_id>/<content_type>/<name>` (no xuid level), which enumeration and opening fall back to while create/thumbnail/install/delete resolve against the writable root alone, so a bundle directory next to the game data can be served without being writable. The container probe asks `file_info.path / file_info.name`, not `file_info.path`: `rex::filesystem::ListFiles` puts the **parent** directory in `FileInfo::path` and the entry name in `FileInfo::name` (as `GetInfo` does too), so the first cut tested the directory itself and every package was rejected as "not a content package" (fixed 2026-10-03 — the DLC package then enumerated and the aggregate enumerator's item count went from 0 to 1). This is what lets `game/dlc/…` deliver Rock Band 3's DLC to Blitz — [../docs/dlc.md](../docs/dlc.md). **Extended 2026-10-05** with a flat library: a dumped song folder is thousands of loose containers with no `title_id`/`content_type` levels, and on a volume that cannot hold links (exFAT) it cannot be indexed into the extra root's shape, so `set_extra_content_library(std::vector<ExtraContentItem>)` takes the same packages keyed by the title id and content type their **own headers** name, and enumeration (`ListContent`) and opening (`ResolvePackagePath`) consult that list beside the extra root; nothing is copied, renamed or linked. The name the guest carries (`XCONTENT_DATA::file_name_raw`, 42 bytes) is assigned host-side (`src/fs/dlc_library.h`) because a custom library's file names are longer than the field. The same patch now also names the content type and device in `XamContentAggregateCreateEnumerator`'s item-count line, which is how a library's enumeration is read back (`added 1401 items (device=0, type=00000001)`), and it gives `OpenContent` a license fallback for those sources: a package served from the extra root or a library has no `.header` beside it, so its license was only what the container carried, and an unsigned custom `CON` carries nothing — which Blitz read as "not licensed" and used to list a custom song and then refuse to start it. `OpenContent` now answers with the configured `license_mask` (the value `XamContentGetLicenseMask` already answers with) when such a package's own mask is zero, leaving a container that does carry entries, and an explicit `license_mask = 0`, untouched. | yes |

| `rexglue-sdk/0007-xamalloc-heap-flag-word.patch` | `XamAlloc_entry` (`src/kernel/xam/xam_info.cpp`) asserted that its first parameter is zero — `assert_true(unk == 0)` — while the implementation ignores it entirely (`SystemHeapAlloc(size)` is all that happens). The parameter is not an unknown: Xenia Canary spells it `flags` in its `XamAllocImpl`, identifies bit `0x00100000` ("HEAP_ZERO_memory used unless this flag") as the only bit it knows about, reports the top four bits as selecting one of several heaps, and allocates from the system heap without acting on them either. The assert therefore only rejected **in Debug** a call a Release build completes unchecked: with the assert compiled out, this title finishes its content enumeration and the whole offline route. The patch names the parameter `flags`, drops the assert, and leaves the allocation untouched (B-013 in [../docs/history/bringup-log.md](../docs/history/bringup-log.md)). | yes |
| `rexglue-sdk/0008-xex-image-bounds-checks.patch` | A XEX header describes its own layout — the header size, an optional-header table, the security info and its page descriptors, the file format info and, for a compressed image, the block table naming the length of each chunk of image data — and the loader read through every one of them with nothing but the image's length to stop it. A truncated copy therefore ran off the end of the mapping inside the load path: the first 1 MiB of this title's recorded image (1,048,576 bytes of 9,023,488) ended the process with `0xC0000005`, the loader's own `Loading XEX image: game:\default.xex` as the last line written and no diagnostic at all (B-014 in [../docs/history/bringup-log.md](../docs/history/bringup-log.md)). The patch checks each of those claims against the image's own length before anything reads through it — for a basic-compressed image by summing the block table against the data region, which is the layout this title and the Ultimate payload's own image use — and gives the module loader (`src/system/user_module.cpp`) a word for a file too short to hold a signature and for one it cannot map. Three measured shapes, one line, no identity line and a non-zero exit each, asserted with their numbers by [scripts/acceptance_wrong_data.ps1](../scripts/acceptance_wrong_data.ps1): 1 MiB → `its block table describes 9011200 bytes of image data, the image holds 1036288 after its 12288-byte header`; 2 KiB → `its header claims 12288 bytes, the image holds 2048`; 0 bytes → `Failed to map … (0 bytes) for module game:\default.xex`. It is a check on the container's own arithmetic, not on the image's contents: a valid image with a different digest is still only a warning, and the boot continues. A **normal**-compressed image (`compression_type = 2`, which neither this title nor the payload's image is) describes its data as a chain in which each block names the length of the next, so it cannot be summed before it is read and keeps the SDK's original de-block loop. | yes |
| `rexglue-sdk/0009-log-levels-table-not-cvars.patch` | `cvar::ApplyTomlTable` (`src/core/cvar.cpp`) walks every table in the profile, so the SDK's own `[log.levels]` convention produced a pending cvar named `log_levels_<category>` for every per-category log level. The logging subsystem reads that table straight out of the file (`ParseCategoryLevelsFromConfig` in `src/core/logging.cpp`) and never registers those keys, so they sat in the pending-value store for the whole run and would be named by `FinalizeInit`'s "unknown cvar" report alongside real typos — which made the report useless for its purpose. `[log.levels]` is now left to the logging subsystem. With the report trustworthy, this project finalizes the registry last, in `RbBlitzApp::OnPreLaunchModule`, so a misspelled key in `rb_blitz.toml` (or an unmatched `--flag`) is logged instead of silently dropped (the limit recorded in [../docs/known-issues.md](../docs/known-issues.md) on 2026-10-03). | yes |
| `rexglue-sdk/0010-input-system-state-filter.patch` | `InputSystem::GetState` merges every device assigned to a user, and the merge only ever *adds* input — so a title that wants to rebind the pad cannot do it with a driver, and the pad the guest is handed is the SDK's own translation with no seam over it. Adds `InputSystem::SetStateFilter`, a callback run on the merged state just before it is handed back, so a title can rewrite what the guest sees (used by [../src/input/remap.cpp](../src/input/remap.cpp) for the launcher's `[remap]` table, D17 in [../docs/plans/launcher-plan.md](../docs/plans/launcher-plan.md)). Ten lines: one `std::function` member, one setter, one call. Not a defect fix, so an upstream candidate only if the same need is general enough to be wanted there. | maybe |
| `rexglue-sdk/0011-deferred-overlapped-delay-cvar.patch` | `KernelState::CompleteOverlappedDeferredEx` slept a hard-coded 100 ms (`constexpr kDeferredOverlappedDelayMillis`) inside every deferred overlapped completion, to make the console's asynchronous storage look asynchronous. That is per completion, and a title that mounts a large content library pays it once per package: measured on a 1402-package library, `XamContentCreate` (which defers the whole mount through this path, `src/kernel/xam/xam_content.cpp`) is entered 2803 times on the way to the main menu, so 280 s of the 667 s the title's own "Discovering Downloadable Content" screen takes. The constant becomes the cvar `deferred_overlapped_delay_ms` (default **100**, the faithful value; a `uint32_t` in the `Kernel` category, hot-reload, and declared in `include/rex/system/flags.h` for a host to write through), read per completion and skipped entirely at 0. [../src/hooks/dlc.cpp](../src/hooks/dlc.cpp) sets it to 0 when R7's `enhancements_dlc_cache` toggle is on, because the cache alone answers for only the host's walk of the library and not for the per-package wait the toggle promises to remove. Nothing else changes: the completion still runs on the dispatch thread, still sets the overlapped, still queues the completion routine, so only the wait goes. Not a defect fix — the emulated latency is a deliberate choice — so an upstream candidate only if a settable delay is wanted there. | maybe |
| `rexglue-sdk/0012-stale-native-object-handles.patch` | `XObject::GetNativeObject` finds a guest-initialised object again through the handle it stashed in that object's dispatch header (`wait_list_flink` = `kXObjSignature`, `wait_list_blink` = the handle). Two things were wrong with trusting that handle unconditionally, and together they were a use-after-free rather than a mis-read (B-016 in [../docs/known-issues.md](../docs/known-issues.md), found while measuring a V-Sync-off boot): **(1)** nothing checked the *type* the header claims against the object the handle names - upstream's own `TODO: assert if the type of the object != as_type` - and handle slots are reused, so a structure the guest keeps using after the object behind it is gone resolves to whatever object holds the slot by then (measured: an `XThread` and an `XEnumerator`, 6 ms after the event that owned it was destroyed), which the typed entry points then read through a blind cast (`ev->Set(...)` is a direct call to `XEvent::Set`, so `+0x68` of a thread was read as its `event_` - that is the `event_ = 0xFFFFFFFFFFFFFFFF` of the two minidumps); and **(2)** `ObDereferenceObject_entry` (`src/kernel/xboxkrnl/xboxkrnl_ob.cpp`) resolved its pointer through `GetNativeObject`, which *creates* an object for a structure it has never seen, and then called `ReleaseHandle()` - the inverse of a reference this SDK hands out, and one it had not handed out here - dropping the creation's only handle reference, removing the object from the object table and letting it be deleted with the signature and handle still stashed in the guest's own KEVENT. Adds `XObject::LookupNativeObject` (follow the stashed handle only while it names an object of the type the header claims; never create), makes `GetNativeObject` treat an unfollowable handle exactly like a structure that was never initialised, checks the caller's type expectation in `GetNativeObject<T>` on top of the header's, and uses the lookup in `ObDereferenceObject`. Anomalies are logged (rate-limited `object: ...` warns) instead of faulting. **Standing limit:** a handle recycled by an object of the same type is still followed, because no pointer-to-object link is recorded for structures the guest initialises itself. | yes |

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
`include/rex/graphics/d3d12/texture_cache.h`,
`include/rex/graphics/pipeline/texture/cache.h`,
`src/graphics/d3d12/texture_cache.cpp`, `src/graphics/pipeline/texture/cache.cpp`,
`include/rex/ui/graphics_upload_buffer_pool.h`,
`include/rex/ui/d3d12/d3d12_upload_buffer_pool.h`,
`src/ui/graphics_upload_buffer_pool.cpp` and
`src/ui/d3d12/d3d12_upload_buffer_pool.cpp` for 0002; `src/kernel/xboxkrnl/xboxkrnl_io.cpp` for 0003;
`src/kernel/xboxkrnl/xboxkrnl_video.cpp`, `src/graphics/command_processor.cpp`
and `src/kernel/xam/xam_input.cpp` for 0004; `src/codegen/manifest.cpp`,
`src/codegen/output_stamp.cpp`, `src/codegen/phase_gapfill.cpp`,
`src/codegen/function_graph.cpp` and `include/rex/codegen/function_node.h` for
0005; `include/rex/system/xam/content_manager.h`,
`src/system/xam/content_manager.cpp` and `src/kernel/xam/xam_content_aggregate.cpp`
for 0006; `src/kernel/xam/xam_info.cpp` for
0007; `src/system/xex_module.cpp` and `src/system/user_module.cpp` for 0008; `src/core/cvar.cpp` for 0009;
`include/rex/input/input_system.h` and `src/input/input_system.cpp` for 0010;
`include/rex/system/flags.h` and `src/system/kernel_state.cpp` for 0011);
`include/rex/system/xobject.h`, `src/system/xobject.cpp` and
`src/kernel/xboxkrnl/xboxkrnl_ob.cpp` for 0012).

`ignore = dirty` hides work-tree edits only. A **moved gitlink is still
reported**: verified 2026-09-19 by pointing the index entry at a different
commit, which produced `MM rexglue-sdk`. That is the case that matters — a pin
bump must never be silent.

Because the setting also hides *accidental* SDK edits, `apply_sdk_patches.ps1`
audits the work tree on every run: each modified file must match a patch here
(the `index` line is ignored, since its abbreviation length follows
`core.abbrev`). Anything else is listed as `UNEXPECTED` and the script exits 1.

```
SDK work tree      : 33 patched, 0 UNEXPECTED, 0 untracked
    patched    : include/rex/codegen/function_node.h
    patched    : include/rex/graphics/d3d12/shared_memory.h
    patched    : include/rex/graphics/d3d12/texture_cache.h
    patched    : include/rex/graphics/pipeline/texture/cache.h
    patched    : include/rex/input/input_system.h
    patched    : include/rex/system/flags.h
    patched    : include/rex/system/xam/content_manager.h
    patched    : include/rex/system/xobject.h
    patched    : include/rex/ui/d3d12/d3d12_upload_buffer_pool.h
    patched    : include/rex/ui/graphics_upload_buffer_pool.h
    patched    : src/codegen/function_graph.cpp
    patched    : src/codegen/manifest.cpp
    patched    : src/codegen/output_stamp.cpp
    patched    : src/codegen/phase_gapfill.cpp
    patched    : src/core/cvar.cpp
    patched    : src/graphics/command_processor.cpp
    patched    : src/graphics/d3d12/texture_cache.cpp
    patched    : src/graphics/pipeline/texture/cache.cpp
    patched    : src/input/input_system.cpp
    patched    : src/kernel/xam/xam_content_aggregate.cpp
    patched    : src/kernel/xam/xam_info.cpp
    patched    : src/kernel/xam/xam_input.cpp
    patched    : src/kernel/xboxkrnl/xboxkrnl_io.cpp
    patched    : src/kernel/xboxkrnl/xboxkrnl_ob.cpp
    patched    : src/kernel/xboxkrnl/xboxkrnl_video.cpp
    patched    : src/system/kernel_state.cpp
    patched    : src/system/user_module.cpp
    patched    : src/system/xam/content_manager.cpp
    patched    : src/system/xex_module.cpp
    patched    : src/system/xobject.cpp
    patched    : src/system/xthread.cpp
    patched    : src/ui/d3d12/d3d12_upload_buffer_pool.cpp
    patched    : src/ui/graphics_upload_buffer_pool.cpp
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
files named in the patch set (the files the eight rows above name). The parent should be
clean: `.gitmodules`
sets `submodule.rexglue-sdk.ignore = dirty`, so the applied patches are no longer
reported as a modified submodule, and `apply_sdk_patches.ps1` audits whatever that
setting hides. Re-run the scripts after `git submodule update`, a fresh clone, or
any checkout that rewrites the SDK tree. Background:
[../docs/history/bringup-log.md](../docs/history/bringup-log.md) B-001 and
[../docs/known-issues.md](../docs/known-issues.md).
