# Experimental iOS IPA builder

Build a private arm64 iPhone/iPad package on a Mac with full Xcode 26+ (AppleClang 17+), CMake 3.25+,
Ninja, Python 3.11+, and Git. Two separate build routes are available: the
original private IPA with bundled assets, and an importer IPA that asks for
your game on the device. Ultimate 2.11 is included in private bundled IPAs and
is optional in importer IPAs.
Use the existing controller support for this experiment.

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
# Put CMake, Ninja and Python 3.11+ on PATH.
sh scripts/ios/build.sh --source '/path/to/your/package-or-extracted-folder'
```

## First-launch importer IPA

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
sh scripts/ios/build-importer.sh
# On a fresh developer checkout, supply --source for build-time code generation:
# sh scripts/ios/build-importer.sh --source '/path/to/your/game-package'
```

This builds the separate `ios-arm64-importer-release` preset and writes
`out/ios/packages/RockBandBlitz-0.1.0-arm64-ios-importer.ipa`. Its default bundle
identifier ends in `.importer`, so it can coexist with the bundled game. It
accepts the same output/signing flags. `--ultimate-source` belongs to the bundled
route; choose Ultimate on the device in this route.

The developer build still needs verified local `game/` input to generate native
game code. Packaging the importer does not read or embed that input or Ultimate,
and does not download Ultimate. The packager checks the binary's importer
identity and rejects game/mod assets in this IPA. Each mode has a distinct
filename and records its mode and asset inventory in the audit sidecar.

On first launch:

1. The welcome screen offers **Choose game file** (including extensionless
   Xbox LIVE/STFS packages) or **Choose extracted game folder** in Files.
2. Setup copies and fingerprints the three required retail files off the UI
   thread. The original source remains untouched.
3. Choose **Play without Ultimate**, import a local Ultimate ZIP/folder, or
   explicitly download the pinned Ultimate release from GitHub. No retail game
   download is offered. Downloads must match the pinned size and SHA-256.
4. Setup prepares the data once, saves completion, then starts the game in the
   same process. Native engine/game code already ships compiled in the IPA;
   the device performs asset extraction and verification. Graphics shaders keep
   their existing runtime compilation/cache behavior.

Game assets and setup state live in Application Support, outside Documents and
separate from saves/settings/caches. Imported assets are excluded from cloud
backup. On later launches, setup verifies and reuses the import without another
picker or download. A failed or canceled Ultimate step retains the base game;
reopening resumes at the optional choice. A corrupt base import requires
selecting the source again. File access is security scoped and coordinated,
including cloud-backed Files providers. Cancel stops preparation safely;
provider operations and a hash already in progress can take time to return.
If preparation finishes while backgrounded, game startup waits for activation.
Deleting the app removes its imported data; no original source is deleted.

Local Ultimate revisions retain the desktop importer's compatibility warning.
The setup record snapshots installed file hashes so later validation can reuse
that exact local revision. The mod's `default.xex` is excluded. The selected
initial Game Version is saved; subsequent explicit settings choices survive
relaunch. Device picker/cloud-provider acceptance and all three setup routes
remain unverified until tested on hardware.

2026-11-16 build evidence: the complete importer CLI produces an audited unsigned
64 MiB IPA, and the original bundled CLI still packages its private game data.
Fifteen packaging tests, six importer CTest suites and the settings service suite
pass. This is build/host evidence; no importer UI has been navigated. The exact
validation and owner checks are in the
[importer plan](../../docs/plans/finished/2026-11-16-ios-importer-plan.md).

2026-11-21 build evidence: the v0.11.0 68-patch SDK snapshot produces audited
unsigned bundled (343 MiB) and importer (64 MiB) IPAs under
`out/ios/packages/rexglue-v0.11`, with SHA-256 inventories and signing entitlements.
Both are arm64 iPhoneOS builds targeting iOS 17.0 using Xcode 26.0.1. The snapshot
passes strict SDK replay and 41 host packaging tests (two PowerShell-only skips).
Device signing, importer UI and gameplay remain unverified.

## Bundled-assets IPA

