# macOS installer

The native route builds a Finder application and an unsigned Installer package
that places **Rock Band Blitz.app** in `/Applications`. The bundle ID and package
receipt are `io.github.gabrielofavero.rbblitz`; the version comes from
`installer/config/pins.toml`. Bundle relocation is disabled, and an update replaces
the app at that destination. There are no privileged install scripts.

Release acceptance is still pending. `--development` produces a local test package
after the same binary, resource, and expanded-payload audits. It does not assert
that Finder launch, graphics, gameplay, Gatekeeper, or upgrades have been tested.
Each architecture needs its own native build and validation record; this route
does not make a universal binary.

## Build

Use a native Mac with Xcode 16 or newer, CMake 3.25+, Ninja, Python 3.11+, and Git.
Select an accepted Xcode with `DEVELOPER_DIR` if the system's Command Line Tools
are older. Put CMake, Ninja, and Python on `PATH`. Supply the supported game dump
under `game/` for code generation; it remains private and is never staged.

From the repository root:

```sh
# Build, audit, and create a local test installer.
sh installer/macos/build.sh --development

# Build and audit only the app bundle.
sh installer/macos/build.sh --stage-only
```

The script selects `mac-arm64-release` or `mac-amd64-release` from the host CPU.
It initializes a missing SDK, applies the complete tracked patch set with
`scripts/setup/apply_sdk_patches.py`, configures with the selected Xcode's
compilers/SDK, handles the first codegen/reconfigure cycle, and builds the game,
Cocoa importer, and controller asset validator. MoltenVK's fetched dependencies
stay under `out/macos/dependencies`.

The app is staged at `out/macos/stage/<arch>/Rock Band Blitz.app`. Test installers
are named `out/macos/packages/RockBandBlitz-<version>-mac-<arch>-development.pkg`.
Every package has a sibling `.audit.json` containing its SHA-256 and the exact
staged file hashes. Move an existing package aside before rebuilding the same
name. Without `--development`, pending release acceptance prevents packaging.

For an already current build, staging/packaging can run directly:

```sh
python3 installer/macos/package.py --arch arm64 \
  --build-dir out/build/mac-arm64-release --development
```

## Package contents and checks

The reviewed manifest is `installer/config/platform-packaging.json`. It includes
the launcher, game, ReXGlue runtime/GPU plugin, Tracy runtime, Vulkan loader,
MoltenVK and ICD, icon, Messages feed, fingerprint and graphics preparation
records, and third-party notices. Controller packages are validated by the same
C++ parser/image decoder as the game; only their declared files are copied,
including the internal keyboard/mouse art. Templates and local retail exports
are excluded.

The packager rewrites bundled dylib IDs and load commands, removes build rpaths,
strips debug information, and signs the final binaries and bundle ad hoc. This
supplies local code integrity; it is not Developer ID signing or notarization.
The `.pkg` has no trusted installer signature. Its first-open security behavior
must still be checked on the supported macOS versions.

The audit checks every Mach-O file's CPU slice, minimum OS, signature, and actual
`LC_RPATH` dependency resolution. It scans all files, including large executables,
for build-machine paths and rejects retail, foreign executable, shader-cache,
and key file types/signatures. The staged portable graphics catalogue must match
the full catalogue embedded in the game. After `pkgbuild`, the package is expanded:
its identity, destination, absence of scripts, file list, hashes, and permissions
must agree with the audited stage. An unsuccessful package is not copied to the
output directory.

## First launch and user data

Finder starts `rb_blitz_macos_launcher`. It offers a package or extracted-folder
picker, shows the matched version, required space, and destination, then verifies
and imports under `~/Library/Application Support/rb_blitz/game`. A verified import
skips the picker on later launches. `rb_blitz_macos_launcher --repair` reimports;
canceling or failing preserves a previous valid set. A guest Settings action for
repair and the native Ultimate selection/download flow are still pending.

The installer contains neither game data nor Ultimate. App updates replace only
the bundle. Imported content and other user data remain outside it. Removing the
app in Finder does not remove the Application Support data or the separate
`~/Library/Caches/rb_blitz` cache. Installed update/removal behavior still needs
owner acceptance.

## Non-UI validation

The importer and Cocoa launcher can be built without the SDK or a game dump:

```sh
cmake -S installer/portable -B out/build/portable-import -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/portable-import
ctest --test-dir out/build/portable-import --output-on-failure
python3 -m unittest discover -s installer/tests -p '*packaging_tests.py' -v
```

Packaging tests use tiny, non-game Mach-O fixtures and create/expand a real test
package without installing it or opening any UI. See Prompt 4 of the
[platform release plan](../../docs/plans/in-progress/2026-11-09-linux-macos-release-plan.md)
for dated evidence and the remaining owner tests.
