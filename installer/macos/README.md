# macOS build and package structure

This adapts the sibling `z0mb1es-rec0mp1led` Mac pattern to the ReXGlue/C++
runtime: a native build, a reviewed `.app` staging manifest, architecture and
load-path inspection, an icon made with `sips` and `iconutil`, and an unsigned
`pkgbuild` package with bundle relocation disabled. The sibling's .NET/FNA,
FFmpeg, and asset-conversion inputs do not belong in this package.

The bundle's `CFBundleExecutable` is `rb_blitz_macos_launcher`. Finder starts its
normal-user Cocoa source picker before guest content exists. It probes a user
package or extracted folder with the shared portable importer, shows the
expected version/space/destination, imports under
`~/Library/Application Support/rb_blitz/game`, and starts the adjacent
`rb_blitz` executable with an explicit `--game_data_root`. A valid import skips
the picker on later launches. `rb_blitz_macos_launcher --repair` offers a
reimport; cancelling or failing one preserves and launches the prior valid set.
No package install script touches game data. The guest-authored Settings action
to enter repair is still pending.

On a Mac with the authorized game dump, required native libraries, CMake,
Ninja, Xcode/Command Line Tools, and Python 3.11+, run:

```sh
sh installer/macos/build.sh --stage-only
```

`build.sh` selects `mac-arm64-release` or `mac-amd64-release` from native host
architecture, configures and builds the ReXGlue target, then calls `package.py`.
The first clean codegen intentionally asks for a second configure; the script
accepts only that specific bootstrap result. `package.py` stages exact files
from `installer/config/platform-packaging.json`, renders the version from
`installer/config/pins.toml` into `Info.plist`, creates `blitz.icns`, checks
Mach-O slices and linked-library paths, and rejects obvious retail files and
build-machine paths. The ReXGlue SDK's staged Vulkan loader, MoltenVK, and ICD
are part of the manifest.

The full package command is the same without `--stage-only`. It intentionally
refuses to create a `.pkg` while the native dependency inventory, Xcode/CLT
pin, icon check, and full release audit flags remain pending. Once those gates
are completed on each architecture, it uses `pkgbuild --root` with
`components.plist` and `/Applications`, expands the package, compares its app
payload byte-for-byte with the audited stage, and prints the package SHA-256.
The package output is `out/macos/packages/RockBandBlitz-<version>-mac-<arch>.pkg`.
The native inventory must also identify every controller asset, required
shared library, and third-party notice that belongs in the final manifest;
the current stage list is deliberately incomplete and cannot be published.
Neither architecture is advertised as supported until native install, Finder
launch, import, Vulkan, and gameplay checks pass on real Mac hardware.
