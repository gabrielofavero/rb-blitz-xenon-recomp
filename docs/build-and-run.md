# Build, run, and capture a log

How to turn a source change into a running `rb_blitz.exe`, and how to hand back
the log excerpt that blocker entries in [bringup-log.md](./bringup-log.md) need.

Everything below is PowerShell, run from the repository root unless noted.

> **Current state (as of B-009):** the fix is written and statically verified but
> has never been compiled or run — no toolchain is installed on the development
> machine and the checked-in build tree is stale. Start at §1, or jump to §2 if
> the toolchain check in §1 already passes.

## 1. Toolchain prerequisites

**Nothing in this section is installed on the machine this doc was written on** —
no `C:\Program Files\LLVM`, no `C:\Program Files\CMake`, no
`C:\ProgramData\chocolatey`, no `cl`, no `vswhere` (only `git`, `python` and
`node` are on `PATH`). So on that machine the install below is the first step and
has to happen before §2 or §3 can run. It is a one-time cost: once it is in, every
later rebuild is the incremental path in §3.

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
clang is installed but the headers are not.

### Installing them

`winget` is available on this machine, so the install is:

```powershell
winget install --id LLVM.LLVM -e --accept-package-agreements --accept-source-agreements
winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements
```

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

### Proving the new code actually linked

Before spending a run on it, confirm the compiled-in diagnostic string is in the
new binary (this is a plain byte scan, not a debugger):

```powershell
$exe = 'out\build\win-amd64-release\rb_blitz.exe'
(Get-Item $exe).LastWriteTime
[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe)).Contains('guest XeKeys:')
```

The timestamp must be from this build, and the check must print `True`.

## 4. Run it

Start it **from the build directory** — that is where the exe looks for
`rexruntime.dll`, the GPU plugin, its `.toml` and where it creates `logs\`:

```powershell
cd d:\Coding\decomps\360\rb-blitz-xenon-recomp\out\build\win-amd64-release
.\rb_blitz.exe --game_data_root=d:\Coding\decomps\360\rb-blitz-xenon-recomp\game
```

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
