# The distributable: what ships, and what must never ship

The release-readiness item asks for a negative claim: no retail data, no symbol taken
from a proprietary database, no credential and no machine-specific path in the installer
payload or in a packaged build. A negative claim is worth exactly as much as the check
behind it, so this file describes that check - [`scripts/audit_distributable.ps1`](../scripts/audit_distributable.ps1),
whose exit code is the answer - and the one thing it found.

```
.\scripts\audit_distributable.ps1
```

| Check | What it looks at | What it would catch |
| --- | --- | --- |
| `inventory` | `installer\out\payload` against the allow-list in `make_payload.ps1` and against `payload-manifest.toml` | a stray `rb_blitz.toml`, a `.pdb`, a `logs\` directory, a file nobody meant to ship |
| `retail-data` | every payload file against `config/game_fingerprints.toml`, by size and digest, by extension, by container magic at offset 0, and by carrying the entrypoint's own first or last 4 KiB | the dump, or part of it, being copied in |
| `mod-data` | the same against `installer/config/ultimate_fingerprints.toml` | the community mod being mirrored into the payload |
| `credentials` | the payload, the helper, the setup executable and the three config files, for private keys, provider tokens, JWTs, `Authorization:` headers and word-level key names | a token or key committed into a shipped file, or a second copy of the console's XEX key |
| `symbols` | every payload PE, for a COFF symbol table and `.debug` sections, plus any guest-address label that ships as a string | a build that stops stripping, which is how a name from somebody else's symbol database would travel |
| `machine-paths` | the payload, the helper, the setup executable and the generated config, for the build machine | the path of the developer's disk shipping inside the product |
| `tracked-tree` | `git ls-files` | game data or a binary artefact being committed into a repository anybody can clone |

Evidence lands in `out/distributable-audit/` - one text file per check with the matches
behind the verdict, plus `summary.json`. Exit codes: 0 every check passed, 1 something
failed, 2 the audit could not measure what it needs (a missing `llvm-readobj`, or a
`make_payload.ps1` whose allow-list it could not read).

## What the payload is

The installer places a recompiled build and nothing else: `rb_blitz.exe` and the launcher
that owns its settings, `rb_blitz_launcher.exe`, the two SDK DLLs the game loads
(`rexruntime.dll`, `rexgpu-xenos.dll`) and the four Visual C++ runtime DLLs the executables
import, plus `payload-manifest.toml`, which records their sizes and digests so the
helper can hash what it placed before it reports success. The list lives in one place,
`$wanted` in [`installer/tools/make_payload.ps1`](../installer/tools/make_payload.ps1),
and the script re-derives the DLL imports from the binaries it copies, so a build that
starts needing another DLL fails while the snapshot is made instead of on a user's
machine. The audit reads that same list rather than keeping a second opinion about it.

The game data is always the user's own, supplied as an STFS package or an extracted
folder, and Rock Band Blitz Ultimate is downloaded from its own GitHub release while the
installer runs - never mirrored, re-hosted or bundled. `installer/README.md` and
[ultimate-compat.md](ultimate-compat.md) are the policy; this file is the check.

## The one finding: `__FILE__`

The first run of the audit failed `machine-paths`, and the failure is the reason the check
exists. Every `REXLOG_*` and `REX_ASSERT` macro expands to a `__FILE__` literal, so each
source file that logs anything contributes its own path to the binary's string table - and
because the compiler is invoked with absolute paths, that path was the checkout's:

| File | Hits before the fix | After |
| --- | --- | --- |
| `rb_blitz.exe` | 8 | 0 |
| `rexruntime.dll` | 122 | 0 |
| `rexgpu-xenos.dll` | 18 | 0 |

The samples were unambiguous - `…/rb-blitz-xenon-recomp/src/hooks/dlc.cpp`,
`…/src/rb_blitz_app.h`, `…/generated/default/rb_blitz_pch.h` - and they named the
developer's disk (`D:/Coding/decomps/360/…`) inside a product that is about to be handed to
somebody else. It is not a credential and not game data; it is the third of the item's four
questions, and it was a real answer: *yes, a machine-specific path reached the payload*.

The fix is the compiler's own mapping, set before `generated/rexglue.cmake` so that the
`add_subdirectory()` that pulls in the SDK inherits it - the SDK's own DLLs had the most
hits, and a fix that only covered our sources would have left them:

```cmake
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
    add_compile_options("-ffile-prefix-map=${CMAKE_BINARY_DIR}=.")
