# installer/ — the setup executable

A standalone Windows installer that puts a ready-to-play Rock Band Blitz build on
a machine that has never seen this repository. The user answers four questions
(where to install, where the game data comes from, whether they want Rock Band
Blitz Ultimate) and gets a folder with a launchable `rb_blitz.exe` in it — no
compiler, no CMake, no instructions to follow.

Everything in this directory is *packaging*. Nothing here is needed to build or
run the game, and none of it is compiled into `rb_blitz.exe`.

## What the installer does and does not contain

| It contains | It never contains |
| --- | --- |
| The recompiled build (`rb_blitz.exe`) and the runtime DLLs it loads | Any game code, archive, music or key |
| The wizard, its images and the helper that does the file work | Rock Band Blitz Ultimate, in any form |
| — | Anything read from the machine it is built on |

The game data is always the user's own: an Xbox 360 package or a game folder they
already extracted. The Rock Band Blitz Ultimate mod is a third-party fan project,
so it is downloaded from its own GitHub release while the installer runs, or
supplied by the user — never mirrored, re-hosted or bundled. The distribution
rules are in [`../prompts/06-reproducible-release.md`](../prompts/06-reproducible-release.md)
and [`../docs/ultimate-compat.md`](../docs/ultimate-compat.md).

## What the user walks through

| Page | What happens |
| --- | --- |
| Welcome | What the installer needs (a copy of the game they own), what it refuses to do (no game files included or downloaded). |
| Licence | The project's GPL-2.0 text. |
| Install folder | Any writable folder; the default is under the user's own profile, so no administrator rights are needed (`/ALLUSERS` still forces a machine-wide install). |
| Game data | Download package / extracted folder / keep what is already installed. The choice is verified *before* the install starts: the geometry of the selection is read and the required files are checked, so a wrong path fails on the page instead of halfway through. |
| Ultimate | Not at all, download the pinned release, use a zip, or use a folder. |
| Progress | One bar, four stages (build, game data, mod, finishing) with a live status line; cancel asks for confirmation while a stage runs. |
| Finish | "Launch Rock Band Blitz" (unticked in silent installs). |

