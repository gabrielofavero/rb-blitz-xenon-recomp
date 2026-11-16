# iOS first-launch importer

Status: implementation delivered (2026-11-16). Device/UI acceptance is unproven;
the owner retains that test.

## Requirements and decisions

- D1: Add a separate importer IPA/build preset; preserve the private bundled-assets route.
- D2: Before starting the game, greet the player and open the native Files picker
  for a supported Xbox package or extracted folder. Never download retail data.
- D3: Ultimate is optional: skip, local ZIP/folder, or the existing pinned GitHub
  release. The owner confirmed these choices in chat on 2026-11-16.
- D4: Ship native game code compiled on the Mac; perform verified extraction and
  asset preparation on device. Zombies Recompiled's persistent-import pattern
  applies; its interpreted managed-code execution is a different mechanism.
- D5: Persist verified assets and setup completion in Application Support.
  Later launches validate and reuse the import; interrupted setup remains resumable.
- D6: Run import/download/verification off the UI thread. Keep progress, retry,
  cancellation and the transition to the existing game startup on UIKit's thread.

## Steps

| ID | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| P1 | Reusable import session and injectable Ultimate transport | Host fixtures for resume, failure, selected content and persistence | Done |
| P2 | Native welcome/picker/Ultimate/progress flow and startup handoff | Device arm64 compile/link; owner retains UI navigation | Done |
| P3 | Separate build preset and asset-free IPA packaging | Both packaging modes tested; actual importer IPA audit | Done |
| P4 | Documentation and validation record | Docs checker, focused host tests, current device build | Done |

## Initial evidence

2026-11-16: [tree] `installer/src/game_import_core.cpp` already implements staged,
fingerprinted Xbox package/folder imports and recovery. Ultimate installation is
transactional but its downloader requires desktop libcurl. The existing IPA
builder embeds private assets and unconditionally requires Ultimate.

2026-11-16: [static] Sister project `src/Port.IosGame/PackagePicker.cs` uses the
native document picker, including extensionless items. Its persistent store
stages assets and reuses verified imports. Its runtime uses interpreted managed
code; this is not evidence of on-device native C++ compilation.

## Validation

2026-11-16: [measured] P1 passes six portable importer CTest suites, including
70 synthetic persistent-session checks. These cover retail/Ultimate interruption
recovery, offline skip, local variants, corrupt downloads, selected-file hashes,
unsafe completion records and late cancellation. Logs:
`out/ios/importer-core-tests.log`. The Foundation transport is injected into the
same shared size/SHA gate; desktop curl remains the default elsewhere.

2026-11-16: [measured] P2 compiles and links an arm64 iPhoneOS Release executable.
Native picker access is coordinated and security scoped; worker cancellation,
background-task expiry and active-only game startup are implemented. Settings
use a separate importer policy and explicit Game Version choice, preserving later
manual edits; the settings service suite passes 468 checks. Logs:
`out/ios/importer-device-build-final.log`, `out/ios/importer-settings-tests.log`.
SDK patch 0079 allows subclass preflight to chain the existing initializer;
78 patches replay with no replacements (`out/ios/importer-patch-check.log`).

2026-11-16: [measured] P3 passes 15 headless IPA packaging regressions, including
tiny real iPhoneOS framework binaries. The unsigned importer IPA is 67,373,920
bytes (64 MiB), with 1,232 archived files and no `game/`, XEX, ARK or HDR assets.
Its sidecar records importer mode, no bundled private game data/Ultimate, and the
separate `.importer` bundle identifier. Evidence:
`out/ios/importer-packaging-tests.log`, `out/ios/importer-package.log`,
`out/ios/packages/RockBandBlitz-0.1.0-arm64-ios-importer.audit.json`.

The Windows build command was attempted on this Mac; its Windows build tree is
absent (`out/ios/importer-windows-build.log`). The build-time code generator
cannot reserve guest address space under the agent sandbox and was run outside
it, without launching a game/UI. The native device build succeeds.

2026-11-16: [measured] P4: both complete CLI routes pass, using separate output
directories to preserve prior packages. The original builder still produces its
private IPA including Ultimate (`out/ios/importer-bundled-build-final.log`). The
new `scripts/ios/build-importer.sh` command builds and audits the asset-free IPA
(`out/ios/importer-cli-build.log`), copied to the standard package destination
with its matching audit/entitlement sidecars. The documentation checker reports
zero errors and 51 existing warnings (`out/ios/importer-docs-check.log`).
Guides, platform usage, package naming, SDK patch notes and known issues reflect
both routes. No game or importer UI has been navigated.

Device acceptance remains unproven: first launch,
file/folder/cloud providers, all three Ultimate choices, cancel/retry,
background/foreground, same-process boot and second-launch reuse.
