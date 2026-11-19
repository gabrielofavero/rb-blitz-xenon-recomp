# Build, run, and capture a log

How to turn a source change into a running `rb_blitz.exe`, and how to hand back
the log excerpt that blocker entries in [bringup-log.md](../history/bringup-log.md) need.

Everything below is PowerShell, run from the repository root unless noted.

For shared development shortcuts, source the root `dev.sh` in Bash or zsh (Git
Bash on Windows), then use `build-game`, `run-game --mobile`, or
`build-installer`. The dispatcher loads the native platform helper; iOS can be
selected explicitly on a Mac. See the [development command guide](../../scripts/dev/README.md)
for prerequisites, DLC discovery, portrait settings copies, and platform limits.

The Linux and macOS releases remain under bring-up. The internal Apple Silicon
build links on native hardware; Linux and Intel Mac acceptance remain pending.
Their target-specific bootstrap toolchain records are selected by CMake;
the required native configure/codegen/link sequence and blockers are in the
[platform expansion plan](../plans/in-progress/2026-11-09-linux-macos-release-plan.md).
The Windows commands below remain the current supported build route.
The current SDK requires LLVM Clang 20+ or AppleClang 17+ (Xcode 26+).
The macOS deployment minimum is 13.3; native validation of the migrated SDK
and modular runtime package remains pending on both Mac architectures.

Prompt 2's Linux desktop launcher and package staging code are in
[`installer/linux/README.md`](../../installer/linux/README.md). The Windows host
can validate the shared importer and package metadata, but a native Linux
configure/codegen/link and package dependency inventory remain required before
the Linux package command can run.
The corresponding Mac build and app/package staging entry point is
[`installer/macos/README.md`](../../installer/macos/README.md). It uses the Mac
presets and must run on native Mac hardware; no macOS release build is accepted yet.

The experimental iPhone/iPad builder is `sh scripts/ios/build.sh --source
'/path/to/package-or-folder'`. Full Xcode provides the iPhoneOS SDK; Mac tools
and device binaries use separate build trees. The IPA embeds the owner's private
assets and requires device signing. See [the iOS CLI guide](../../installer/ios/README.md)
for signing options and the exact unverified device gates.
A separate `sh scripts/ios/build-importer.sh` flow builds an IPA without retail
or Ultimate assets. On device it greets the player, imports a game file/folder
through Files, offers skip/local/GitHub choices for Ultimate, and reuses the
verified preparation on subsequent launches. The original bundled-assets flow
remains available; developer-side code generation still requires local game input.

The Mac direct builder is `sh scripts/macos/build.sh`, followed by
`sh scripts/macos/run.sh`. Supply `--source '/path/to/package-or-folder'` on the
first build to import and verify private `game/` input without navigating the
importer. Later builds verify that input and stage the validated controller art.
The builder also creates `out/macos/Run Rock Band Blitz.command`; it never starts
the game. Select Xcode through `DEVELOPER_DIR` and put CMake, Ninja, and Python
3.11+ on `PATH`. The same build pipeline feeds
`sh installer/macos/build.sh --development` for a local unsigned installer.
Add `--ultimate` to the direct builder for the pinned official Ultimate download,
or `--ultimate-source '/path/to/ZIP-or-folder'` for local import. The packaged
app offers the same optional mod setup after base import; its data never ships
inside the installer. Select Ultimate with the in-game Game Version setting.

The following native measurement predates v0.11 and is not acceptance for the migrated stack. On 2026-11-15, the internal `mac-arm64-release` build linked on an M1 with
Xcode 26.0.1 (17A400), AppleClang 17.0.0, SDK 26.0, CMake 3.31.6, and Ninja
1.13.2. This is bring-up evidence, not release acceptance. Select the intended
Xcode with `DEVELOPER_DIR` and put the tools and Python 3.11+ on `PATH`; apply
patches with `python3 scripts/setup/apply_sdk_patches.py`, then configure/build
`mac-arm64-release`. Launch the internal executable with
`out/build/mac-arm64-release/rb_blitz --game_data_root="<checkout>/game"`.
The native import lookup, protected-memory and signal-handler preservation
regressions have focused tests. The Mac-only `vulkan_fragment_gradient` CTest
renders synthetic triangles through the production SPIR-V arithmetic builder
and the pinned MoltenVK stack, without game assets or a window. It verifies the
fragment precision-helper workaround from SDK patch 0065 at 1Ã—/2Ã—/4Ã— and checks
separate arithmetic rounding. Build `rb_blitz_vulkan_fragment_gradient_tests`
and run `ctest --test-dir out/build/mac-arm64-release -R '^vulkan_fragment_gradient$' --output-on-failure`.
Logs and startup before/after captures for this pass are under `out/macos/`.
The owner performs game navigation and gameplay acceptance.

Prompt 1's SDK-free game importer can be built on a native host without the game
SDK or Inno Setup. From the repository root:

```sh
cmake -S installer/portable -B out/build/portable-import -G Ninja
cmake --build out/build/portable-import
ctest --test-dir out/build/portable-import -R '^portable_import$' --output-on-failure
```

This tests the shared transaction with synthetic game bytes and checks that
the repository's supported fingerprint rejects them. It does not establish a
Linux/macOS game build or graphical importer.

> **Where to start:** Â§0 is once per checkout and Â§1 once per machine. On a checkout whose
> toolchain and build tree are already in place, **Â§3 is the entry point** for a normal
> source change; Â§2 is only for a build tree that has been moved or copied.

## 0. Prepare the SDK submodule (once per checkout)

Initialize the pinned SDK and apply its registered stack before configuring. The applier reads the staged parent gitlink; [patches/sdk-stacks.json](../../patches/sdk-stacks.json) selects the matching baseline or v0.11 stack and rejects unknown revisions. Do not apply the old stack to v0.11.

```powershell
git submodule update --init --recursive
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup\apply_sdk_patches.ps1
python scripts/setup/prepare_vulkan_stack.py --source-root out/deps/rex-vulkan
$vulkanSources = (Resolve-Path out/deps/rex-vulkan).Path
cmake --preset win-amd64-release "-DREXGLUE_VULKAN_SOURCE_ROOT=$vulkanSources" -DREXGLUE_FETCH_VULKAN_DEPS=OFF
cmake --build --preset win-amd64-release
```

Vulkan source preparation is an explicit configure prerequisite. It verifies the SDK manifest's exact Headers, Loader, VMA and MoltenVK commits and canonical nested patch. Reuse the verified source root on later builds. `--mirror-root` or repeated `--mirror NAME=PATH` selects local Git mirrors. Audit populated sources without network access:

```powershell
python scripts/setup/prepare_vulkan_stack.py --source-root out/deps/rex-vulkan --check --offline
```

`REXGLUE_FETCH_VULKAN_DEPS=OFF` prevents Vulkan acquisition during configure; other SDK dependencies still need populated caches or network access. Windows D3D12-only builds use `-DREXGLUE_USE_VULKAN=OFF` and skip Vulkan preparation. See [toolchain.md](toolchain.md) for pins and validation limits. `apply_sdk_patches.ps1 -Check` audits patch state without writing.

The project preserves the selected CMake install prefix across SDK dependency
configuration. A relative `CMAKE_INSTALL_PREFIX` resolves against the build
directory; an explicit absolute prefix retains its value.

### Historical checkout repairs (2026-09-28)

The counts and removed dependency paths below describe the pre-v0.11 checkout, retained to explain the repair tool. They are not the current dependency inventory.

