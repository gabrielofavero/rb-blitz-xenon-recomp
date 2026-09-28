# The frozen toolchain

The toolchain this project is built, accepted and released with, recorded in the
repository rather than in one machine's build tree. This file is the prose half;
[config/toolchain.toml](../config/toolchain.toml) is the machine-readable half, and
`rb_blitz_toolchain --check` (built from
[tools/toolchain_check.cpp](../tools/toolchain_check.cpp), run by every build before
codegen) is what compares a machine against it.

## Why it is frozen

Until 2026-09-28 the only record of "which toolchain does this build work with" was
the absolute `CMAKE_CXX_COMPILER` / `CMAKE_MAKE_PROGRAM` / `CMAKE_COMMAND` paths in
`out\build\win-amd64-release\CMakeCache.txt` — one machine's file, recreated by every
clone, and silently different on any other box. That is the same failure mode the game
dump had before [config/game_fingerprints.toml](../config/game_fingerprints.toml): the
knowledge existed, but only where nobody could read it. The dump is now recorded and
enforced; this file is the toolchain doing the same.

Two things follow from it being recorded rather than assumed:

- a build **states** the toolchain it is about to use, in its own output, every time;
- a built binary **states** it too, at boot, next to the build and game data identity:
  `toolchain: rexglue-sdk c94f5eb, clang 23.1.1, … (frozen set)`.

## The set

Measured on the machine the acceptance runs are made on, Windows x86-64
(`win-amd64-release` / `win-amd64-debug`). It is what the clean-checkout acceptance run
of 2026-09-28 was made with — three runs, `launch-to-results 3 / 3`, each a 315 s
envelope — so the record starts out justified by evidence rather than aspirational
([history/bringup-log.md](history/bringup-log.md), "Clean-checkout smoke test").

| Component | Version | Measured at | Why it is in the set |
| --- | --- | --- | --- |
| ReXGlue SDK | commit `c94f5ebd…` (`nightly-20260826-f5337cdc-2-gc94f5eb`) | `rexglue-sdk/` submodule | It generates the recompiled code and is the runtime the host links against: its exports, its codegen, its kernel behaviour. A different commit is a different emulator. Pinned by the submodule gitlink, so a clone refuses another one before this check ever runs. |
| clang / LLVM | `23.1.1` (minimum `20`) | `C:\Program Files\LLVM\bin\clang++.exe` | Builds the SDK from source, the recompiled partitions and the host code: C++23, the guest-SEH `-fasync-exceptions` flag, and the `-march=x86-64-v2` the presets pass. `20` is the floor the linux presets already name (`clang-20`). |
| CMake | `4.4.3` (minimum `3.25`) | `C:\Program Files\CMake\bin\cmake.exe` | Owns the presets, the codegen integration in `generated/rexglue.cmake`, the object-library wiring and the fingerprint gate. `3.25` is the same floor `cmake_minimum_required` states. |
| Ninja | `1.13.2` | `…\WinGet\Packages\Ninja-build.Ninja_…\ninja.exe` | The generator the presets use; the codegen stamp/depfile logic and the incremental build are Ninja's. |
| MSVC toolset | `14.44.35207` | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207` | Supplies the C++/UCRT headers and the import libraries clang links against. This is the half that fails as a missing `<windows.h>` rather than as a missing compiler, which is why the check reads it off the compiler's include roots instead of trusting `PATH`. The enclosing Visual Studio Build Tools install is product version 17.14.41. |
| Windows SDK | `10.0.26100.0` | `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0` | Win32/D3D12 headers and libraries; same failure mode as the toolset. |

The paths are the ones this set was measured at, recorded so a reader can compare their
own box. They are **reported, never required**: the same versions live somewhere else on
another machine, and a path is not what breaks a build.

## What the check does

```
toolchain  rexglue-sdk c94f5eb, clang 23.1.1, CMake 4.4.3, Ninja 1.13.2, MSVC toolset 14.44.35207, Windows SDK 10.0.26100.0  (config/toolchain.toml)
ok        rexglue-sdk     c94f5eb           nightly-20260826-f5337cdc-2-gc94f5eb
ok        clang           23.1.1            C:\Program Files\LLVM\bin\clang++.exe
ok        CMake           4.4.3             C:\Program Files\CMake\bin\cmake.exe
ok        Ninja           1.13.2            …\ninja.exe
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
2. `--emit-header` writes [generated/toolchain_build.h](../generated) (not committed)
   with `RBBLITZ_TOOLCHAIN_STAMP` — what *this* machine reported, so a binary built off
   the frozen set says so.
3. [src/rb_blitz_app.h](../src/rb_blitz_app.h) logs that stamp at boot, between the
   build identity and the game data identity, which is the line that makes a bug report
   self-describing.

## What is not covered

- **The installer's own toolchain.** [installer/](../installer) needs Inno Setup 6
  (`ISCC`) and PowerShell 5.1; neither is in the record or checked.
- **Anything outside `win-amd64`.** The linux and mac presets exist, but the record is
  one platform's — on another host every component reports differently, and the check
  can only report that, not judge it.
- **Python.** The diagnostics under `scripts/*.py` and the installer helper use it;
  the build does not.
- **The graphics driver and the machine itself.** Frame pacing and the D3D12 path are
  properties of the host too ([known-issues.md](known-issues.md)).
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
2. update [config/toolchain.toml](../config/toolchain.toml) and the table above, with the
   measured paths if they moved;
3. run the acceptance route
   (`.\scripts\acceptance_song.ps1 -Runs 3`) and record it in
   [history/bringup-log.md](history/bringup-log.md);
4. commit the record change together with that evidence — the frozen set is only
   meaningful if the run that justifies it is named.

If the components are newer but nobody has run the acceptance route on them yet, leave
the record alone: a `different` line in the report is exactly that statement, and it
still builds.
