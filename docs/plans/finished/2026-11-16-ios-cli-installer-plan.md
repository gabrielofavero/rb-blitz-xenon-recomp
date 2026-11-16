# iOS CLI installer

Status: implementation delivered (2026-11-16). Initial private device experiment; runtime
acceptance is unverified. The owner retains the game/UI test.

## Scope

Build an arm64 iPhone/iPad IPA from a supported user-supplied asset using a CLI.
Embed the verified runtime assets for this first route. A future on-device asset
picker is out of scope. Keep existing controller input; do not add touch controls
or new host overlays. Hide desktop display/renderer settings on iOS.

## Steps and acceptance

| ID | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| P1 | iOS SDK/runtime and cross-build support | Configure and compile a device Release target; replay tracked SDK patch | Done |
| P2 | CLI import, packaging and optional signing | Verify supplied assets, device Mach-O checks, IPA layout, focused packaging tests | Done |
| P3 | iOS settings filtering | Focused policy/menu tests; desktop regression checks | Done |
| P4 | Usage and validation record | Documentation checker; exact build limits and owner device checks | Done |

## Evidence

2026-11-16: [tree] Native macOS packaging, importer, controller validation and
opaque Apple icon already exist. The default developer selection is Command Line
Tools; explicitly selecting a side-by-side Xcode exposes the iPhoneOS 26.0 SDK.
No iOS game launch or device test has been performed.


2026-11-16: [static] P1 implementation is in `CMakeLists.txt`,
`CMakePresets.json`, `config/toolchain-ios-arm64.toml`, the project iOS path/browser
branches, and SDK patch 0074. Mac build-time executables are imported into the
device build. Toolchain stamps now live per build tree to prevent host/device
cross-contamination. SDK patch 0075 replaces the enormous generated registration function with a loop
over the existing mapping table; Release optimization remains enabled. The
initial straight-line registration compile remained CPU-active after more than
six minutes and was interrupted; an `-O0` attempt was also interrupted. Exact
comparison proved all 38457 mapping pairs and their order agree; the loop source
compiled for device arm64 at `-O3` in 2.06 seconds. Completed objects were retained.

2026-11-16: [static] P2 implementation is in `installer/ios/build.sh`,
`installer/ios/package.py` and `scripts/ios/build.sh`. It reuses the native asset
importer and controller validator, fingerprints all three game files, packages
real iOS frameworks and supports compatible local certificate/profile signing.
The unsigned route emits the requested virtual-address entitlement as a sidecar.
The iOS SDK has no system libcurl, so `src/store/store_apple.mm` preserves the
existing network interfaces through Foundation with bounded bodies, refused
redirects and streaming cancellation.

2026-11-16: [measured] ten IPA packaging tests pass, including real tiny device
Mach-O fixtures, framework relocation, ad hoc signature structure (not device
installation), asset validation and overwrite refusal. Evidence:
`out/ios/packaging-tests.log`. SDK patch replay passes (74 patches):
`out/ios/patch-check.log`. Four native importer CTest suites pass:
`out/ios/import-tests.log`; the local private asset verifies:
`out/ios/import-verify.log`.

2026-11-16: [measured] P3 host validation passes 442 settings service checks and
132117 menu checks. The Foundation transport passes 27 host fixture checks and
a separate loopback test returning 302 without following it. Logs:
`out/ios/settings-service.log`, `out/ios/settings-screen.log`,
`out/ios/store-apple.log`, `out/ios/store-apple-loopback.log`.
The affected project and SDK sources compile for device arm64; runtime checks
remain unproven. The requested Windows build command cannot run on this Mac:
its Windows build tree is absent (`out/ios/windows-build.log`).

2026-11-16: [measured] the complete `sh scripts/ios/build.sh` route passes with
verified local assets. The device Release game links and its private unsigned
IPA passes platform/framework, resource and ZIP checks. Evidence:
`out/ios/cli-build-delivered.log`,
`out/ios/packages/RockBandBlitz-0.1.0-ios-arm64-unsigned.audit.json`.
The IPA is 337 MiB; its sibling report records every file hash and archive SHA-256.
Real-binary packaging exposed CMake's automatic `.app` output default and
install_name_tool's refusal to delete/add the same rpath in one command; both
are corrected, with a native fixture covering the existing-rpath case.

2026-11-16: [measured] the three focused tests also build through native CMake
and pass CTest (`settings_service`, `settings_screen`, `store_apple`):
`out/ios/host-tests-build.log`, `out/ios/host-ctest.log`. The documentation checker
passes with zero errors; its 51 existing warnings remain (`out/ios/docs-check.log`).
No game/UI navigation has occurred. Actual signing credentials, device
installation, controller delivery, VM alias/fiber execution, backgrounding,
rendering, audio, gameplay and memory pressure remain unverified.
