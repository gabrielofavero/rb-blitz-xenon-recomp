# Known issues and standing limitations

Enduring facts only. The **chronology** — what was tried, in what order, and what
the logs said — lives in [bringup-log.md](bringup-log.md); this file is the index
of things that are still true and will still be true next session.

Issue IDs (`B-00N`) are allocated in `bringup-log.md`. A merged or disproved issue
is removed from this file, not from the log.

## Open

| Issue | What it is | Where |
| --- | --- | --- |
| **B-006 (class, not closed)** | Functions found only through an indirect call — no PDATA, no static `bl`, or a hole inside a run of 8-byte adjuster thunks — are missing from the register table and appear at runtime as `[FATAL] Call to invalid or unregistered function at guest address 0x…`. Fix = add `[functions."0x…"]` (no `size`/`end`) to [config/functions.toml](../config/functions.toml), re-run codegen, rebuild. | [bringup-log.md](bringup-log.md) |
| **B-009 (fix in place, unverified)** | Music was silent because the `xboxkrnl` `XeKeysSetKey`/`XeKeysAesCbc` layer is a `REX_EXPORT_STUB` no-op in ReXGlue; [src/hooks/crypto.cpp](../src/hooks/crypto.cpp) intercepts both imports and forwards to `XeCryptAesKey`/`XeCryptAesCbc`. **Not yet compiled or run** — see the blocker below. | [bringup-log.md](bringup-log.md) B-009 |
| **No C++ toolchain on the development machine** | No LLVM/CMake/Ninja/VS Build Tools/Windows SDK; only git/python/node are on `PATH`. Blocks every build. | [build-and-run.md](build-and-run.md) §1 |
| **Checked-in build tree is stale** | `out/build/win-amd64-release` was generated at a since-deleted path, so `CMAKE_HOME_DIRECTORY` mismatches. Must be wiped and reconfigured; back up the gitignored `rb_blitz.toml` runtime profile first. | [build-and-run.md](build-and-run.md) §2 |
| **No unit tests** | The port has zero automated tests, so fixes are "verified once and forgotten". Pure host code (starting with the B-009 key-table deobfuscation) can be tested without booting the game. | [rb3-references.md](rb3-references.md) §7.3 |
| **`longjmp_address` / `setjmp_address` unset** | The 0.10 SDK supports both at `[entrypoint]` level and `band3_recomp` sets them; [rb_blitz_manifest.toml](../rb_blitz_manifest.toml) sets neither, so guest `longjmp`/`setjmp` has no host bridge. | [rb3-references.md](rb3-references.md) §8 |
| **Hooks carry no "why is this disabled" record** | A hook that early-returns is a bug unless it is documented as a deliberate deviation. Same failure class as the RB3 port's "faithful behaviour switched off" audit. | [rb3-references.md](rb3-references.md) §7.3, §9 |
| **Codegen `GapFill` candidate fix (not applied)** | `rexglue-sdk/src/codegen/phase_gapfill.cpp` `splitRegionOnTerminators` should also split on `bctr` when the following word is a known function entry. Upstream fix, plan fix-order #6. | [symbols.md](symbols.md) |
| **Local SDK patch is applied inside the SDK working tree** | `rexglue-sdk/src/system/xthread.cpp` (pinned SDK `c94f5ebd`) logs the "Too few processor cores - scheduling will be wonky" warning **once per process** instead of on every thread creation and CPU migration, which flooded the log (~580 KB per boot). Tracked as [patches/rexglue-sdk/0001-xthread-log-once-core-count-warning.patch](../patches/rexglue-sdk/0001-xthread-log-once-core-count-warning.patch) and re-applied after a checkout by `scripts/apply_sdk_patches.ps1`. The submodule therefore always reports ` M src/system/xthread.cpp`; that is expected, not drift. Upstream candidate. | [patches/README.md](../patches/README.md) |
| **SDK symlinks are flattened — `lzxd.c` can break the build** | 16 tracked symlink entries under `rexglue-sdk/thirdparty/` (15 `libmspack/cabextract/mspack/*`, 1 o1heap `CLAUDE.md`) exist as regular files here: the machine cannot create symlinks (not elevated, Developer Mode off) and the submodule repos inherit `core.symlinks=false` from the system gitconfig. The SDK **compiles** `cabextract/mspack/lzxd.c`, so a checkout that writes link text instead of source (29 bytes vs 30,796) fails to compile. Setting `core.symlinks true` in the submodules is *not* a fix — checkout then fails outright (`Function not implemented`, exit 255, file missing). `scripts/repair_flat_symlinks.ps1` re-expands the files (idempotent) and marks them `--skip-worktree`. | [bringup-log.md](bringup-log.md) B-001, [patches/README.md](../patches/README.md) |
| **9 SDK submodules are uninitialised** | `catch2`, `glslang`, `moltenvk`, `spirv-headers`, `spirv-tools`, `vulkan-headers`, `vulkan-loader`, `vulkan-memory-allocator` and `simde/test/munit` are absent; the D3D12 build path used so far does not need them (milestones 1–3 configured and built without them). If a configure starts asking for one: `git -C rexglue-sdk submodule update --init --recursive thirdparty/<name>`. | `git -C rexglue-sdk submodule status` |

## Accepted (by design, not bugs)

- Offline services stay honest `STUB`s — `XamVoiceSetMicArrayIdleUsers`,
  `XeKeysSetKey`, `XeKeysAesCbc` are **not** faked to success (B-009 is the one
  deliberate exception, and it is a real implementation, not a fake).
- `update:\gen\patch_xbox.hdr` → `0xc000000f`: no title update in this dump.
- Three key/voice `STUB` warnings at boot.
- Online features (achievements, leaderboards, DLC enumeration) are offline-
  disabled by policy until the core loop works.
- The project is **GPL-2.0-only**, deliberately not "or later": GPL-2.0 is what
  keeps us compatible with the GPL-2.0 Rock Band 3 recompilation project we adapt
  code from. Do not relicense, and do not add GPL-3.0-only or otherwise
  incompatible third-party code ([README.md](../README.md)).
- Borrowed code must carry provenance. Anything adapted from another project
  keeps its original license, says so in a header comment, and gets a row in the
  provenance table in [rb3-references.md](rb3-references.md) §9.
