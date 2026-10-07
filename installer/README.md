# installer/ — the setup executable

A standalone Windows installer that puts a ready-to-play Rock Band Blitz build on
a machine that has never seen this repository. The user answers four questions
(where to install, where the game data comes from, whether they want Rock Band
Blitz Ultimate, which shortcuts to create) and gets a folder with a launchable
`rb_blitz.exe` in it — no compiler, no CMake, no instructions to follow.

Everything in this directory is *packaging*. Nothing here is needed to build or
run the game, and none of it is compiled into `rb_blitz.exe`.

## What the installer does and does not contain

| It contains | It never contains |
| --- | --- |
| The recompiled build (`rb_blitz.exe` and its launcher) and the runtime DLLs the game loads | Any game code, archive, music or key |
| The wizard, its images and the helper that does the file work | Rock Band Blitz Ultimate, in any form |
| — | Anything read from the machine it is built on |

The game data is always the user's own: an Xbox 360 package or a game folder they
already extracted. The Rock Band Blitz Ultimate mod is a third-party fan project,
so it is downloaded from its own GitHub release while the installer runs, or
supplied by the user — never mirrored, re-hosted or bundled. The distribution
rules are in [`../docs/distributable.md`](../docs/distributable.md) — what ships, and
the audit that refuses a payload with game data, a credential, a symbol table or the
build machine's path in it — and [`../docs/ultimate-compat.md`](../docs/ultimate-compat.md).

## What the user walks through

