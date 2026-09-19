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
| **Local SDK patch lives only in the submodule working tree** | `rexglue-sdk/src/system/xthread.cpp` (pinned SDK `c94f5ebd`) has a local edit that logs the "Too few processor cores - scheduling will be wonky" warning **once per process** instead of on every thread creation and CPU migration, which flooded the log. It is uncommitted and would be lost by re-cloning or wiping the SDK. Upstream it, or move it to a tracked patch. | `git -C rexglue-sdk diff` |

## Accepted (by design, not bugs)

- Offline services stay honest `STUB`s — `XamVoiceSetMicArrayIdleUsers`,
  `XeKeysSetKey`, `XeKeysAesCbc` are **not** faked to success (B-009 is the one
  deliberate exception, and it is a real implementation, not a fake).
- `update:\gen\patch_xbox.hdr` → `0xc000000f`: no title update in this dump.
- Three key/voice `STUB` warnings at boot.
- Online features (achievements, leaderboards, DLC enumeration) are offline-
  disabled by policy until the core loop works.