endif()
```

A log line that used to say `D:/Coding/decomps/360/rb-blitz-xenon-recomp/src/hooks/dlc.cpp`
now says `./src/hooks/dlc.cpp`: still useful, no machine in it. The same flag makes the
generated code and every object file independent of the tree's location, which is what a
reproducible build needs anyway. MSVC is not covered by it - it needs `/pathmap` for debug
info and `/d1trimfile` for the `__FILE__` literal, and nothing here builds the game with
it - so the audit is what refuses a payload that leaks, whichever compiler produced it.

## The audit can fail

A check that has never gone red is a hope, so the two failures worth being able to
reproduce are reproduced. Copy the payload, append a checkout path to one of its binaries
and the recorded dump's first 4 KiB to another, correct the two sizes and digests in
`payload-manifest.toml` so the inventory check still passes, and point the audit at the
copy:

```powershell
# a copy of the payload, then: append "D:\\Coding\\…\\rb-blitz-xenon-recomp\\src\\leaked.cpp"
# to rexgpu-xenos.dll and the first 4096 bytes of game\default.xex to rexruntime.dll, and
# update their size/sha256 in payload-manifest.toml.
.\scripts\audit_distributable.ps1 -PayloadDir out\distributable-audit\negative-control\payload -SkipSetup
```

Measured 2026-09-28: `retail-data` fails on the dump's bytes and `machine-paths` fails on
the injected path (twice - the literal and the repository-name needle both match it), the
other five checks pass, and the process exits 1.

## What is *not* a finding

Things the audit sees and deliberately does not treat as defects, recorded here so a future
reader does not have to re-derive the reasoning:

- **The build stamp carries a timestamp.** `rexglue-v0.10.0.0-dev.unknown-win-amd64-Release@20260928_1827`
  is in the executable, and the docs treat it as the point: a log says which build it is
  ([toolchain.md](toolchain.md), [build-and-run.md](build-and-run.md) §4). It names no
  machine and no person.
- **The console's XEX key is in `rexruntime.dll`.** The runtime has to read the user's own
  dump, so the emulated console's key is in the SDK's source
  (`rexglue-sdk/src/system/xex_module.cpp`, `xe_xex2_retail_key`) and therefore in the DLL
  built from it. It is a public constant of every Xenia-derived runtime, it is not this
  project's secret, and there is exactly one copy: `scripts/decrypt_xex.py` reads it from
  the SDK rather than carrying a second one ([rb3-references.md](rb3-references.md) §9).
  What the audit checks is that **we** add no literal of our own.
- **The project's own names.** `sub_821D0A18`-style labels, hook names and guest addresses
  appear throughout the repository's docs and source, which are public under GPL-2.0. What
  must not travel is a name from *somebody else's* dump of symbols, and the way to be sure
  of that is that no symbol table travels at all - measured, not assumed: all seven payload
  PEs report `PointerToSymbolTable: 0x0` and no `.debug` section.
- **Fifteen paths that are nobody's secret**, listed in full in
  `out/distributable-audit/machine-paths.txt` so that the next reader does not have to
  re-derive them: `D:\a\_work\1\s\binaries\…` in the four Microsoft CRT DLLs (Microsoft's
  own CI path, baked into their binaries), `C:\Windows\Fonts\msgothic.ttc` and
  `C:/Windows/USB_Vibration` in `rexruntime.dll` (the font and XInput device paths the SDK
  looks for), and `C:\Coding\Is\issrc-build\Components\ChaCha20.pas` in each setup
  executable (Inno Setup's own build path, inside ISCC's compiled script). None of them
  names this machine or this checkout, and none of them is ours to remove - which is why the
  check fails on *this* checkout's path and reports the rest.

## What this does not cover

- **The setup executable's interior.** Inno Setup compresses what it embeds, so the setup
  exe is scanned as a file (its own strings, its version info, its `[Files]` list) but the
  payload inside it is not re-extracted: what lands on a user's disk is the payload the
  audit already read, and the setup exe is a function of it. `installer/build.ps1` prints
  the exe's size and SHA-256 for a release to publish.
- **The install-time downloads.** The audit does not fetch the Ultimate release; the pinned
  URL and its SHA-256 are checked in `installer/config/pins.toml`, and the upstream artefact
  is somebody else's to vouch for.
- **A first-party installer build is not byte-reproducible.** ISCC embeds the payload it
  finds, so the published checksum has to be the one measured on the release run. The audit
  makes no claim about that beyond the payload's own digests.
- **Legal questions the tooling cannot answer.** "May this be redistributed" is decided by
  the licence rules in [rb3-references.md](rb3-references.md) §9 and the provenance table
  there, not by a string scan. The audit's job is narrower: nothing *unintended* is in the
  payload.
- **The uninstaller and the install manifest.** They write on the user's machine, not into
  the payload, and they record the user's own paths - which is what a report the user
  attaches to a bug is for.