The original `sh scripts/ios/build.sh` flow is unchanged. The paragraphs below
refer to this private, bundled-assets route unless stated otherwise.

Later builds can omit `--source` to verify and reuse the private `game/` input.
If Ultimate is missing, the native importer downloads the pinned upstream release.
For an offline build, pass `--ultimate-source /path/to/ultimate-2.11-folder-or-zip`.
The packager requires the supported Ultimate data pair, supplies its screenshot
folder placeholder, and excludes the mod's `default.xex`; runtime hooks apply its
edits to the verified retail executable.
Existing iOS installs using the old retail default switch to Ultimate once.
Later Game Version selections remain available and survive restart.
The CLI uses the existing transactional importer and supported fingerprints,
builds the Mac tools separately, applies tracked SDK patches, cross-compiles the
`ios-arm64-release` target, validates controller art, and packages an IPA.
It never launches the game or installs to a device.

The default output is
`out/ios/packages/RockBandBlitz-0.1.0-arm64-ios.ipa`, with a sibling
`.audit.json` inventory and `.entitlements.plist`. An unsigned IPA must be signed
for the target device before installation. Keep its requested extended virtual
addressing entitlement when signing: the guest reserves several GiB of virtual
address space. This is address space, not a claim about physical memory use.
The profile used for signing must permit that entitlement.

Render Scale uses Audio-style Interface and Gameplay buttons with one bottom
native ten-stop wheel. **1.0 is device native pixel density, not the Xbox 360
720p base.** Landscape uses display backing pixels; portrait expands the landscape
source so its full-height crop keeps native density. The ten stops are calculated
from exact 720 source-height pixels through native, with a ratio and pixel-height
label. A display at or below 720 has a single native value.

First boot probes the device and applies a conservative 720p recommendation
before graphics allocation when no sustainable resolution has been verified.
The backend multiplier is selected from both chosen profiles. Edits within that
capacity apply live; larger capacity requires closing/reopening the app.
Vulkan strictly refuses an unsupported multiplier or an unsafe dense allocation
instead of silently calling a reduced image native. MoltenVK's scaled resolve
backing is still large: 2x2 needs 2 GiB and 4x4 needs 8 GiB. Native can therefore
be unavailable on a device even though the wheel describes its real resolution.

A durable per-context recovery marker remains armed throughout the session.
Stable output plus a clean exit clears it. A crash or forced termination starts
both contexts at 720p on subsequent boots, preserving the saved request and layout.
The failed choice is not retried automatically until the request changes.
See [the rendering policy](../../docs/settings/graphics.md) for rounding, live
capacity limits and validation. This is a safety policy, not a device benchmark.

Alternatively, provide an existing signing identity and an iOS development or
ad hoc provisioning profile covering your device and bundle identifier:

```sh
sh scripts/ios/build.sh --source '/path/to/your/game-package' \
  --bundle-id com.example.rbblitz \
  --sign-identity 'Apple Development: Your Name (TEAMID)' \
  --provisioning-profile '/path/to/profile.mobileprovision'
```

The signer checks the profile's platform, expiration, application identifier,
required entitlement and signing certificate. Signing credentials are supplied
locally; the builder does not obtain or create them. Signed and unsigned builds
use the same filename; the audit sidecar records signing status. Move an existing
IPA aside before rebuilding the same name.

The minimum target is iOS/iPadOS 17.0. The experiment uses fullscreen Vulkan
through MoltenVK, device aspect and VSync. Display offers separate Interface and
Gameplay orientation and Render Scale preferences. Desktop window mode, fixed
Resolution, Force 16:9, VSync and renderer selectors remain hidden. Existing
graphics-quality and control settings remain available. No new host overlay is
introduced.
After the first launch, Files shows **On My iPhone > Rock Band Blitz** (or
**On My iPad**), with these folders:

- `DLCs`: the default DLC library; copy your DLC packages or extracted songs here
  while the game is closed, then reopen it to scan them.
- `Pictures`: Ultimate's results-screen captures, saved as numbered BMP images.
  Captures contain the game image, without host overlays. The folder exists from
  first launch. Empty old `Import`/`Imports` placeholders are removed; folders
  containing files are preserved. Asset import will use a file picker.