A copy of everything the helper did is left next to the game as
`install-manifest.toml` (machine-readable, includes the source, the file count,
the byte count and whether the fingerprints matched) and `install-report.txt`
(the same, for a human, with the helper's log appended).

## Layout

| Path | What |
| --- | --- |
| `setup.iss` | The wizard: pages, sequencing, progress mapping, uninstall. Presentation only — it has no filesystem, HTTP or crypto support. |
| `src/` | `rb_blitz_setup_helper.exe`: STFS extraction, zip inflate, WinHTTP download, SHA-256 verification, file placement, manifests. |
| `src/commands.cpp` | The helper's command line: one command per stage, `--summary` / `--log` / `--progress` for each. |
| `config/pins.toml` | The only file that names the outside world: version, URLs, sizes, hashes. |
| `config/ultimate_fingerprints.toml` | What the mod's files hash to, in the same schema as `../config/game_fingerprints.toml`. |
| `tools/make_payload.ps1` | Snapshots a recompiled build into `out/payload` (and optionally `out/payload.zip`). |
| `tools/make_art.ps1` | Downloads the optional wizard images into `assets/`. |
| `tools/embed_config.cpp` | Compiles `config/pins.toml` + both fingerprint files into `out/generated/embedded_config.h` and `out/generated/pins.iss`. |
| `build.ps1` | The release entry point: payload, helper, tests, images, setup executable. |
| `tests/installer_tests.cpp` | The helper's test suite (`ctest`), dependency-free and game-data-free. |
| `out/` | Build output. Nothing in it is committed. |

The wizard and the helper are two halves of the same program and cannot be built
independently: `pins.iss` is generated from `config/pins.toml` so the version,
the URL and the size the wizard shows are literally the ones the helper downloads
and checks.

## Building it

Prerequisites: Windows, CMake 3.25+, Ninja, a C++20 toolchain, [Inno Setup 6](https://jrsoftware.org/isdl.php),
and — for the default embed mode — a recompiled build in
`out/build/win-amd64-release`.

```powershell
# from the repository root
powershell -ExecutionPolicy Bypass -NoProfile -File installer\build.ps1
```

That one command:

1. snapshots the recompiled build into `installer\out\payload` if the snapshot is
   missing (or rebuilds it with `-RefreshPayload`);
2. generates the embedded config from `config\pins.toml`;
3. builds the helper with the `installer-release` preset (clang++) and stages it
   next to `pins.iss` in `out\generated`;
4. runs the helper's test suite;
5. refreshes the optional wizard images;
6. compiles `setup.iss` with ISCC and prints the setup exe's size and SHA-256.

Result: `installer\out\dist\RockBandBlitzSetup-<version>.exe`. Use
`-Preset installer-release-msvc` from a Visual Studio developer prompt, and
`-SkipArt`, `-SkipTests`, `-SkipPayload`, `-SkipSetup`, `-IsccPath` as needed —
`Get-Help .\build.ps1 -Full` documents all of them.

Tests alone, from this directory:

```powershell
ctest --preset installer-release --output-on-failure
```

The presets are directory-scoped, so `cmake` and `ctest` must run with
`installer\` as the working directory.

### Why the helper has a static CRT

The helper runs from the wizard's temporary directory on a machine that may not
have the Visual C++ redistributable installed, and in the download mode it is
what fetches the payload — the DLLs the payload ships are not there yet. Linking
the CRT statically leaves the helper importing only OS DLLs. The *game* keeps
using the redistributable DLLs; those travel with the payload, which is why
`make_payload.ps1` snapshots them too.

## How the recompiled build travels

One of two modes, chosen at build time:

* **Embedded** (default). `out\payload` is compressed into the setup executable,
  so the installer needs no network access at all. The setup exe is then about
  15 MB bigger than the payload.
* **Downloaded at install time** (`-PayloadUrl -PayloadSha256 [-PayloadSize]`).
  Nothing is embedded; the wizard checks free space against `-PayloadSize` and the
  helper downloads the release asset, verifies its SHA-256, extracts it into
  `<install folder>\.staging` and deletes the archive before moving the files into
  place. Nothing else about the install differs.

The recompiled build is GPL-2.0 and is ours, so embedding it is a packaging choice,
not a licensing one. The Ultimate mod is the opposite — see below.

### Cutting a release

```powershell
# 1. the build the installer will put on disk, plus the asset to publish
powershell -ExecutionPolicy Bypass -File installer\tools\make_payload.ps1 -Clean -Zip

# 2. publish installer\out\payload.zip as a release asset, then build the setup
#    exe against the URL and the hash of what you just published
powershell -ExecutionPolicy Bypass -File installer\build.ps1 `
    -PayloadUrl 'https://github.com/<owner>/<repo>/releases/download/<tag>/payload.zip' `
    -PayloadSha256 '<64 hex characters>' -PayloadSize <bytes> -SkipTests
```

`-SkipTests` is required for a download-mode build: the suite cross-checks the
pins embedded in `pins.toml` against a command-line pin, and by design they
differ. It is the *only* thing that fails; run the suite once in embed mode (the
default) and it covers the same code.

Neither the payload zip nor the setup exe is byte-reproducible across machines
(the zip depends on the deflate implementation that wrote it, the exe on the
payload directory ISCC happens to find), so publish the checksums `build.ps1`
prints rather than a fixed one.

## Configuration: `config/pins.toml`

One file, parsed at build time, embedded into both halves:

| Section | Keys | Meaning |
| --- | --- | --- |
| `[installer]` | `name`, `version`, `publisher`, URLs, `default_dir_name`, `min_windows_build` | Everything the wizard's own resources need. `default_dir_name` is appended to the folder the user picks; `min_windows_build` is `10.0.17763` because the runtime and its DLLs are x86-64-v2. |
| `[payload]` | `version`, `url`, `sha256`, `size` | Empty in the repository. A pin that does not exist yet fails with "no payload source configured" instead of downloading nothing; the release workflow fills it in (or overrides it with `-PayloadUrl`/`-PayloadSha256`/`-PayloadSize`). |
| `[ultimate]` | `version`, `url`, `sha256`, `size`, repository/release URLs, `archive_prefix`, `destination_dir` | The Ultimate release the wizard offers and the helper downloads. |

`tools/embed_config.cpp` also compiles `config/ultimate_fingerprints.toml` and
`../config/game_fingerprints.toml`, and the helper reuses `src/util/sha256.cpp`
and `src/util/game_fingerprint.cpp` **from the emulator tree**, so the rules the
installer enforces are literally the rules the runtime enforces — the installer
cannot drift from what the game expects.

### Changing the pinned Ultimate version

The version the user sees is `[ultimate] version`. To move it:

1. set `version` to the new release tag;
2. set `url` to that release's Xbox archive, and `size` / `sha256` to the file you
   downloaded from it — the helper refuses a download that does not match;
3. update `config/ultimate_fingerprints.toml` with the sizes and hashes of the
   new `gen/patch_xbox.hdr` and `gen/patch_xbox_0.ark`;
4. keep `archive_prefix` pointing at the subtree of the archive that belongs in
   `<game root>\ultimate` and `destination_dir` at `ultimate`;
5. rebuild. The wizard's option label and the helper's expectations move together
   because both come from the same file.

If `[ultimate] url` is empty the wizard greys the download option out and only
offers the user-supplied zip and folder.

## The payload allow-list

`make_payload.ps1` copies exactly what the game needs to run:

| File | Why |
| --- | --- |
| `rb_blitz.exe` | The game. |
| `rexruntime.dll`, `rexgpu-xenos.dll` | Runtime DLLs the executable loads. |
| `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` | The Visual C++ runtime the three binaries import. |

Nothing else is quoted from the build tree — in particular `rb_blitz.toml` is a
developer profile rather than a shipped file, and the `*d.dll` / `*.pdb` files
belong to a debug build. The DLL list is not a guess: the script re-derives the
imports from the binaries it copies, so a build that starts needing another DLL
fails while the snapshot is made instead of on the user's machine. The snapshot is
written as `payload-manifest.toml`, in the same fingerprint format the helper reads
back, and the helper hashes every file it placed before it reports success.

## Rock Band Blitz Ultimate

An optional community mod. It is *not* needed to play: the runtime reproduces the
mod's three edits host-side, and an installed payload works by drag-and-drop
(`docs/ultimate-compat.md`). The installer offers three ways to get it, and none
of them involve us distributing it:

| `ULTIMATESOURCE` | What happens |
| --- | --- |
| `pin` | Helper downloads the release named in `config/pins.toml`, verifies the size and the hash, extracts only the `archive_prefix` subtree into `<game root>\ultimate`. |
| `zip` | Helper reads a zip the user already has, same subtree rule. |
| `folder` | Helper copies the mod's `gen/patch_xbox.hdr` and `gen/patch_xbox_0.ark` from a folder the user points at (the folder may be the extract root or a folder above it). |

The mod's own `default.xex` is deliberately never installed: it would shadow the
user's entrypoint, and the runtime derives the mod's delta from the *vanilla*
`default.xex` instead. If a supplied source contains that file, the installer
recognises it, warns, and refuses to copy it.

Fingerprints for the mod are advisory, not a gate: users may legitimately supply
another release, so the installer records what it found and warns when it does not
match the pin, and installs anyway. What decides whether the overlay works is its
shape — `ultimate/gen/patch_xbox.hdr` plus `ultimate/gen/patch_xbox_0.ark` — and
that is what the runtime reads.

## Wizard images

`assets/` holds the two images Inno Setup can show (the large one on the welcome
and finish pages, the small one in the header). They are generated by
`tools/make_art.ps1` and are **not** committed: the artwork is not ours to
license. Their absence is a supported configuration — `setup.iss` only applies the
`WizardImageFile`/`WizardSmallImageFile` directives when the files exist, and
`build.ps1` treats a failed download as a warning, not a build failure. The script
writes the whole ladder of sizes Inno Setup documents so the right one is picked
on the machine the installer runs on, whatever its DPI setting.

## Silent / unattended installs

The wizard's answers can all be given on the command line, together with Inno
Setup's own switches.

| Parameter | Value | Meaning |
| --- | --- | --- |
| `/GAMEMETHOD` | `package` (default), `folder`, `installed` | Where the game data comes from. |
| `/GAMEFOLDER` | folder | With `folder`: the extract root, or any folder above it. |
| `/GAMEPACKAGE` | file | With `package`: the Xbox 360 container file. |
| `/ULTIMATESOURCE` | `none` (default), `pin`, `zip`, `folder` | Whether and how to install the mod. |
| `/ULTIMATEZIP` | file | With `zip`. |
| `/ULTIMATEFOLDER` | folder | With `folder`. |
| `/DIR` | folder | Install folder (Inno Setup). |
| `/ALLUSERS`, `/CURRENTUSER` | — | Machine-wide instead of the default per-user install (Inno Setup). |
| `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=<file>` | — | Usual unattended switches (Inno Setup). |

Example — unattended install from a package, with the pinned mod, no dialog at all:

```bat
RockBandBlitzSetup-0.1.0.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART ^
  /DIR="C:\Games\Rock Band Blitz" /LOG="%TEMP%\rbb-install.log" ^
  /GAMEMETHOD=package /GAMEPACKAGE="D:\dd774f20c36263f22afd0a8b6fd742ac9e400b0a58" ^
  /ULTIMATESOURCE=pin
```

A silent install never launches the game. A silent *uninstall* always keeps the
game data (see below).

## The helper contract

`rb_blitz_setup_helper.exe <command> [options] --summary <file> [--log <file>] [--progress <file>]`

Every command writes a one-line `key=value;key=value` summary; the wizard polls
that file (every 120 ms) and treats a non-empty `ok` as "the helper finished". The
commands are:

| Command | Purpose |
| --- | --- |
| `check-space` | Free-space pre-check before anything is written. |
| `probe-folder`, `probe-package` | Read the geometry of the user's selection and report which required files are present, so the wizard can reject a wrong path on the page. |
| `probe-archive` | Inspect a zip (`--kind payload\|ultimate`): entry count, root, whether the manifest is where it belongs. |
| `install-payload` | Place the recompiled build: from the embedded snapshot, a pinned download, a zip or a folder; verifies every file against the payload manifest. |
| `verify-payload` | Re-verify a placed payload. |
| `import-game` | Extract the game data from a package, or copy it from a folder, then verify the fingerprints. |
| `install-ultimate` | The mod, from the pin, a zip, a folder or a URL. |
| `verify-game` | Independent check of the finished game folder. |
| `finalize` | Write `install-manifest.toml` and `install-report.txt`. |
| `uninstall-cleanup` | Remove what the helper wrote, `--keep-game-data` to keep the import. |
| `version` | Print the versions it was built with. |

Two properties matter for reliability:

* **A failure always writes a summary.** A command that returns an error, and a
  C++ exception that escapes one, both end up as `ok=0;error=…`, so the wizard
  fails immediately with the reason instead of waiting out its timeout. (A hard
  crash — an access violation — still cannot report anything; the wizard's 60
  minute per-stage ceiling is the backstop for that, and control of the progress
  page is only given back when the stage ends.)
* **Progress is a file, not a window.** The helper draws nothing: it appends to
  `--progress` and the wizard repaints the bar and the status line itself, so a
  stage behaves the same in silent and in interactive installs.

## Uninstall

The uninstaller runs the helper once (`uninstall-cleanup`) and then removes the
install folder. The imported game data — several hundred megabytes the user may
well want to keep, for example to reinstall later without the disc or the package
again — is only deleted if the user answers yes; a silent uninstall keeps it. The
mod's `ultimate\` folder lives inside the game data and follows it.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| `no payload to embed: pass /DPayloadUrl and /DPayloadSha256, or create the payload snapshot first` | Embed mode with no `out\payload\payload-manifest.toml`. Run `make_payload.ps1`, or pass `-PayloadUrl`. |
| `pins.iss is missing from the generated directory` | The helper was never built; run `build.ps1` (or configure the preset) first. |
| The test suite fails in a download-mode build with a mismatch between the pin and `pins.toml` | Expected: pass `-SkipTests` (see "Cutting a release"). |
| `-PayloadUrl needs -PayloadSha256` | An installer that downloads its own build must name which build it is. `-AllowUnverifiedPayload` exists for local testing only. |
| A rebuild ignores a pin you just removed | `-DRBBLITZ_EMBED_EXTRA_ARGS` is a *cached* CMake variable. `build.ps1` always passes it (possibly empty), so build through the script rather than a bare `cmake --preset`. |
| The wizard is up but nothing happens, then it fails after ten minutes | The helper died silently. Its own log is next to the install folder (`rbblitz-probe.log` in the temporary directory while the wizard is still open); `install-report.txt` in the finished install has the rest. |
