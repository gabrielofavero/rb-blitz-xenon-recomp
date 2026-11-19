# Resolution transition integrity

Date: 2026-11-18. Status: finished implementation; owner visual acceptance pending.

## Deliverables

- D1: Apply profile resource changes before the native frame snapshots its tile rectangles.
- D2: Refresh aspect-dependent presentation without stretching existing scene geometry.
- D3: Keep gameplay resolution through pause and confirmation dialogs; use Interface in Help & Options.
- D4: Build the release, run focused host checks, and record unverified visual acceptance.

## Evidence

- 2026-11-18 [recording]: offline frame extraction under `out/resize-artifacts/`
  shows transient magenta bands during resolution changes and persistent aspect deformation.
- 2026-11-18 [log]: `out/build/win-amd64-release/logs/rb_blitz_007.log`
  records old resolve rectangles outside the replacement surface pitch immediately
  after each profile change. Native `sub_82203260` snapshots tiling before
  `sub_823F3C40`, where the existing replacement hook runs.
- Game navigation remains an owner check. The recording is observational evidence;
  no game UI has been driven for this investigation.

## Implementation

- D1 [static]: replacement now runs at `sub_82203260` only while renderer byte
  +247 is clear. Native `sub_823F17D8` then captures the updated rectangles via
  BeginTiling, instead of retaining rectangles from the previous frame size.
- D2 [static]: camera selection `sub_821FFBC8` compares the cached aspect enum
  at camera +764 with renderer +248. Profiles change the ratio, not that enum.
  Selection and WorldToScreen now track each camera's applied ratio and call
  native `sub_821FF728` once when it differs, refreshing the native matrices.
  The ratio is claimed before calling native code to allow callback reentry.
  Offscreen cameras retain their native texture-derived aspect calculations.
- D3 [static]: a separate screen classifier retains Gameplay for `game_screen`
  (including its pause panel), `pause_confirm_quit_song_screen`, and
  `pause_confirm_restart_song_screen`. Separate Help & Options screens and their
  subpages use Interface. The existing live-song input predicate is unchanged.
- Scope: shared title hooks on supported hosts and both render backends. No SDK
  implementation, patch, API, backend default, or dependency changes are needed.

## Validation

- D4 [build]: `cmake --build --preset win-amd64-release` passed on Windows AMD64;
  evidence: `out/resolution-transition-build.log`.
- D4 [host tests]: 15/15 focused CTests passed in 17.46s; evidence:
  `out/resolution-transition-tests.log`. Graphics cases cover independent camera
  refreshes, unchanged-ratio reuse and returning ratios. Native mouse cases
  cover gameplay, pause confirmations, Help & Options and its pause subpages.
  Other checks cover settings registry/bridge/service/store/migration/restore,
  settings payloads, window modes and resize tracking.
- D4 [static review]: native frame/camera signatures, tile capture ordering,
  register isolation and reentrant callback behavior were checked against the
  generated retail implementation. No additional SDK patch is indicated.
- D4 [documentation]: the plan moved to finished, references were updated, and
  the documentation checker passed with zero errors and 143 baseline warnings;
  evidence: `out/resolution-transition-docs.log`.
- [unverified]: actual magenta-free switching, scene proportions across repeated
  16:9/ultrawide changes, pause/Help & Options transitions, and performance after
  these changes. No game/UI was driven. Linux, macOS, iOS, Android and MoltenVK
  builds or runtime transitions were not tested on this Windows host.
