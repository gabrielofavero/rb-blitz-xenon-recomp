# Experimental iOS IPA builder

Build a private arm64 iPhone/iPad package on a Mac with full Xcode, CMake 3.25+,
Ninja, Python 3.11+, and Git. The initial route embeds your verified game assets
in the IPA. An on-device asset picker is future work.
Ultimate 2.11 is included in private IPAs and is the default Game Version.
Use the existing controller support for this experiment.

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
# Put CMake, Ninja and Python 3.11+ on PATH.
sh scripts/ios/build.sh --source '/path/to/your/package-or-extracted-folder'
```

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
`out/ios/packages/RockBandBlitz-0.1.0-ios-arm64-unsigned.ipa`, with a sibling
`.audit.json` inventory and `.entitlements.plist`. An unsigned IPA must be signed
for the target device before installation. Keep its requested extended virtual
addressing entitlement when signing: the guest reserves several GiB of virtual
address space. This is address space, not a claim about physical memory use.
The profile used for signing must permit that entitlement.

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
locally; the builder does not obtain or create them. Signed output uses the
`-signed.ipa` suffix. Move an existing IPA aside before rebuilding the same name.

The minimum target is iOS/iPadOS 17.0; the experiment uses landscape fullscreen,
Vulkan through MoltenVK, automatic device aspect and 1x render scale with
VSync. The existing Auto planner chooses an aligned guest width at 720p (or
540p for very wide displays); an A18 Pro iPhone reporting 2622x1206 uses
1568x720. Presentation uses the device's exact aspect so width alignment does
not leave side bars. Desktop display mode, resolution, aspect, scale, VSync, overscan and
renderer rows are hidden, and empty settings groups disappear. Existing graphics
quality and control settings remain. No new host overlay is introduced.
After the first launch, Files shows **On My iPhone > Rock Band Blitz** (or
**On My iPad**), with these folders:

- `DLCs`: the default DLC library; copy your DLC packages or extracted songs here
  while the game is closed, then reopen it to scan them.
- `Pictures`: Ultimate's results-screen captures, saved as numbered BMP images.
  Captures contain the game image, without host overlays. The folder exists from
  first launch. Empty old `Import`/`Imports` placeholders are removed; folders
  containing files are preserved. Asset import will use a file picker.
- `diagnostics`: `game.log` holds the latest session and `game.1.log` its rotated
  portion; the preceding session is retained as `previous.log` and
  `previous.1.log`. Build details are saved beside them in `session-info.txt`.
  Logs are bounded to 2 MiB per file and flushed after every emitted line.
  During iOS bring-up, `ios-guest-frame.bmp` also records the first non-black
  splash/menu GPU image each session. Only the latest capture is kept. The
  `ios_frame`, `ios_view` and `ios_present` log lines distinguish guest image
  generation, UIKit visibility and final swapchain painting.

Settings, saves and caches use the app's writable sandbox under `Library`;
the bundled game remains read-only. Restart-required settings currently require
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

Graphics recovery retains the iPhone/iPad Auto aspect and 1x render scale, even
when the startup journal contains a 16:9 safe mode or an older desktop layout.
It replans guest geometry before presentation, rather than stretching a 16:9
frame across the display.

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

The package checks iPhoneOS arm64 Mach-O platform/minimum versions, relocates
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
