# installer/ — the setup executable

A standalone Windows installer that puts a ready-to-play Rock Band Blitz build on
a machine that has never seen this repository. The user answers where to install —
including where the DLC library and the saved games live — where the game data comes
from, whether they want Rock Band Blitz Ultimate, and which shortcuts to create, and
gets a folder with a launchable `rb_blitz.exe` in it — no compiler, no CMake, no
instructions to follow.

One product is built from this directory: the *complete package* — one wizard with the
recompiled build inside it, made from the working tree it is run in ([the section
below](#the-one-product)). A release publishes this one file and nothing else.

Everything in this directory is *packaging*. Nothing here is needed to build or
run the game, and none of it is compiled into `rb_blitz.exe`.

## The one product

| Product | What it is | What a release publishes |
| --- | --- | --- |
| `RockBandBlitzSetup-<version>.exe` | **The complete package.** `setup.iss` with the payload inside it: it installs with no network access at all. Built from the checkout `build.ps1` runs in, named after the version in `config/pins.toml`. | this file, alone |

Why it is shaped that way:

* **One file per release, always.** A release is the setup executable and nothing
  else: no manifest, no payload archive, no checksum file to keep in step with it. A
  release that published a second file would be a release that can be published
  halfway.
* **The build is the checkout.** `build.ps1` snapshots the build tree in
  `out\build\win-amd64-release`, records the commit `git rev-parse HEAD` answers, and
  writes one file — the one a release publishes. Cutting a release is running the same
  command from the tagged commit.

```powershell
# from the repository root; the result is installer\out\dist\RockBandBlitzSetup-<version>.exe
powershell -ExecutionPolicy Bypass -NoProfile -File installer\build.ps1
```

The version comes from one value — `[installer] version` in `installer\config\pins.toml`
— and the file name, the wizard's title (`Setup - Rock Band Blitz <version>`) and every
install manifest follow it; bump it there and run the command again.
`scripts/acceptance_product_titles.ps1` asserts the built product's caption when the
machine lets it open the window unattended.

## What the installer does and does not contain

| It contains | It never contains |
| --- | --- |
| The recompiled build (`rb_blitz.exe`) and the runtime DLLs the game loads | Any game code, archive, music or key |
| The wizard, its images and the helper that does the file work | Rock Band Blitz Ultimate, in any form |
| — | Anything read from the machine it is built on |

The game data is always the user's own: an Xbox 360 package or a game folder they
already extracted. The Rock Band Blitz Ultimate mod is a third-party fan project,
so it is downloaded from its own GitHub release while the installer runs, or
supplied by the user — never mirrored, re-hosted or bundled. The distribution
rules are in [`../docs/guides/distributable.md`](../docs/guides/distributable.md) — what ships, and
the audit that refuses a payload with game data, a credential, a symbol table or the
build machine's path in it — and [`../docs/guides/ultimate-compat.md`](../docs/guides/ultimate-compat.md).

## What the user walks through

| Page | What happens |
| --- | --- |
| Welcome | What the installer needs (a copy of the game they own), what it refuses to do (no game files included or downloaded). |
| Install folder | `C:\Program Files\Rock Band Blitz` by default: the install is the administrative one (Inno Setup's `PrivilegesRequired=admin`), so it prompts for elevation, and `/CURRENTUSER` is what installs under a user's own profile instead. The page also asks for the DLC library and the save folder, and is shown even when an install is already registered — a reinstall has to be able to see and change them ([details below](#the-dlc-library-and-the-saved-games)). |
| Xbox 360 Game Files | The package / an extracted game folder / keep the data that is already installed in the install folder. The location is asked on this page, not on one of its own: choosing "package" or "folder" puts the prompt, the path box and its Browse button under the radios. The choice is verified *before* the install starts: the geometry of the selection is read and the required files are checked, so a wrong path fails on the page instead of halfway through. |
| Rock Band Blitz Ultimate | Download the pinned release from the mod's own GitHub release (selected by default), use a zip, use a folder, or do not install it. The zip or folder is asked for on this page, and *do not install* puts how to add it later in the same place: run this installer again, or copy the release's `Xbox` folder into `<install folder>\game\ultimate`. |
| Shortcuts | Which of the two shortcuts to create — both are the game ([details below](#shortcuts)). |
| Ready to install | The destination, the answers, and the commit this build is, for a last look before anything is written. |
| Progress | One bar across four stages (build, game data, mod, finishing). The first line names the stage and how far it has got ("Installing Rock Band Blitz Ultimate  (71%)"), the second what the helper is doing inside it ("adding the mod's songs and modes…"), which is what the stage's own sentence says until the helper has reported. Cancel asks for confirmation while a stage runs. |
| Finish | A two-way choice — launch the game, or do nothing ([details below](#the-finish-page)). |

The licence has no page: nothing in this wizard is agreed to, so the GPL-2.0 text
is not shown on the way past. It ships with the sources, and the installer is
built from them.

### The updater: the same wizard, asked for less (dormant since 2026-10-15)

`setup.iss` is compiled twice for the complete package, and `/DUpdaterMode` decides which
build the result is ([details below](#the-updater-executable-and-its-manifest)). The updater is the same wizard with the pages it has
no question about removed — no welcome, no install folder (the install it is updating is registered, so
Inno Setup already knows where it is), no shortcuts, no Ultimate pages, no summary — and it
puts the payload the *release manifest* names in place instead of one of its own.

**Nothing starts it any more.** It was Controller Settings' *Update* control that ran it, and
that control was removed with D19's update check on 2026-10-15; the executable itself retired
on 2026-10-19 (gameplay-mapping plan §19). Installing a release is now downloading the release's
setup executable and running it. The updater
is still built and still placed under the user's local application data, so the machinery stays
working and tested for whatever the release model decides to do with it next; the manifest it
reads is no longer published (see [below](#the-manifest-is-no-longer-published)).

### Shortcuts

Two shortcuts, one per `[Tasks]` entry, so the wizard's Shortcuts page is the one
place that decides what is created and in what form a silent install can change
it. Both are the game, and both keep their `--game_data_root="{app}\game"`
argument: the game is runnable on its own, and it owns every player setting in
the in-game Help & Options menu.

| Task | Where | Shortcut | Default |
| --- | --- | --- | --- |
| `startmenu` | Start menu | **Rock Band Blitz** — the game, with `--game_data_root="{app}\game"` | unchecked, interactive and silent |
| `desktopicon` | Desktop | **Rock Band Blitz** — the game | unchecked |

There is one program. The external settings application a 2026-10-14-and-earlier
release shipped has retired (gameplay-mapping plan §19), so an upgrade removes its
executable and the Start-menu/desktop entries called **Rock Band Blitz Controller
Settings** through `[InstallDelete]`; see [Uninstall](#uninstall). The two parameters
that once asked for its desktop icon (`/LAUNCHERICON=1`, `/CONTROLLERICON=1`) are gone
with the task they named: a script that still passes one gets no shortcut and no
error, which is what it would have got from the page anyway.

Nothing is created unless the user asks: the wizard's page starts from no answer,
the `[Tasks]` declarations carry `Flags: unchecked` so a silent install starts from
no answer too, and `UsePreviousTasks=no` keeps an earlier install's choice from
reappearing on a reinstall. `/TASKS=startmenu` (or `/MERGETASKS`) is what opts a
shortcut back in, and it is the only thing that does.

### A game that is already installed

The wizard looks for the game before it shows its first page, using the Add/Remove
Programs entry of a previous install (the AppId in `[Setup]`) for the folder and
the version. An install that is there and the same version is offered as a
reinstall; a lower one is an update and a higher one a downgrade, and both warn
that saves made with the other version may not load, because the game's save
format is not part of what the installer records. Answering No ends setup before
it changes anything, and a silent install does not ask — it has no page to ask on
and no way to answer, so it installs.

The install folder page is no longer skipped in that case. It is the page the two
external folders are asked for, so a reinstall has to show it; Inno Setup's own
`UsePreviousAppDir` still prefills the folder with the one the install was
registered with, so an update lands on the game rather than beside it. `/DIR`
overrides it either way.

### The finish page

The last page asks what to do when the wizard closes, with the game preselected:

| Choice | What it starts |
| --- | --- |
| **Launch Rock Band Blitz** | The game, with `--game_data_root="{app}\game"` — the same command its own shortcut carries. The game reads its settings and input profile on its own, so it still follows the target the install left behind. It is named once rather than "… Ultimate" when the mod is installed: the install folder is the same game either way. |
| **Do nothing** | Nothing; the wizard just closes. |

The game is the interactive default: the install exists to be played, and it owns
its own settings. An update is different: the updater always launches the game when
it is done.

The old single `Launch Rock Band Blitz` checkbox is gone: Inno Setup's `[Run]`
entries are checkboxes, not a choice, and only one of them could be the post-install
action. The wizard has no test harness, so this page is checked by hand rather than
implied to be covered:

1. Install with **Launch Rock Band Blitz** selected — the game starts.
2. Install with **Do nothing** selected — nothing starts.
3. `/VERYSILENT` with no `/RUNATEND` — nothing starts; `/VERYSILENT /RUNATEND=game`
   — the game starts.

A failed install never starts anything.

A copy of everything the helper did is left next to the game:

* `install-manifest.toml` — machine-readable, the support record. It
  carries `schema_version`, an `[install]` table (`installed_at`, `installer_version`,
  `helper_version`, `payload_commit`, `directory`, `game_directory`, `windows_build`,
  `architecture`), a `[payload]` component table (version, source, file and byte counts,
  whether the fingerprints matched, the executables it laid down), a `[game_data]`
  table (`source`, `directory`, `ultimate_installed`), a `[paths]` table recording the
  two folders the folder page asked for (`file`, and `dlc_root`/`save_root` — or
  `dlc_default`/`save_default` when the answer was the game's own, which only a silent
  install can still give by passing an empty `/DLCDIR`/`/SAVEDIR`), and, when the mod
  was installed, an `[ultimate]` table. `payload_commit` is the build a bug report
  names (`launcher-plan` D4). An update, which has no folder page, writes no `[paths]`
  table: it answered nothing.
* `install-report.txt` — the same, for a human, with the helper's log appended.

## Layout

| Path | What |
| --- | --- |
| `setup.iss` | The wizard: pages, sequencing, progress mapping, uninstall. Presentation only — it has no filesystem, HTTP or crypto support. Also the updater (compiled with `/DUpdaterMode=1`) and, with the payload inside it, the complete package. |
| `src/` | `rb_blitz_setup_helper.exe`: STFS extraction, zip inflate, WinHTTP download, SHA-256 verification, file placement, manifests. |
| `src/commands.cpp` | The helper's command line: one command per stage, `--summary` / `--log` / `--progress` for each. |
| `src/paths.cpp` | The two folders: reading `paths.toml` for the folder page's prefill, checking the answers, and committing them. The file format itself comes from the game (`src/settings/paths.cpp`, compiled in), so the installer cannot write a file the game reads differently. |
| `config/pins.toml` | The only file that names the outside world: version, URLs, sizes, hashes. |
| `config/ultimate_fingerprints.toml` | What the mod's files hash to, in the same schema as `../config/game_fingerprints.toml`. |
| `tools/make_payload.ps1` | Snapshots a recompiled build — the game and the DLLs it needs — into `out/payload` (and optionally `out/payload.zip`), recording the snapshot's provenance in the manifest. |
| `tools/payload_provenance.ps1` | The provenance checks the two scripts share: which backends a build tree was configured with, how to read the manifest back, and whether a snapshot is really the tree it claims to be a copy of. `build.ps1` decides with it whether an existing snapshot may be reused. |
| `tools/make_art.ps1` | Downloads the optional side wizard image into the repository's `assets/installer/`. |
| `tools/embed_config.cpp` | Compiles `config/pins.toml` + both fingerprint files into `out/generated/embedded_config.h` and `out/generated/pins.iss`, and generates `out/generated/update.toml` — the manifest the dormant updater would install from. It also derives `AppVerName` from the payload version (falling back to the [installer] version), which is what the wizard's title shows. |
| `build.ps1` | The build entry point: the payload, the helper, the tests, the images, the updater, and the setup executable, all from this checkout. |
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

1. snapshots the recompiled build — the game and the DLLs it needs — into
   `installer\out\payload`, after checking an existing snapshot against the
   build tree it claims to be a copy of (see [The payload allow-list](#the-payload-allow-list));
   `-RefreshPayload` rebuilds it anyway, and `-SkipPayload` keeps it as it is, deliberately;
2. generates the embedded config from `config\pins.toml`, resolving the
   `[payload] commit` pin to a real commit on the way;
3. builds the helper with the `installer-release` preset (clang++) and stages it
   next to `pins.iss` in `out\generated`;
4. runs the helper's test suite;
5. refreshes the optional side wizard image;
6. compiles `setup.iss` with ISCC and prints the setup exe's size and SHA-256.

Result: `installer\out\dist\RockBandBlitzSetup-<version>.exe`, named after the
version in `config\pins.toml`, recording the commit the `[payload] commit` pin
resolves to (`HEAD` — the checkout being built — by default; `-PayloadCommit <40 hex
digits>` records another one). Use
`-Preset installer-release-msvc` from a Visual Studio developer prompt, and
`-SkipArt`, `-SkipTests`, `-SkipPayload`,
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

Everything the complete package installs is *inside* it, so an install needs no
network access at all. That is what every release relies on: the payload travels in
the same file as the wizard ([the one product](#the-one-product)).

`setup.iss` still knows one older way to get the payload: the wizard downloads a
pinned payload archive at install time (`-PayloadUrl -PayloadSha256 [-PayloadSize]`)
instead of carrying one. It is kept working because it is the engine the updater
installs with — and it is **not** what a release uses: a release publishes one file,
so putting the payload in a second file would be putting a release on two files.

### Cutting a release

A release is the complete package and nothing else:

```powershell
# 1. the build itself, from the commit the release is for
cmake --preset win-amd64-release; cmake --build --preset win-amd64-release

# 2. the package, made from that checkout
powershell -File installer\build.ps1

# 3. what ships, and what must never be in it (exit 0 = nothing was)
powershell -File scripts\audit_distributable.ps1

# 4. publish installer\out\dist\RockBandBlitzSetup-<version>.exe as the release's
#    only asset, and write the sha256 build.ps1 printed into the release notes
```

`.github/workflows/builds.yml` does the same on a tag whose name matches the pinned
version, and publishes the file as the release's only asset. It runs on a self-hosted
runner because the payload needs the user's own game data, which is deliberately not
in the repository.

The setup executable is not byte-reproducible across machines (the exe depends on the
payload directory ISCC happens to find), so publish the checksum `build.ps1` printed
rather than a fixed one.

A payload still may not be published without the audit having looked at it, because a
payload is the one artefact that reaches somebody else's machine, and the audit is
what found the build machine's `__FILE__` path inside the first one —
[distributable.md](../docs/guides/distributable.md) has the finding, the fix and the
two deliberate failures that prove the check can fail.

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
| `HEAD` (the repository default) | The commit `git rev-parse HEAD` answers for the checkout being built. |
| A commit id, or any other ref | That commit: `build.ps1` resolves it with git before the pins are compiled in. |
| Empty | Nothing. The manifest carries `payload_commit = ""` and the report says `Commit     : unknown` — the honest answer for a build with no commit behind it, such as a tarball. |

`-PayloadCommit <40 hex digits>` overrides the pin for one build, which is what
you want when the installer is deliberately not being built from the commit it is
going to claim (a release cut from a tag, or a rebuild of an earlier build). The
value is never checked against anything at install time: it is a record, like the
`[payload] version` string next to it, not a pin the helper enforces. The
working tree is not consulted either — a build from a modified checkout still
claims the last commit it has. (The *snapshot* records its own provenance too, in
`payload-manifest.toml`'s `[payload]` table, and that one is checked against the build tree on
every release build — see [The payload allow-list](#the-payload-allow-list).)

Only `build.ps1` can turn a ref into a commit, so `tools/embed_config.cpp` refuses
to compile a pin that is not a 40-character commit id — including a bare
`cmake --preset installer-release` on a fresh clone, where the repository's
`HEAD` has nothing to resolve it. Configure through the script, write a commit
id in `pins.toml`, or pass `-PayloadCommit`.

## Updating

**There is no update check any more.** The settings application used to ask for a release
manifest on every start, offer an update, and run an updater that installed the build
the manifest named (D19 of [2026-09-27-launcher-plan.md](../docs/plans/finished/2026-09-27-launcher-plan.md)). That was
removed on 2026-10-15, and the application itself retired on 2026-10-19 (gameplay-mapping
plan §19), taking the prompt, the manifest URL it compiled in and the two modules behind it
with them.

What replacing an install means now:

| What the user wants | What they do |
| --- | --- |
| the newest release | download the newest release's setup executable and run it |
| one exact version | download that release's `RockBandBlitzSetup-<version>.exe` and run it |

All three end the same way: a wizard installs over what is there, keeping the user's
game data, DLC library and saved games, and replacing the files it owns. Nothing checks
for anything in the background, nothing asks on a timer, and an install that nobody
touches stays exactly as it is.

### The updater executable and its manifest

`RockBandBlitzUpdater.exe` (the same wizard with `/DUpdaterMode=1`) is still built and still
placed by the setup executable under the user's own local application data:

```
%LOCALAPPDATA%\rb_blitz\update\RockBandBlitzUpdater.exe
```

Deliberately **not** in the install folder, which is the game's own: the payload audit allows
what may ship there, and a second copy of an installer in it would be one more file to explain.
What the updater does, when something runs it: fetch a release manifest over the URL pinned in
`config/pins.toml`, install the payload it names, import the game data *or* keep the one that is
there according to `requires_game_data`, write the manifest and the report with the **new**
version and commit, and launch the game again.

**Nothing runs it**, and that is a deliberate, recorded state rather than an oversight:

* its only caller was Controller Settings' *Update* control, which no longer exists, and the
  executable itself retired on 2026-10-19 (gameplay-mapping plan §19);
* the manifest it fetches (`update.toml`) is **no longer published**: a release is one file,
  and the manifest would be a second one. A machine that ran it today would fetch a 404 and
  say so.

It is kept because it is the one piece of the tree that installs a *payload* rather than a
whole package, and the release model may still want that. Until something calls it, treat it as
dormant equipment: built, tested by the installer suite, and not part of any user path.

Two details are worth knowing before changing any of it. Inno Setup records `AppVersion` — the
version of the executable that ran — and an updater is older than the release it installs, so
`CurStepChanged(ssDone)` writes the manifest's version into the uninstall entry. Without it, a
machine that had been updated twice would report the version of the first updater it ever ran,
and the next real install would offer to "update" an install that is already newer. And it does
not touch shortcuts, the Ultimate mod, or the helper: `[Tasks]` and `[Icons]` are compiled out
of it, the mod is neither reinstalled nor removed, and the helper is placed only when it is
missing.

An install made by an *older* setup executable has an updater under the application data that
nothing will ever run. That is fine: it is a dormant file in the user's own folder, and
`[UninstallDelete]` removes it with the rest.

### The manifest is no longer published

`out/generated/update.toml` is still generated by `tools/embed_config.cpp` from
`config/pins.toml`, so the version in it cannot drift from the version the setup executable is
named after. It is no longer **published**: a release is one file, and the manifest would be the
second one. What it was, for whoever picks the updater up again:

| Reader | Keys it uses | What it decided |
| --- | --- | --- |
| Controller Settings (removed 2026-10-19) | `version`, `requires_game_data`, `payload_url` | whether a newer release exists, whether it was worth asking about, and whether accepting it would ask for the game files |
| the updater | `payload_version`, `payload_url`, `payload_sha256`, `payload_size`, `payload_commit` | what to install, from where, and what it must hash to |

`schema_version` is the manifest's own shape — `1` today, `kUpdateManifestSchemaVersion` in
`src/config.h` — and a reader that does not know it refuses the document instead of guessing,
which is what makes it safe for a future release to add keys. `[update] manifest_url` in
`config/pins.toml` still names the *latest* release's `update.toml` (`.../releases/latest/download/
update.toml`), which is the URL that would keep working forever without a rebuild — if a release
published the file. `[update] requires_game_data` is this release's answer to "does the payload
change what the game reads out of the user's own game files": `false` — the usual case — keeps the
game data that is already installed, and `true` has the updater ask for the package or the folder
again.

One file, parsed at build time, embedded into both halves:

| Section | Keys | Meaning |
| --- | --- | --- |
| `[installer]` | `name`, `short_name`, `version`, `publisher`, URLs, `default_dir_name`, `min_windows_build` | Everything the wizard's own resources need. `name` titles the wizard, `short_name` is the program Add/Remove Programs lists, and `publisher` is the author rather than the project; `default_dir_name` is appended to the folder the user picks; `min_windows_build` is `10.0.17763` because the runtime and its DLLs are x86-64-v2. Values may be UTF-8 — the generated `pins.iss` is, and Inno Setup reads it as UTF-8 — so an accented name arrives intact; a byte that is not valid UTF-8 is refused at build time instead. |
| `[payload]` | `version`, `url`, `sha256`, `size`, `commit` | `version`, `url`, `sha256` and `size` are empty in the repository: a pin that does not exist yet fails with "no payload source configured" instead of downloading nothing, and `-PayloadUrl`/`-PayloadSha256`/`-PayloadSize` fill them in for a build that downloads at install time. `commit` is the exception — it ships as `HEAD`, which `build.ps1` resolves to the checkout's commit before the pins are compiled in, so that every install can tell which build it is: see "Recording which build this is". |
| `[ultimate]` | `version`, `url`, `sha256`, `size`, repository/release URLs, `archive_prefix`, `destination_dir` | The Ultimate release the wizard offers and the helper downloads. |
| `[update]` | `manifest_url`, `dir_name`, `requires_game_data` | The update channel: where a check would ask, the folder under the user's local application data the updater is placed in, and whether this release's payload needs the game data re-derived from the user's files. See "Updating". `manifest_url` may be empty (a build with no release channel, which then checks nothing), `dir_name` may not. |

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
| `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` | The Visual C++ runtime the binaries import. |
| `vulkan-1.dll` | The Khronos Vulkan loader — only when the build offers Vulkan (see below). It is *opened at run time*, never imported, so nothing else would reveal its absence. |

Nothing else is quoted from the build tree — in particular `rb_blitz.toml` is a
developer profile rather than a shipped file, and the `*d.dll` / `*.pdb` files
belong to a debug build. The DLL list is not a guess: the script re-derives the
imports from the binaries it copies, so a build that starts needing another DLL
fails while the snapshot is made instead of on the user's machine. The snapshot is
written as `payload-manifest.toml`, in the same fingerprint format the helper reads
back, and the helper hashes every file it placed before it reports success.

The script also prints the payload's **renderer backends** (`payload renderer backends: d3d12,
vulkan`), read from the build tree's `CMakeCache.txt`, and warns when Vulkan is missing: the SDK's
own Windows default for `REXGLUE_USE_VULKAN` is OFF, so a hand-configured tree ships a
`rexgpu-xenos.dll` with one backend and the game's *Renderer* row then
offers one choice, which has already been mistaken for a bug once. The Windows presets configure it
ON ([build-and-run.md](../docs/guides/build-and-run.md)), and the game additionally probes the loader
before offering the backend (`src/settings/boot.cpp`).

### A snapshot has to prove it is still the tree you meant to ship

A snapshot is a copy of a build tree that was correct the day it was taken, and
`installer\out\payload` outlives the build it came from. `REXGLUE_USE_VULKAN=ON` reached the
Windows presets on 2026-10-13; a snapshot taken earlier still held a `rexgpu-xenos.dll` with
one backend, and every plain `build.ps1` after that kept it, embedded it and installed it —
which is what the owner met on 2026-10-14 as an installed game whose *Renderer* row offered
D3D12 alone (P1 of [installed-game-fix-prompts.md](../docs/plans/finished/2026-10-14-installed-game-fix-prompts.md)).

The manifest therefore records the snapshot's **provenance** — the backends of the tree it came
from, and the commit it was taken at — and `build.ps1` checks a snapshot against the tree it is
about to embed before reusing it:

| Checked | On a mismatch |
| --- | --- |
| the manifest's `backends` against `CMakeCache.txt` in the build tree | rebuild |
| the manifest's `commit` against the commit this build resolved, when both name one | rebuild |
| every file the tree owns, byte for byte: `rb_blitz.exe`, `rexruntime.dll`, `rexgpu-xenos.dll` | rebuild |
| `vulkan-1.dll` listed, when the tree offers Vulkan at all | rebuild |

So a snapshot that records no provenance (every one written before 2026-10-14), or one built for
different backends, is rebuilt with the reason printed (`stale   : the existing snapshot is not
this build tree:`) rather than shipped. The same check runs again over the snapshot that was just
taken, so a release that cannot account for its own payload fails the build instead of the
install. `-SkipPayload` is the only way to keep a snapshot that does not match — the deliberate
case, such as a snapshot made by hand from another tree — and it warns with the same reasons;
`-RefreshPayload` forces a rebuild when nothing is wrong.

The recorded commit is a label rather than an identity: it is the checkout's `HEAD`, so a build
from a modified working tree records the last commit it has. The file digests above are what
decide whether two snapshots are the same build.

**The payload carries the Vulkan loader.** `vulkan-1.dll` is opened at run time by the SDK with
`LoadLibrary` and never imported by anything, so a machine with a GPU driver but no loader — a
stripped Windows install, a VM with a passthrough adapter — would otherwise be offered Vulkan and
fail inside presentation setup, after the backend is committed. When the build offers Vulkan the
script therefore ships a copy: `-VulkanLoaderDir <dir>` pins the source, otherwise it looks at
`$env:VULKAN_SDK\Bin`, then `C:\VulkanSDK\*\Bin` (LunarG SDK, newest first), then
`$env:WINDIR\System32`. The copy wins at load time because Windows searches the application
directory before System32, so the install is self-contained; it is the Khronos loader (Apache-2.0),
the same file the GPU vendors' runtimes install, and it is *not* a driver — a machine without a
Vulkan-capable GPU still cannot use the backend, and the game says so and falls back. Not finding a
loader is fatal to the payload build (`install the Vulkan runtime, or pass -VulkanLoaderDir`);
`-AllowMissingVulkanLoader` ships anyway and warns, which is the shape for a machine whose users
all have drivers.

`-VulkanLoaderDir` itself did nothing at all until 2026-10-14: the line that chose the loader
assigned `$vulkanLoaderDir`, which *is* the `-VulkanLoaderDir` parameter (PowerShell variable
names are case-insensitive), so the parameter's value was wiped before it was read and the
search always ran through to its last resort. Measured then: a `-VulkanLoaderDir` holding a
19-byte `vulkan-1.dll` still shipped System32's 1,452,408-byte copy, which is also why the fatal
"no loader anywhere" path was unreachable on a machine that has one. The answer lives in
`$vulkanLoaderSource` now; `installer_tests` fails if the old assignment comes back.

## Rock Band Blitz Ultimate

An optional community mod. It is *not* needed to play: the runtime reproduces the
mod's three edits host-side, and an installed payload works by drag-and-drop
(`docs/guides/ultimate-compat.md`). The installer offers three ways to get it, and none
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

`assets/icon/`, at the repository root, holds the images Inno Setup shows next to
`blitz.ico`. That icon is the app's, compiled into `rb_blitz.exe` by `rb_blitz.rc`
and used here as `SetupIconFile`; `blitz.png` is the app's own art too, committed
and used unconditionally as `WizardSmallImageFile` for the badge in the top-right
corner of the header. It is stored at 159x159, the largest the badge area reaches
(at 250% scaling), so it is only ever shrunk.

The large image on the left of the welcome and finish pages is the only optional
one. It is generated by `tools/make_art.ps1` into `assets/installer/` and is
**not** committed: the artwork
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
| `/DLCDIR` | folder | Where the DLC library is. Omitted, the installation's own `paths.toml` is read, then the legacy settings profile, then the default under the user's Documents; given empty (`/DLCDIR=`), the game's own `<game data>\dlc` is stored. |
| `/SAVEDIR` | folder | Where the saves go. Same rule as `/DLCDIR`; its default is `Documents\Rock Band Blitz\Saved Games`, and empty means the game's own save root under the user's Documents. |
| `/ULTIMATESOURCE` | `none` (default), `pin`, `zip`, `folder` | Whether and how to install the mod. |
| `/ULTIMATEZIP` | file | With `zip`. |
| `/ULTIMATEFOLDER` | folder | With `folder`. |
| `/TASKS` | `startmenu,desktopicon` (Inno Setup) | Which shortcuts to create. Nothing is created without this: the declarations are unchecked and a silent install starts from no answer. |
| `/MERGETASKS` | `startmenu` (Inno Setup) | The additive form of the same thing; `!name` takes one away, which is already the default. |
| `/RUNATEND` | `game` (wizard default), `none` | What to start when the install finishes. A silent install defaults to `none` — it launches nothing unless this asks for one; the wizard's finish page preselects the value when it is given. The retired `controller`/`launcher` spellings are refused rather than ignored: there is no settings application to start. |
| `/DIR` | folder | Install folder (Inno Setup). |
| `/CURRENTUSER` | — | Install under the current user's own profile instead of machine-wide. The default is machine-wide (`/ALLUSERS`), because that is the mode whose default folder is `C:\Program Files`; an unattended run that cannot answer an elevation prompt has to pass this. |
| `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=<file>` | — | Usual unattended switches (Inno Setup). |

Example — unattended install from a package, with the pinned mod, no dialog at all:

```bat
RockBandBlitzSetup-0.1.0.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART ^
  /CURRENTUSER /DIR="C:\Games\Rock Band Blitz" /LOG="%TEMP%\rbb-install.log" ^
  /GAMEMETHOD=package /GAMEPACKAGE="<the extracted package folder>" ^
  /DLCDIR="D:\Rock Band DLC" /SAVEDIR="D:\Rock Band Saves" ^
  /ULTIMATESOURCE=pin
```

A silent install launches nothing unless `/RUNATEND` asks for it (see the finish
page above); a silent *uninstall* always keeps the game data (see below). It also
cannot answer a UAC prompt: the setup asks for the elevation a machine-wide install
needs, so an unattended run that is not already elevated passes `/CURRENTUSER` (as
the example does) or `/DIR` into a folder it can write.

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
| `read-paths` | Read an installation's `paths.toml` and the legacy settings profile beside it, for the folder page's prefill. Read-only. |
| `validate-paths` | Check the three locations before the install starts: refuse a save folder inside the read-only game data, say what a DLC folder is, create and probe the save folder. `--no-touch` reports the same things without creating anything, which is what the check that runs while the folder page is merely shown uses. |
| `write-paths` | Commit the two folders to `paths.toml` — transactionally, keeping every other key, comment and hand edit in the file. |
| `finalize` | Write `install-manifest.toml` and `install-report.txt`. |
| `uninstall-cleanup` | Remove what the helper wrote, `--keep-game-data` to keep the import. |
| `version` | Print the versions it was built with. |

**The wizard is the helper's only caller.** Controller Settings used to invoke three commands
(`install-ultimate`, `verify-payload`, `verify-game`) from its own actions; that application
retired on 2026-10-19 (gameplay-mapping plan §19), and the mod is installed by this wizard or by
copying its files into `<game root>\ultimate` ([ultimate-compat.md](../docs/guides/ultimate-compat.md)).

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
mod's `ultimate\` folder lives inside the game data and follows it. The
controller-settings executable an earlier release shipped is removed by an upgrade's
`[InstallDelete]` (see [Shortcuts](#shortcuts)) and by this uninstaller;
`launcher.toml` and `paths.toml` — the user's own settings, and the two folders
they chose — are deliberately left behind: like the game data, they are not the
installer's to delete, and a reinstall finds them again.

## The DLC library and the saved games

The install folder page asks for three locations, and the last two are stored in
`paths.toml`, which sits **beside the installed `rb_blitz.exe`** — one file per
installation, so finding it never depends on the folder it selects. The game reads
it at start-up; the format is the game's own (`src/settings/paths.h`), and the
installer compiles that same code rather than reimplementing it, so what the
wizard writes is byte for byte what the game reads:

```toml
schema_version = 1

[paths]
dlc_root = 'D:\Rock Band DLC'
user_data_root = 'D:\Rock Band Saves'
```

The values are TOML literal strings, so a Windows path needs no escaping, and they
are UTF-8 — a folder with an accented or non-ASCII name round trips through a
silent install's `/DLCDIR` and `/SAVEDIR` unchanged.

The page is prefilled in this order, and the first answer that exists wins:

| Order | Source |
| --- | --- |
| 1 | `/DLCDIR` or `/SAVEDIR` on the command line — a silent install's answers |
| 2 | the installation's own `paths.toml`, when it parses |
| 3 | the legacy settings profile (`%APPDATA%\rb_blitz\launcher.toml`, or beside a portable install), the file the game's input configuration uses — its `[settings] user_data_root`/`dlc_root`, or the older `[launch] user_data_dir`/`dlc_dir` |
| 4 | the installer's own default, a folder under the user's Documents: `Documents\Rock Band Blitz\Downloaded Content` and `Documents\Rock Band Blitz\Saved Games` |

Rung 4 is a folder rather than an empty field since 2026-10-14, and it is what makes
both boxes *answers*: the page refuses an empty one, so what is recorded is what the
page shows, and a silent install that was asked nothing writes the same two. The save
root is the folder the game itself means by an empty one (its own settings row reads
"Empty uses Documents\Rock Band Blitz"), so the installer records the answer the
runtime would have fallen back to. The DLC root is this installer's own choice: the
game's fallback is a `dlc` directory inside the read-only game data, which is nothing
to ask a player for, so the library folder beside the saves is offered instead.

The legacy profile is only consulted when the install is **not** running elevated:
an all-users install started by an administrator must not import that
administrator's own profile into the player's install. The ordinary install *is*
elevated since 2026-10-14, so rung 3 now reaches `/CURRENTUSER` installs only - a
known narrowing of the machine-wide default, recorded in
[`../docs/reference/known-issues.md`](../docs/reference/known-issues.md).

What the installer refuses and what it accepts:

* It refuses a **save folder inside the read-only game data tree** (`{app}\game`,
  however it is spelled — `..` segments, a trailing separator, a junction or a
different case). The runtime mounts that tree read-only, so a save written there is
invisible to the game and destroyed by an uninstall that removes the game data.
* It accepts **any DLC folder that can work**: the structured `<title id>/<content
  type>/<package>` layout, a flat library of containers nested to any depth, an
  empty folder, or a folder that does not exist yet (the game then starts with an
  empty library). The page says which it found and how many entries it holds; no
  package is opened to do it — the folder page stays instant on a library of
  thousands.
* It **creates** the save folder when it does not exist and writes one probe file
  into it, then removes the probe: a folder the game cannot write to is a save that
  would be lost, so it is worth failing on the page rather than after the install.
  The check that runs *while the page is shown* touches nothing — it only reports —
  so a path on its way to being corrected leaves no folder behind; the folder is
  created when the user moves on.
* An answer of **empty** is no longer one the page can give: both boxes are answers
and the page refuses an empty one. A *silent* install still can, and an explicitly
empty `/DLCDIR=` or `/SAVEDIR=` means the game's own folder and stores nothing, which
is not the same thing as **no answer** — an update writes no folder keys at all.

The file is committed **last**, after the payload, the game data and the install
manifest have all succeeded, so cancelling or failing an install leaves the
configuration that was already there exactly as it was.

The wizard half has no test harness — it is Inno Setup, not C++ — so the three
things the folder page does are checked by hand, and `tests/installer_tests.cpp`
pins the script's shape (every task unchecked, `UsePreviousTasks=no`,
`DisableDirPage=no`, the game as the finish default, and the three helper calls in
order) so a change that drops one fails the suite:

1. Fresh install with both folders given: `paths.toml` appears in the install
   folder with both values, the DLC folder is described on the page, and the save
   folder is created.
2. Same dialog at 100%, 200% and 300% DPI: the two rows, their Browse buttons and
   the status line all fit on the page, and the disk-space line moves below them.
3. Reinstall over an existing install: the folder page comes up with the previous
   install folder and both stored folders; changing only the DLC folder leaves the
   save folder alone.
4. A save folder typed as `<install folder>\game\saves`: the page refuses it and
   nothing is written.
5. One of the two boxes cleared and Next pressed: the page refuses it and nothing is
   written; typing a folder back in continues.
6. A fresh install in the default `C:\Program Files\Rock Band Blitz`: the page opens
   on it, `paths.toml` lands in it with both Document folders, and the game writes
   its saves there rather than under Program Files.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| `no payload to embed: pass /DPayloadUrl and /DPayloadSha256, or create the payload snapshot first` | Embed mode with no `out\payload\payload-manifest.toml`. Run `make_payload.ps1`, or pass `-PayloadUrl`. |
| `pins.iss is missing from the generated directory` | The helper was never built; run `build.ps1` (or configure the preset) first. |
| The test suite fails in a download-mode build with a mismatch between the pin and `pins.toml` | Expected: pass `-SkipTests` (see "Cutting a release"). |
| `-PayloadUrl needs -PayloadSha256` | An installer that downloads its own build must name which build it is. `-AllowUnverifiedPayload` exists for local testing only. |
| A rebuild ignores a pin you just removed | `-DRBBLITZ_EMBED_EXTRA_ARGS` is a *cached* CMake variable. `build.ps1` always passes it (possibly empty), so build through the script rather than a bare `cmake --preset`. |
| `[payload] commit '<x>' is not a 40-character commit id` while configuring | The pin is a ref — `HEAD` by default — and only `build.ps1` has git at the right moment to resolve it. Configure through the script, or write a commit id (or nothing) in `config\pins.toml`. |
| `the generated pins.iss says commit '<a>' but this build resolved '<b>'` | A stale cached `-DRBBLITZ_EMBED_EXTRA_ARGS`, which would ship a setup executable claiming to be another build. Delete `installer\out\build` and build again. |
| The wizard is up but nothing happens, then it fails after ten minutes | The helper died silently. Its own log is next to the install folder (`rbblitz-probe.log` in the temporary directory while the wizard is still open); `install-report.txt` in the finished install has the rest. |

## Shader preparation catalogue

The payload includes `graphics-preparation.json` (schema-1 source locators and
portable recipes only) and the same catalogue embedded in `rb_blitz.exe`.
`tools/make_payload.ps1` verifies they match, and records their identity/counts
in `[graphics_preparation]` of `payload-manifest.toml`. Extraction/vertex
linking runs natively against the user's own assets after title-screen A;
Python, retail `.xsh` bytes and developer native caches never ship.
Updates preserve local caches and player saves/settings. The runtime validates
and merges/rebuilds layers at next preparation. Run the distribution audit
after creating the payload. See [the release record](../docs/engine/shader-cache.md)
part 9 for measurements, troubleshooting and unproven installed UI/hardware gates.
