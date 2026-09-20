# Build, run, and capture a log

How to turn a source change into a running `rb_blitz.exe`, and how to hand back
the log excerpt that blocker entries in [bringup-log.md](./bringup-log.md) need.

Everything below is PowerShell, run from the repository root unless noted.

> **Current state (as of 2026-09-19):** the toolchain is installed on this machine
> and the checkout has been rebuilt from source, so B-009's fix is no longer
> "statically verified only" — it compiles, links, and a 40 s boot writes the §5
> `guest XeKeys` lines. §0 and §2 are both already done; **§3 is the entry point**
> for a normal source change, and §1 only needs revisiting on a fresh machine.

## 0. Prepare the SDK submodule (once per checkout)

The pinned SDK submodule needs two idempotent repairs before it can be built from.
Run both after a fresh clone, a `git submodule update`, or any checkout that
rewrites `rexglue-sdk/` — not on every build:

```powershell
# This machine's execution policy is Restricted, so -ExecutionPolicy Bypass is
# required; a bare `.\scripts\...ps1` fails with UnauthorizedAccess.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\apply_sdk_patches.ps1
```

1. **`repair_flat_symlinks.ps1`** expands the 16 SDK files that are symbolic links
   upstream into regular files holding the target's content. Windows here cannot
   create symlinks (not elevated, Developer Mode off) and the submodule repos
   inherit `core.symlinks=false`, so a checkout leaves them as link-text stubs. This
   is build-critical: the SDK compiles
   `thirdparty/libmspack/cabextract/mspack/lzxd.c`, which becomes a 29-byte text
   file (`lzxd.c` upstream is the real 30,796-byte source, under the same repo's
   `libmspack/mspack/`). A healthy tree prints `content already correct : 16` and
   `repaired from target    : 0`. The script then marks those 16 paths
   `--skip-worktree`, because the residual difference (a file *type* change, not
   content) cannot be resolved on this machine.
   **Do not** try to fix this by setting `core.symlinks true` in the submodules:
   checkout then fails with `unable to create symlink …: Function not implemented`
   and deletes the file.
2. **`apply_sdk_patches.ps1`** applies [patches/rexglue-sdk/](../patches/rexglue-sdk)
   to the pinned checkout — currently the log-once fix for the "Too few processor
   cores" warning, and the 32-bit fixed-point texture support that the note
   highway needs ([bringup-log.md](bringup-log.md) B-010). It prints each patch as
   *already applied*, *applied now*, or *failed* (exit 1, with guidance). `-Check`
   reports without writing. It also audits the SDK work tree: any edit not covered
   by a patch is listed as `UNEXPECTED` and exits 1.

Afterwards the submodule showing ` M` on the files the patch set names is the
expected, correct state — they are the applied patches, not drift to be cleaned.
The parent repo stays clean, because `.gitmodules` marks the submodule
`ignore = dirty`; the audit above is what replaces the warning that setting
suppresses. Full rationale and manual equivalents:
[patches/README.md](../patches/README.md).

## 1. Toolchain prerequisites

**This section is satisfied on the machine this doc was written on.** When the doc
was first written none of it was installed — no `C:\Program Files\LLVM`, no
`C:\Program Files\CMake`, no `C:\ProgramData\chocolatey`, no `cl`, no `vswhere`
(only `git`, `python` and `node` were on `PATH`) — so the install below was the
first step and had to happen before §2 or §3 could run. It is now in place (LLVM
23.1.1, CMake 4.4.3, Ninja 1.13.2, VS 2022 Build Tools 17.14.41 with the Windows
10.0.26100.0 SDK). The recipe is kept for a fresh machine; it is a one-time cost,
and every later rebuild is the incremental path in §3.

The build is a plain `Ninja` + `clang` build of the pinned SDK **from source**
(`REXSDK_DIR` points at `rexglue-sdk/`, and `generated/rexglue.cmake` does an
`add_subdirectory`), so the first build compiles SDL3, fmt, spdlog,
tomlplusplus, lzma, mspack and the ReXGlue runtime as well. That needs four
things:

