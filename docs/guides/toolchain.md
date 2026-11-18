# The frozen toolchain

The current build toolchain and historical accepted sets, recorded in the
repository rather than in one machine's build tree. This file is the prose half;
the target record in `config/` is the machine-readable half, and
`rb_blitz_toolchain --check` (built from
[tools/toolchain_check.cpp](../../tools/toolchain_check.cpp), run by every build before
codegen) is what compares a machine against it.

The experimental iOS device target selects `config/toolchain-ios-arm64.toml`
with the `ios-arm64-release` preset (iOS 17.0 minimum). Full Xcode supplies the
iPhoneOS SDK. Native Mac `rexglue`, fingerprint and toolchain executables run
build-time work; they are imported into the cross-build rather than compiled
for and executed as iPhone binaries. See [the CLI guide](../../installer/ios/README.md).
The separate `ios-arm64-importer-release` preset inherits this toolchain and
links the native first-launch importer; the Mac also embeds its pinned importer
configuration. Both presets compile native code on the Mac. The importer performs
one-time asset extraction/preparation on device, then reuses verified data.
This bootstrap record is not device acceptance evidence.

## Why it is frozen

Until 2026-09-28 the only record of "which toolchain does this build work with" was
the absolute `CMAKE_CXX_COMPILER` / `CMAKE_MAKE_PROGRAM` / `CMAKE_COMMAND` paths in
`out\build\win-amd64-release\CMakeCache.txt` — one machine's file, recreated by every
clone, and silently different on any other box. That is the same failure mode the game
dump had before [config/game_fingerprints.toml](../../config/game_fingerprints.toml): the
knowledge existed, but only where nobody could read it. The dump is now recorded and
enforced; this file is the toolchain doing the same.

Two things follow from it being recorded rather than assumed:

- a build **states** the toolchain it is about to use, in its own output, every time;
- a built binary **states** it too, at boot, next to the build and game data identity:
  `toolchain: rexglue-sdk c94f5eb, clang 23.1.1, … (frozen set)`.

## Target records

`config/toolchain.toml` is the current Windows x86-64 build record described below. Older acceptance measurements retain their original SDK/toolchain scope. The
Linux x86-64, macOS arm64 and macOS x86-64 records have their own files in
`config/`. CMake selects the record by target OS and architecture and passes both
`--pin` and `--target` to the checker; a mismatched record is an error. The new
records contain the SDK commit and bootstrap minimums. Their versions have not
been measured on native target builders and are **not** release acceptance pins.
They must be updated after native configure, codegen, link and gameplay evidence.
The macOS records require AppleClang 17+ from Xcode 26+ and target macOS 13.3
or newer. LLVM Clang has a separate 20+ floor and needs a matching record. The
checker requires the recorded compiler family, so the AppleClang version floor
does not admit an older LLVM compiler.
Prompt 1's native store adapter also requires libcurl headers and library
version 7.62 or newer for URL parsing. The first native builders must record
the exact libcurl version and whether the final package declares or bundles it.

**Pre-v0.11 native bring-up measurement, 2026-11-15:** Apple M1, macOS 27.2.1;
Xcode 26.0.1 build 17A400, AppleClang 17.0.0, Apple SDK 26.0, CMake 3.31.6,
Ninja 1.13.2, Python 3.12.16, and the SDK's system libcurl 8.7.1 headers/stub.
The game linked with `mac-arm64-release`; configure/build logs are under
`out/macos/`. The current toolchain TOML remains a bootstrap record until
native gameplay is accepted. The system-selected Command Line Tools had
AppleClang 14.0.3/SDK 13.3, so this pass selected a side-by-side Xcode through
`DEVELOPER_DIR` instead of changing the machine's global selection.

**Pre-v0.11 packaging measurement, 2026-11-16:** the same Xcode 26.0.1/17A400, SDK 26.0 and
AppleClang 17.0.0 inputs built the direct Release target and audited development
app/package. `installer/config/platform-packaging.json` now pins those packaging
inputs under `macos.nativeValidation.arm64`; the packager rejects another selected
Xcode/SDK/compiler. The seven Mach-O images use bundled dependencies and Apple
system libraries, including system libcurl. This freezes a reproducible local
packaging route; it does not close installed-player release acceptance or establish
Intel/clean-minimum-OS support. Evidence: `out/macos/installer-package.log` and
the sibling package `.audit.json` under `out/macos/packages/`.

## Current v0.11 record