1. **`repair_flat_symlinks.ps1`** expands the SDK files that are symbolic links
   upstream into regular files holding the target's content. Windows here cannot
   create symlinks (not elevated, Developer Mode off) and the submodule repos
   inherit `core.symlinks=false`, so a checkout leaves them as link-text stubs. This
   is build-critical: the SDK compiles
   `thirdparty/libmspack/cabextract/mspack/lzxd.c`, which becomes a 29-byte text
   file (`lzxd.c` upstream is the real 30,796-byte source, under the same repo's
   `libmspack/mspack/`).
   How many entries the scan meets depends on which submodules are initialised, which
   is why the number in this paragraph moved when the smoke test of 2026-09-28 cloned
   with `--recurse-submodules`: **16** in this machine's long-lived tree, which has
   MoltenVK unchecked, and **18** in a full clone (those 16 â€” 15
   `libmspack/cabextract/mspack` files and o1heap's `CLAUDE.md` â€” plus two MoltenVK
   headers that link to a sibling file). Either way a healthy tree prints
   `repaired from target    : 0`, `not repairable          : 0` and exit 0, and marks
   what it repaired `--skip-worktree`, because the residual difference (a file *type*
   change, not content) cannot be resolved on this machine.
   A full clone also brings eight links with **no file to copy**, which are reported
   rather than repaired: one points at a *directory* (MoltenVK's
   `MoltenVK/include/MoltenVK` â†’ `../MoltenVK/API`) and seven point into MoltenVK's
   `External/` and `Package/` trees, which that submodule's own scripts fetch and which
   no target on this host compiles. They print as `directory links (skip) : 1` and
   `external targets (skip) : 7`; only a target the submodule *does* track and that is
   nevertheless absent is `not repairable` and exits 1, because that one means the
   checkout itself is incomplete. (Before 2026-09-28 all eight were fatal, so the script
   exited 1 on every fresh clone: [bringup-log.md](../history/bringup-log.md),
   "Clean-checkout smoke test".)
   **Do not** try to fix this by setting `core.symlinks true` in the submodules:
   checkout then fails with `unable to create symlink â€¦: Function not implemented`
   and deletes the file.
2. **`apply_sdk_patches.ps1`** replays the complete
   [SDK patch stack](../../patches/README.md), including the initialized pinned
   MoltenVK dependency, using the shared Python engine (Python 3.10+ on `PATH`).
   Initialize with `git submodule update --init --recursive` before applying.
   It verifies final source and pins before writing; conflicts or tracked edits
   outside patch coverage fail. `-Check` audits without writing. PowerShell accepts
   CRLF equivalents and reports unrelated untracked files; native Python requires
   exact bytes and rejects them. Both reject pin drift. Hosted Linux/Windows
   push/PR checks in `.github/workflows/sdk-patches.yml` exercise clean replay and
   repeated invocation without private game data or SDK compilation.

Afterwards the submodule showing ` M` on the files the patch set names is the
expected, correct state â€” they are the applied patches, not drift to be cleaned.
The parent repo stays clean, because `.gitmodules` marks the submodule
`ignore = dirty`; the audit above is what replaces the warning that setting
suppresses. Full rationale and manual equivalents:
[patches/README.md](../../patches/README.md).

## 1. Toolchain prerequisites

Check these before the first build and install whatever is missing â€” it is a one-time
cost, and every later rebuild is the incremental path in Â§3.

The versions in [config/toolchain.toml](../../config/toolchain.toml) are the **frozen set**
([toolchain.md](toolchain.md)): the toolchain the last recorded acceptance run was made
with. That is a record, not a wall â€” a different version is reported and still builds, and
only one below the record's `minimum` stops the build. So the installs below may be newer
than the record, or older down to the minimum, without any other change.

The build is a plain `Ninja` + `clang` build of the pinned SDK **from source**
(`REXSDK_DIR` points at `rexglue-sdk/`; the parent CMake file adds it directly
and calls `rexglue_project(rb_blitz OUTPUT_DIR generated/default)`), so the first build compiles SDL3, fmt, spdlog,
tomlplusplus, lzma, mspack and the ReXGlue runtime as well. That needs four
things:

1. **LLVM/Clang 20+** â€” supplies `clang`, `clang++`, `lld-link` and `llvm-ar`.
2. **MSVC build tools _and_ the Windows SDK** â€” the presets target
   `x86_64-pc-windows-msvc`, so clang needs the MSVC/UCRT headers and the Windows
   SDK import libraries. Clang on its own is **not** enough, and a failure here
   looks like a missing `<windows.h>` / `<vector>` rather than a missing
   compiler.
3. **CMake >= 3.25** on `PATH`.
4. **Ninja** on `PATH`.

Check all four in one go:

```powershell
foreach ($t in 'cmake','ninja','clang','clang++','lld-link') {
    $c = Get-Command $t -ErrorAction SilentlyContinue
    "{0,-10} {1}" -f $t, $(if ($c) { $c.Source } else { 'MISSING' })
}
New-Item -ItemType File -Force "$env:TEMP\probe.cpp" | Out-Null
clang++ -v -E -x c++ "$env:TEMP\probe.cpp" 2>&1 |
    Select-String 'Microsoft Visual Studio|Windows Kits'
```

All five tools must resolve, and the last command must list at least one MSVC
include directory and one Windows Kits include directory. If it lists neither,
clang is installed but the headers are not. An "ignoring nonexistent directory
`...\MSVC\<ver>\atlmfc\include`" note in that output is normal â€” ATL/MFC is an
optional VS component that this build does not use.

### Installing them

`winget` is available on this machine, so the install is:

```powershell
winget install --id LLVM.LLVM -e --accept-package-agreements --accept-source-agreements
winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements
```

> **Trap met installing this here:** `LLVM.LLVM`'s MSI installs to
> `C:\Program Files\LLVM`, but the silent install **does not add its `bin` to
> `PATH`** â€” the Â§1 check above still reports `clang`, `clang++` and `lld-link` as
> `MISSING` after a "Successfully installed". Append `C:\Program Files\LLVM\bin`
> to the machine `PATH` by hand and open a new shell. `Kitware.CMake`'s MSI does
> add itself, and `Ninja-build.Ninja` is a user-scope portable that adds its own
> alias.

The fourth one is the multi-GB part and needs an **elevated (Administrator)**
shell:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e `
    --accept-package-agreements --accept-source-agreements `
    --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

`--includeRecommended` brings in the MSVC x64 toolset plus a current Windows SDK;
allow 15â€“30 minutes and several GB. To pin them explicitly instead, add
`--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64` and
`--add Microsoft.VisualStudio.Component.Windows11SDK.22621` (or whichever SDK
revision is current) to the `--override` string.

Equally valid alternatives if one of them already exists:

- an existing **Visual Studio** install â€” open *x64 Native Tools Command Prompt
  for VS* and run the same `cmake --preset` commands from there, so `INCLUDE` and
  `LIB` are already set;
- **LLVM in a non-standard location** â€” the presets take the compiler from
  `PATH`, so either add its `bin` to `PATH`, or configure once by hand with
  `-DCMAKE_C_COMPILER=<path>\clang.exe -DCMAKE_CXX_COMPILER=<path>\clang++.exe`.

**Open a new shell afterwards.** Each terminal here is a fresh process, and a
newly added `PATH` entry only exists in sessions started after the install, so
re-run the check block above in a new window.

Then do a five-second end-to-end sanity check of exactly the chain the preset
uses, before spending tens of minutes on the SDK:

```powershell
Push-Location $env:TEMP
Set-Content -Path probe.cpp -Value 'int main() { return 0; }'
clang++ -fuse-ld=lld probe.cpp -o probe.exe
.\probe.exe; "exit=$LASTEXITCODE"
Pop-Location
```

`exit=0` means clang, lld and the MSVC/Windows SDK headers and libraries are all
mutually findable. If it instead reports a missing `libcmt.lib`, `ucrt.lib`,
`kernel32.lib` or `windows.h`, the MSVC/Windows SDK half of step 2 is incomplete
â€” that is the most common way this toolchain ends up broken.

