# Live profile performance and native aspect tiers

Date: 2026-11-18. Status: in progress.

## Deliverables

- D1: Correct sustained GPU stalls after live interface resolution transitions.
- D2: Offer lower native-aspect desktop resolutions alongside 16:9 equivalents,
  keeping native first, precise dimension labels and the 720-pixel height floor.
- D3: Build, run scoped host checks and document runtime limits. Game navigation
  and visual/performance acceptance remain owner checks.

## Evidence

- 2026-11-18 [owner log]: `out/build/win-amd64-release/logs/rb_blitz_004.log`
  records one successful rebuild for each selected profile, not a repeated
  allocation loop. Before edits the menu averaged approximately 59.8 FPS.
  After selecting 720p, submission fence waits became sustained; restoring
  native left approximately 3 FPS through shutdown. The late native interval
  has 114 submission waits with a median around 325.5 ms. Helper compilation
  finished by 20:05:18.043 and totalled 178.3 ms, while the stall continued for
  about 37 seconds. Pipeline compilation alone does not explain the regression.
- D2 (2026-11-18, implemented, static): desktop offers include each lower
  standard height tier at the native display aspect and at 16:9, rounded to
  whole pixels, sorted by height then width, with duplicates removed. Examples:
  2560x1080 offers 1707x720 and 1280x720; 3440x1440 offers 1720x720 and 1280x720.
  Persisted tokens remain dimensions, and native keeps its stable token.

## Validation

Runtime correction, release build and scoped automated validation are pending.
The owner performs final game/UI checks; no game navigation is authorized.
Windows AMD64 is available for compilation; cross-platform runtime performance
and visual acceptance remain unverified.