| Page | What happens |
| --- | --- |
| Welcome | What the installer needs (a copy of the game they own), what it refuses to do (no game files included or downloaded). |
| Install folder | Any writable folder; the default is under the user's own profile, so no administrator rights are needed (`/ALLUSERS` still forces a machine-wide install). Skipped when an install is already registered, because Inno Setup then knows where the game is ([details below](#a-game-that-is-already-installed)). |
| Xbox 360 Game Files | The package / an extracted game folder / keep the data that is already installed in the install folder. The choice is verified *before* the install starts: the geometry of the selection is read and the required files are checked, so a wrong path fails on the page instead of halfway through. The folder or package page follows it, and asks for nothing but the location. |
| Rock Band Blitz Ultimate | Download the pinned release from the mod's own GitHub release, use a zip, use a folder, or do not install it. |
| Shortcuts | Which of the four shortcuts to create ([details below](#shortcuts)). |
| Ready to install | The destination, the answers, and the commit this build is, for a last look before anything is written. |
| Progress | One bar across four stages (build, game data, mod, finishing) with a live status line; cancel asks for confirmation while a stage runs. |
| Finish | A three-way choice — open the launcher, launch the game, or do nothing ([details below](#the-finish-page)). |

The licence has no page: nothing in this wizard is agreed to, so the GPL-2.0 text
is not shown on the way past. It ships with the sources, and the installer is
built from them.

### The updater: the same wizard, asked for less

`setup.iss` is compiled twice, and `/DUpdaterMode` decides which build the result is
([details below](#updating)). The updater is the same wizard with the pages it has no question
about removed — no welcome, no install folder (the install it is updating is registered, so
Inno Setup already knows where it is), no shortcuts, no Ultimate pages, no summary — and it
puts the payload the *release manifest* names in place instead of one of its own. What is left
is the one question it may have (the game files, when the release says it needs them) and the
progress bar, and then it opens the launcher it came from.

### Shortcuts

Four shortcuts, one per `[Tasks]` entry, so the wizard's Shortcuts page is the one
place that decides what is created and in what form a silent install can change
it. The game's shortcut keeps its `--game_data_root="{app}\game"` argument, so the
game still launches standalone; the launcher's takes no arguments and finds the
game beside itself.

| Task | Where | Shortcut | Default |
| --- | --- | --- | --- |
| `startmenu` | Start menu | **Rock Band Blitz** — the game, with `--game_data_root="{app}\game"` | checked |
| `startmenulauncher` | Start menu | **Rock Band Blitz Launcher** — the settings launcher | checked |
| `launchericon` | Desktop | **Rock Band Blitz Launcher** | checked in the wizard; in a silent install only with `/LAUNCHERICON=1` |
| `desktopicon` | Desktop | **Rock Band Blitz** — the game | unchecked, as before |

The names are deliberately different so a list can tell them apart, and the
game's shortcut is never repointed at the launcher: the game has to stay runnable
on its own (D9, [`../docs/plans/launcher-plan.md`](../docs/plans/launcher-plan.md)).
A silent install creates every checked task, which is the two Start-menu shortcuts
(and the launcher's desktop one with `/LAUNCHERICON=1`), exactly as before the page
existed; `/MERGETASKS=!startmenu,!startmenulauncher` (Inno Setup) takes any of them
away.

### A game that is already installed

The wizard looks for the game before it shows its first page, using the Add/Remove
Programs entry of a previous install (the AppId in `[Setup]`) for the folder and
the version. An install that is there and the same version is offered as a
reinstall; a lower one is an update and a higher one a downgrade, and both warn
that saves made with the other version may not load, because the game's save
format is not part of what the installer records. Answering No ends setup before
it changes anything, and a silent install does not ask — it has no page to ask on
and no way to answer, so it installs.

The install folder page is skipped in that case: Inno Setup keeps the previous
folder, so an update lands on the game rather than beside it. It is the user's
choice to change it — the page still exists for an install that is not registered
anywhere, and `/DIR` overrides it either way.

### The finish page

The last page asks what to do when the wizard closes, with the game preselected:

| Choice | What it starts |
| --- | --- |
| **Open the launcher** | `rb_blitz_launcher.exe`, which finds the game beside itself. |
| **Launch Rock Band Blitz** | The game, with `--game_data_root="{app}\game"` — the same command its own shortcut carries. The game reads the launcher's profile on its own, so it still follows the target the install left behind. It is named once rather than "… Ultimate" when the mod is installed: the install folder is the same game either way. |
| **Do nothing** | Nothing; the wizard just closes. |

The old single `Launch Rock Band Blitz` checkbox is gone: Inno Setup's `[Run]`
entries are checkboxes, not a choice, and only one of them could be the post-install
action. The wizard has no test harness, so this page is checked by hand rather than
implied to be covered:

1. Install with **Launch Rock Band Blitz** selected — the game starts.
2. Install with **Open the launcher** selected — the launcher window opens.
3. Install with **Do nothing** selected — nothing starts.
4. `/VERYSILENT` with no `/RUNATEND` — nothing starts; `/VERYSILENT /RUNATEND=game`
   — the game starts.

A failed install never starts anything.

A copy of everything the helper did is left next to the game:

* `install-manifest.toml` — machine-readable, and the record the launcher reads. It
  carries `schema_version`, an `[install]` table (`installed_at`, `installer_version`,
  `helper_version`, `payload_commit`, `directory`, `game_directory`, `windows_build`,
  `architecture`), a `[payload]` component table (version, source, file and byte counts,
  whether the fingerprints matched, the executables it laid down), a `[game_data]`
  table (`source`, `directory`, `ultimate_installed`) and, when the mod was installed,
  an `[ultimate]` table. The launcher's first-run prefill reads
  `[install] game_directory` and `[game_data] ultimate_installed` from it, and
  `payload_commit` is the build a bug report names (`launcher-plan` D4).
* `install-report.txt` — the same, for a human, with the helper's log appended.

## Layout

| Path | What |
| --- | --- |
| `setup.iss` | The wizard: pages, sequencing, progress mapping, uninstall. Presentation only — it has no filesystem, HTTP or crypto support. |
| `src/` | `rb_blitz_setup_helper.exe`: STFS extraction, zip inflate, WinHTTP download, SHA-256 verification, file placement, manifests. |
| `src/commands.cpp` | The helper's command line: one command per stage, `--summary` / `--log` / `--progress` for each. |
| `config/pins.toml` | The only file that names the outside world: version, URLs, sizes, hashes. |
| `config/ultimate_fingerprints.toml` | What the mod's files hash to, in the same schema as `../config/game_fingerprints.toml`. |
| `tools/make_payload.ps1` | Snapshots a recompiled build — the game, its launcher and the DLLs they need — into `out/payload` (and optionally `out/payload.zip`). |
| `tools/make_art.ps1` | Downloads the optional side wizard image into the repository's `assets/`. |
| `tools/embed_config.cpp` | Compiles `config/pins.toml` + both fingerprint files into `out/generated/embedded_config.h` and `out/generated/pins.iss`, and generates `out/generated/update.toml` — the manifest a launcher checks and an updater installs from. |
| `build.ps1` | The release entry point: the launcher, the payload, the helper, the tests, the images, the updater, the setup executable, `update.toml`. |
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

1. builds `rb_blitz_launcher.exe` in the game's build tree (skip it with
   `-SkipLauncher`, or point at one built elsewhere with `-LauncherTarget`);
2. snapshots the recompiled build — the game, its launcher and the DLLs they
   need — into `installer\out\payload` if the snapshot is missing (or rebuilds it
   with `-RefreshPayload`);
3. generates the embedded config from `config\pins.toml`, resolving the
   `[payload] commit` pin to a real commit on the way;
4. builds the helper with the `installer-release` preset (clang++) and stages it
   next to `pins.iss` in `out\generated`;
5. runs the helper's test suite;
6. refreshes the optional side wizard image;
7. compiles `setup.iss` with ISCC and prints the setup exe's size and SHA-256.

Result: `installer\out\dist\RockBandBlitzSetup-<version>.exe`, or
`RockBandBlitzSetup-latest.exe` when the `[payload] commit` pin is `latest` — the
repository's own pin, which is a rolling build of the checkout rather than a
release cut from a tag, and is named so nobody mistakes one for the other. Use
`-Preset installer-release-msvc` from a Visual Studio developer prompt, and
`-SkipArt`, `-SkipTests`, `-SkipLauncher`, `-LauncherTarget`, `-SkipPayload`,
`-SkipSetup`, `-IsccPath` as needed — `Get-Help .\build.ps1 -Full` documents all
of them.

Tests alone, from this directory:

```powershell
ctest --preset installer-release --output-on-failure
```

The presets are directory-scoped, so `cmake` and `ctest` must run with
`installer\` as the working directory. Configure through `build.ps1` at least
once: `config\pins.toml` ships with a commit pin that only the script can resolve,
so a bare `cmake --preset` fails with a message saying exactly that (see
"Recording which build this is").

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

This is also the mode a release has to be cut in to be **updatable**: an update installs the
payload the release manifest names, and a build that embedded its payload pinned no download
for the manifest to name. `build.ps1` says so at the end of an embed-mode build rather than
leaving it to be discovered by a user whose *Update* button does nothing.

`build.ps1` prints the three assets a release publishes; publish all of them:

| Asset | What it is |
| --- | --- |
| `RockBandBlitzSetup-<version>.exe` | the setup executable the user downloads |
| `update.toml` | the release manifest: what a launcher checks, and what the updater installs from |
| the payload archive `-PayloadUrl` points at | what the updater fetches, and what `-PayloadSha256`/`-PayloadSize` were measured from |

Neither the payload zip nor the setup exe is byte-reproducible across machines
(the zip depends on the deflate implementation that wrote it, the exe on the
payload directory ISCC happens to find), so publish the checksums `build.ps1`
prints rather than a fixed one. `update.toml` records those same numbers, so a
release whose asset is re-uploaded with different bytes needs `update.toml`
regenerated too.

Either half can be published only after the audit has looked at it, because a
payload is the one artefact that reaches somebody else's machine:

```powershell
# 3. what ships, and what must never be in it (exit 0 = nothing was)
powershell -ExecutionPolicy Bypass -File scripts\audit_distributable.ps1
```

It reads the payload allow-list, the game and mod fingerprints, the frozen
toolchain paths and `git ls-files`, and it is the check that found the build
machine's `__FILE__` path inside the first payload - [distributable.md](../docs/distributable.md)
has the finding, the fix and the two deliberate failures that prove the check can
fail.

### Recording which build this is

The setup executable says which commit of this repository it was built from, so
that a bug report can name a build rather than a version string: it shows on the
wizard's "Ready to install" page next to the recompiled build, and lands in
`install-manifest.toml` (`payload_commit`, always present) and in
`install-report.txt` (`Commit`). What it records comes from the `[payload] commit`
pin in `config/pins.toml`, resolved **once, at build time** — an installer that
looked a ref up at install time would be asking a machine that has no checkout,
and possibly no git:

| The pin says | The build records |
| --- | --- |
| `latest` (the repository default) | The newest commit of the checkout being built, whatever `git rev-parse HEAD` answers at that moment. |
| A commit id, or any other ref | That commit: `build.ps1` resolves it with git before the pins are compiled in. |
| Empty | Nothing. The manifest carries `payload_commit = ""` and the report says `Commit     : unknown` — the honest answer for a build with no commit behind it, such as a tarball. |

`-PayloadCommit <40 hex digits>` overrides the pin for one build, which is what
you want when the installer is deliberately not being built from the commit it is
going to claim (a release cut from a tag, or a rebuild of an earlier build). The
value is never checked against anything at install time: it is a record, like the
`[payload] version` string next to it, not a pin the helper enforces. The
working tree is not consulted either — a build from a modified checkout still
claims the last commit it has.

Only `build.ps1` can turn a ref into a commit, so `tools/embed_config.cpp` refuses
to compile a pin that is not a 40-character commit id — including a bare
`cmake --preset installer-release` on a fresh clone, where the repository's
`latest` has nothing to resolve it. Configure through the script, write a commit
id in `pins.toml`, or pass `-PayloadCommit`.

## Updating

The launcher checks for a newer release every time it starts and offers to install it
([launcher/README.md](../launcher/README.md), section "Checking for updates"). Both halves of
that are built from this directory, and they are the two build products described below: a
release manifest, and a second build of the wizard that installs a release it did not come with.

### `update.toml`: the release manifest

Generated by `tools/embed_config.cpp` from `config/pins.toml`, so the version in it cannot drift
from the version the setup executable is named after, and published as a release asset named
`update.toml` (see "Cutting a release"). It is read twice, by two different programs:

| Reader | Keys it uses | What it decides |
| --- | --- | --- |
| the launcher | `version`, `requires_game_data`, `payload_url` | whether a newer release exists, whether it is worth asking about, and whether accepting it will ask for the game files |
| the updater | `payload_version`, `payload_url`, `payload_sha256`, `payload_size`, `payload_commit` | what to install, from where, and what it must hash to |

`schema_version` is the manifest's own shape — `1` today, `kUpdateManifestSchemaVersion` in
`src/config.h`. An updater or launcher that reads a manifest with a schema it does not know
refuses it and says so instead of guessing, which is what makes it safe for a future release to
add keys.

The URL the launcher checks is `[update] manifest_url` in `pins.toml`, and it names the asset of
the *latest* release (`.../releases/latest/download/update.toml`): GitHub resolves that to the
newest release, so a launcher compiled today keeps finding tomorrow's release without being
rebuilt. `[update] requires_game_data` is this release's answer to "does the payload change what
the game reads out of the user's own game files": `false` — the usual case — keeps the game data
that is already installed, and `true` has the updater ask for the package or the folder again.

### `RockBandBlitzUpdater.exe`: the second build of the wizard

`build.ps1` compiles `setup.iss` twice. The first build is the setup executable the user
downloads; the second passes `/DUpdaterMode=1` and produces the updater, which carries **no
payload** — it installs the build the release manifest names — and the setup executable embeds
it. The setup then places it under the user's own local application data:

```
%LOCALAPPDATA%\rb_blitz\update\RockBandBlitzUpdater.exe
```

Deliberately **not** in the install folder, which is the game's own: the payload audit allows
what may ship there, and a second copy of an installer in it would be one more file to explain.
`[UninstallDelete]` removes it with the rest, and the launcher resolves the same path
(`kUpdateDirName` in `launcher/src/update_launcher.cpp`).

What the updater does, in order: fetch `update.toml` (the wizard's own copy of the helper does
it, over the pinned URL — the manifest is the only thing it needs to know), install the payload
it names, import the game data *or* keep the one that is there according to
`requires_game_data`, write the manifest and the report with the **new** version and commit, and
open the launcher again.

Two details are worth knowing before changing any of it: Inno Setup records `AppVersion` — the
  version of the executable that ran — and an updater is older than the release it installs, so
  `CurStepChanged(ssDone)` writes the manifest's version into the uninstall entry. Without it, a
  machine that had been updated twice would report the version of the first updater it ever ran,
  and the next real install would offer to "update" an install that is already newer.
* **It does not touch shortcuts, the Ultimate mod, or the helper.** `[Tasks]` and `[Icons]` are
  compiled out of it, the mod is neither reinstalled nor removed, and the helper is placed only
  when it is missing: the updater's own helper belongs to whatever release last put the updater
  in place, which can be older than the one a later release left behind, and an update that moved
  a file backwards would be worse than one that left it alone. A full install is what refreshes
  the helper and the updater together — which is also the route for a manifest whose
  `schema_version` this updater does not understand: it says so and names the release page rather
  than guessing at a document it cannot read.

An install made by an *older* setup executable has no updater under the application data. The
launcher then opens the release page instead of running one, and records the version as
answered — the user was told, which is the whole of what the check promises. Installing a newer
setup by hand puts the updater in place for the next release.

## Configuration: `config/pins.toml`

One file, parsed at build time, embedded into both halves:

| Section | Keys | Meaning |
| --- | --- | --- |
| `[installer]` | `name`, `short_name`, `version`, `publisher`, URLs, `default_dir_name`, `min_windows_build` | Everything the wizard's own resources need. `name` titles the wizard, `short_name` is the program Add/Remove Programs lists, and `publisher` is the author rather than the project; `default_dir_name` is appended to the folder the user picks; `min_windows_build` is `10.0.17763` because the runtime and its DLLs are x86-64-v2. Values may be UTF-8 — the generated `pins.iss` is, and Inno Setup reads it as UTF-8 — so an accented name arrives intact; a byte that is not valid UTF-8 is refused at build time instead. |
| `[payload]` | `version`, `url`, `sha256`, `size`, `commit` | `version`, `url`, `sha256` and `size` are empty in the repository: a pin that does not exist yet fails with "no payload source configured" instead of downloading nothing, and the release workflow fills it in (or overrides it with `-PayloadUrl`/`-PayloadSha256`/`-PayloadSize`). `commit` is the exception — it ships as `latest`, which `build.ps1` resolves to the newest commit of the checkout before the pins are compiled in, so that every install can tell which build it is: see "Recording which build this is". |
| `[ultimate]` | `version`, `url`, `sha256`, `size`, repository/release URLs, `archive_prefix`, `destination_dir` | The Ultimate release the wizard offers and the helper downloads. |
| `[update]` | `manifest_url`, `dir_name`, `requires_game_data` | The update channel: where a launcher checks, the folder under the user's local application data the updater is placed in, and whether this release's payload needs the game data re-derived from the user's files. See "Updating". `manifest_url` may be empty (a build with no release channel, which then checks nothing), `dir_name` may not. |

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
| `rb_blitz_launcher.exe` | The launcher (D1), built beside the game in the same build tree. |
| `rexruntime.dll`, `rexgpu-xenos.dll` | Runtime DLLs the executable loads. |
| `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` | The Visual C++ runtime the binaries import. |

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

`assets/`, at the repository root, holds the images Inno Setup shows next to
`blitz.ico`. That icon is the app's, compiled into `rb_blitz.exe` by `rb_blitz.rc`
and used here as `SetupIconFile`; `blitz.png` is the app's own art too, committed
and used unconditionally as `WizardSmallImageFile` for the badge in the top-right
corner of the header. It is stored at 159x159, the largest the badge area reaches
(at 250% scaling), so it is only ever shrunk.

The large image on the left of the welcome and finish pages is the only optional
one. It is generated by `tools/make_art.ps1` and is **not** committed: the artwork
is not ours to license. Its absence is a supported configuration — `setup.iss`
only applies the `WizardImageFile` directive when the files exist, and
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
| `/LAUNCHERICON` | `1` | Silent installs only: also create the launcher's desktop shortcut. Omitted, nothing is added to the desktop. |
| `/MERGETASKS` | `!startmenu,!startmenulauncher` (Inno Setup) | Takes individual shortcuts away from a silent install; every checked task is created without it. |
| `/RUNATEND` | `game` (wizard default), `launcher`, `none` | What to start when the install finishes. A silent install defaults to `none` — it launches nothing unless this asks for one; the wizard's finish page preselects the value when it is given. |
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

A silent install launches nothing unless `/RUNATEND` asks for it (see the finish
page above); a silent *uninstall* always keeps the game data (see below).

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

**The helper has two callers.** The wizard uses every command above. The **launcher**
also invokes it, but with exactly three commands (`launcher-plan` §4.6, B8):
`install-ultimate` (its *Install Ultimate* action), and `verify-payload` /
`verify-game` (its *Verify installation* action). All three are read-only checks
apart from the mod install, which never takes administrator rights and never
bundles the mod; importing game data or re-placing the payload stays the
wizard's job, and the launcher points at the installer for those.

Two properties matter for reliability:

* **A failure always writes a summary.** A command that returns an error, and a
  C++ exception that escapes one, both end up as `ok=0;error=…`, so the wizard
  fails immediately with the reason instead of waiting out its timeout. (A hard
  crash — an access violation — still cannot report anything; the wizard's 60
  minute per-stage ceiling is the backstop for that, and control of the progress
  page is only given back when the stage ends.)
* **Progress is a file, not a window.** The helper draws nothing: it appends to
  `--progress` and the wizard repaints the bar and the status line itself, so a
  stage behaves the same in silent and in interactive installs. The reports are
  fine-grained enough to be worth watching — the game data import counts the bytes
  of each file as they land, inside the 360 MB archive as well as between files —
  because a bar that only moves per file would sit still for the whole import and
  then jump to the end of it.

## Uninstall

The uninstaller runs the helper once (`uninstall-cleanup`) and then removes the
install folder. It asks one question — "Delete the imported Rock Band Blitz game
data as well?" — and the imported data is only deleted if the user answers yes:
several hundred megabytes the user may well want to keep, for example to reinstall
later without the disc or the package again. A silent uninstall keeps it. The
mod's `ultimate\` folder lives inside the game data and follows it. The launcher
(`rb_blitz_launcher.exe`) travels in the payload and is removed with it;
`launcher.toml`, the user's own settings, is deliberately left behind — like the
game data, it is not the installer's to delete.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| `no payload to embed: pass /DPayloadUrl and /DPayloadSha256, or create the payload snapshot first` | Embed mode with no `out\payload\payload-manifest.toml`. Run `make_payload.ps1`, or pass `-PayloadUrl`. |
| `pins.iss is missing from the generated directory` | The helper was never built; run `build.ps1` (or configure the preset) first. |
| The test suite fails in a download-mode build with a mismatch between the pin and `pins.toml` | Expected: pass `-SkipTests` (see "Cutting a release"). |
| `-PayloadUrl needs -PayloadSha256` | An installer that downloads its own build must name which build it is. `-AllowUnverifiedPayload` exists for local testing only. |
| A rebuild ignores a pin you just removed | `-DRBBLITZ_EMBED_EXTRA_ARGS` is a *cached* CMake variable. `build.ps1` always passes it (possibly empty), so build through the script rather than a bare `cmake --preset`. |
| `[payload] commit '<x>' is not a 40-character commit id` while configuring | The pin is a name — `latest` by default — and only `build.ps1` has git at the right moment to resolve it. Configure through the script, or write a commit id (or nothing) in `config\pins.toml`. |
| `the generated pins.iss says commit '<a>' but this build resolved '<b>'` | A stale cached `-DRBBLITZ_EMBED_EXTRA_ARGS`, which would ship a setup executable claiming to be another build. Delete `installer\out\build` and build again. |
| The wizard is up but nothing happens, then it fails after ten minutes | The helper died silently. Its own log is next to the install folder (`rbblitz-probe.log` in the temporary directory while the wizard is still open); `install-report.txt` in the finished install has the rest. |