## 2. Regenerate a stale build tree

**A build tree is only valid where it was configured.** `CMakeCache.txt` records the
absolute path the checkout had at configure time (`CMAKE_HOME_DIRECTORY`), plus the
absolute paths of the compiler, Ninja and CMake it used, and `build.ninja` repeats them in
its `-I` flags. That is why `out/` is gitignored and never shared: copy or move the
checkout, or build it from another drive, and CMake refuses to reuse the cache. Check
where a tree was configured with:

```powershell
Select-String CMAKE_HOME_DIRECTORY out\build\win-amd64-release\CMakeCache.txt
```

If that path is not where the checkout is now, delete the tree and regenerate it â€” do
**not** build into it as-is.

`out\build\win-amd64-release\rb_blitz.toml` is *not* a build artifact and CMake
never writes it: the runtime reads `<exe name>.toml` from its own directory (the
in-game settings overlay edits the same file), and this checkout's copy is the
hand-maintained local profile â€” `mnk_mode = true` (the deterministic keyboard
input path), `d3d12_readback_resolve = true` (render-target readback on the CPU,
the B-010 escape hatch for the stale CPU mirror),
`log_level = "debug"`,
`log_high_frequency_kernel_calls = true`, and `apu`/`gpu` levels switched off.
It is gitignored, so wiping `out\` loses it. Back it up before wiping:

```powershell
$build = 'out\build\win-amd64-release'
Copy-Item "$build\rb_blitz.toml" "$env:TEMP\rb_blitz.toml.bak"
Remove-Item -Recurse -Force $build
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
Copy-Item "$env:TEMP\rb_blitz.toml.bak" "$build\rb_blitz.toml"
```

If the backup is gone, recreate it with:

```toml
mnk_mode = true
d3d12_readback_resolve = true
log_level = "debug"
log_high_frequency_kernel_calls = true