1. **LLVM/Clang** — supplies `clang`, `clang++`, `lld-link` and `llvm-ar`.
2. **MSVC build tools _and_ the Windows SDK** — the presets target
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
`...\MSVC\<ver>\atlmfc\include`" note in that output is normal — ATL/MFC is an
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
> `PATH`** — the §1 check above still reports `clang`, `clang++` and `lld-link` as
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
allow 15–30 minutes and several GB. To pin them explicitly instead, add
`--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64` and
`--add Microsoft.VisualStudio.Component.Windows11SDK.22621` (or whichever SDK
revision is current) to the `--override` string.

Equally valid alternatives if one of them already exists:

- an existing **Visual Studio** install — open *x64 Native Tools Command Prompt
  for VS* and run the same `cmake --preset` commands from there, so `INCLUDE` and
  `LIB` are already set;
- **LLVM in a non-standard location** — the presets take the compiler from
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
— that is the most common way this toolchain ends up broken.

## 2. The existing build tree is stale — regenerate it

> **Already done on this machine (2026-09-19).** Both stale trees were wiped and
> `out/build/win-amd64-release` has been regenerated from source, so the
> `rb_blitz.exe` in it is current — do **not** re-run this section unless the
> tree's own `CMakeCache.txt` again records a path other than the checkout below.
> The check is `Select-String CMAKE_HOME_DIRECTORY out\build\win-amd64-release\CMakeCache.txt`.

`out/build/win-amd64-release` was produced when this checkout lived at
`D:\Gabriel\Documentos\Coding\decomps\360\rb-blitz-xenon-recomp` (that path is
gone; `CMakeCache.txt` still records it as `CMAKE_HOME_DIRECTORY`, `build.ninja`
still has `-ID:/Gabriel/...` include paths, and the cache's compiler paths point
at `C:\Program Files\LLVM\bin\clang++.exe`,
`C:\ProgramData\chocolatey\bin\ninja.exe`, `C:\Program Files\CMake\bin\cmake.exe`).
CMake refuses to reuse a cache whose home directory moved, so the tree has to be
deleted and regenerated once. Do **not** try to build into it as-is.

`out\build\win-amd64-release\rb_blitz.toml` is *not* a build artifact and CMake
never writes it: the runtime reads `<exe name>.toml` from its own directory (the
in-game settings overlay edits the same file), and this checkout's copy is the
hand-maintained local profile — `mnk_mode = true` (the deterministic keyboard
input path), `log_level = "debug"`,
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
log_level = "debug"
log_high_frequency_kernel_calls = true