- `Diagnostics`: `game.log` holds the latest session and `game.1.log` its rotated
  portion; the preceding session is retained as `previous.log` and
  `previous.1.log`. Build details are saved beside them in `session-info.txt`.
  Logs are bounded to 2 MiB per file and flushed after every emitted line.
  The `ios_view` and `ios_present` log lines record UIKit visibility and final
  swapchain painting.

Settings, saves and caches use the app's writable sandbox under `Library`;
the game mount remains read-only (bundled or imported). Restart-required settings currently require
closing and reopening the app; spawning a replacement process is unavailable on
iOS. Automatic load-failure relaunch is also unavailable in this first route.

Gameplay has fixed touch controls, independent of keyboard/mouse and controller
bindings. Touching the left/right half strikes First/Second Note immediately;
two fingers can hit both notes. A horizontal swipe of at least 24 screen points
moves one lane in the opposite direction (right swipe moves left, and vice versa).
A successful starting note counts immediately. A failed starting press waits for
that touch's release: a swipe discards it, while a normal release applies it.
Missed chart notes keep their native penalties. Each finger resolves independently;
mouse and controller presses never receive this forgiveness. The top-right `||`
button pauses the game on release and respects the iPhone safe area.
It appears only during a song. The native pause menu retains ordinary touch
navigation. Touch gestures reset on pause, focus loss, cancellation and resize.
These controls have no settings rows; physical mouse input keeps its bindings.

Render-profile recovery preserves the selected orientation/aspect and replans
both quality tiers at the conservative floor before allocating graphics buffers.
It reduces backend capacity too, rather than retaining native-sized backing
behind a 720p image.

On iOS, the synchronous will-resign-active event pauses audio, parks the guest
main loop at its next draw, freezes guest time, blocks new Vulkan queue calls,
and drains already submitted GPU work before UIKit enters the background.
UI paint requests are skipped while inactive. Did-become-active opens the queues,
resumes audio/time/the guest, and requests a fresh paint. Startup uploads waiting
on the UI thread keep pumping UIKit so foreground notifications remain reachable.
This avoids the observed Metal background-submission device loss; repeated
switching, locking the phone and interrupted cold startup need device acceptance.

For a current device build, packaging alone is:

```sh
python3 installer/ios/package.py --build-dir out/build/ios-arm64-release \
  --game-dir game \
  --controller-validator out/build/mac-arm64-release/rb_blitz_controller_schemes
```

For an existing importer build, use the same packaging command with
`--mode importer --build-dir out/build/ios-arm64-importer-release` (no
`--game-dir` is needed). Both package modes check iPhoneOS arm64 Mach-O
platform/minimum versions and relocate
native dependencies into frameworks, copies only the three fingerprinted game
files and declared authored resources, and verifies archive integrity. Its
audit marks device gameplay unverified. The resulting IPA contains your private
retail game data; do not upload or distribute it. Public desktop installers
continue to exclude game assets.

On 2026-11-16, the complete CLI built and audited a 337 MiB unsigned IPA with
verified local assets, using Xcode 26.0.1 and the iPhoneOS 26.0 SDK. Ten packaging
regressions, four importer CTest suites and three focused settings/network CTest
suites pass. Evidence is under `out/ios/`; this is headless build/package evidence.

On 2026-11-16, the owner confirmed device installation, the native startup icon,
menu music and touch input on an A18 Pro iPhone. The guest splash/menu remained
black while the app was active. The diagnostic run captured a correct splash
image while every sampled screen presentation returned `VK_SUBOPTIMAL_KHR`.
The pinned MoltenVK can set the shared Metal layer to 1x1 while retiring a
same-size swapchain, after the replacement has set its correct size. SDK patch
0077 restores the committed drawable extent after retirement; a native layer
test covers that correction. The owner confirmed visible output with this fix.
A later background-GPU permission error from the first run does not establish
the cause of the foreground symptom.

Memory pressure and gameplay touch acceptance remain unverified. There is no simulator target in this
route. The build/test evidence is tracked in the
[iOS plan](../../docs/plans/finished/2026-11-16-ios-cli-installer-plan.md).