[log.levels]
apu = "off"
gpu = "off"
```

`d3d12_readback_resolve` is a cvar, so it belongs here rather than in
[rb_blitz_manifest.toml](../../rb_blitz_manifest.toml); see
[rb3-references.md](../reference/rb3-references.md) Â§8.

**Historical local-profile example:** the following retired toggles describe the early bring-up build; current settings and defaults are listed in [engine/toggles.md](../engine/toggles.md).

An `[enhancements]` table belonged in this file when a toggle was being tested. Every toggle in it
is **off by default and unimplemented**, so an absent table is the faithful configuration and no
installed build ships one. The nine toggles, what each will gate, and the boot line that reports them
are in [engine/toggles.md](../engine/toggles.md):

```toml
[enhancements]
skip_offline_dialog = true   # R3; not implemented yet, the log will say so on every boot
```

Expect the first configure + build to take tens of minutes. A `.gitignore`d
`out/` is the only thing removed, and `generated/` is regenerated by the
codegen target during the build.

### The two graphics backends (D3D12 and Vulkan)

The parent `REXGLUE_USE_VULKAN` option maps to SDK `REXGLUE_GPU_VULKAN` before adding the SDK; both default ON. D3D12 uses SDK `REXGLUE_GPU_D3D12` on supported Windows builds. Renderer rows follow actual backend targets (`rex::gpu-xenos-d3d12` / `rex::gpu-xenos-vulkan`).

Vulkan uses the pinned source stack from Â§0. Windows source-builds the loader with MASM (`ml64` from Visual Studio); Linux stages its SONAME link and resolved file. Desktop macOS uses the pinned loader and source-built patched MoltenVK. iOS links MoltenVK directly and stages signed frameworks without a desktop loader or ICD. Native Linux/macOS/iOS acceptance for this migrated stack remains unproven.

`-DREXGLUE_USE_VULKAN=OFF` omits Vulkan targets and dependencies. Runtime selection also probes a usable driver; an installed loader alone does not establish device availability. See [distributable.md](distributable.md) for the configured package inventory.

In Help & Options â†’ Audio / Video â†’ Video, confirm the Renderer row to cycle between available
backends. The row shows the saved selection immediately; leave Help & Options and accept the
restart prompt to run it. A usable inactive backend must remain selected through saving and the
next boot. Before 2026-10-24 the graphics reconciler replaced even a usable Vulkan preference with
the active D3D12 backend, making the row appear stuck and preventing the next boot from using it.
Vulkan also needs the B-010 fixed-point texture conversion in SDK patch 0002; without it the
highway is invisible and the log repeats `k_32_32_32_32 resource unsigned`. The patch covers
both backends as of 2026-10-24. SDK patch 0022 moves Vulkan's conversion onto the GPU and
removes the title's automatic resolve readback. Explicit `readback_resolve` or
`vulkan_readback_resolve` overrides are still honored; use `readback_resolve = 'none'`
when comparing performance without an old readback override. See
[Vulkan performance](../engine/vulkan-performance.md) for scope and validation.
For a dev override, launch `rb_blitz.exe --gpu_backend=vulkan`; an explicit override owns the
setting for that process, so use a normal launch when testing the menu's saved preference.

The same pair of switches reaches the **installed** product through a chain that has to agree at
every step: the configured source tree â†’ the freshly built game and GPU plugin â†’ the installer's
payload snapshot â†’ the setup executable â†’ the installed binaries. `build.ps1` checks the middle
of that chain: `make_payload.ps1` records the snapshot's backends and commit in
`payload-manifest.toml`, and an existing `installer\out\payload` is reused only when it proves to
be the tree being shipped â€” otherwise it is rebuilt, with the reason printed. That check exists
because it failed once: `REXGLUE_USE_VULKAN` reached these presets on 2026-10-13 and an older
snapshot was kept, embedded and installed, so the installed game's *Renderer* row offered D3D12
alone ([installer/README.md](../../installer/README.md) Â§"The payload allow-list",
[known-issues.md](../reference/known-issues.md)). Read available backend targets from the current configure log,
not out of a `CMakeCache.txt` you happen to find â€” a second, stale `out/build/*` tree is a real
configuration and not the one a given setup executable was built from.

### The first build on a tree that has never run codegen

The v0.11 `rexglue_project` integration generates and reloads the source list during the ordinary build. No deliberate codegen failure or mandatory second invocation remains. All generated translation and fingerprint outputs are ignored.

**Historical pre-v0.11 failure:** the following diagnostic and workaround describe the former generated integration, retained as evidence.

`generated/default/sources.cmake` is what names the 104 recompiled partitions to
CMake, and codegen writes it *during* the build. A build system cannot learn about
sources that appear in the middle of it â€” Ninja only reloads the build system
between invocations, and naming the generated file as a `CMAKE_CONFIGURE_DEPENDS`
dependency does not change that (measured: the property is accepted and ignored for
a file a custom command produces). So the first build of a fresh checkout stops by
design, right after codegen, with:

```text
[9/14] Generating recompiled code for rb_blitz
[10/14] Checking that this build system knows about the generated code
CMake Error at rb_blitz_codegen_check.cmake:2 (message):
  Recompiled code has just been generated, but this build system was
  configured before it existed, so rb_blitz.exe cannot be linked in this
  build.
  Re-run the configure step, then build again.  The second build is
  incremental and produces the executable:
    cmake --preset win-amd64-release
    cmake --build --preset win-amd64-release
```

Run those two commands again and the build completes normally. The check is added
by [CMakeLists.txt](../../CMakeLists.txt) only to a configuration that predates the
generated sources, and it exists to replace the alternative: linking without the
recompiled code at all, which fails as a screenful of undefined symbols
(`_sub_â€¦`, `PPCImageConfig`) that names neither codegen nor the fix. Once one
preset has run codegen, the other preset configures with the sources already
present and needs no second pass.

## 3. Incremental rebuild after a source change

```powershell
cmake --build --preset win-amd64-release
```

This re-runs CMake automatically when `CMakeLists.txt` changes (so a file added
to `RBBLITZ_SOURCES` is picked up without a manual configure step) and then
compiles only what changed â€” routinely under a minute. Useful variants:

```powershell
cmake --build --preset win-amd64-release --target rb_blitz    # skip unrelated targets
cmake --build --preset win-amd64-release -- -v                # show full command lines
```

Outputs land in `out\build\win-amd64-release\`. Run the title with its staged modular SDK runtime, SDL3/provider modules, configured backend dependencies and resources. The [runtime module inventory](../../installer/config/runtime-modules.json) and shared [inventory helper](../../installer/tools/runtime_inventory.py) define the complete platform contract; two SDK DLLs are insufficient for v0.11.

`rexglue_setup_target` delegates staging to public `rexglue_configure_target` and tracks linked target files so runtime changes refresh the executable directory. Release, Debug and RelWithDebInfo use separate preset trees. Validate staged files for the selected configuration.

**Historical staging measurement (2026-09-30):** a plugin-only build left a 2,852,352-byte `rexgpu-xenos.dll` in the old `rexglue-sdk/out/win-amd64` directory and a stale 2,850,304-byte copy beside the title. The former integration copied only after a title link. Current target dependency tracking replaces that manual-copy workaround.

**Historical Debug acceptance:** the former `d`-suffixed runtime (`rexruntimed.dll`, `rexglued.exe`) passed host tests and the offline route after patch 0007 removed the ignored `XamAlloc_entry` flag assert. See [bringup-log.md](../history/bringup-log.md) B-013. This does not establish v0.11 Debug gameplay acceptance. Release remains the shipping configuration.

### The build gate: `game/` must be the fingerprinted dump

Every build of `rb_blitz_codegen` â€” and therefore of `rb_blitz` â€” first runs
`rb_blitz_fingerprint` against `game/default.xex`, and stops the build (exit 1)
when it is not the dump
[config/game_fingerprints.toml](../../config/game_fingerprints.toml) describes:

```text
[0/4] Checking game data against config/game_fingerprints.toml
Rock Band Blitz  title 5841122D  media 78492654  region-free  xex 0.0.0.2  (schema 1, game_fingerprints.toml)
ok             entrypoint       default.xex                9023488 bytes  sha256 e2195d6241d423197df31a84666a8d658cb7999521332f40c69f99f5d36e84bb
```

That is deliberate: every address in [config/](../../config) belongs to one image, so a
wrong `default.xex` has to fail here, with this message, instead of at runtime as
`Call to invalid or unregistered function`. A mismatch prints the digest it found
next to the one it expected, and the gate also fails if the fingerprint file itself
cannot be read â€” malformed input is not a way past the check.

The same binary is the audit tool. The gate checks the codegen input (`default.xex`)
only, which costs ~0.07 s; `--all` additionally hashes `gen/main_xbox.hdr` and the
361 MB `gen/main_xbox_0.ark` (~2 s):

```powershell
.\out\build\win-amd64-release\rb_blitz_fingerprint.exe --project-dir . --all
```

Its other job is generating
[generated/fingerprint_expected.h](../../generated), the compiled-in copy of the same
numbers that the runtime logs against (see Â§4). It is regenerated automatically
whenever the `.toml` changes, and is not committed.

A deliberately modified content root is a supported configuration â€” copying an
Ultimate payload's `gen/` and `default.xex` over a dump produces one â€” so the gate
can be relaxed for a checkout whose `game/` is one:

```powershell
cmake --preset win-amd64-release -DRBBLITZ_ALLOW_MODIFIED_GAME_DATA=ON
```

The mismatch is then reported but not fatal (it also switches the fingerprint test's
real-dump case from a failure to a skip). The option is **cached**: a later
`cmake --preset win-amd64-release` keeps the old value, so turn it back off with
`-DRBBLITZ_ALLOW_MODIFIED_GAME_DATA=OFF` rather than leaving a stale cache behind.
Codegen still runs on whatever
`default.xex` is present, so this is for the case where the *content root* is the
mod's, never a claim that an Ultimate image compiles: see
[ultimate-compat.md](ultimate-compat.md).

### The other gate: the frozen toolchain

The output below is the historical 2026-09-28 baseline; current pins are in [toolchain.md](toolchain.md).

Every build of `rb_blitz_codegen` also runs `rb_blitz_toolchain` against
[config/toolchain.toml](../../config/toolchain.toml), right after the game-data gate,
and prints the same two-line report either way:

```text
[0/3] Checking the toolchain against config/toolchain.toml
toolchain  rexglue-sdk c94f5eb, clang 23.1.1, CMake 4.4.3, Ninja 1.13.2, MSVC toolset 14.44.35207, Windows SDK 10.0.26100.0  (config/toolchain.toml)
ok        rexglue-sdk     c94f5eb           nightly-20260826-f5337cdc-2-gc94f5eb
ok        clang           23.1.1            C:\Program Files\LLVM\bin\clang++.exe
...
ok         the frozen toolchain
```

This is what replaced "the absolute compiler paths in this build tree's
`CMakeCache.txt`" as the record of a working toolchain: the same knowledge, in the
repository, checked on every machine. Only a version below the recorded `minimum` and an
SDK checkout off its pinned commit stop a build; anything else is reported and builds.
Both behaviours have a switch â€” `-DRBBLITZ_STRICT_TOOLCHAIN=ON` refuses every
deviation, `-DRBBLITZ_ALLOW_OTHER_TOOLCHAIN=ON` accepts every one â€” and the whole
subject is [toolchain.md](toolchain.md).

`rb_blitz.exe` carries the Blitz icon. [rb_blitz.rc](../../rb_blitz.rc) compiles
[assets/icon/blitz.ico](../../assets/icon/blitz.ico) into the executable, so the icon is a
build input like any other source: replacing the `.ico` and rebuilding is the
whole edit, since nothing else references the file. Controller Settings ships the
black-and-white alt icon instead â€” `assets/icon/blitz-mono.ico`, made from the colour
art by [scripts/assets/make_mono_icon.py](../../scripts/assets/make_mono_icon.py) â€” so a game window and
Controller Settings window are told apart in the taskbar. One resource covers every
surface of each exe, because SDL registers its window class with the first
`RT_GROUP_ICON` in it: Explorer, title bar, taskbar and Alt-Tab all follow from it.

### Proving the new code actually linked

Before spending a run on it, confirm the compiled-in diagnostic string is in the
new binary (this is a plain byte scan, not a debugger):

```powershell
$exe = 'out\build\win-amd64-release\rb_blitz.exe'
(Get-Item $exe).LastWriteTime
[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe)).Contains('guest XeKeys:')
```

The timestamp must be from this build, and the check must print `True`.

### Host unit tests (run these instead of booting, when they cover the change)

The tests under [tests/](../../tests) exercise pure host logic â€” the B-009 MOGG key
path, whose SDK-free half lives in
[src/hooks/crypto_keytable.h](../../src/hooks/crypto_keytable.h), the fingerprint
parsing/comparison in
[src/util/game_fingerprint.h](../../src/util/game_fingerprint.h), and with them the DLC
layout, the payload overlay, the path policy, the mouse-navigation stepper, the
Ultimate compatibility layer's decidable half in
[src/hooks/ultimate_plan.h](../../src/hooks/ultimate_plan.h) and the frozen-toolchain
record in
[src/util/toolchain_pin.h](../../src/util/toolchain_pin.h) â€” so they need no game
image, no runtime and no window, and finish in a couple of seconds:

```powershell
cmake --build --preset win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure
```

Expected output is however many tests that tree registers â€” `ctest --test-dir <dir> -N` lists them.
The title preserves the requested `BUILD_TESTING` value around SDK dependency
configuration, so Vulkan release builds can retain the title tests. SDK unit and
PPC fixture registration are controlled separately by `REXGLUE_BUILD_TESTS` and
`REXGLUE_BUILD_PPC_TESTS`. Configure with `-DBUILD_TESTING=ON` for the title suite;
disabling Vulkan is not required. Windows application builds always validate and
stage the authored controller and internal keyboard/mouse packages, including
when `BUILD_TESTING=OFF`; artwork packaging is a runtime dependency. Exactly one case needs the dump at
all (`fingerprint_real_game_dump` hashes `game\default.xex`), and it prints
`[ SKIP ]` instead of running when the dump is absent or, with
`RBBLITZ_ALLOW_MODIFIED_GAME_DATA=ON`, when it is not the supported revision. A failing check
prints the case name, `file:line`, and for memory comparisons the first differing
bytes, so a red test is self-explanatory. The harness is
[tests/check.h](../../tests/check.h) â€” deliberately dependency-free, because the
title checks do not require the SDK's Catch2 test framework â€” so a new test file needs
only an `add_executable` + `add_test` pair next to the existing one in
`CMakeLists.txt`, plus `src` on its include path. Adding coverage here is the
cheapest way to make a fix durable: `docs/reference/rb3-references.md` Â§7.3 counts "zero
automated tests" as the failure mode that let the RB3 port's fixes rot.

## 4. Run it

Start it **from the build directory** â€” that is where the exe looks for
the configured modular SDK runtime, its `.toml` and where it creates `logs\`:

```powershell
cd out\build\win-amd64-release
.\rb_blitz.exe --game_data_root=..\..\..\game
```

`--game_data_root` may point at either supported content variant â€” the retail dump,
or a vanilla dump with a [Rock Band Blitz
Ultimate](https://github.com/ultimate-mods-rb/blitz-ultimate) payload installed next
to it. Nothing else on the command line has to change: the payload is detected and
unioned over the game root at boot, and `--ultimate_mode` / `--ultimate_payload_root`
/ `--ultimate_patches` exist only for probing. If the payload's files were instead
copied *over* the game root, that merged layout is detected too. The
**build** input does not get that choice: codegen and every byte-guarded patch are
addressed against the vanilla `default.xex`, so keep it (the mod's own installer
suggests renaming it to `default_vanilla.xex` before installing) â€”
[ultimate-compat.md](ultimate-compat.md).

DLC needs no command-line change either: packages dropped in
`<game_data_root>/dlc/<title_id>/<content_type>/<package>` are mounted in place and
read-only at boot, and `--dlc_root` (or `dlc_root` in `rb_blitz.toml`) points
somewhere else if they live elsewhere. A dumped song folder â€” thousands of loose
`CON`/`LIVE` containers with no arrangement at all â€” goes in `--dlc_library` instead (or
straight into `--dlc_root`, which is read as a library when it is not the layout
above); each package is placed under the title id and content type its own header
names, recursively, and nothing is copied or linked â€” [dlc.md](dlc.md), which also
names the boot log line that says what was found.

Every boot logs the identity of both halves, so a log states which binary ran
against which image before any other evidence is read:

```text
[info] [core] [t20932] boot identity: build: rexglue-v0.10.0.0-dev.unknown-win-amd64-Release@20260919_2358
[info] [core] [t20932] game data identity: Rock Band Blitz 0.0.0.2 (9023488 bytes, sha256 e2195d62â€¦84bb)
```

The first line is the SDK build stamp compiled into the binary, the second the
`default.xex` it actually booted. A different `default.xex` **warns and continues**
â€” `game data identity: MODIFIED - â€¦ is N bytes, sha256 â€¦` followed by an `expected:`
line naming the values in
[config/game_fingerprints.toml](../../config/game_fingerprints.toml) â€” because pointing
a built executable at an Ultimate content root is a supported run, not a build error.
The fatal half of that check is the gate in Â§3, not the boot.

What that warning can and cannot see is measured by
`scripts\acceptance\acceptance_wrong_data.ps1`, which boots the recorded image, the Ultimate
payload's own image (a valid XEX with a different digest) and three corrupt copies,
and reports the Â§3 gate's verdict on the same file as the contrast:

```powershell
.\scripts\acceptance\acceptance_wrong_data.ps1
```

Three things it pins down. A valid image with a different digest boots on to the title
screen rather than being rejected â€” the warning is the whole response. A corrupt copy
never reaches the identity check at all, because that runs after `OnPostLoadXexImage`
and the loader has already refused the file â€” with a sentence, one per shape, since
[patches/rexglue-sdk/0008](../../patches/rexglue-sdk/0008-xex-image-bounds-checks.patch):
1 MiB of the recorded image (`truncated`) is `its block table describes 9011200 bytes
of image data, the image holds 1036288 after its 12288-byte header`, its first 2 KiB
(`short`) is `its header claims 12288 bytes, the image holds 2048`, and a zero-length
file (`empty`) is `Failed to map â€¦ (0 bytes) for module game:\default.xex`. Before
patch 0008 the first of those ended the process with `0xC0000005` and the loader's own
`Loading XEX image: game:\default.xex` as the last line written (B-014 in
[known-issues.md](../reference/known-issues.md)). And the gate refuses all three on the same file
(`SIZE MISMATCH`), which is the half that covers the build.

Development launches write a new numbered file, `logs\rb_blitz_001.log`, `_002`, â€¦:

```powershell
Get-ChildItem logs\*.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1 Name,Length,LastWriteTime
```

Installed launches keep the current and previous sessions under the selected save
root's `Diagnostics` directory: `game.log`, `game.1.log`, `previous.log`, and
`previous.1.log`. Each session has a 2 MiB file and one rotated file, so the total
log budget is about 8 MiB. A new launch moves the former `game` files to `previous`.
An explicit `log_file` setting still writes to that requested path.

On Windows, the boot log also reports its process ID and crash-dump directory.
An unhandled native exception writes a matching `.txt` record (exception code,
address, thread, dump status) and `.dmp` file there. The adjacent
`song-load-recovery.log` records the process ID, UTC exit time, uptime, exit
code, and song-load attempt number even if the exception handler cannot run.
Match these IDs to the boot log when investigating an abrupt exit. Windows
fail-fast termination can bypass in-process handlers, so the monitor record may
be the only artifact for that case.

A source path that shows up in a log is relative to the checkout
(`./src/hooks/dlc.cpp`), not absolute: the build maps its own two trees with
`-ffile-prefix-map`, which is the same thing that keeps the path of the machine
that built a payload out of a shipped binary ([distributable.md](distributable.md)).

For a repeatable run, the acceptance harness clears `logs\` and drives the window
itself (`-BootWaitSec` is how long it waits before closing):

```powershell
.\scripts\acceptance\acceptance_launches.ps1 -Runs 2 -BootWaitSec 40
```

It reports `alive` / `fatal` / `log KB` / `clean` per run and is the canonical
launch check. Related helpers: `scripts\diagnostics\capture_window.ps1 -OutFile <png>`
and `scripts\diagnostics\drive_ui.ps1`.

The harnesses that press keys (`acceptance_song.ps1`, `acceptance_screens.ps1`,
`acceptance_persistence.ps1`, `measure_pacing_input.ps1`, `audit_ark_reads.ps1
-Actions`) pass `--mnk_mode=1` on the command line instead of relying on the local
profile above: that profile is gitignored and machine-specific, so on a fresh
checkout controller emulation is off and every injected key would be dropped in
silence. The screens run also passes `--no-mouse_ui_nav`: the mouse is a second
synthetic pad on the same guest slot, and a keyboard-driven walk should not depend
on where the mouse pointer happens to be resting.

The song-route counterpart drives the whole offline route instead of stopping at
the window: it presses title â†’ sign-in â†’ offline prompt â†’ main menu â†’ song list,
selects the song by row with the *left* stick (arrow keys are the right stick in
this title), waits for the song's own audio envelope
(`XMPSetPlaybackController(0,1)` â†’
`(0,0)`), and reads the results screen back as OCR text, passing a run only if that
screen names the expected song. Vanilla content by default
(`--ultimate_mode=0`), so an installed Ultimate payload cannot change the result:

```powershell
.\scripts\acceptance\acceptance_song.ps1 -Runs 3              # row 3 = "These Days"
.\scripts\acceptance\acceptance_song.ps1 -Runs 1 -Replay      # ... then play it again
```

Two behaviours the script has to work around, both found by making the run
deterministic: the song list previews the rows the cursor moves over **through the
same playback controller as the song**, so envelopes are matched in pairs and any
pair shorter than 60 s is rejected as a preview; and the offline notice over the
results screen swallows the first `A`, so returning to the list presses and
re-checks instead of assuming one press is enough.

It clears `logs\` before each run and writes `out\m5-acceptance\runNN-*.png` (every
screen it asserted on as it went), a copy of each run's log, and `summary.json`; the
exit code is non-zero unless every run passed the whole launch â†’ results path.

`-Pause` adds the one route inside the song that the log cannot show: a pause
taken and released with Start. The title prints nothing when it pauses â€” a pause
does not touch the playback controller the song's envelope is read from, so a
paused run's log carries nothing but the pair that brackets the whole song â€” so
the pause menu is the evidence: `GAME PAUSED` has to be on the screen, and off it
again on the second Start, and the song still has to reach its own stop marker and
name itself on the results screen afterwards. The paused span stays inside the
reported envelope (the guest's clock stops, the wall clock does not):

```powershell
.\scripts\acceptance\acceptance_song.ps1 -Runs 1 -Pause      # pause 45 s in, then resume
```

The menu routes the song never takes have their own run. `acceptance_screens.ps1`
boots once and walks the main menu and the HELP & OPTIONS submenu from the top,
taking every screen the offline song route does not reach: the career leaderboard
(which names Rock Central as the reason it is empty), the achievements row
(inert without a signed-in profile, so the menu still being up is what is
asserted), the four HELP & OPTIONS pages, the eStore notice (left with `A`, which
is what it asks for, and not with `B`) and the EXIT GAME dialog â€” cancelled with
`B` and then asked for a second time and confirmed, so the **guest** ends its own
process. That is a different shutdown from the window close the launch check
uses: the guest's own exit prints `KernelState::TerminateTitle` and `Execution
complete`, where closing the window prints `Title terminated; hard-exiting
process.`

The menu-exit host cleanup has a two-second process deadline after the guest
main thread exits. Ordinary cleanup can finish sooner. If a host destructor or
worker wait hangs, an independent watchdog ends the process without taking
application or logging locks. This also covers static destruction; repeated
requests do not extend the deadline. It starts after guest execution ends, so
it does not interrupt gameplay or the menu's own guest exit work. An unfinished
host cache may need rebuilding on the next boot. The log marker is
`shutdown: guest exited; host cleanup has a 2000 ms process-exit deadline`.

This bounds the intermittent hang reported in `issues 1.zip`: `game.log`
reached `Execution complete` at 05:48:18.082 on November 11, 2026, then printed
audio totals at 05:48:18.950. The exact cleanup wait was not logged. The
`exit_deadline` process tests cover prompt normal exit, blocked cleanup, a
blocked static destructor, and repeated arming without opening the game UI.

```powershell
.\scripts\acceptance\acceptance_screens.ps1
```

Every row of that walk is clamped up to the first row of its list and counted
down from there, then checked against the screen it produced, because the guest
resets a list's selection whenever the list is entered â€” leaving the leaderboard
screen and accepting with no tap at all opens the song list â€” so carrying on from
wherever the last screen left the highlight reads a row nobody chose. A step that
lands somewhere else walks the route again from the first row rather than
pressing on into an unknown one. `-Pause` and this run write their screens to
`out\m5-acceptance\<run>-paused|resumed.png` and `out\m6-screens\` respectively,
with a copy of each run's log and a `summary.json` beside them.

The storage counterpart runs five cases against a *dedicated* writable
root rather than the user's `Documents\Rock Band Blitz`, and checks each run's own trace
log as well as the files it leaves behind â€” creation, restart byte-identity, a
missing root, a corrupt payload, a corrupt header:

```powershell
.\scripts\acceptance\acceptance_persistence.ps1                 # all five cases
.\scripts\acceptance\acceptance_persistence.ps1 -Only restart   # a single case
```

It needs the trace patch in [`patches/README.md`](../../patches/README.md) (`0003`) to
see the title's writes at all, writes `out\m4-persistence\<case>.log`,
`-files.txt`, `-menu.png` and `summary.json`, and exits non-zero unless every case
passes. `--user_data_root` and its `--cache_root`/`--update_data_root` siblings are
what make that isolation safe to run on a machine with real saves; they are honoured
as of 2026-09-20, before which they were silently discarded
([docs/reference/known-issues.md](../reference/known-issues.md)).

### Saving and DLC under the alpha (`acceptance_save.ps1`, `acceptance_dlc.ps1`)

Offline player persistence is enabled by default through `local_save=true`.
Controller mapping, scores, stars, and star counters are checkpointed to
`<user_data_root>/<XUID>/5841122D/local_save.bin`, using the guest's own save format
inside a checked host envelope. Audio/video changes trigger the native
`globaloptions` autosave. `--local_save=false` restores retail offline behavior;
it does not delete either file. An invalid/unreadable local save is preserved and
protected from overwrite for that session, with an error in the log.
**The new hooks still need in-game restart validation**; see the manual checks in
[save-persistence-findings.md](../plans/finished/2026-10-09-save-persistence-findings.md).

The two features the alpha is judged on have their own runs, and
`acceptance_alpha.ps1` drives both against the dev tree *and* against the payload the
installer ships (snapshotted by `installer/tools/make_payload.ps1`, run from a bare
install folder with no `rb_blitz.toml`):

```powershell
.\scripts\acceptance\acceptance_alpha.ps1                 # both suites, both build kinds
.\scripts\acceptance\acceptance_alpha.ps1 -SkipInstalled  # the dev tree only
.\scripts\acceptance\acceptance_save.ps1                  # save/load round trip on its own
.\scripts\acceptance\acceptance_save.ps1 -Steps 3         # -15 ms instead of -10 ms
.\scripts\acceptance\acceptance_dlc.ps1                   # DLC visibility on its own
.\scripts\acceptance\acceptance_dlc.ps1 -SkipControl -DlcLibrary "D:\Songs" -ExpectedSong "LYCANTHROPE"
```

* `acceptance_save.ps1` changes the **manual calibration** offset through the guest's
  own menus (HELP & OPTIONS â†’ Calibration â†’ Calibrate Manually), quits, restarts and
  requires the value to read back, then reverts it and checks the revert too. The
  three legs share one isolated `--user_data_root`. Manual calibration is the setting
  it uses because its native commit was measured: A on the page's CONTINUE rewrites
  `globaloptions` (a `[NtCreateFile] disp=0x5` plus a 1024-byte `[NtWriteFile]`, and
  the file's hash changes). Before the local persistence hooks, Audio/Video toggles
  and controller mapping only changed on screen; the new paths need separate
  restart checks ([docs/reference/known-issues.md](../reference/known-issues.md)).
  The value comes from OCR, so digits the OCR engine reads as letters ("0" â†’ "O",
  "-10" â†’ "-IO") are normalised before they are parsed.
* `acceptance_dlc.ps1` requires the DLC root's packages to be enumerated by the guest
  (`XamContentAggregateCreateEnumerator: added N items`, N > 0) and a song only the
  DLC package carries to be on the song-selection screen, read with Windows OCR. Its
  control leg points `--dlc_root` at a path that does not exist and requires the same
  song to be *absent*, so a pass cannot come from the bundled game data. Each leg
  uses a fresh writable root, because the guest caches its song list into `songcache`.
  `-DlcLibrary` adds a flat library to the positive leg and points the control leg at a
  library path that does not exist too, so the same two legs prove either source
  ([dlc.md](dlc.md) Â§2.1). `acceptance_song.ps1` takes the same `-DlcLibrary` /
  `-DlcLibraryContentType` passthrough and is what proves a library song actually
  *plays*.

Evidence lands in `out\m7-dev\`, `out\m7-installed\` and, per standalone run,
`out\m7-save\` / `out\m7-dlc\`; `out\alpha-acceptance.json` is the combined verdict.

### Mouse navigation (menus only), and its cvars

The mouse is a **third input device**, not a pointer: the row the pointer rests on is
the row the highlight is put on, and the buttons *are* A and B. It is on by default,
needs no setup, and announces itself once per boot, twice â€” once when the window
appears, with the row it computed for that size, and once when the driver is installed:

```text
[info] [core] [t14740] mouse_ui: a menu row is 32 px on a 1360x768 window (fallback pitch), hover alignment true, guest frames at 5 Hz
[info] [core] [t14740] mouse_ui: the mouse navigates the menus (hover alignment true, a row of travel per 0.0417 of the window's height as a fallback, --no-mouse_ui_nav to disable)
```

| Cvar | Default | Range | Meaning |
| --- | --- | --- | --- |
| `mouse_ui_nav` | `true` | flag | the whole device; `--no-mouse_ui_nav` (or `--mouse_ui_nav=false`) makes the driver enumerate no device at all |
| `mouse_ui_hover` | `true` | flag | hover alignment: the highlight follows the row the pointer is over, by measuring the guest's own frames |
| `mouse_ui_hover_delay_ms` | 80 | 0â€“2000 | how long the pointer rests before the selection follows it; shorter than a deliberate hover, longer than the gaps inside one movement |
| `mouse_ui_idle_hz` | 5 | 0.1â€“60 | how often the guest's frame is read while nothing is being aligned, for spotting a screen change and for the fallback pitch |
| `mouse_ui_row_fraction` | 0.0417 | 0.01â€“0.25 | pointer travel that moves the selection by one row in the fallback path, as a fraction of the window's height â€” 32 px at 768 |
| `mouse_ui_press_ms` | 24 | 1â€“1000 | how long one step of the fallback path holds the stick |
| `mouse_ui_release_ms` | 24 | 0â€“1000 | the gap between two steps of the fallback path |
| `mouse_ui_hover_log` | `false` | flag | write `out/mouse-probe/guestframes.log`: one line per press and one per hover, for measuring a screen |
| `mouse_ui_probe` | `false` | flag | the same file, with one line and an ASCII thumbnail per *changed* frame |
| `mouse_ui_probe_path` | `out/mouse-probe/guestframes.log` | path | where both of the above write |
| `mouse_ui_probe_thumb_every` | 10 | 0â€“1000 | thumbnail every Nth frame in the probe log; 0 is none |

Like every other cvar they come from the command line or `rb_blitz.toml` (Â§2), and
there is no runtime console, so changing one means a relaunch:

```powershell
.\rb_blitz.exe --game_data_root=..\..\..\game --mouse_ui_hover_log=true
```

**Hover alignment is measured, not guessed.** The guest exposes no list model and no hit
test, but the presenter can hand the guest's own last frame back
(`rex::ui::Presenter::CaptureGuestOutput`), and that frame is enough: a menu step *moves*
the highlight, so the difference between the frame before the press and the frame after it
holds the highlight twice â€” where it was and where it went. From that pair
([src/input/nav_detect.h](../../src/input/nav_detect.h)) come the row pitch and the row the
highlight is on, both in guest pixels; the pointer is mapped into the same pixels through
the presenter's own letterbox geometry ([src/input/ui_nav.h](../../src/input/ui_nav.h),
`ComputeGuestImageMapping`), and the left stick is then deflected one row at a time until
the measured row is the pointer's row. So the highlight lands **on** the row under the
pointer rather than a fixed distance from wherever it started, and screens whose rows are
spaced differently need no per-screen constant: the main menu's 26-27 px, the
HELP & OPTIONS screen's 53 px, MOD SETTINGS' 40 px and the in-song pause menu's 37-38 px
were all measured, and the ones hovered landed exactly.

Four properties of that loop are what make it behave like a native menu rather than a
controller:

- **A press lasts a couple of the guest's own frames** (measured from how often the guest
  asks for the pad state, floored at 60 Hz) and the gap after it lasts a couple more, so a
  press is always seen and never held long enough to auto-repeat. The menus were measured
  to be free of repeats up to a third of a second, so 50 ms carries about six times the
  headroom.
- **A press is measured against a frame read while the pointer was resting**, never against
  one from before the last move the guest made â€” otherwise one press's difference holds two
  moves, the pitch comes out doubled, and the hover stops a row short of the pointer while
  the log insists it arrived.
- **A screen that changes under a still pointer is left alone.** A click that opens a menu
  leaves the pointer over a row of a screen that is gone; the new screen's selection is not
  the pointer's to move until the pointer moves.
- **A marker at least as tall as its row is measured across two presses.** Its old and new
  positions overlap in the frame as well as in space, so one difference holds a single band
  with nothing in it to separate the two positions. The next press, in the same direction,
  moves that band by exactly one row â€” the band is rigid â€” and that shift is the pitch,
  with the marker being what is left of the band once a row is taken out of it. The turn of
  the stick that follows a press which measured nothing is therefore deferred to the third
  press, so the two presses a tall marker needs can go the same way. This is the shape the
  settings, pause and Ultimate-style lists with a wide selection bar present.

The pointer falling back to travel: with `--no-mouse_ui_hover`, or on a guest whose frames
cannot be read back (no presenter, or a resized window with no guest size yet), the driver
is the older relative model â€” cumulative pointer travel turned into
`round(distance / row)` presses, with the fraction of a row carried forward. It is exact on
a screen whose pitch matches the cvar and drifts with distance on one that does not (the
song list's selectable rows are 96 px apart), which is what the measured path exists to
avoid. The 24 ms default is not arbitrary: the guest's menus **auto-repeat** under a held
stick (one extra row per ~275 ms), so a long press overshoots, while a press shorter than
the guest's own frame (~17 ms) is never seen.

**Clicks wait for the alignment.** A click on a row the pointer has just moved to must not
fire A before the highlight has arrived, or it activates the row the selection was on when
the button went down. `UiClickPulser` holds the button until nothing is in flight, which is
why A lands where the hand is â€” and why clicking a row the pointer was already resting on
is instant.

**The game window has to be the foreground window** for any of this to work â€” and for the
keyboard driver too, which is easy to confuse with "the mouse is broken". Both the runtime
and this driver gate on real activation, so while the window is unfocused every event is
dropped and a menu that ignores both the mouse *and* the arrow keys is telling you the
window is not really in front, not that navigation is off. Clicking the window is the fix.

The standing limits of the device (menus only, no visible cursor, travel dropped while an
overlay owns the pointer, and the foreground requirement above) are in
[known-issues.md](../reference/known-issues.md); the measurements behind the defaults â€” the per-screen
pitches, the pulse widths, the letterbox mapping â€” are in
[bringup-log.md](../history/bringup-log.md), in the "Mouse navigation" sections.

### The retired settings application

This tree used to build a second executable, `rb_blitz_controller_settings.exe`, as the
pre-game settings window. It retired on 2026-10-19: every player setting it edited is an
in-game Help & Options row (General, Controls, Audio/Video, Interface, Gameplay), and the
installer now ships `rb_blitz.exe` alone
([gameplay-mapping plan](../plans/finished/2026-10-17-gameplay-input-mapping-plan.md) Â§19 has the
retirement record). There is nothing to build or start beside the game.

Two of its host suites tested modules the game owns, so they kept their checks under
game-owned names; the rest went with the application:

| CTest name | Target | What it covers |
| --- | --- | --- |
| `profile` | `rb_blitz_profile_tests` | the controller profile's format: the parse, the patch-in-place writer, what a save preserves, and the path resolution order (was `controller_settings_profile`) |
| `profile_remap` | `rb_blitz_profile_remap_tests` | the `[remap]` vocabulary: the grammar, the serialiser, the round trip (was `controller_settings_remap`) |

```powershell
cmake --build --preset win-amd64-debug
ctest --test-dir out\build\win-amd64-debug -R "profile|profile_remap" --output-on-failure
```

The application's own capture and installed-acceptance scripts
(`capture_controller_settings.ps1`, `acceptance_controller_settings.ps1`) were removed with it.
The in-game Controls surface is driven by
[scripts/acceptance/acceptance_controls.ps1](../../scripts/acceptance/acceptance_controls.ps1),
[scripts/acceptance/acceptance_controls_in_song.ps1](../../scripts/acceptance/acceptance_controls_in_song.ps1) and the
recovery arm [scripts/acceptance/acceptance_recovery.ps1](../../scripts/acceptance/acceptance_recovery.ps1).

## 5. What a good run looks like (B-009, music)

The hook logs only the first 8 AES calls, so a long run stays quiet. One combined
extraction for everything that matters:

```powershell
$log = Get-ChildItem out\build\win-amd64-release\logs\*.log |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName `
    -Pattern 'guest XeKeys|XeKeys.*STUB|\[FATAL\]|MOGG|mogg|vorbis|HMX' |
    ForEach-Object { '{0}: {1}' -f $_.LineNumber, $_.Line.Trim() }
```

Expected (the log category prefix may differ from `[krnl]`; the message text is
what matters):

```text
guest XeKeys: aes states 0xâ€¦, key table 0xâ€¦ (8 slots), feed 0xâ€¦
guest XeKeys: key 0xE0 -> slot 0 (obscured table entry +0xâ€¦)
guest XeKeys: AES-CBC key 0xE0 slot 0 size 16 encrypt 0 in 0xâ€¦ out 0xâ€¦
guest XeKeys:   in  <32 hex chars> |<ascii>|
guest XeKeys:   out <32 hex chars> |<ascii>|
```

- the key-install and AES-CBC lines prove the hook ran and that the key for the
  obscured table entry was installed (`+0xâ€¦` is the entry offset, so `+0x0` for a
  MOGG version of 12/13, `+0x10` for 14, `+0x20` for 15, `+0x30` for 16 â€” the
  versionâ†’index mapping is in
  [bringup-log.md](../history/bringup-log.md#b-009-music-never-plays--xekeyssetkeyxekeysaescbc-were-no-op-stubs));
- the `in` block echoes those same obscured table bytes (the dump is
  `min(inp_size, 32)` bytes, so 16 here), making it a second, independent
  confirmation that the right entry and slot are in play;
- there must be **no** `__imp__XeKeysSetKey STUB` / `__imp__XeKeysAesCbc STUB`
  lines (their absence is one of the success criteria);
- the `out` bytes are the AES-128-ECB decryption of the header block â€” that value
  becomes the MOGG stream's key mask, so a *plausible* value here plus audible
  music is the whole fix;
- **sound effects must be unchanged**.

Failure signatures, in the order you are likely to meet them:

| Log line | Meaning / next step |
| --- | --- |
| `guest XeKeys: kernel memory not available yet, ignoring the call` | Hook ran before the guest heap existed. The call is passed through; report it rather than assuming success. |
| `guest XeKeys: unexpected key id 0xâ€¦, using slot 0` | A second key path exists that this fix does not model yet. New information â€” report the line. |
| `guest XeKeys: key 0xâ€¦ -> slot â€¦ has unusable buffer 0xâ€¦` | The guest handed over a key buffer that is neither the obscured table nor plausible guest memory. The known key was kept; report it. |
| `guest XeKeys: AES-CBC on key 0xâ€¦ before XeKeysSetKey` | Call ordering surprise (one-shot fallback warning). Report it. |
| No `guest XeKeys` lines at all, and no music | The crypto path was never reached â€” the run did not get as far as opening a MOGG stream. Check the boot tail for `[FATAL]` / the title screen, not the crypto hook. |
| `__imp__XeKeysSetKey STUB` still present | The hook is not in the binary: check the byte scan in Â§3, then confirm `src/hooks/crypto.cpp` is in `RBBLITZ_SOURCES` in [CMakeLists.txt](../../CMakeLists.txt). |
| `[FATAL] â€¦` | Regression, most likely unrelated to crypto. Send the line plus ~20 lines of context. |

If the hook lines appear, the keys are right, and music is *still* silent, the
next suspect is the guest side that consumes the revealed mask:
`setupCypher` at `0x82768AD0` (`gKey = GrindArray(keychain key) ^ mKeyMask`,
then AES-CTR with the header nonce). See
[symbols.md](../reference/symbols.md#audio-mogg-decryption-path).

## 6. Optional A/B: prove the fix is what changed it

To show causality, keep two binaries and run the same route with both:

```powershell
$b = 'out\build\win-amd64-release'
Copy-Item "$b\rb_blitz.exe" "$env:TEMP\rb_blitz.with-fix.exe"
# edit CMakeLists.txt: drop src/hooks/crypto.cpp from RBBLITZ_SOURCES
cmake --build --preset win-amd64-release
Copy-Item "$b\rb_blitz.exe" "$env:TEMP\rb_blitz.without-fix.exe"
# restore CMakeLists.txt and rebuild
cmake --build --preset win-amd64-release
```

The no-fix build should reproduce the original symptom exactly: the two `STUB`
lines at boot, no `guest XeKeys` lines, and no music.

## 7. Handing the log back

Logs are ~2 MB, so don't paste them whole. Either point me at the newest log â€”
it is on this machine, e.g.
`out\build\win-amd64-release\logs\rb_blitz_003.log` â€” or reduce it to an excerpt:

```powershell
$log = Get-ChildItem out\build\win-amd64-release\logs\*.log |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
$out = "$env:TEMP\xekeys-excerpt.txt"
"# $($log.Name)  $((Get-Item $log.FullName).Length) bytes" | Set-Content $out
Select-String -Path $log.FullName `
    -Pattern 'guest XeKeys|XeKeys.*STUB|\[FATAL\]|mogg|vorbis|HMX|Failed to load' |
    ForEach-Object { '{0}: {1}' -f $_.LineNumber, $_.Line.Trim() } | Add-Content $out
Get-Content $out
```

Useful context to include with the excerpt: build type (`Release`), the route
you took (title only, or into a song), and whether music/sound effects were
audible. The excerpt is normally well under 50 lines.

## Shader catalogue changes and installer snapshots

Changing `config/graphics_preparation.json` regenerates the embedded header and
build-tree `graphics-preparation.json`. Always run
`cmake --build --preset win-amd64-release` before `installer/build.ps1`.
Packaging rejects an executable/catalogue mismatch and refreshes stale payload
snapshots. Installed extraction runs natively from the owner's assets; the
development Python tools are not runtime dependencies. Release evidence and
limits: [shader-cache.md](../engine/shader-cache.md) part 9.