[log.levels]
apu = "off"
gpu = "off"
```

Expect the first configure + build to take tens of minutes. A `.gitignore`d
`out/` is the only thing removed, and `generated/` is regenerated by the
codegen target during the build.

## 3. Incremental rebuild after a source change

```powershell
cmake --build --preset win-amd64-release
```

This re-runs CMake automatically when `CMakeLists.txt` changes (so a file added
to `RBBLITZ_SOURCES` is picked up without a manual configure step) and then
compiles only what changed — routinely under a minute. Useful variants:

```powershell
cmake --build --preset win-amd64-release --target rb_blitz    # skip unrelated targets
cmake --build --preset win-amd64-release -- -v                # show full command lines
```

Outputs land in `out\build\win-amd64-release\`: `rb_blitz.exe`, `rexruntime.dll`,
`rexgpu-xenos.dll`.

### The build gate: `game/` must be the fingerprinted dump

Every build of `rb_blitz_codegen` — and therefore of `rb_blitz` — first runs
`rb_blitz_fingerprint` against `game/default.xex`, and stops the build (exit 1)
when it is not the dump
[config/game_fingerprints.toml](../config/game_fingerprints.toml) describes:

```text
[0/4] Checking game data against config/game_fingerprints.toml
Rock Band Blitz  title 5841122D  media 78492654  region-free  xex 0.0.0.2  (schema 1, game_fingerprints.toml)
ok             entrypoint       default.xex                9023488 bytes  sha256 e2195d6241d423197df31a84666a8d658cb7999521332f40c69f99f5d36e84bb
```

That is deliberate: every address in [config/](../config) belongs to one image, so a
wrong `default.xex` has to fail here, with this message, instead of at runtime as
`Call to invalid or unregistered function`. A mismatch prints the digest it found
next to the one it expected, and the gate also fails if the fingerprint file itself
cannot be read — malformed input is not a way past the check.

The same binary is the audit tool. The gate checks the codegen input (`default.xex`)
only, which costs ~0.07 s; `--all` additionally hashes `gen/main_xbox.hdr` and the
361 MB `gen/main_xbox_0.ark` (~2 s):

```powershell
.\out\build\win-amd64-release\rb_blitz_fingerprint.exe --project-dir . --all
```

Its other job is generating
[generated/fingerprint_expected.h](../generated), the compiled-in copy of the same
numbers that the runtime logs against (see §4). It is regenerated automatically
whenever the `.toml` changes, and is not committed.

A deliberately modified content root is a supported configuration — copying an
Ultimate payload's `gen/` and `default.xex` over a dump produces one — so the gate
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

`rb_blitz.exe` carries the Blitz icon. [rb_blitz.rc](../rb_blitz.rc) compiles
[blitz.ico](../blitz.ico) into the executable, so the icon is a build input like
any other source: replacing the `.ico` and rebuilding is the whole edit, since
nothing else references the file. One resource covers every surface, because SDL
registers its window class with the first `RT_GROUP_ICON` in the executable —
Explorer, title bar, taskbar and Alt-Tab all follow from it.

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

The tests under [tests/](../tests) exercise pure host logic — the B-009 MOGG key
path, whose SDK-free half lives in
[src/hooks/crypto_keytable.h](../src/hooks/crypto_keytable.h), and the fingerprint
parsing/comparison in
[src/util/game_fingerprint.h](../src/util/game_fingerprint.h) — so they need no
game image, no runtime and no window, and finish in about half a second:

```powershell
cmake --build --preset win-amd64-release `
    --target rb_blitz_crypto_keytable_tests rb_blitz_fingerprint_tests
ctest --test-dir out\build\win-amd64-release --output-on-failure
```

Expected output is `2/2` passing — `Test #1: crypto_keytable` and
`Test #2: fingerprint`, 419 checks between them. Exactly one case needs the dump at
all (`fingerprint_real_game_dump` hashes `game\default.xex`), and it prints
`[ SKIP ]` instead of running when the dump is absent or, with
`RBBLITZ_ALLOW_MODIFIED_GAME_DATA=ON`, when it is not the supported revision. A failing check
prints the case name, `file:line`, and for memory comparisons the first differing
bytes, so a red test is self-explanatory. The harness is
[tests/check.h](../tests/check.h) — deliberately dependency-free, because the
SDK's vendored `catch2` submodule is not initialised — so a new test file needs
only an `add_executable` + `add_test` pair next to the existing one in
`CMakeLists.txt`, plus `src` on its include path. Adding coverage here is the
cheapest way to make a fix durable: `docs/rb3-references.md` §7.3 counts "zero
automated tests" as the failure mode that let the RB3 port's fixes rot.

## 4. Run it