[config/toolchain.toml](../../config/toolchain.toml) records SDK `0.11.0` at `a3e1d82678ca20d68d4837a8e9ee4652ff073745`, LLVM Clang `20.1.8` (minimum 20), CMake `4.4.0` (minimum 3.25), Ninja `1.13.2`, MSVC `14.44.35207` and Windows SDK `10.0.26100.0`. The SDK revision is an exact gate. Updated build pins do not inherit older gameplay or native-platform acceptance evidence.

The parent CMake file adds the modular SDK directly and calls `rexglue_project`; `generated/rexglue.cmake` is obsolete. Title integration uses public headers under `include/rex/` and optional `TitleRuntime` services. Private `src/` includes belong only to explicit SDK regression targets. Removed runtime, Device/Entry and cvar APIs are not restored.

The project preserves the selected install prefix before configuring SDK
dependencies. Relative prefixes resolve against the build directory; explicit
absolute prefixes keep their values. This prevents pinned glslang from caching
a relative Windows default that breaks later SDK regeneration.

### Platform validation scope

The migrated SDK build, CTest and title-link evidence currently comes from Windows AMD64; see the [migration record](../plans/in-progress/2026-11-17-rexglue-v0.11-migration-plan.md). Windows ARM64 native and cross-link validation is unavailable here: MSVC toolset `14.44.35207` has no `lib/arm64` libraries, although Windows SDK `10.0.26100.0` supplies `um/arm64`. AMD64 results do not establish ARM64 correctness. Native Linux, macOS and iOS migration acceptance remains pending; older Apple measurements below describe the former SDK.

### Pinned Vulkan sources

The SDK manifest `cmake/rexglue_vulkan_stack.json` owns these exact sources:

| Source | Commit |
| --- | --- |
| Vulkan-Headers | `e3b1eec08173d6b825cd3ac88c885a63b621504a` |
| Vulkan-Loader | `5f157b62e333c63260d05d81bf66faa216ab0fb8` |
| VulkanMemoryAllocator | `1d8f600fd424278486eade7ed3e877c99f0846b1` |
| MoltenVK | `db445ff2042d9ce348c439ad8451112f354b8d2a` |

Run [prepare_vulkan_stack.py](../../scripts/setup/prepare_vulkan_stack.py) before configure, then pass its source root as `REXGLUE_VULKAN_SOURCE_ROOT`. The canonical nested [MoltenVK patch](../../patches/rexglue-sdk-v0.11/thirdparty/moltenvk/0001-retain-presentation-drawable.patch) is checked against the SDK's bundled copy (SHA-256 `46b6637d99c4fe3eea73c3660cbc07cebc4bad4edb5fc60993ed27d58e48a886`) and replayed before use. `--check --offline` audits existing sources; `REXGLUE_FETCH_VULKAN_DEPS=OFF` prevents configure-time Vulkan fetches. See [build-and-run.md](build-and-run.md) §0 for commands and mirrors. Other SDK dependency caches remain a separate offline prerequisite.

The project `REXGLUE_USE_VULKAN` option maps to SDK `REXGLUE_GPU_VULKAN`; both default ON. OFF skips Vulkan acquisition and targets for D3D12-only Windows builds. Windows loader compilation requires MASM from Visual Studio. Desktop Apple source-builds patched MoltenVK; iOS links it directly without a desktop loader/ICD. Native Linux/macOS/iOS validation of this migrated source stack remains pending.

## Historical accepted set (2026-09-28)

Measured on the machine the acceptance runs are made on, Windows x86-64
(`win-amd64-release` / `win-amd64-debug`). It is what the clean-checkout acceptance run
of 2026-09-28 was made with — three runs, `launch-to-results 3 / 3`, each a 315 s
envelope — so the record starts out justified by evidence rather than aspirational
([history/bringup-log.md](../history/bringup-log.md), "Clean-checkout smoke test").

| Component | Version | Where it is found | Why it is in the set |
| --- | --- | --- | --- |
| ReXGlue SDK | commit `c94f5ebd…` (`nightly-20260826-f5337cdc-2-gc94f5eb`) | `rexglue-sdk/` submodule | It generates the recompiled code and is the runtime the host links against: its exports, its codegen, its kernel behaviour. A different commit is a different emulator. Pinned by the submodule gitlink, so a clone refuses another one before this check ever runs. |
| clang / LLVM | `23.1.1` (minimum `20`) | `clang++` on `PATH` | Builds the SDK from source, the recompiled partitions and the host code: C++23, the guest-SEH `-fasync-exceptions` flag, and the `-march=x86-64-v2` the presets pass. `20` is the SDK's own floor — its `CMakeLists.txt` fails with *"ReXGlue requires Clang 20 or newer"* below it — so the record refuses nothing the SDK would accept. |
| CMake | `4.4.3` (minimum `3.25`) | `cmake` on `PATH` | Owns the presets, the codegen integration in `generated/rexglue.cmake`, the object-library wiring and the fingerprint gate. `3.25` is the same floor `cmake_minimum_required` states. |
| Ninja | `1.13.2` | `ninja` on `PATH` | The generator the presets use; the codegen stamp/depfile logic and the incremental build are Ninja's. |
| MSVC toolset | `14.44.35207` | read off the compiler's include roots | Supplies the C++/UCRT headers and the import libraries clang links against. This is the half that fails as a missing `<windows.h>` rather than as a missing compiler, which is why the check reads it off the compiler's include roots instead of trusting `PATH`. Any Visual Studio 2022 Build Tools install with the C++ workload and a Windows SDK will do. |
| Windows SDK | `10.0.26100.0` | read off the compiler's include roots | Win32/D3D12 headers and libraries; same failure mode as the toolset. |