Start it **from the build directory** — that is where the exe looks for
`rexruntime.dll`, the GPU plugin, its `.toml` and where it creates `logs\`:

```powershell
cd d:\Coding\decomps\360\rb-blitz-xenon-recomp\out\build\win-amd64-release
.\rb_blitz.exe --game_data_root=d:\Coding\decomps\360\rb-blitz-xenon-recomp\game
```

`--game_data_root` may point at either supported content variant — the retail dump,
or a vanilla dump with a [Rock Band Blitz
Ultimate](https://github.com/ultimate-mods-rb/blitz-ultimate) payload installed next
to it. Nothing else on the command line has to change: the payload is detected and
unioned over the game root at boot, and `--ultimate_mode` / `--ultimate_payload_root`
/ `--ultimate_patches` exist only for probing. If the payload's files were instead
copied *over* the game root, that merged layout is detected too. The
**build** input does not get that choice: codegen and every byte-guarded patch are
addressed against the vanilla `default.xex`, so keep it (the mod's own installer
suggests renaming it to `default_vanilla.xex` before installing) —
[ultimate-compat.md](ultimate-compat.md).

Every boot logs the identity of both halves, so a log states which binary ran
against which image before any other evidence is read:

```text
[info] [core] [t20932] boot identity: build: rexglue-v0.10.0.0-dev.unknown-win-amd64-Release@20260919_2358
[info] [core] [t20932] game data identity: Rock Band Blitz 0.0.0.2 (9023488 bytes, sha256 e2195d62…84bb)
```

The first line is the SDK build stamp compiled into the binary, the second the
`default.xex` it actually booted. A different `default.xex` **warns and continues**
— `game data identity: MODIFIED - … is N bytes, sha256 …` followed by an `expected:`
line naming the values in
[config/game_fingerprints.toml](../config/game_fingerprints.toml) — because pointing
a built executable at an Ultimate content root is a supported run, not a build error.
The fatal half of that check is the gate in §3, not the boot.

Each launch writes a new numbered file, `logs\rb_blitz_001.log`, `_002`, …:

```powershell
Get-ChildItem logs\*.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1 Name,Length,LastWriteTime
```

For a repeatable run, the milestone harness clears `logs\` and drives the window
itself (`-BootWaitSec` is how long it waits before closing):

```powershell
cd d:\Coding\decomps\360\rb-blitz-xenon-recomp
.\scripts\acceptance_launches.ps1 -Runs 2 -BootWaitSec 40
```

It reports `alive` / `fatal` / `log KB` / `clean` per run and is the canonical
Milestone 3 check. Related helpers: `scripts\capture_window.ps1 -OutFile <png>`
and `scripts\drive_ui.ps1`.

The Milestone 5 counterpart drives the whole offline route instead of stopping at
the window: it presses title → sign-in → offline prompt → main menu → song list,
selects the song by row with the *left* stick (arrow keys are the right stick in
this title), waits for the song's own audio envelope
(`XMPSetPlaybackController(0,1)` →
`(0,0)`), and reads the results screen back as OCR text, passing a run only if that
screen names the expected song. Vanilla content by default
(`--ultimate_mode=0`), so an installed Ultimate payload cannot change the result:

```powershell
cd d:\Coding\decomps\360\rb-blitz-xenon-recomp
.\scripts\acceptance_song.ps1 -Runs 3              # row 3 = "These Days"
.\scripts\acceptance_song.ps1 -Runs 1 -Replay      # ... then play it again
```

Two behaviours the script has to work around, both found by making the run
deterministic: the song list previews the rows the cursor moves over **through the
same playback controller as the song**, so envelopes are matched in pairs and any
pair shorter than 60 s is rejected as a preview; and the offline notice over the
results screen swallows the first `A`, so returning to the list presses and
re-checks instead of assuming one press is enough.

It clears `logs\` before each run and writes `out\m5-acceptance\runNN-*.png` (every
screen it asserted on as it went), a copy of each run's log, and `summary.json`; the
exit code is non-zero unless every run passed the whole launch → results path.

The Milestone 4 storage counterpart runs five cases against a *dedicated* writable
root rather than the user's `Documents\rb_blitz`, and checks each run's own trace
log as well as the files it leaves behind — creation, restart byte-identity, a
missing root, a corrupt payload, a corrupt header:

```powershell
cd d:\Coding\decomps\360\rb-blitz-xenon-recomp
.\scripts\acceptance_persistence.ps1                 # all five cases
.\scripts\acceptance_persistence.ps1 -Only restart   # a single case
```

It needs the trace patch in [`patches/README.md`](../patches/README.md) (`0003`) to
see the title's writes at all, writes `out\m4-persistence\<case>.log`,
`-files.txt`, `-menu.png` and `summary.json`, and exits non-zero unless every case
passes. `--user_data_root` and its `--cache_root`/`--update_data_root` siblings are
what make that isolation safe to run on a machine with real saves; they are honoured
as of 2026-09-20, before which they were silently discarded
([docs/known-issues.md](known-issues.md)).

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
guest XeKeys: aes states 0x…, key table 0x… (8 slots), feed 0x…
guest XeKeys: key 0xE0 -> slot 0 (obscured table entry +0x…)
guest XeKeys: AES-CBC key 0xE0 slot 0 size 16 encrypt 0 in 0x… out 0x…
guest XeKeys:   in  <32 hex chars> |<ascii>|
guest XeKeys:   out <32 hex chars> |<ascii>|
```

- the key-install and AES-CBC lines prove the hook ran and that the key for the
  obscured table entry was installed (`+0x…` is the entry offset, so `+0x0` for a
  MOGG version of 12/13, `+0x10` for 14, `+0x20` for 15, `+0x30` for 16 — the
  version→index mapping is in
  [bringup-log.md](./bringup-log.md#b-009-music-never-plays--xekeyssetkeyxekeysaescbc-were-no-op-stubs));
- the `in` block echoes those same obscured table bytes (the dump is
  `min(inp_size, 32)` bytes, so 16 here), making it a second, independent
  confirmation that the right entry and slot are in play;
- there must be **no** `__imp__XeKeysSetKey STUB` / `__imp__XeKeysAesCbc STUB`
  lines (their absence is one of the success criteria);
- the `out` bytes are the AES-128-ECB decryption of the header block — that value
  becomes the MOGG stream's key mask, so a *plausible* value here plus audible
  music is the whole fix;
- **sound effects must be unchanged**.

Failure signatures, in the order you are likely to meet them:

| Log line | Meaning / next step |
| --- | --- |
| `guest XeKeys: kernel memory not available yet, ignoring the call` | Hook ran before the guest heap existed. The call is passed through; report it rather than assuming success. |
| `guest XeKeys: unexpected key id 0x…, using slot 0` | A second key path exists that this fix does not model yet. New information — report the line. |
| `guest XeKeys: key 0x… -> slot … has unusable buffer 0x…` | The guest handed over a key buffer that is neither the obscured table nor plausible guest memory. The known key was kept; report it. |
| `guest XeKeys: AES-CBC on key 0x… before XeKeysSetKey` | Call ordering surprise (one-shot fallback warning). Report it. |
| No `guest XeKeys` lines at all, and no music | The crypto path was never reached — the run did not get as far as opening a MOGG stream. Check the boot tail for `[FATAL]` / the title screen, not the crypto hook. |
| `__imp__XeKeysSetKey STUB` still present | The hook is not in the binary: check the byte scan in §3, then confirm `src/hooks/crypto.cpp` is in `RBBLITZ_SOURCES` in [CMakeLists.txt](../CMakeLists.txt). |
| `[FATAL] …` | Regression, most likely unrelated to crypto. Send the line plus ~20 lines of context. |

If the hook lines appear, the keys are right, and music is *still* silent, the
next suspect is the guest side that consumes the revealed mask:
`setupCypher` at `0x82768AD0` (`gKey = GrindArray(keychain key) ^ mKeyMask`,
then AES-CTR with the header nonce). See
[symbols.md](./symbols.md#milestone-4-audio-mogg-decryption-path).

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

Logs are ~2 MB, so don't paste them whole. Either point me at the newest log —
it is on this machine, e.g.
`out\build\win-amd64-release\logs\rb_blitz_003.log` — or reduce it to an excerpt:

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