Where each tool lives is deliberately **not** recorded in the file. That is a property of
the machine a build runs on, not of the project, and a committed path is one developer's
disk leaking into everyone else's checkout. The check prints the location it actually used
on the machine it ran on, which is what a reader compares against their own box. A `path`
key is still parsed if you want to keep one locally, but it is informational and never
compared.

## What the check does

The sample below records the old accepted set; current output uses the v0.11 record above.

```
toolchain  rexglue-sdk c94f5eb, clang 23.1.1, CMake 4.4.3, Ninja 1.13.2, MSVC toolset 14.44.35207, Windows SDK 10.0.26100.0  (config/toolchain.toml)
ok        rexglue-sdk     c94f5eb           nightly-20260826-f5337cdc-2-gc94f5eb
ok        clang           23.1.1            <the clang++ this machine resolved>
ok        CMake           4.4.3             <the cmake this machine resolved>
ok        Ninja           1.13.2            <the ninja this machine resolved>
ok        MSVC toolset    14.44.35207       from the compiler's include roots
ok        Windows SDK     10.0.26100.0      from the compiler's include roots
ok         the frozen toolchain
```

It probes the compiler with `clang++ -v -E -x c++ <empty file>` (the same command
[build-and-run.md](build-and-run.md) §1 suggests by hand), and reads the MSVC toolset and
the Windows SDK off the include roots in that output; `--version` answers for CMake and
Ninja, and `git rev-parse HEAD` for the SDK checkout. The first line is the frozen
record, the rest is this machine.

| Verdict | Meaning | Build |
| --- | --- | --- |
| `ok` | reported value == the record | builds |
| `different` | not the frozen value, but not below the minimum either | builds, reported. The frozen set is what the last acceptance run was made with, not a wall in front of the next developer |
| `too old` | below the recorded `minimum` | **stops** |
| `unknown` | the probe could not read it (no such tool, none of that component on this host) | builds, reported. A build that really cannot use its toolchain has its own error to give |

Two more things stop a build: the SDK on a commit other than the pin (a commit has no
ordering to argue about), and anything at all under `--strict`.

Run it by hand on any box — this is what the build runs, and it is how to find out
whether a build failure is a toolchain failure:

```powershell
# from a configured build tree
.\out\build\win-amd64-release\rb_blitz_toolchain.exe --check --project-dir .
# against another interpreter, or another SDK checkout
.\out\build\win-amd64-release\rb_blitz_toolchain.exe --check --pin config\toolchain.toml --sdk-dir rexglue-sdk
```

Exit codes: `0` usable, `1` a deviation that stops a build, `2` usage or a malformed
record (a record that cannot be read is not a way past the check).

### The two switches

| CMake option | Effect | When |
| --- | --- | --- |
| `-DRBBLITZ_STRICT_TOOLCHAIN=ON` | every `different`/`unknown` becomes fatal too | a release build that wants the frozen set or nothing |
| `-DRBBLITZ_ALLOW_OTHER_TOOLCHAIN=ON` | nothing is fatal, everything is reported | building on a box that is knowingly not the frozen set |

Both are cached like any CMake option, so turn them back off by naming them
(`=OFF`) rather than leaving one set in a build tree.

## How it reaches a build and a log

1. `rb_blitz_toolchain_gate` runs before codegen, next to the game-data gate, on every
   build: `[N/M] Checking the toolchain against config/toolchain.toml`. A fatal
   deviation stops the build there, before anything is translated.
2. `--emit-header` writes `out/build/<preset>/generated/toolchain_build.h` (not committed;
   each host/device build keeps its own stamp)
   with `RBBLITZ_TOOLCHAIN_STAMP` — what *this* machine reported, so a binary built off
   the frozen set says so.
3. [src/rb_blitz_app.h](../../src/rb_blitz_app.h) logs that stamp at boot, between the
   build identity and the game data identity, which is the line that makes a bug report
   self-describing.

## What is not covered

- **The installer's own toolchain.** [installer/](../../installer) needs Inno Setup 6
  (`ISCC`) and PowerShell 5.1; neither is in the record or checked.
- **Native Linux/macOS acceptance.** Target records now prevent Windows MSVC and
  Windows SDK values from being compared on those hosts, but native compiler,
  OS ABI and graphics evidence is still pending. See the
  [platform expansion plan](../plans/in-progress/2026-11-09-linux-macos-release-plan.md).
- **Python.** Python 3.10+ runs SDK patch/dependency preparation and tooling. The checker does not freeze its interpreter version.
- **The graphics driver and the machine itself.** Frame pacing, and whether a graphics
  backend can run at all, are properties of the host too ([known-issues.md](../reference/known-issues.md)):
  compiled backend targets determine Renderer rows. Vulkan defaults ON but can be omitted explicitly; runtime probes a usable driver before offering it. Source pins and a bundled loader do not freeze the installed graphics driver.

- **Byte-for-byte reproducibility.** The check says which toolchain built a binary, not
  that two machines produce identical bytes; the build stamp carries a timestamp, and no
  build-id or timestamp pinning is attempted.
- **The Visual Studio product version** (17.14.41) is recorded here and not checked: the
  tool reads the toolset and SDK versions off the include roots, because that is what the
  build actually uses.

## Changing the record

Bumping a version is a decision, not a discovery, so it goes through the acceptance run:

1. install the new component and re-check what the machine reports:
   `rb_blitz_toolchain --check --project-dir .` prints the new values;
2. update [config/toolchain.toml](../../config/toolchain.toml) and the table above;
3. run the acceptance route
   (`.\scripts\acceptance\acceptance_song.ps1 -Runs 3`) and record it in
   [history/bringup-log.md](../history/bringup-log.md);
4. commit the record change together with that evidence — the frozen set is only
   meaningful if the run that justifies it is named.

If the components are newer but nobody has run the acceptance route on them yet, leave
the record alone: a `different` line in the report is exactly that statement, and it
still builds.


## ReXGlue v0.11 optimization integration (2026-11-18)

The project uses release commit
`a3e1d82678ca20d68d4837a8e9ee4652ff073745`. These mechanisms are verified in
the pinned source and current build inputs; they are not measured speedups.

| Mechanism | Integration |
| --- | --- |
| Address translation and page-protection reads | Upstream `bd752001` and `5b033656` remove runtime overhead automatically; no title switch is needed. |
| Thread/detour state access | Upstream `66b73123` reads state through the PPC context automatically. |
| Worker idle CPU use | Upstream `ecf56926` uses work notifications/backoff; the migrated ordered ring-publication patch preserves nonblocking submission. |
| Vector stores and tail calls | Upstream `55dd9b0b` fuses eligible vector stores and emits eligible tail calls during regeneration. Exception scopes, hooks and setjmp/longjmp retain their safety boundaries. |
| Fast vector math | The manifest has no exact-mode overrides; the nine selectable instruction families retain upstream fast defaults. `vlogefp` remains exact. |
| Stack-store forwarding | The entrypoint manifest explicitly enables `forward_stack_stores = true`. Same-block stack reloads can reuse proven local values; writes still update guest memory. Calls, labels, aliases, barriers and hooks invalidate affected state. |
| GPU setup and state reuse | Upstream paint-pipeline reuse, no-op depth rejection and D3D12 sampler-layout fixes are automatic. Supported bindless, native-cache, shader-storage and asynchronous compilation defaults remain enabled; Vulkan feature support is checked by the driver. |

Release compilation uses Clang 20.1.8 with `-O3`; the title does not replace it
with a lower generated-code optimization level. Existing correctness settings,
including required D3D12 readback, remain in force. Old register-localization
switches and global unsafe floating-point compiler flags are separate choices,
not requirements for these v0.11 mechanisms.

The forwarding comparison preserves all 38,458 registrations and 38,196 function
bodies. It replaces 2,215 stack-load macro occurrences with local reads. The
final SDK suite passes all 16 forwarding unit cases and the nine dedicated
forward/direct PPC sequences. These counts establish emitted-code and test
coverage, not CPU/GPU timing gains. Windows AMD64 candidate validation does not
establish native Linux, Apple or ARM64 behavior. Matched performance and the
production adoption state remain recorded in the
[migration plan](../plans/in-progress/2026-11-17-rexglue-v0.11-migration-plan.md).
